// Чип: тень, очередь, писатель (chip.h). Пишет в чип только chip_poll (ядро 1).
#include "chip.h"
#include "config.h"
#include "modes.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"

chip_stat_t chip_stat;
volatile uint8_t chip_regs[256];
volatile uint8_t chip_sel;

static uint16_t q[CHIP_Q_SIZE];           // запись: бит 8 - A0, 7..0 - значение
static volatile uint32_t q_head, q_tail;  // head - писать сюда, tail - брать отсюда
static uint32_t ready_at;                 // такт ЦП (DWT не нужен - time_us_32 * MHz), когда чип готов
static uint8_t chip_addr;                 // адрес, последний записанный в сам чип (для паузы SSG / FM)

#define CHIP_PINS_MASK  ((0xFFull << PIN_CHIP_D0) | (1ull << PIN_CHIP_CS) | (1ull << PIN_CHIP_WR) | (1ull << PIN_CHIP_A0))

static uint32_t ns_to_cyc(uint32_t ns) {
    return (uint32_t)((uint64_t)clock_get_hz(clk_sys) * ns / 1000000000u) + 1;
}
static uint32_t clk_to_us(uint32_t chip_clk) {      // такты чипа -> мкс (вверх)
    return (uint32_t)(((uint64_t)chip_clk * 1000000u + cfg.phim_hz - 1) / cfg.phim_hz);
}

void chip_init(void) {
    for (uint p = PIN_CHIP_D0; p <= PIN_CHIP_A0; p++) {
        gpio_init(p);
    }
    gpio_put_masked64(CHIP_PINS_MASK, (1ull << PIN_CHIP_CS) | (1ull << PIN_CHIP_WR));
    gpio_set_dir_masked64(CHIP_PINS_MASK, CHIP_PINS_MASK);
    chip_reset_state();
}

void chip_reset_state(void) {
    for (int i = 0; i < 256; i++) chip_regs[i] = 0;
    chip_sel = 0;
    q_head = q_tail = 0;
    ready_at = time_us_32();
    chip_addr = 0;
}

bool chip_put(bool a0, uint8_t v) {
    uint32_t h = q_head;
    if (((h + 1) & (CHIP_Q_SIZE - 1)) == q_tail) { chip_stat.q_lost++; return false; }
    q[h] = (uint16_t)((a0 ? 0x100 : 0) | v);
    q_head = (h + 1) & (CHIP_Q_SIZE - 1);
    return true;
}

void chip_slot_write(bool a0, uint8_t v) {
    chip_stat.slot_wr++;
    if (!a0) chip_sel = v;                // адрес: выбор регистра
    else chip_regs[chip_sel] = v;         // данные: в тень выбранного
    if (mode_chip_from_slot()) chip_put(a0, v);
}

// Одна запись в чип: A0, данные, /CS и /WR - импульс по cfg
static void chip_cycle(bool a0, uint8_t v) {
    uint64_t val = ((uint64_t)v << PIN_CHIP_D0) | (a0 ? (1ull << PIN_CHIP_A0) : 0) | (1ull << PIN_CHIP_WR);
    gpio_put_masked64(CHIP_PINS_MASK, val);                     // данные, A0, /CS = 0, /WR = 1
    busy_wait_at_least_cycles(ns_to_cyc(cfg.setup_ns));
    gpio_put_masked64(1ull << PIN_CHIP_WR, 0);                  // /WR вниз
    busy_wait_at_least_cycles(ns_to_cyc(cfg.wr_pulse_ns));
    gpio_put_masked64((1ull << PIN_CHIP_WR) | (1ull << PIN_CHIP_CS), (1ull << PIN_CHIP_WR) | (1ull << PIN_CHIP_CS));
}

void chip_poll(void) {
    if (q_tail == q_head) return;
    if ((int32_t)(time_us_32() - ready_at) < 0) return;         // чип ещё занят
    uint16_t e = q[q_tail];
    q_tail = (q_tail + 1) & (CHIP_Q_SIZE - 1);
    bool a0 = e & 0x100;
    uint8_t v = (uint8_t)e;
    chip_cycle(a0, v);
    chip_stat.chip_wr++;
    uint32_t wait;
    if (!a0) { chip_addr = v; wait = cfg.wait_addr_clk; }       // адрес
    else wait = chip_addr < 16 ? cfg.wait_ssg_clk : cfg.wait_fm_clk;   // данные: SSG или FM
    ready_at = time_us_32() + clk_to_us(wait);
}
