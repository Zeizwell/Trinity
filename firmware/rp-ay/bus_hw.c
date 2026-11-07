// Шина слота макета, ответ на чтение - железом (bus_hw.h, doc/zx-trinity.md п.14е). Ножки - config.h PIN_SLOT_*.
//   PIO0: снимок записей (bus_wr) -> DMA C -> кольцо -> прерывание ядра 1 -> card_slot_write (разбор, без сроков);
//   PIO1: A (выбор регистра) -> A1 -> cur -> A1b -> L: слово ans -> R.rxfifo[0];
//         D (данные)         -> D1 -> D2.READ_ADDR, D1b: cur -> D2.WRITE_ADDR_TRIG; D2: байт -> ans, затем A1b;
//   PIO2: R (чтение): слово, статус ядра rxfifo[1], статус слота rxfifo[2], поток - TX FIFO (DMA S).
// Один автомат R на карту: TurboSound на макете выбирает CPLD (cfg.chips = 1), чип - 0
#include "bus_hw.h"
#include "config.h"
#include "card.h"
#include "chip.h"                                   // cyc_now
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "hardware/structs/busctrl.h"
#include "bus.pio.h"                                // bus_wr, bus_add_program
#include "bus_dma.pio.h"                            // bus_sel, bus_dat, bus_ans
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/clocks.h"                         // clock_get_hz (журнал: нс)
#include "hardware/timer.h"                          // busy_wait_us_32 (самопроверка)
#include "ayfx.h"                                  // AYFX_P_INFO (стенд)
#include <stdio.h>

#define RING_N   1024                               // записей в кольце (степень двойки): ~1.5 мс при otir на 14 МГц
#define K_STATUS 0x100                              // род слова ans: статус (п.14е-1)
#define K_STREAM 0x200                              //   поток

static uint32_t ring[RING_N] __attribute__((aligned(RING_N * 4)));
static uint32_t ring_rd;                            // сколько разобрано (индекс в кольце)
static uint32_t ans[CARD_CHIPS][256] __attribute__((aligned(2048)));   // слово: значение, род; чип - 1К
static uint8_t  one[256] __attribute__((aligned(256)));               // one[i] = i: байт данных как адрес для D2
static volatile uint32_t cur;                       // адрес слова выбранного регистра: пишет A1, читают A1b и D1b

static PIO  pio_w = pio0, pio_ad = pio1, pio_r = pio2;
static uint sm_wr, sm_a, sm_d, sm_r;
static uint sm_rs;                                  // подслушка чтений (длина /RD, bus_rdw) - только консоль
static uint sm_t, t_off;                            // T: метки потока (bus_tok)
static int  ch_c, a1, a1b, l, d1, d1b, d2, n1, n2;
#define SBUF_N 1024                                 // поток ответа: байты 1..n-1 и тень (самый длинный ответ - 512)
static uint8_t  sbuf[SBUF_N];
static uint32_t n_sink;                             // сюда N1 сбрасывает метку
static bool ready;

volatile uint32_t bus_hw_irq_cyc, bus_hw_left;
volatile bool bus_hw_jr_force;                      // консоль: напечатать журнал, даже если не менялся
enum { JR_W = 1, JR_R = 2, JR_P = 3, JR_F = 4, JR_X = 5 };   // журнал шины (ниже): запись, чтение, ответ, чужие, потеря
static void jr_reads(void);
static void jr_put(uint32_t t, uint32_t v);

// ---- Таблица ответов (ядро; DMA D2 пишет байт значения, ядро - байт значения или рода, оба - по байту)
static void val_set(int c, uint8_t r, uint8_t v) { ((volatile uint8_t *)&ans[c][r])[0] = v; }
static void kind_set(int c, uint8_t r, uint32_t k) { ((volatile uint8_t *)&ans[c][r])[1] = (uint8_t)(k >> 8); }

static void table_reset(void) {
    for (int c = 0; c < CARD_CHIPS; c++) {
        for (int r = 0; r < 256; r++) ans[c][r] = 0;
        kind_set(c, 0xC0, K_STATUS);
        kind_set(c, 0xC1, K_STREAM);                // пусто - тень (значение), как модель
    }
    for (int i = 0; i < 256; i++) one[i] = (uint8_t)i;
    cur = (uint32_t)&ans[0][0];
}

// ---- Записи: кольцо снимков -> разбор (прерывание DMA C, ядро 1, высший приоритет - как раньше прерывание шины)
static void ring_irq(void) {
    uint32_t t0 = cyc_now();
    dma_irqn_acknowledge_channel(1, (uint)ch_c);
    uint32_t wp = ((uint32_t)dma_hw->ch[ch_c].write_addr - (uint32_t)ring) / 4 & (RING_N - 1);   // куда ляжет следующий
    while (ring_rd != wp) {
        uint32_t w = ring[ring_rd];                 // снимок ножек от PIN_SLOT_D0
        ring_rd = (ring_rd + 1) & (RING_N - 1);
        if (w >> (PIN_SLOT_CS - PIN_SLOT_D0) & 1) continue;   // /CS = 1 - не наш чип
        jr_reads();                                 // журнал: чтения до этой записи, потом она
        jr_put(JR_W, (w >> (PIN_SLOT_A0 - PIN_SLOT_D0) & 1) << 8 | (w & 0xFF));
        card_slot_write(w >> (PIN_SLOT_A0 - PIN_SLOT_D0) & 1, (uint8_t)w);
    }
    uint32_t d = (t0 - cyc_now()) & 0xFFFFFF;
    if (d > bus_hw_irq_cyc) bus_hw_irq_cyc = d;
}

