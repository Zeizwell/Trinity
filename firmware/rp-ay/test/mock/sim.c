// Модель железа RP2350 (sim.h). Такт RP - 150 МГц; время идёт от вызовов прошивки.
#include "sim.h"
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#define HZ 150000000u
pio_hw_t sim_pio0 = {0}, sim_pio1 = {1};
const pio_program_t chip_wr_program, ws2812_program, bus_wr_program, bus_rd_program;

static uint64_t cyc;                    // модельное время, такты RP
uint64_t sim_now_ns(void) { return cyc * 1000000000ull / HZ; }

// ---- ножки: у каждой - кому отдана (SIO / PIO), направление и уровень SIO
static uint64_t sio_out, sio_oe, is_pio;
static uint32_t pio_pins11;             // ножки GP28..38 от PIO записи (D0..D7, /CS, /WR, A0)
#define CHIP_BASE 28

// ---- модель YM2203 на ножках
uint8_t  sim_ym_regs[256];
uint32_t sim_ym_writes;
uint8_t  sim_fault_tie;
bool     sim_no_chip;
static uint8_t ym_addr;
static bool    prev_wr = true;
static uint64_t wr_fall_cyc, addr_latch_cyc;
uint32_t sim_wr_min_ns = 0xFFFFFFFF, sim_gap_addr_min_ns = 0xFFFFFFFF;

static uint8_t tie(uint8_t d) {          // склеенные линии: разные - обе 0
    if (sim_fault_tie && (d & sim_fault_tie) != sim_fault_tie) d &= ~sim_fault_tie;
    return d;
}
static bool pin(uint p) {               // уровень ножки чипа (кто ею владеет)
    if (p >= CHIP_BASE && p < CHIP_BASE + 11 && (is_pio >> p & 1)) return pio_pins11 >> (p - CHIP_BASE) & 1;
    return sio_out >> p & 1;
}
static void ym_eval(void) {             // запись в чип - по подъёму /WR при /CS = 0
    bool wr = pin(37), cs = pin(36), a0 = pin(38);
    if (prev_wr && !wr) wr_fall_cyc = cyc;
    if (!prev_wr && wr && !cs) {
        uint32_t w = (uint32_t)((cyc - wr_fall_cyc) * 1000000000ull / HZ);
        if (w < sim_wr_min_ns) sim_wr_min_ns = w;
        uint8_t d = 0;
        for (int i = 0; i < 8; i++) d |= (uint8_t)(pin(CHIP_BASE + i) << i);
        d = tie(d);
        if (!a0) { ym_addr = d; addr_latch_cyc = cyc; }
        else {
            uint32_t g = (uint32_t)((wr_fall_cyc - addr_latch_cyc) * 1000000000ull / HZ);
            if (g < sim_gap_addr_min_ns) sim_gap_addr_min_ns = g;
            sim_ym_regs[ym_addr] = d; sim_ym_writes++;
        }
    }
    prev_wr = wr;
}

// ---- PIO записи в чип: та же программа (pull / out pins,11 / out x,21 / jmp x-- hold), 1 команда = 1 такт
static uint32_t fifo[8]; static int f_n, f_h;
static uint32_t osr, x; static int pc; static uint wr_offset;
static void pio_step(void) {
    switch (pc) {
    case 0: if (!f_n) return;                                    // pull block: пусто - стоим
            osr = fifo[f_h]; f_h = (f_h + 1) & 7; f_n--; pc = 1; break;
    case 1: pio_pins11 = osr & 0x7FF; osr >>= 11; pc = 2; ym_eval(); break;
    case 2: x = osr & 0x1FFFFF; osr >>= 21; pc = 3; break;
    case 3: if (x) { x--; } else { x--; pc = 0; } break;          // jmp x-- hold; затем wrap на pull
    }
}
static void advance(uint64_t c) {       // время идёт: PIO исполняет такты
    while (c--) { cyc++; pio_step(); }
}

// ---- ядро 1 в модели: идёт внутри multicore_launch_core1, пока не дойдёт до своего цикла
static jmp_buf core1_back; static bool in_core1;
static void hang_check(void) {
    if (in_core1 && cyc > 30ull * HZ) { printf("HANG: ядро 1 не дошло до цикла за 30 с\n"); exit(2); }
}

uint32_t time_us_32(void) { advance(8); hang_check(); return (uint32_t)(cyc / (HZ / 1000000)); }
void busy_wait_at_least_cycles(uint32_t c) { advance(c); }
void busy_wait_us_32(uint32_t us) { advance((uint64_t)us * (HZ / 1000000)); }
static jmp_buf stop_back; static uint64_t stop_cyc;     // sim_run_main: остановить ядро 0 в этот момент
static uint32_t led_log_n;
void sleep_ms(uint32_t ms) {
    advance((uint64_t)ms * (HZ / 1000));
    if (stop_cyc && cyc >= stop_cyc) longjmp(stop_back, 1);
}
void sim_run_main(int (*m)(void), uint32_t ms) {
    stop_cyc = cyc + (uint64_t)ms * (HZ / 1000);
    if (!setjmp(stop_back)) m();
    stop_cyc = 0;
}
uint32_t clock_get_hz(int clk) { (void)clk; return HZ; }
void vreg_set_voltage(int v) { (void)v; }
bool set_sys_clock_khz(uint32_t khz, bool r) { (void)khz; (void)r; return true; }   // модель - такт не меняет

