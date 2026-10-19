// Чип: очередь записей "регистр + данные" и писатель (chip.h). Пишет в чип только chip_poll (ядро 1).
#include "chip.h"
#include "config.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"

chip_stat_t chip_stat;

static uint16_t q[CHIP_Q_SIZE];           // регистр << 8 | данные
static volatile uint32_t q_head, q_tail;  // head - писать сюда, tail - брать отсюда
static uint32_t ready_at;                 // time_us_32, когда чип готов к следующему циклу
static bool     data_next;                // регистр записи q_tail уже выставлен - следующий цикл данные

#define CHIP_PINS_MASK  ((0xFFull << PIN_CHIP_D0) | (1ull << PIN_CHIP_CS) | (1ull << PIN_CHIP_WR) | (1ull << PIN_CHIP_A0))

static uint32_t ns_to_cyc(uint32_t ns) {
    return (uint32_t)((uint64_t)clock_get_hz(clk_sys) * ns / 1000000000u) + 1;
}
static uint32_t clk_to_us(uint32_t chip_clk) {      // такты чипа -> мкс (вверх)
    return (uint32_t)(((uint64_t)chip_clk * 1000000u + cfg.phim_hz - 1) / cfg.phim_hz);
}

void chip_init(void) {
    for (uint p = PIN_CHIP_D0; p <= PIN_CHIP_A0; p++) gpio_init(p);
    gpio_init(PIN_CHIP_RD);                                     // /RD чипа - вверх (чтение только в тесте)
    gpio_put(PIN_CHIP_RD, 1);
    gpio_set_dir(PIN_CHIP_RD, GPIO_OUT);
    gpio_put_masked64(CHIP_PINS_MASK, (1ull << PIN_CHIP_CS) | (1ull << PIN_CHIP_WR));
    gpio_set_dir_masked64(CHIP_PINS_MASK, CHIP_PINS_MASK);
    chip_reset_state();
}

void chip_reset_state(void) {
    q_head = q_tail = 0;
    ready_at = time_us_32();
    data_next = false;
}

bool chip_put(uint8_t reg, uint8_t v) {
    uint32_t h = q_head;
    if (((h + 1) & (CHIP_Q_SIZE - 1)) == q_tail) { chip_stat.q_lost++; return false; }
    q[h] = (uint16_t)(reg << 8 | v);
    q_head = (h + 1) & (CHIP_Q_SIZE - 1);
    return true;
}

// Один цикл записи в чип: A0, данные, /CS и /WR - импульс по cfg
static void chip_cycle(bool a0, uint8_t v) {
    uint64_t val = ((uint64_t)v << PIN_CHIP_D0) | (a0 ? (1ull << PIN_CHIP_A0) : 0) | (1ull << PIN_CHIP_WR);
    gpio_put_masked64(CHIP_PINS_MASK, val);                     // данные, A0, /CS = 0, /WR = 1
    busy_wait_at_least_cycles(ns_to_cyc(cfg.setup_ns));
    gpio_put_masked64(1ull << PIN_CHIP_WR, 0);                  // /WR вниз
    busy_wait_at_least_cycles(ns_to_cyc(cfg.wr_pulse_ns));
    gpio_put_masked64((1ull << PIN_CHIP_WR) | (1ull << PIN_CHIP_CS), (1ull << PIN_CHIP_WR) | (1ull << PIN_CHIP_CS));
    chip_stat.chip_wr++;
}