// ---- Каналы DMA
// 32 бита, адреса стоят, высокий приоритет; treq - DREQ или DREQ_FORCE, chain - кого запустить в конце
static dma_channel_config ch_cfg(int ch, uint treq, int chain) {
    dma_channel_config c = dma_channel_get_default_config((uint)ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, treq);
    channel_config_set_chain_to(&c, (uint)chain);
    channel_config_set_high_priority(&c, true);
    return c;
}

// Счётчик передач: n и после конца - снова ждать свой DREQ (режим TRIGGER_SELF)
static uint32_t self_n(uint32_t n) {
    return n | DMA_CH0_TRANS_COUNT_MODE_VALUE_TRIGGER_SELF << DMA_CH0_TRANS_COUNT_MODE_LSB;
}

static void dma_init(void) {
    ch_c = dma_claim_unused_channel(true);
    a1 = dma_claim_unused_channel(true), a1b = dma_claim_unused_channel(true), l = dma_claim_unused_channel(true);
    d1 = dma_claim_unused_channel(true), d1b = dma_claim_unused_channel(true), d2 = dma_claim_unused_channel(true);
    n1 = dma_claim_unused_channel(true), n2 = dma_claim_unused_channel(true);
    busctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;   // ядра DMA не задержат

    // Выбор: A1 (DREQ A) адрес слова -> cur; A1b: cur -> L.READ_ADDR_TRIG; L: слово -> R.rxfifo[0]
    dma_channel_config c = ch_cfg(l, DREQ_FORCE, l);
    dma_channel_configure((uint)l, &c, &pio_r->rxf_putget[sm_r][0], &ans[0][0], 1, false);
    c = ch_cfg(a1b, DREQ_FORCE, a1b);
    dma_channel_configure((uint)a1b, &c, &dma_hw->ch[l].al3_read_addr_trig, &cur, 1, false);
    c = ch_cfg(a1, pio_get_dreq(pio_ad, sm_a, false), a1b);
    dma_channel_configure((uint)a1, &c, &cur, &pio_ad->rxf[sm_a], self_n(1), true);

    // Данные: D1 (DREQ D) адрес one[v] -> D2.READ_ADDR; D1b: cur -> D2.WRITE_ADDR_TRIG; D2: байт -> значение слова,
    // в конце - A1b: найти слово заново (чтение назад записанного)
    c = ch_cfg(d2, DREQ_FORCE, a1b);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    dma_channel_configure((uint)d2, &c, &ans[0][0], one, 1, false);
    c = ch_cfg(d1b, DREQ_FORCE, d1b);
    dma_channel_configure((uint)d1b, &c, &dma_hw->ch[d2].al2_write_addr_trig, &cur, 1, false);
    c = ch_cfg(d1, pio_get_dreq(pio_ad, sm_d, false), d1b);
    dma_channel_configure((uint)d1, &c, &dma_hw->ch[d2].read_addr, &pio_ad->rxf[sm_d], self_n(1), true);

    // Поток ответа: N1 (DREQ T) метка -> n_sink; N2: следующий байт sbuf -> значение слова #C1 (адрес чтения растёт
    // от метки к метке, ставит bus_hw_reply), в конце - A1b: R видит новый байт до следующего чтения
    c = ch_cfg(n2, DREQ_FORCE, a1b);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, true);
    dma_channel_configure((uint)n2, &c, &ans[0][0xC1], sbuf, 1, false);
    c = ch_cfg(n1, pio_get_dreq(pio_w, sm_t, false), n2);
    dma_channel_configure((uint)n1, &c, &n_sink, &pio_w->rxf[sm_t], self_n(1), true);

    // Снимки записей: C (DREQ снимка) -> кольцо, на каждую - прерывание
    c = ch_cfg(ch_c, pio_get_dreq(pio_w, sm_wr, false), ch_c);
    channel_config_set_write_increment(&c, true);
    channel_config_set_ring(&c, true, 12);                  // кольцо 4К (RING_N * 4)
    dma_channel_configure((uint)ch_c, &c, ring, &pio_w->rxf[sm_wr], self_n(1), true);
    irq_set_exclusive_handler(DMA_IRQ_1, ring_irq);
    irq_set_priority(DMA_IRQ_1, 0);                         // высший: как раньше прерывание шины
    dma_irqn_set_channel_enabled(1, (uint)ch_c, true);
    irq_set_enabled(DMA_IRQ_1, true);
}

// ---- Автоматы
// Регистр Y автомата = v (до запуска)
static void sm_set_y(PIO pio, uint sm, uint32_t v) {
    pio_sm_put(pio, sm, v);
    pio_sm_exec(pio, sm, pio_encode_pull(false, false));
    pio_sm_exec(pio, sm, pio_encode_mov(pio_y, pio_osr));
}

// A / D: снимок от D0, /WR - конец записи, ISR влево (адрес собирается сдвигом), OSR вправо (разбор снимка).
// Y кладём через TX FIFO - пока он есть: склейка в RX (8 записей в запасе) отнимает TX, её включаем после
static void wr_init(uint sm, uint off, pio_sm_config c, uint32_t y) {
    sm_config_set_in_pins(&c, PIN_SLOT_D0);
    sm_config_set_jmp_pin(&c, PIN_SLOT_WR);
    sm_config_set_in_shift(&c, false, false, 32);
    sm_config_set_out_shift(&c, true, false, 32);
    pio_sm_init(pio_ad, sm, off, &c);
    sm_set_y(pio_ad, sm, y);
    hw_set_bits(&pio_ad->sm[sm].shiftctrl, PIO_SM0_SHIFTCTRL_FJOIN_RX_BITS);   // теперь склеить (FIFO очищается)
}

