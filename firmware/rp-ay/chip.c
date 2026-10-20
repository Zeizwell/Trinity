// Чип: очередь записей "регистр + данные" и писатель (chip.h). Цикл записи ведёт PIO (chip_wr.pio): ядро кладёт
// состояния ножек с длительностями и никогда не ждёт - ответы на чтение слота успевают. Тест чтения - ножками
// напрямую (SIO), PIO на это время отключён от ножек.
#include "chip.h"
#include "config.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "chip_wr.pio.h"

chip_stat_t chip_stat;

static uint16_t q[CHIP_Q_SIZE];           // регистр << 8 | данные
static volatile uint32_t q_head, q_tail;  // head - писать сюда, tail - брать отсюда
static uint8_t  step;                     // какое состояние записи q_tail кладём следующим (0..7)

static PIO  wr_pio = pio0;                // окно ножек 16..47 (main: pio_set_gpio_base)
static uint wr_sm, wr_off;                // машина записи и адрес её программы (pull - первая команда)
static uint32_t c_setup, c_pulse, c_hold, c_addr, c_ssg, c_fm;   // длительности из cfg, такты PIO

#define CHIP_PINS_MASK  ((0xFFull << PIN_CHIP_D0) | (1ull << PIN_CHIP_CS) | (1ull << PIN_CHIP_WR) | (1ull << PIN_CHIP_A0))
#define P_CS  (1u << 8)                   // биты состояния ножек в слове PIO
#define P_WR  (1u << 9)
#define P_A0  (1u << 10)

static uint32_t ns_to_cyc(uint32_t ns) {
    return (uint32_t)((uint64_t)clock_get_hz(clk_sys) * ns / 1000000000u) + 1;
}
static uint32_t ht_to_cyc(uint32_t half_t) {          // полутакты Z80 -> такты RP (вверх)
    return (uint32_t)(((uint64_t)half_t * clock_get_hz(clk_sys) + 2u * cfg.z80_hz - 1) / (2u * cfg.z80_hz));
}
static uint32_t clk_to_cyc(uint32_t chip_clk) {      // такты чипа -> такты RP (вверх)
    return (uint32_t)(((uint64_t)chip_clk * clock_get_hz(clk_sys) + cfg.phim_hz - 1) / cfg.phim_hz);
}

// Слово PIO: состояние ножек и сколько тактов его держать (PIO сам тратит 4)
static uint32_t word(uint32_t pins, uint32_t cyc) {
    return pins | ((cyc > 4 ? cyc - 4 : 0) << 11);
}

void chip_init(void) {
    gpio_init(PIN_CHIP_RD);                                     // /RD чипа - вверх (чтение только в тесте)
    gpio_put(PIN_CHIP_RD, 1);
    gpio_set_dir(PIN_CHIP_RD, GPIO_OUT);
    chip_timing(0);
    wr_sm = pio_claim_unused_sm(wr_pio, true);
    wr_off = pio_add_program(wr_pio, &chip_wr_program);
    chip_wr_init(wr_pio, wr_sm, wr_off, PIN_CHIP_D0);
    pio_sm_put(wr_pio, wr_sm, word(P_CS | P_WR, 0));            // покой: /CS и /WR вверх
    chip_reset_state();
}

void chip_reset_state(void) {
    q_head = q_tail = 0;
    step = 0;
}

void chip_timing(int set) {
    c_setup = ht_to_cyc(cfg.setup_ht);
    c_pulse = ht_to_cyc(cfg.pulse_ht);
    c_hold  = ht_to_cyc(cfg.hold_ht);
    c_addr  = ht_to_cyc(cfg.gap_addr_ht);
    c_ssg   = ht_to_cyc(cfg.gap_ssg_ht);
    c_fm    = clk_to_cyc(cfg.wait_fm_clk);
    if (set == 1) {                                             // даташит YM2203: 17 тактов чипа (3.5 МГц: 4.86 мкс)
        c_addr = clk_to_cyc(17);
        c_ssg  = clk_to_cyc(17);
    }
    if (set == 2) {                                             // всё в 4 раза медленнее
        c_setup *= 4, c_pulse *= 4, c_hold *= 4, c_addr *= 4, c_ssg *= 4;
    }
}

bool chip_put(uint8_t reg, uint8_t v) {
    uint32_t h = q_head;
    if (((h + 1) & (CHIP_Q_SIZE - 1)) == q_tail) { chip_stat.q_lost++; return false; }
    q[h] = (uint16_t)(reg << 8 | v);
    q_head = (h + 1) & (CHIP_Q_SIZE - 1);
    return true;
}

// Восемь состояний на запись, как цикл Z80: регистр (установка, /WR вниз, /WR вверх при /CS внизу, /CS вверх и
// пауза адреса), данные (то же, в конце - пауза занятости). Чип защёлкивает по подъёму /WR, /CS должен быть ещё внизу
static uint32_t step_word(uint8_t reg, uint8_t v, uint8_t s) {
    switch (s) {
    case 0:  return word(reg | P_WR, c_setup);                  // регистр, A0 = 0, /CS = 0
    case 1:  return word(reg, c_pulse);                         //   /WR вниз
    case 2:  return word(reg | P_WR, c_hold);                   //   /WR вверх, /CS ещё внизу
    case 3:  return word(reg | P_CS | P_WR, c_addr);            //   /CS вверх, пауза после адреса
    case 4:  return word(v | P_A0 | P_WR, c_setup);             // данные, A0 = 1, /CS = 0
    case 5:  return word(v | P_A0, c_pulse);                    //   /WR вниз
    case 6:  return word(v | P_A0 | P_WR, c_hold);              //   /WR вверх, /CS ещё внизу
    default: return word(v | P_A0 | P_CS | P_WR, reg < 16 ? c_ssg : c_fm);   // /CS вверх, пауза занятости
    }
}