// Следующий цикл. Каждая запись очереди - всегда два цикла: регистр, потом данные (адрес в чипе не
// запоминаем - чужие записи движка его не собьют). Между ними и после - паузы занятости из cfg
void chip_poll(void) {
    if (q_tail == q_head) return;
    if ((int32_t)(time_us_32() - ready_at) < 0) return;         // чип ещё занят
    uint16_t e = q[q_tail];
    uint8_t reg = (uint8_t)(e >> 8), v = (uint8_t)e;
    if (!data_next) {                                           // 1) регистр
        chip_cycle(false, reg);
        data_next = true;
        ready_at = time_us_32() + clk_to_us(cfg.wait_addr_clk);
        return;                                                 // данные - в следующий раз (когда готов)
    }
    chip_cycle(true, v);                                        // 2) данные
    data_next = false;
    q_tail = (q_tail + 1) & (CHIP_Q_SIZE - 1);
    ready_at = time_us_32() + clk_to_us(reg < 16 ? cfg.wait_ssg_clk : cfg.wait_fm_clk);
}

bool chip_idle(void) { return q_tail == q_head && !data_next; }

// ---- Тест чтения. Пишем и читаем сразу, паузы - те же из cfg
#define CHIP_D_MASK  (0xFFull << PIN_CHIP_D0)

static void wait_clk(uint32_t clk) { busy_wait_us_32(clk_to_us(clk)); }

static void write_now(uint8_t reg, uint8_t v) {
    chip_cycle(false, reg); wait_clk(cfg.wait_addr_clk);
    chip_cycle(true, v);    wait_clk(cfg.wait_ssg_clk);
}

// Чтение: регистр, потом данные - вход, /CS и /RD вниз при A0 = 1, читаем, всё вверх, данные снова выход
static uint8_t read_now(uint8_t reg) {
    chip_cycle(false, reg); wait_clk(cfg.wait_addr_clk);
    gpio_set_dir_masked64(CHIP_D_MASK, 0);                      // сначала отпустить данные
    gpio_put_masked64((1ull << PIN_CHIP_A0) | (1ull << PIN_CHIP_CS), 1ull << PIN_CHIP_A0);   // A0 = 1, /CS = 0
    busy_wait_at_least_cycles(ns_to_cyc(cfg.setup_ns));
    gpio_put(PIN_CHIP_RD, 0);
    busy_wait_at_least_cycles(ns_to_cyc(cfg.rd_pulse_ns));
    uint8_t v = (uint8_t)(gpio_get_all64() >> PIN_CHIP_D0);
    gpio_put(PIN_CHIP_RD, 1);
    gpio_put(PIN_CHIP_CS, 1);
    busy_wait_at_least_cycles(ns_to_cyc(cfg.wr_pulse_ns));      // чип отпустил шину
    gpio_set_dir_masked64(CHIP_D_MASK, CHIP_D_MASK);            // данные - снова выход
    return v;
}

// Сколько бит в регистрах SSG 0..13 (R7: биты портов 6, 7 не трогаем)
static const uint8_t ssg_mask[14] = { 0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0x1F, 0x3F, 0x1F, 0x1F, 0x1F, 0xFF, 0xFF, 0x0F };

static void cmp(chip_test_t *r, uint8_t reg, uint8_t v, uint8_t got) {
    r->checks++;
    if (((v ^ got) & ssg_mask[reg]) == 0) return;
    if (!r->errors) r->bad_reg = reg, r->bad_wrote = v, r->bad_read = got;
    r->errors++;
}

void chip_test(chip_test_t *r) {
    static const uint8_t pat[4] = { 0x00, 0xFF, 0x55, 0xAA };
    *r = (chip_test_t){ 0 };
    for (int p = 0; p < 4; p++)                                 // 1) каждый регистр: записать и сразу прочитать
        for (uint8_t reg = 0; reg < 14; reg++) {
            write_now(reg, pat[p] & ssg_mask[reg]);
            cmp(r, reg, pat[p], read_now(reg));
        }
    for (uint8_t reg = 0; reg < 14; reg++)                      // 2) все разными, потом все прочитать (адреса не путаются)
        write_now(reg, (uint8_t)(reg * 0x11 + 0x23) & ssg_mask[reg]);
    for (uint8_t reg = 0; reg < 14; reg++)
        cmp(r, reg, (uint8_t)(reg * 0x11 + 0x23), read_now(reg));
}