static void pio_init(void) {
    sm_wr = pio_claim_unused_sm(pio_w, true);
    bus_wr_init(pio_w, sm_wr, bus_add_program(pio_w, &bus_wr_program, PIN_SLOT_WR));   // разбор - ядру
    pio_sm_set_enabled(pio_w, sm_wr, false);                // запуск - после цепочек
    sm_rs = pio_claim_unused_sm(pio_w, true);               // подслушка: снимок и длина каждого /RD (bus_rdw)
    uint off_rs = bus_add_program(pio_w, &bus_rdw_program, PIN_SLOT_RD);
    pio_sm_config cs = bus_rdw_program_get_default_config(off_rs);
    sm_config_set_in_pins(&cs, PIN_SLOT_D0);
    sm_config_set_jmp_pin(&cs, PIN_SLOT_RD);
    sm_config_set_in_shift(&cs, false, false, 32);
    sm_config_set_fifo_join(&cs, PIO_FIFO_JOIN_RX);         // первые 8 чтений за период
    pio_sm_init(pio_w, sm_rs, off_rs, &cs);
    sm_t = pio_claim_unused_sm(pio_w, true);                // T: флаг 0 PIO0 поднимает R (irq next)
    t_off = pio_add_program(pio_w, &bus_tok_program);
    pio_sm_config ct = bus_tok_program_get_default_config(t_off);   // FIFO не склеивать: в TX - n
    pio_sm_init(pio_w, sm_t, t_off, &ct);

    sm_a = pio_claim_unused_sm(pio_ad, true);
    sm_d = pio_claim_unused_sm(pio_ad, true);
    uint off = bus_add_program(pio_ad, &bus_sel_program, PIN_SLOT_WR);
    wr_init(sm_a, off, bus_sel_program_get_default_config(off), (uint32_t)&ans[0][0] >> 10);
    off = bus_add_program(pio_ad, &bus_dat_program, PIN_SLOT_WR);
    wr_init(sm_d, off, bus_dat_program_get_default_config(off), (uint32_t)one >> 8);

    sm_r = pio_claim_unused_sm(pio_r, true);
    off = bus_add_program(pio_r, &bus_ans_program, PIN_SLOT_RD);
    pio_sm_config c = bus_ans_program_get_default_config(off);   // FIFO: TXGET (.fifo в bus_dma.pio)
    sm_config_set_in_pins(&c, PIN_SLOT_CS);                 // биты /CS, /WR, /RD, A0
    sm_config_set_jmp_pin(&c, PIN_SLOT_RD);
    sm_config_set_out_pins(&c, PIN_SLOT_D0, 8);
    sm_config_set_out_shift(&c, true, false, 32);
    for (uint i = 0; i < 8; i++) pio_gpio_init(pio_r, PIN_SLOT_D0 + i);
    pio_sm_set_consecutive_pindirs(pio_r, sm_r, PIN_SLOT_D0, 8, false);   // по умолчанию - входы
    pio_sm_init(pio_r, sm_r, off, &c);
    pio_sm_exec(pio_r, sm_r, pio_encode_set(pio_x, 0));    // X - ответ, пока не посчитан
    pio_r->rxf_putget[sm_r][0] = ans[0][0];                 // выбран регистр 0
    pio_r->rxf_putget[sm_r][1] = 0;                         // статус ядра
    pio_r->rxf_putget[sm_r][2] = cfg.status_answer;         // статус чипа слота (A0 = 0)
}

// ---- Самопроверка при старте: автоматы ещё стоят, цепочки уже готовы. Каждое звено - один раз; команды автоматам -
// снаружи (exec), что держит R - через его защёлку выхода (DBG_PADOUT; ножки D - входы, на шину ничего не выходит).
// Итог - биты BUS_ST_* (bus_hw.h) в bus_hw_check, консоль
#define ST_SEL 0x1F                                 // регистр проверки (set - до 31)
#define ST_VAL 0x15                                 //   байт данных
#define ST_W   0x5A                                 //   значение слова до записи
volatile uint32_t bus_hw_check;

// R: команда instr кладёт байт в OSR, out - в защёлку выхода -> байт
static uint8_t r_out(uint instr) {
    pio_sm_exec(pio_r, sm_r, instr);
    pio_sm_exec(pio_r, sm_r, pio_encode_out(pio_pins, 8));
    return (uint8_t)(pio_r->dbg_padout >> PIN_SLOT_D0);
}

// Цепочки отработали: 10 мкс хватает с запасом (замер - доли мкс), потом каналы стоят (A1 и D1 - TRIGGER_SELF:
// после передачи снова ждут свой DREQ и всегда «заняты» - их не смотрим)
static bool chains_idle(void) {
    busy_wait_us_32(10);
    int ch[] = { a1b, l, d1b, d2 };
    for (unsigned i = 0; i < sizeof ch / sizeof ch[0]; i++) if (dma_channel_is_busy((uint)ch[i])) return false;
    return true;
}

