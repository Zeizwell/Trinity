// Ответ на чтение железом (doc/zx-trinity.md п.14е). Цепочки DMA - как в работе, только вместо автоматов A / D
// слова кладёт ядро (автомат bus_feed), а найденное слово L пишет не в R, а в out_w - его и ждём
#include "bus_dma.h"
#include "chip.h"                           // cyc_now - такты RP
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "hardware/clocks.h"
#include "hardware/structs/busctrl.h"
#include "bus_dma.pio.h"
#include <stdio.h>

#define RUNS     1000
#define WAIT_MAX 100000                     // столько опросов без ответа - цепочка встала

static uint32_t ans[2][256] __attribute__((aligned(2048)));  // слово ответа: значение, род (п.14е-1), чип - 1К
static uint8_t  one[256] __attribute__((aligned(256)));      // one[i] = i: байт данных как адрес для D2
static volatile uint32_t cur;               // адрес слова выбранного регистра: пишет A1, читают A1b и D1b
static volatile uint32_t out_w;             // сюда L кладёт найденное слово (в работе - R.RXF_PUTGET0)

static PIO  dma_pio = pio0;                 // PIO1 / PIO2 - автоматы шины (bus_hw.c)
static uint sm_a, sm_d;                     // подставные A и D (bus_feed)
static int  a1, a1b, l, d1, d1b, d2;        // каналы цепочек
static bool ready;

// Канал: 32 бита, адреса стоят, высокий приоритет; treq - DREQ или DREQ_FORCE, chain - кого запустить в конце
static dma_channel_config ch_cfg(int ch, uint treq, int chain) {
    dma_channel_config c = dma_channel_get_default_config(ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, treq);
    channel_config_set_chain_to(&c, chain);
    channel_config_set_high_priority(&c, true);
    return c;
}

// Счётчик передач: n и после конца - снова ждать свой DREQ (режим TRIGGER_SELF)
static uint32_t self_n(uint32_t n) {
    return n | DMA_CH0_TRANS_COUNT_MODE_VALUE_TRIGGER_SELF << DMA_CH0_TRANS_COUNT_MODE_LSB;
}

static void dma_init(void) {
    cyc_init();                                             // SysTick у каждого ядра свой: замер - на ядре 0
    for (int c = 0; c < 2; c++)                             // слово: значение r, метка чипа и регистра выше рода -
        for (int r = 0; r < 256; r++)                       //   слова все разные, ответ узнаём по нему
            ans[c][r] = (uint32_t)r | (uint32_t)c << 24 | (uint32_t)r << 16;
    for (int i = 0; i < 256; i++) one[i] = (uint8_t)i;

    uint off = pio_add_program(dma_pio, &bus_feed_program);
    sm_a = pio_claim_unused_sm(dma_pio, true);
    sm_d = pio_claim_unused_sm(dma_pio, true);
    uint sm[2] = { sm_a, sm_d };
    for (int i = 0; i < 2; i++) {
        pio_sm_config c = bus_feed_program_get_default_config(off);
        pio_sm_init(dma_pio, sm[i], off, &c);
        pio_sm_set_enabled(dma_pio, sm[i], true);
    }

    a1 = dma_claim_unused_channel(true), a1b = dma_claim_unused_channel(true), l = dma_claim_unused_channel(true);
    d1 = dma_claim_unused_channel(true), d1b = dma_claim_unused_channel(true), d2 = dma_claim_unused_channel(true);
    busctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;   // ядра DMA не задержат

    // Выбор: A1 (DREQ A) адрес слова -> cur; A1b: cur -> L.READ_ADDR_TRIG; L: слово -> out_w
    dma_channel_config c = ch_cfg(l, DREQ_FORCE, l);
    dma_channel_configure(l, &c, &out_w, &ans[0][0], 1, false);
    c = ch_cfg(a1b, DREQ_FORCE, a1b);
    dma_channel_configure(a1b, &c, &dma_hw->ch[l].al3_read_addr_trig, &cur, 1, false);
    c = ch_cfg(a1, pio_get_dreq(dma_pio, sm_a, false), a1b);
    dma_channel_configure(a1, &c, &cur, &dma_pio->rxf[sm_a], self_n(1), true);

    // Данные: D1 (DREQ D) адрес one[v] -> D2.READ_ADDR; D1b: cur -> D2.WRITE_ADDR_TRIG; D2: байт -> значение слова,
    // в конце - A1b: найти слово заново (чтение назад записанного)
    c = ch_cfg(d2, DREQ_FORCE, a1b);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    dma_channel_configure(d2, &c, &ans[0][0], one, 1, false);
    c = ch_cfg(d1b, DREQ_FORCE, d1b);
    dma_channel_configure(d1b, &c, &dma_hw->ch[d2].al2_write_addr_trig, &cur, 1, false);
    c = ch_cfg(d1, pio_get_dreq(dma_pio, sm_d, false), d1b);
    dma_channel_configure(d1, &c, &dma_hw->ch[d2].read_addr, &dma_pio->rxf[sm_d], self_n(1), true);
    ready = true;
}