void gpio_init(uint p) { is_pio &= ~(1ull << p); sio_oe &= ~(1ull << p); sio_out &= ~(1ull << p); }
void gpio_set_dir(uint p, bool out) { if (out) sio_oe |= 1ull << p; else sio_oe &= ~(1ull << p); }
void gpio_put(uint p, bool v) { if (v) sio_out |= 1ull << p; else sio_out &= ~(1ull << p); advance(1); ym_eval(); }
bool gpio_get(uint p) { advance(2); return p == 13 ? true : (sio_out >> p & 1); }   // /IC (13) - вверх
void gpio_put_masked64(uint64_t m, uint64_t v) { sio_out = (sio_out & ~m) | (v & m); advance(1); ym_eval(); }
void gpio_set_dir_masked64(uint64_t m, uint64_t v) { sio_oe = (sio_oe & ~m) | (v & m); advance(1); }
uint64_t gpio_get_all64(void) {          // чтение: /CS, /RD внизу, A0 = 1, данные - входы -> чип отдаёт регистр
    advance(1);
    uint64_t v = sio_out;
    bool reading = !sim_no_chip && !pin(36) && !pin(39) && pin(38) && !((sio_oe >> CHIP_BASE) & 0xFF);
    if (reading) v = (v & ~(0xFFull << CHIP_BASE)) | ((uint64_t)tie(sim_ym_regs[ym_addr]) << CHIP_BASE);
    return v;
}
void gpio_set_function(uint p, int f) { if (f == GPIO_FUNC_SIO) is_pio &= ~(1ull << p); else is_pio |= 1ull << p; ym_eval(); }

static uint next_off[2], next_sm[2];
int  pio_set_gpio_base(PIO pio, uint base) { (void)pio; (void)base; return 0; }
uint pio_add_program(PIO pio, const pio_program_t *p) {
    uint off = next_off[pio->index];
    next_off[pio->index] += 4;
    if (p == &chip_wr_program) wr_offset = off;
    return off;
}
uint pio_claim_unused_sm(PIO pio, bool r) { (void)r; return next_sm[pio->index]++; }
void pio_gpio_init(PIO pio, uint p) { (void)pio; is_pio |= 1ull << p; ym_eval(); }
uint32_t sim_led;
void pio_sm_put(PIO pio, uint sm, uint32_t v) {
    advance(1);
    if (pio == pio0 && sm == 0) {                               // светодиод: журнал смен цвета
        if (v != sim_led && led_log_n++ < 40)
            printf("  %6.3f с  LED R%3u G%3u B%3u\n", (double)sim_now_ns() / 1e9, v >> 16 & 255, v >> 24, v >> 8 & 255);
        sim_led = v; return;
    }
    if (pio == pio1) return;                                    // ответ шины - не моделируем
    if (f_n == 8) { printf("FAIL: put в полную очередь PIO\n"); exit(3); }
    fifo[(f_h + f_n) & 7] = v; f_n++;
}
void pio_sm_put_blocking(PIO pio, uint sm, uint32_t v) { pio_sm_put(pio, sm, v); }
bool pio_sm_is_tx_fifo_full(PIO pio, uint sm) { (void)pio; (void)sm; advance(4); hang_check(); return f_n == 8; }
bool pio_sm_is_tx_fifo_empty(PIO pio, uint sm) { (void)pio; (void)sm; advance(4); hang_check(); return f_n == 0; }
bool pio_sm_is_rx_fifo_full(PIO pio, uint sm) {
    (void)pio; (void)sm;
    if (in_core1) { in_core1 = false; longjmp(core1_back, 1); } // ядро 1 дошло до своего цикла - старт закончен
    return false;
}
bool pio_sm_is_rx_fifo_empty(PIO pio, uint sm) { (void)pio; (void)sm; return true; }
uint32_t pio_sm_get(PIO pio, uint sm) { (void)pio; (void)sm; return 0; }
uint pio_sm_get_pc(PIO pio, uint sm) { (void)pio; (void)sm; advance(2); hang_check(); return wr_offset + (uint)pc; }

void chip_wr_init(PIO pio, uint sm, uint off, uint base) { (void)sm; (void)off; for (uint i = 0; i < 11; i++) pio_gpio_init(pio, base + i); }
void ws2812_program_init(PIO pio, uint sm, uint off, uint pin, float f) { (void)pio; (void)sm; (void)off; (void)pin; (void)f; }
void bus_wr_init(PIO pio, uint sm, uint off) { (void)pio; (void)sm; (void)off; }
void bus_rd_init(PIO pio, uint sm, uint off) { (void)pio; (void)sm; (void)off; }
void bus_rd_set(PIO pio, uint sm, uint8_t d, uint8_t s) { (void)pio; (void)sm; (void)d; (void)s; }

void stdio_init_all(void) {}
int  getchar_timeout_us(uint32_t us) { (void)us; return -1; }
void multicore_launch_core1(void (*fn)(void)) {
    if (setjmp(core1_back)) return;     // ядро 1 закончило старт
    in_core1 = true;
    fn();
}

void pio_set_irq0_source_enabled(PIO pio, enum pio_interrupt_source s, bool on) { (void)pio; (void)s; (void)on; }
void irq_set_exclusive_handler(uint n, void (*h)(void)) { (void)n; (void)h; }
void irq_set_priority(uint n, uint8_t p) { (void)n; (void)p; }
void irq_set_enabled(uint n, bool on) { (void)n; (void)on; }
uint32_t save_and_disable_interrupts(void) { return 0; }
void restore_interrupts(uint32_t s) { (void)s; }