// Автомат A или D: в FIFO адрес Y << 8 | x (у A ещё << 2 - как его программа) - будто Спектрум записал
static void sm_push(uint sm, uint x, bool sel) {
    pio_sm_exec(pio_ad, sm, pio_encode_mov(pio_isr, pio_y));
    pio_sm_exec(pio_ad, sm, pio_encode_set(pio_x, x));
    pio_sm_exec(pio_ad, sm, pio_encode_in(pio_x, 8));
    if (sel) pio_sm_exec(pio_ad, sm, pio_encode_in(pio_null, 2));
    pio_sm_exec(pio_ad, sm, pio_encode_push(false, false));
}

static void self_test(void) {
    uint32_t f = 0;
    // exec вообще исполняется: R кладёт #FF
    if (r_out(pio_encode_mov_not(pio_osr, pio_null)) != 0xFF) f |= BUS_ST_EXEC;
    // L сама (A1b вручную): слово регистра -> rxfifo[0]
    ans[0][ST_SEL] = ST_W;
    cur = (uint32_t)&ans[0][ST_SEL];
    dma_start_channel_mask(1u << a1b);
    if (!chains_idle()) f |= BUS_ST_HANG;
    if (r_out(0x8098) != ST_W) f |= BUS_ST_L;               // mov osr, rxfifo[0]
    // статус ядра: запись системы в rxfifo[1]
    pio_r->rxf_putget[sm_r][1] = 0x33;
    if (r_out(0x8099) != 0x33) f |= BUS_ST_PUTGET;          // mov osr, rxfifo[1]
    // выбор: A -> A1 -> cur -> A1b -> L
    cur = (uint32_t)&ans[0][0];
    sm_push(sm_a, ST_SEL, true);
    if (!chains_idle()) f |= BUS_ST_HANG;
    if (cur != (uint32_t)&ans[0][ST_SEL]) f |= BUS_ST_A;
    else if (r_out(0x8098) != ST_W) f |= BUS_ST_A_L;
    // данные: D -> D1, D1b -> D2 (байт в слово) -> A1b -> L
    sm_push(sm_d, ST_VAL, false);
    if (!chains_idle()) f |= BUS_ST_HANG;
    if ((uint8_t)ans[0][ST_SEL] != ST_VAL) f |= BUS_ST_D;
    else if (r_out(0x8098) != ST_VAL) f |= BUS_ST_D_L;
    // назад: таблица, R - как после init
    table_reset();
    pio_r->rxf_putget[sm_r][0] = ans[0][0];
    pio_r->rxf_putget[sm_r][1] = 0;
    pio_sm_exec(pio_r, sm_r, pio_encode_set(pio_x, 0));
    bus_hw_check = f;
}

void bus_hw_init(void) {
    table_reset();
    pio_init();
    dma_init();
    self_test();
    pio_sm_set_enabled(pio_ad, sm_a, true);
    pio_sm_set_enabled(pio_ad, sm_d, true);
    pio_sm_set_enabled(pio_r, sm_r, true);
    pio_sm_set_enabled(pio_w, sm_wr, true);
    pio_sm_set_enabled(pio_w, sm_rs, true);
    pio_sm_set_enabled(pio_w, sm_t, true);
    ready = true;
}

// T заново: старые метки и флаг R - прочь, в TX - сколько байт потока ещё положить (n). Только пока Спектрум не читает
// #C1 (протокол: ответ читают после статуса «готов»; сброс - шина молчит)
static void t_start(uint32_t n) {
    pio_sm_set_enabled(pio_w, sm_t, false);
    pio_sm_clear_fifos(pio_w, sm_t);
    pio_sm_restart(pio_w, sm_t);
    pio_sm_exec(pio_w, sm_t, pio_encode_jmp(t_off));
    pio_w->irq = 1u << 0;                                   // флаг 0: R поднимал его и на чтениях тени
    pio_sm_put(pio_w, sm_t, n);
    pio_sm_set_enabled(pio_w, sm_t, true);
}

// Сброс (/IC: шина молчит): таблица, поток пуст, статус 0, выбран регистр 0
void bus_hw_reset(void) {
    table_reset();
    if (!ready) return;
    t_start(0);
    pio_r->rxf_putget[sm_r][0] = ans[0][0];
    pio_r->rxf_putget[sm_r][1] = 0;
}

// Слово меняет ядро: R увидит его со следующего выбора регистра (п.14е-3). Запускать цепочку самому нельзя:
// запуск занятого канала игнорируется - пропал бы запуск от записи Спектрума
void bus_hw_value(int c, uint8_t r, uint8_t v) { val_set(c, r, v); }

// ---- Журнал шины от старта (отладка п.14е, консоль): записи, чтения (подслушка), ответы - по порядку.
// Чтения сливаются перед каждой записью (прерывание кольца) и в цикле ядра 1 - порядок с записями сохраняется
#define JR_N 160
static uint32_t jr[JR_N];
static volatile uint32_t jr_n;
static uint32_t fr_n, fr_min;                       // чужие /RD (/CS = 1) с прошлой записи журнала: сколько, самый короткий
void bus_hw_jr_clear(void) { jr_n = 0, fr_n = 0; }
static void jr_add(uint32_t t, uint32_t v) { if (jr_n < JR_N) jr[jr_n++] = t << 24 | v; }
static void jr_put(uint32_t t, uint32_t v) {
    if (fr_n) jr_add(JR_F, (fr_n > 0xFFF ? 0xFFF : fr_n) << 12 | (fr_min > 0xFFF ? 0xFFF : fr_min)), fr_n = 0;
    jr_add(t, v);
}

