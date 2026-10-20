// Модель железа RP2350 для проверки прошивки RP-AY на ПК (test/host_test.c). Подменяет pico-sdk теми функциями,
// которые зовёт прошивка. Время - модельное (нс), идёт от вызовов; PIO записи в чип исполняется по тактам
// (та же программа, что chip_wr.pio); на ножках чипа - модель YM2203 (регистры, запись по фронту /WR, чтение по /RD).
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef unsigned int uint;
typedef struct { int index; } pio_hw_t;
typedef pio_hw_t *PIO;
extern pio_hw_t sim_pio0, sim_pio1;
#define pio0 (&sim_pio0)
#define pio1 (&sim_pio1)
typedef struct { int dummy; } pio_program_t;
enum { clk_sys = 0 };
enum { GPIO_IN = 0, GPIO_OUT = 1 };
enum { GPIO_FUNC_SIO = 5, GPIO_FUNC_PIO0 = 6 };

// --- время
uint32_t time_us_32(void);
void busy_wait_at_least_cycles(uint32_t cyc);
void busy_wait_us_32(uint32_t us);
void sleep_ms(uint32_t ms);
uint32_t clock_get_hz(int clk);

// --- ножки
void gpio_init(uint p);
void gpio_set_dir(uint p, bool out);
void gpio_put(uint p, bool v);
bool gpio_get(uint p);
void gpio_put_masked64(uint64_t mask, uint64_t val);
void gpio_set_dir_masked64(uint64_t mask, uint64_t val);
uint64_t gpio_get_all64(void);
void gpio_set_function(uint p, int f);

// --- PIO
int  pio_set_gpio_base(PIO pio, uint base);
uint pio_add_program(PIO pio, const pio_program_t *p);
uint pio_claim_unused_sm(PIO pio, bool required);
void pio_gpio_init(PIO pio, uint p);
void pio_sm_put(PIO pio, uint sm, uint32_t v);
void pio_sm_put_blocking(PIO pio, uint sm, uint32_t v);
bool pio_sm_is_tx_fifo_full(PIO pio, uint sm);
bool pio_sm_is_tx_fifo_empty(PIO pio, uint sm);
bool pio_sm_is_rx_fifo_full(PIO pio, uint sm);
bool pio_sm_is_rx_fifo_empty(PIO pio, uint sm);
uint32_t pio_sm_get(PIO pio, uint sm);
uint pio_sm_get_pc(PIO pio, uint sm);

// --- программы и их init из .pio.h
extern const pio_program_t chip_wr_program, ws2812_program, bus_wr_program, bus_rd_program;
void chip_wr_init(PIO pio, uint sm, uint offset, uint base);
void ws2812_program_init(PIO pio, uint sm, uint offset, uint pin, float freq);
void bus_wr_init(PIO pio, uint sm, uint offset);
void bus_rd_init(PIO pio, uint sm, uint offset);
void bus_rd_set(PIO pio, uint sm, uint8_t data, uint8_t status);

// --- прочее sdk
void stdio_init_all(void);
int  getchar_timeout_us(uint32_t us);
void multicore_launch_core1(void (*fn)(void));

// --- для теста
uint64_t sim_now_ns(void);
extern uint8_t  sim_ym_regs[256];       // регистры модели YM2203
extern uint32_t sim_ym_writes;          // записей данных в чип
extern uint32_t sim_led;                // последний цвет светодиода (GRB << 8)
extern uint32_t sim_wr_min_ns, sim_gap_addr_min_ns;   // самый короткий /WR и пауза адрес -> данные
extern uint8_t  sim_fault_tie;          // склеить биты данных чипа (маска двух бит; 0 - исправен)
void sim_run_main(int (*m)(void), uint32_t ms);   // запустить main прошивки на ms модельного времени