// Слово w - в подставной автомат sm, ждать out_w == want -> такты RP (UINT32_MAX - не дождались)
static uint32_t meas(uint sm, uint32_t w, uint32_t want) {
    uint32_t s = save_and_disable_interrupts();
    uint32_t t0 = cyc_now();
    pio_sm_put(dma_pio, sm, w);
    int n = 0;
    while (out_w != want && ++n < WAIT_MAX) {}
    uint32_t d = (t0 - cyc_now()) & 0xFFFFFF;
    restore_interrupts(s);
    return n < WAIT_MAX ? d : UINT32_MAX;
}

// Итог: такты и нс
static void stat_print(const char *what, uint32_t mx, uint64_t sum, int n, int err) {
    uint32_t mhz = clock_get_hz(clk_sys) / 1000000;
    uint32_t avg = n ? (uint32_t)(sum / n) : 0;
    printf("  %s: max %lu cyc (%lu ns), avg %lu cyc (%lu ns), errors %d\n", what, (unsigned long)mx,
           (unsigned long)(mx * 1000 / mhz), (unsigned long)avg, (unsigned long)(avg * 1000 / mhz), err);
}

// Нагрузка для замера (ядро 1): SRAM и общий порт быстрых AHB (PIO, регистры DMA) - без пауз, пока просят
volatile bool bus_dma_load;
static volatile bool load_on;
static volatile uint32_t load_buf[2048];

void bus_dma_hammer(void) {
    load_on = true;
    for (uint32_t i = 0; bus_dma_load; i += 7) load_buf[i & 2047] += dma_pio->fdebug;
    load_on = false;
}

// Прогон: RUNS выборов и записей данных, итог - в консоль
static void pass(const char *what) {
    uint32_t smx = 0, dmx = 0;
    uint64_t ssum = 0, dsum = 0;
    int sn = 0, dn = 0, serr = 0, derr = 0;
    for (int i = 0; i < RUNS; i++) {
        int c = i & 1, r = (i * 37 + 11) & 0xFF;                    // чип и регистр - вразброс
        // выбор регистра: ответ - его слово
        uint32_t d = meas(sm_a, (uint32_t)&ans[c][r], ans[c][r]);
        if (d == UINT32_MAX) serr++;
        else { if (d > smx) smx = d; ssum += d; sn++; }
        // данные: значение меняется, ответ - слово с новым значением
        uint8_t v = (uint8_t)(ans[c][r] ^ 0x81);
        uint32_t want = (ans[c][r] & ~0xFFu) | v;
        d = meas(sm_d, (uint32_t)&one[v], want);
        if (d == UINT32_MAX || ans[c][r] != want) derr++;
        else { if (d > dmx) dmx = d; dsum += d; dn++; }
    }
    printf(" %s:\n", what);
    stat_print("select -> answer", smx, ssum, sn, serr);
    stat_print("data -> answer  ", dmx, dsum, dn, derr);
}

void bus_dma_test(void) {
    if (!ready) dma_init();
    uint32_t t0 = cyc_now(), base = (t0 - cyc_now()) & 0xFFFFFF;    // сам замер (два чтения счётчика)
    printf("bus dma test: %d runs, measure overhead %lu cyc\n", RUNS, (unsigned long)base);
    pass("cores idle");
    bus_dma_load = true;                                            // ядро 1 - нагрузка (main.c, цикл ядра 1)
    while (!load_on) {}
    pass("core 1 hammers SRAM + PIO port");
    bus_dma_load = false;
}