// Подслушка -> журнал: A0, байт на шине. Чужие чтения (/CS = 1: Eva читает свои AY) - мимо: иначе забьют
// журнал за миллисекунды после старта
static void jr_reads(void) {
    if (pio_w->fdebug & 1u << (PIO_FDEBUG_RXSTALL_LSB + sm_rs)) {   // подслушка переполнилась - чтения потеряны
        pio_w->fdebug = 1u << (PIO_FDEBUG_RXSTALL_LSB + sm_rs);
        jr_put(JR_X, 0);
    }
    while (!pio_sm_is_rx_fifo_empty(pio_w, sm_rs)) {
        uint32_t w = pio_sm_get(pio_w, sm_rs);
        uint32_t p = w >> 20, n = ~w & 0xFFFFF;              // снимок D0..A0, оборотов счётчика
        if (p >> (PIN_SLOT_CS - PIN_SLOT_D0) & 1) {        // чужой: только счёт и самый короткий
            if (!fr_n || n < fr_min) fr_min = n;
            fr_n++;
            continue;
        }
        if (n > 0x3FFF) n = 0x3FFF;
        jr_put(JR_R, n << 10 | (p >> (PIN_SLOT_A0 - PIN_SLOT_D0) & 1) << 8 | (p & 0xFF));
    }
}

void bus_hw_poll(void) {
    if (!ready) return;
    uint32_t s = save_and_disable_interrupts();
    jr_reads();
    restore_interrupts(s);
}

// Ответ в поток #C1: байт 0 - сразу в значение слова (R увидит с выбора #C1), остальные и тень - в sbuf, их по одному
// кладёт N2 по меткам T. n = 0 - в слове тень. Тень - что Спектрум записал в #C1 (значение слова до ответа)
void bus_hw_reply(int c, const uint8_t *r, uint16_t n) {
    (void)c;
    if (!ready) return;
    if (pio_sm_get_pc(pio_w, sm_t) != t_off) bus_hw_left++;   // T ещё ждёт чтений прошлого ответа - его не дочитали
    static uint8_t shadow;
    if (pio_sm_get_pc(pio_w, sm_t) == t_off) shadow = (uint8_t)ans[0][0xC1];   // поток кончился: в слове тень
    if (n > SBUF_N) n = SBUF_N;
    t_start(0);                                             // старое - прочь; N2 больше не запустится
    if (n == 0) {
        val_set(0, 0xC1, shadow);
        jr_put(JR_P, 0);
        return;
    }
    for (uint16_t i = 1; i < n; i++) sbuf[i - 1] = r[i];
    sbuf[n - 1] = shadow;
    dma_channel_set_read_addr((uint)n2, sbuf, false);
    val_set(0, 0xC1, r[0]);
    t_start(n);
    jr_put(JR_P, (uint32_t)n << 8);
}

void bus_hw_status(int c, uint8_t st) {
    if (ready && c == 0) pio_r->rxf_putget[sm_r][1] = st;
}

void bus_hw_unmark(int c) { val_set(c, 0xC0, 0); }

// Консоль: выбран ли регистр цепочкой A (cur), слова #C0 / #C1, что держит R (rxfifo 0..2), где R, флаги FIFO
void bus_hw_print(void) {
    if (!ready) return;
    static const char *st_name[] = { "exec", "chain hang", "L", "putget", "A", "A->L", "D", "D->L", };
    printf("  bus self test: %s", bus_hw_check ? "FAIL" : "OK");
    for (int i = 0; i < 8; i++) if (bus_hw_check >> i & 1) printf(" %s", st_name[i]);
    printf("\n");
    printf("  bus hw: cur reg %02lX, ans C0 %03lX C1 %03lX; R rx0 %03lX rx1 %02lX rx2 %02lX, pc %u, tx %u;"
           " fdebug w %08lX ad %08lX r %08lX; sm on %lX/%lX/%lX; D0 func %d\n",
           (unsigned long)((cur - (uint32_t)&ans[0][0]) / 4), (unsigned long)ans[0][0xC0], (unsigned long)ans[0][0xC1],
           (unsigned long)pio_r->rxf_putget[sm_r][0], (unsigned long)pio_r->rxf_putget[sm_r][1],
           (unsigned long)pio_r->rxf_putget[sm_r][2], (unsigned)pio_sm_get_pc(pio_r, sm_r),
           (unsigned)pio_sm_get_tx_fifo_level(pio_r, sm_r), (unsigned long)pio_w->fdebug,
           (unsigned long)pio_ad->fdebug, (unsigned long)pio_r->fdebug, (unsigned long)(pio_w->ctrl & 15),
           (unsigned long)(pio_ad->ctrl & 15), (unsigned long)(pio_r->ctrl & 15), (int)gpio_get_function(PIN_SLOT_D0));
    // журнал: Wa:vv - запись (a = A0), Ra:vv/нс - чтение нашего чипа и длина /RD, Fn/нс - чужих n (самый короткий), X - подслушка теряла, P n - ответ n байт
    static uint32_t shown;
    uint32_t n = jr_n;
    if (n == shown && !bus_hw_jr_force) return;
    bus_hw_jr_force = false;
    shown = n;
    printf("  bus journal %lu:", (unsigned long)n);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t e = jr[i], v = e & 0xFFFFFF;
        if (e >> 24 == JR_W) printf(" W%lu:%02lX", (unsigned long)(v >> 8 & 1), (unsigned long)(v & 0xFF));
        else if (e >> 24 == JR_R) printf(" R%lu:%02lX/%lu", (unsigned long)(v >> 8 & 1), (unsigned long)(v & 0xFF),
                                         (unsigned long)((v >> 10) * 3000000ull / (clock_get_hz(clk_sys) / 1000)));
        else if (e >> 24 == JR_F) printf(" F%lu/%lu", (unsigned long)(v >> 12),
                                         (unsigned long)((v & 0xFFF) * 3000000ull / (clock_get_hz(clk_sys) / 1000)));
        else if (e >> 24 == JR_X) printf(" X");
        else printf(" P%lu", (unsigned long)(v >> 8));
    }
    printf("\n");
}