// Подложить PIO следующие состояния, пока есть место в его очереди (ядро не ждёт)
void chip_poll(void) {
    while (q_tail != q_head && !pio_sm_is_tx_fifo_full(wr_pio, wr_sm)) {
        uint16_t e = q[q_tail];
        pio_sm_put(wr_pio, wr_sm, step_word((uint8_t)(e >> 8), (uint8_t)e, step));
        if (++step < 8) continue;
        step = 0;
        q_tail = (q_tail + 1) & (CHIP_Q_SIZE - 1);
        chip_stat.chip_wr += 2;                                 // циклы: адрес и данные
    }
}

// Всё записано: очередь пуста, у PIO пусто и он стоит на pull (первая команда программы), а не держит состояние.
// (Флаг TXSTALL не годится: ставится только в момент остановки, после сброса снова не встаёт.)
bool chip_idle(void) {
    if (q_tail != q_head || !pio_sm_is_tx_fifo_empty(wr_pio, wr_sm)) return false;
    return pio_sm_get_pc(wr_pio, wr_sm) == wr_off;
}

// ---- Тест чтения. Ножки чипа - на время теста ядру (SIO), пишем и читаем сразу, паузы - те же из cfg
#define CHIP_D_MASK  (0xFFull << PIN_CHIP_D0)

static void wait_cyc(uint32_t cyc) { busy_wait_at_least_cycles(cyc); }

// Один цикл записи ножками: A0, данные, /CS и /WR - импульс по cfg
static void sio_cycle(bool a0, uint8_t v) {
    uint64_t val = ((uint64_t)v << PIN_CHIP_D0) | (a0 ? (1ull << PIN_CHIP_A0) : 0) | (1ull << PIN_CHIP_WR);
    gpio_put_masked64(CHIP_PINS_MASK, val);                     // данные, A0, /CS = 0, /WR = 1
    wait_cyc(c_setup);
    gpio_put_masked64(1ull << PIN_CHIP_WR, 0);                  // /WR вниз
    wait_cyc(c_pulse);
    gpio_put(PIN_CHIP_WR, 1);                                   // /WR вверх, /CS ещё внизу
    wait_cyc(c_hold);
    gpio_put(PIN_CHIP_CS, 1);                                   // /CS вверх
}

static void write_now(uint8_t reg, uint8_t v) {
    sio_cycle(false, reg); wait_cyc(c_addr);
    sio_cycle(true, v);    wait_cyc(c_ssg);
}

// Чтение: регистр, потом данные - вход, /CS и /RD вниз при A0 = 1, читаем, всё вверх, данные снова выход
static uint8_t read_now(uint8_t reg) {
    sio_cycle(false, reg); wait_cyc(c_addr);
    gpio_set_dir_masked64(CHIP_D_MASK, 0);                      // сначала отпустить данные
    gpio_put_masked64((1ull << PIN_CHIP_A0) | (1ull << PIN_CHIP_CS), 1ull << PIN_CHIP_A0);   // A0 = 1, /CS = 0
    wait_cyc(c_setup);
    gpio_put(PIN_CHIP_RD, 0);
    wait_cyc(ns_to_cyc(cfg.rd_pulse_ns));
    uint8_t v = (uint8_t)(gpio_get_all64() >> PIN_CHIP_D0);
    gpio_put(PIN_CHIP_RD, 1);
    gpio_put(PIN_CHIP_CS, 1);
    wait_cyc(c_pulse);                                          // чип отпустил шину
    gpio_set_dir_masked64(CHIP_D_MASK, CHIP_D_MASK);            // данные - снова выход
    return v;
}

// Сколько бит в регистрах SSG 0..13 (R7: биты портов 6, 7 не трогаем)
static const uint8_t ssg_mask[14] = { 0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0x1F, 0x3F, 0x1F, 0x1F, 0x1F, 0xFF, 0xFF, 0x0F };

static void cmp(chip_test_t *r, uint8_t reg, uint8_t v, uint8_t got) {
    r->checks++;
    if (((v ^ got) & ssg_mask[reg]) == 0) return;
    r->bad_bits |= (uint8_t)((v ^ got) & ssg_mask[reg]);
    if (!r->errors) r->bad_reg = reg, r->bad_wrote = v, r->bad_read = got;
    r->errors++;
}

// Каждый регистр SSG 0..13 - все значения 0..255: записать в чип и сразу прочитать обратно (3584 проверки)
void chip_test(chip_test_t *r) {
    *r = (chip_test_t){ 0 };
    gpio_put_masked64(CHIP_PINS_MASK, (1ull << PIN_CHIP_CS) | (1ull << PIN_CHIP_WR));   // ножки - ядру, покой
    gpio_set_dir_masked64(CHIP_PINS_MASK, CHIP_PINS_MASK);
    for (uint p = PIN_CHIP_D0; p <= PIN_CHIP_A0; p++) gpio_set_function(p, GPIO_FUNC_SIO);
    for (uint8_t reg = 0; reg < 14; reg++)
        for (uint v = 0; v < 256; v++) {
            write_now(reg, (uint8_t)v & ssg_mask[reg]);
            cmp(r, reg, (uint8_t)v, read_now(reg));
        }
    for (uint p = PIN_CHIP_D0; p <= PIN_CHIP_A0; p++) pio_gpio_init(wr_pio, p);   // ножки - снова PIO (стоит в покое)
}