// ---- Стенд (консоль 'b', Ева ВЫКЛЮЧЕНА): ядро 0 изображает Спектрум - подмена входов ножек слота (INOVER), выход на
// ножки D запрещён (OEOVER), ответ R - с его защёлки выхода (DBG_PADOUT / DBG_PADOE). Работает всё настоящее: PIO, DMA,
// ядро 1 (кольцо записей, card.c, движок). Последовательность - как SDK Detect (sdk/trinity_ay.asm: InfoSend, SvcState,
// Detect). Строб и пауза - как у Z80 (замер журнала v46: /RD на 3.5 МГц ~450 нс; 14 МГц - в 4 раза короче, OUT (C) - 12 T)
typedef struct { const char *name; uint32_t st, gap; } bn_t;   // имя, строб нс, пауза до следующего обращения нс
static const bn_t bn_spd[] = { { "3.5 MHz", 450, 3000 }, { "14 MHz", 112, 745 } };
static uint32_t bn_khz;

static void bn_ns(uint32_t ns) { busy_wait_at_least_cycles((uint32_t)((uint64_t)ns * bn_khz / 1000000u)); }
static void bn_pin(uint p, bool v) { gpio_set_inover(p, v ? GPIO_OVERRIDE_HIGH : GPIO_OVERRIDE_LOW); }
static void bn_data(uint8_t v) { for (uint i = 0; i < 8; i++) bn_pin(PIN_SLOT_D0 + i, v >> i & 1); }

// Запись Спектрума: a0 = 0 - #FFFD (адрес), 1 - #BFFD (данные)
static void bn_wr(const bn_t *t, bool a0, uint8_t v) {
    uint32_t s = save_and_disable_interrupts();
    bn_data(v);
    bn_pin(PIN_SLOT_A0, a0);
    bn_pin(PIN_SLOT_CS, 0);
    bn_ns(30);                                      // адрес раньше строба
    bn_pin(PIN_SLOT_WR, 0);
    bn_ns(t->st);
    bn_pin(PIN_SLOT_WR, 1);
    bn_ns(10);
    bn_pin(PIN_SLOT_CS, 1);
    restore_interrupts(s);
    bn_ns(t->gap);
}

// Чтение Спектрума (#FFFD: A0 = 1). Байт - что выдал R в конце строба (там Z80 и защёлкивает); -1 - R не вышел на шину
static int bn_rd(const bn_t *t) {
    uint32_t s = save_and_disable_interrupts();
    bn_data(0xFF);
    bn_pin(PIN_SLOT_A0, 1);
    bn_pin(PIN_SLOT_CS, 0);
    bn_ns(30);
    bn_pin(PIN_SLOT_RD, 0);
    bn_ns(t->st);
    uint32_t out = pio_r->dbg_padout, oe = pio_r->dbg_padoe;
    bn_pin(PIN_SLOT_RD, 1);
    bn_ns(10);
    bn_pin(PIN_SLOT_CS, 1);
    restore_interrupts(s);
    bn_ns(t->gap);
    if ((oe >> PIN_SLOT_D0 & 0xFF) != 0xFF) return -1;
    return (int)(out >> PIN_SLOT_D0 & 0xFF);
}

// Один Detect как SDK: ключ, INFO в поток, закрыть, метка; статус до «ответ готов»; #C1 - 5 байт. Итог: 0 - TRIN,
// 1 - статус не дождались, 2 - не тот ответ, 3 - после ответа не тень; got - что прочли (статус, 5 байт, тень)
static int bn_detect(const bn_t *t, int got[7]) {
    static const uint8_t key[4] = { 'T', 'R', 'I', 'N' };
    bn_wr(t, 0, 0xFF);                              // чип 0
    bn_wr(t, 0, 0xC0);
    for (int i = 0; i < 4; i++) bn_wr(t, 1, key[i]);
    bn_wr(t, 0, 0xC1);
    bn_wr(t, 1, AYFX_P_INFO);
    bn_wr(t, 0, 0xC0);                              // закрыть
    bn_wr(t, 1, 0x00);
    bn_wr(t, 1, 0x80);                              // метка
    int st = -1;
    for (int i = 0; i < 2000; i++) {                // SvcState: выбор #C0, чтение; пока не «занята» и есть ответ
        bn_wr(t, 0, 0xC0);
        st = bn_rd(t);
        if (st >= 0 && (st & 0xC0) == 0x40) break;
    }
    got[0] = st;
    if (st < 0 || (st & 0xC0) != 0x40) return 1;
    bn_wr(t, 0, 0xC1);
    for (int i = 0; i < 6; i++) got[1 + i] = bn_rd(t);   // ответ 5 байт и тень
    for (int i = 0; i < 4; i++) if (got[1 + i] != key[i]) return 2;
    if (got[6] != AYFX_P_INFO) return 3;            // после ответа - тень: что записали в #C1
    return 0;
}

// Служебный пакет как SDK (Ayfx_SvcSend + Wait + Ayfx_SvcReply): ключ, поток, байты, закрыть, метка; статус - пока
// «занята» (ядро 0 тем временем разбирает пакеты - как его цикл, card_svc_poll); ответ: длина 2, байты. Итог: длина
// ответа, -1 - нет ответа (rep[0] - последний статус), -2 - не дождались
static int bn_svc(const bn_t *t, const uint8_t *p, int n, int *rep, int max) {
    static const uint8_t key[4] = { 'T', 'R', 'I', 'N' };
    bn_wr(t, 0, 0xFF);
    bn_wr(t, 0, 0xC0);
    for (int i = 0; i < 4; i++) bn_wr(t, 1, key[i]);
    bn_wr(t, 0, 0xC1);
    for (int i = 0; i < n; i++) bn_wr(t, 1, p[i]);
    bn_wr(t, 0, 0xC0);
    bn_wr(t, 1, 0x00);
    bn_wr(t, 1, 0x80);
    int st = -1;
    for (int i = 0; i < 20000; i++) {             // SvcState: каждый раз выбор #C0, потом чтение
        bn_wr(t, 0, 0xC0);
        st = bn_rd(t);
        if (st >= 0 && !(st & 0x80)) break;
        card_svc_poll(false);
    }
    rep[0] = st;
    if (st < 0 || (st & 0x80)) return -2;
    if (!(st & 0x40)) return -1;
    bn_wr(t, 0, 0xC1);
    int lo = bn_rd(t), hi = bn_rd(t), len = lo | hi << 8;
    for (int i = 0; i < len; i++) { int v = bn_rd(t); if (i < max) rep[i] = v; }
    return len;
}

// Ход плагина SNDH (tools/wcsndh: Load, Play): слоты SNDH и STAT, файл (мусор) - SLOT_WRITE, PLAY -> итог 0 (принял;
// мусор плеер потом не разберёт - это уже задание), MODE AY, слоты - прочь. Итог: 0 - всё как надо, иначе шаг
static int bn_plugin(const bn_t *t, int *why) {
    int rep[8];
    uint8_t p[16 + 64];
    static const uint8_t pn[] = { 0x80, 9, 0, 'S', 'N', 'D', 'H', 64, 0, 0, 0, 0 };
    static const uint8_t ps[] = { 0x80, 9, 0, 'S', 'T', 'A', 'T', 32, 0, 0, 0, 0 };
    if (bn_svc(t, pn, sizeof pn, rep, 8) != 1 || rep[0] == 0) { *why = rep[0]; return 1; }
    int si = rep[0];
    if (bn_svc(t, ps, sizeof ps, rep, 8) != 1 || rep[0] == 0) { *why = rep[0]; return 2; }
    int ss = rep[0];
    p[0] = 0x81, p[1] = 5 + 64, p[2] = 0, p[3] = (uint8_t)si, p[4] = p[5] = p[6] = p[7] = 0;
    for (int i = 0; i < 64; i++) p[8 + i] = (uint8_t)i;
    int r = bn_svc(t, p, 8 + 64, rep, 8);
    if (r != -1) { *why = r; return 3; }                 // без ответа
    uint8_t pc[] = { 0x91, 8, 0, 1, 1, (uint8_t)si, (uint8_t)ss, 0, 0, 0, 0 };
    r = bn_svc(t, pc, sizeof pc, rep, 8);
    if (r != 1 || rep[0] != 0) { *why = r == 1 ? rep[0] : 100 + r; return 4; }
    uint8_t pm[] = { 0xC0, 1, 0, 0 };
    r = bn_svc(t, pm, sizeof pm, rep, 8);
    if (r != 1 || rep[0] != 0) { *why = r == 1 ? rep[0] : 100 + r; return 5; }
    uint8_t pf[] = { 0x84, 1, 0, (uint8_t)si };
    bn_svc(t, pf, sizeof pf, rep, 8);
    pf[3] = (uint8_t)ss;
    bn_svc(t, pf, sizeof pf, rep, 8);
    return 0;
}

// Настоящий SNDH (отладка стенда, CMake BN_SNDH - путь к файлу): весь ход плагина с ним - залить, PLAY, ждать, что
// скажет задание (состояние, лог), STOP, MODE AY, слоты прочь
#ifdef BN_SNDH
__asm__(".section .flashdata.bn_file,\"a\"\n.balign 4\n.global bn_file\nbn_file:\n.incbin \"" BN_SNDH "\"\n.global bn_file_end\nbn_file_end:\n.text\n");
extern const uint8_t bn_file[], bn_file_end[];

static void bn_real(const bn_t *t) {
    static uint8_t p[8 + 512];
    static int rep[600];
    uint32_t size = (uint32_t)(bn_file_end - bn_file);
    uint8_t pn[] = { 0x80, 9, 0, 'S', 'N', 'D', 'H', (uint8_t)size, (uint8_t)(size >> 8), (uint8_t)(size >> 16), 0, 0 };
    static const uint8_t ps[] = { 0x80, 9, 0, 'S', 'T', 'A', 'T', 32, 0, 0, 0, 0 };
    int r = bn_svc(t, pn, sizeof pn, rep, 8);
    printf("  bench %s real SNDH %lu bytes: SLOT_NEW -> %d (%d)", t->name, (unsigned long)size, r, rep[0]);
    int si = rep[0];
    r = bn_svc(t, ps, sizeof ps, rep, 8);
    int ss = rep[0];
    printf(", STAT -> %d (%d)", r, ss);
    int lost = 0;
    for (uint32_t off = 0; off < size; off += 512) {
        uint32_t n = size - off < 512 ? size - off : 512;
        p[0] = 0x81, p[1] = (uint8_t)(5 + n), p[2] = (uint8_t)((5 + n) >> 8), p[3] = (uint8_t)si;
        p[4] = (uint8_t)off, p[5] = (uint8_t)(off >> 8), p[6] = (uint8_t)(off >> 16), p[7] = 0;
        for (uint32_t i = 0; i < n; i++) p[8 + i] = bn_file[off + i];
        if (bn_svc(t, p, (int)(8 + n), rep, 8) != -1) lost++;
    }
    printf(", writes with reply %d", lost);
    uint8_t pc[] = { 0x91, 8, 0, 1, 1, (uint8_t)si, (uint8_t)ss, 0, 0, 0, 0 };
    r = bn_svc(t, pc, sizeof pc, rep, 8);
    printf(", PLAY -> %d (%d)\n", r, rep[0]);
    static const uint8_t pj[] = { 0xA0, 0, 0 }, pl[] = { 0xA1, 0, 0 };
    for (int i = 0; i < 300; i++) {                     // ~ пока задание не заиграло / не упало
        r = bn_svc(t, pj, sizeof pj, rep, 8);
        if (r >= 2 && rep[0] != 1) break;
        if (r >= 2 && rep[0] == 1 && rep[1] > 0) break;
        busy_wait_us_32(10000);
        card_svc_poll(false);
    }
    printf("  bench %s real: job state %d progress %d;", t->name, rep[0], rep[1]);
    r = bn_svc(t, pl, sizeof pl, rep, 600);
    printf(" log:");
    for (int i = 0; i < r && i < 600; i++) putchar(rep[i] >= 32 && rep[i] < 127 ? rep[i] : rep[i] == 10 ? '|' : '.');
    printf("\n");
    uint8_t pst[] = { 0x91, 8, 0, 1, 2, (uint8_t)si, (uint8_t)ss, 0, 0, 0, 0 };
    r = bn_svc(t, pst, sizeof pst, rep, 8);
    uint8_t pm[] = { 0xC0, 1, 0, 0 };
    int r2 = bn_svc(t, pm, sizeof pm, rep + 4, 8);
    uint8_t pf[] = { 0x84, 1, 0, (uint8_t)si };
    bn_svc(t, pf, sizeof pf, rep + 8, 8);
    pf[3] = (uint8_t)ss;
    bn_svc(t, pf, sizeof pf, rep + 8, 8);
    printf("  bench %s real: STOP -> %d (%d), MODE AY -> %d (%d)\n", t->name, r, rep[0], r2, rep[4]);
}
#endif

void bus_hw_bench(void) {
    if (!ready) return;
    bn_khz = clock_get_hz(clk_sys) / 1000;
    for (uint p = PIN_SLOT_D0; p < PIN_SLOT_D0 + 8; p++) gpio_set_oeover(p, GPIO_OVERRIDE_LOW);   // на шину - ничего
    bn_pin(PIN_SLOT_IC, 1);                         // сброса нет
    bn_pin(PIN_SLOT_CS, 1), bn_pin(PIN_SLOT_WR, 1), bn_pin(PIN_SLOT_RD, 1);
    busy_wait_us_32(100);
    for (unsigned s = 0; s < sizeof bn_spd / sizeof bn_spd[0]; s++) {
        const bn_t *t = &bn_spd[s];
        int ok = 0, fail_run = -1, fail_why = 0, fail_got[7] = { 0 };
        for (int run = 0; run < 200; run++) {
            int got[7];
            int r = bn_detect(t, got);
            if (r == 0) ok++;
            else if (fail_run < 0) { fail_run = run, fail_why = r; for (int i = 0; i < 7; i++) fail_got[i] = got[i]; }
        }
        printf("  bench %s: Detect %d of 200 OK", t->name, ok);
        if (fail_run >= 0) {
            printf("; first fail run %d (%s): status %d, #C1", fail_run, fail_why == 1 ? "no reply" : fail_why == 2 ? "wrong reply" : "no shadow after reply",
                   fail_got[0]);
            for (int i = 1; i < 7; i++) printf(" %d", fail_got[i]);
        }
        printf("\n");
        int pl_ok = 0, pl_step = 0, pl_why = 0, pl_run = -1;
        for (int run = 0; run < 50; run++) {
            int why = 0, st = bn_plugin(t, &why);
            if (st == 0) pl_ok++;
            else if (pl_run < 0) pl_run = run, pl_step = st, pl_why = why;
        }
#ifdef BN_SNDH
        bn_real(t);
#endif
        printf("  bench %s: plugin %d of 50 OK", t->name, pl_ok);
        if (pl_run >= 0) printf("; first fail run %d: step %d (1 SNDH, 2 STAT, 3 WRITE, 4 PLAY, 5 MODE), got %d", pl_run,
                                pl_step, pl_why);
        printf("\n");
    }
    for (uint p = PIN_SLOT_D0; p < PIN_SLOT_D0 + 8; p++) gpio_set_inover(p, GPIO_OVERRIDE_NORMAL);
    for (uint p = PIN_SLOT_CS; p <= PIN_SLOT_A0; p++) gpio_set_inover(p, GPIO_OVERRIDE_NORMAL);
    gpio_set_inover(PIN_SLOT_IC, GPIO_OVERRIDE_NORMAL);
    for (uint p = PIN_SLOT_D0; p < PIN_SLOT_D0 + 8; p++) gpio_set_oeover(p, GPIO_OVERRIDE_NORMAL);
}
