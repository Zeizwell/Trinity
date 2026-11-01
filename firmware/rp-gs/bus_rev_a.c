// Шина платы рев А: код порта от CPLD (doc/cpld.md). Макет - bus.c
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "hardware/structs/systick.h"
#include "config.h"
#include "gs/gs_core.h"
#include "bus.h"
#include "bus_gs.pio.h"

// Коды CPLD (cpld/rtl/top.v, CODE_*) -> коды коре. Индекс - код на ножках
#define CPLD_CODE_B3 1
#define CPLD_CODE_BB 2
#define CPLD_CODE_33 3
#define CODE_NONE 0xFF
static const uint8_t code_map[8] = {
    CODE_NONE, GS_CODE_B3, GS_CODE_BB, CODE_NONE,   // 0 не нам, #B3, #BB, #33 (пока некому)
    OPL4_CODE_C4, OPL4_CODE_C5, OPL4_CODE_C6, OPL4_CODE_C7
};

// Снимок записи: код с GP0, данные с PIN_GS_D0
#define WR_CODE(w) ((w) & 7u)
#define WR_DATA(w) ((uint8_t)((w) >> PIN_GS_D0))

#define CPLD_MASK_ALL 7u            // маска устройств CPLD (MASK_* в top.v): все включены, config п.38
#define CPLD_CLK_DIV  8             // 250 МГц / 8 = 31.25 МГц, целый делитель - без дрожания

static PIO pio = pio1;
static uint sm_wr, sm_rd;
static volatile uint32_t irq_top;

static void answer_now(void) {
    bus_gs_rd_set(pio, sm_rd, gs_core_port_peek(GS_CODE_B3), gs_core_port_peek(GS_CODE_BB));
}

static void on_write(uint8_t code, uint8_t v) {
    gs_core_port_write(code, v);
    answer_now();
}

static void on_read_data(void) {
    gs_core_port_read(GS_CODE_B3);
    answer_now();
}

static uint32_t cyc_now(void) { return systick_hw->cvr; }

static void __isr bus_irq(void) {
    uint32_t t0 = cyc_now();
    while (!pio_sm_is_rx_fifo_empty(pio, sm_wr)) {
        uint32_t w = pio_sm_get(pio, sm_wr);
        on_write(code_map[WR_CODE(w)], WR_DATA(w));
    }
    while (!pio_sm_is_rx_fifo_empty(pio, sm_rd)) {  // метка PIO: данные прочитали
        pio_sm_get(pio, sm_rd);
        on_read_data();
    }
    uint32_t d = (t0 - cyc_now()) & 0xFFFFFF;
    if (d > irq_top) irq_top = d;
    __sev();                                        // ядро 0 ждёт в wfe
}

#ifndef BUS_SELFTEST
// Подъём RDY вне чтения - CPLD берёт маску с GS_D, данные держим, пока RDY не опустим
static void mask_load(uint8_t mask) {
    const uint32_t d_mask = 0xFFu << PIN_GS_D0;
    gpio_init_mask(d_mask | 1u << PIN_GS_RDY);
    gpio_put_masked(d_mask | 1u << PIN_GS_RDY, (uint32_t)mask << PIN_GS_D0);
    gpio_set_dir_out_masked(d_mask | 1u << PIN_GS_RDY);
    busy_wait_us(1);
    gpio_put(PIN_GS_RDY, 1);
    busy_wait_us(1);                                // 31 такт CPLD, ей нужно 3
    gpio_put(PIN_GS_RDY, 0);
    busy_wait_us(1);
    gpio_set_dir_in_masked(d_mask);
}
#endif

void bus_start(void) {
    systick_hw->rvr = 0xFFFFFF;
    systick_hw->csr = 5;                            // такт ядра
    clock_gpio_init(PIN_CPLD_CLK, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_SYS, CPLD_CLK_DIV);
    busy_wait_us(10);
#ifndef BUS_SELFTEST
    mask_load(CPLD_MASK_ALL);
#endif
    sm_wr = pio_claim_unused_sm(pio, true);
    sm_rd = pio_claim_unused_sm(pio, true);
    bus_gs_wr_init(pio, sm_wr, bus_gs_add_program(pio, &bus_gs_wr_program, PIN_GS_WR));
    bus_gs_rd_init(pio, sm_rd, bus_gs_add_program(pio, &bus_gs_rd_program, PIN_GS_RD));
    answer_now();
    pio_set_irq0_source_enabled(pio, (enum pio_interrupt_source)(pis_sm0_rx_fifo_not_empty + sm_wr), true);
    pio_set_irq0_source_enabled(pio, (enum pio_interrupt_source)(pis_sm0_rx_fifo_not_empty + sm_rd), true);
    irq_set_exclusive_handler(PIO1_IRQ_0, bus_irq);
    irq_set_priority(PIO1_IRQ_0, 0);                // высший
    irq_set_enabled(PIO1_IRQ_0, true);
}

void bus_answer(void) {
    uint32_t ints = save_and_disable_interrupts();
    answer_now();
    restore_interrupts(ints);
}

// Замер: те же действия, что в прерывании, с запретом прерываний
void bus_inject_wr(uint8_t code, uint8_t v) {
    uint32_t ints = save_and_disable_interrupts();
    uint32_t t0 = cyc_now();
    on_write(code, v);
    uint32_t d = (t0 - cyc_now()) & 0xFFFFFF;
    if (d > irq_top) irq_top = d;
    restore_interrupts(ints);
}

int bus_inject_rd(uint8_t code) {
    uint32_t ints = save_and_disable_interrupts();
    int v = gs_core_port_peek(code);
    if (code == GS_CODE_B3) on_read_data();
    restore_interrupts(ints);
    return v;
}

uint32_t bus_irq_top(void) { return irq_top; }

#ifdef BUS_SELFTEST
// Проверка PIO без CPLD: ядро изображает CPLD на тех же ножках. Только на голой RP - на плате ими правит CPLD
#define ST_RUNS  100000
#define ST_LIMIT 2000               // тактов ядра на ответ: 8 мкс, дальше считаем, что RP молчит

static uint32_t st_top_rdy, st_top_flag;

static void st_code(uint code) { gpio_put_masked(7u << PIN_GS_C0, code << PIN_GS_C0); }

// CPLD читает: GS_RD вниз, ждём RDY, берём данные, GS_RD вверх, ждём опускания RDY. -1 - не ответила
static int st_read(uint code) {
    st_code(code);
    busy_wait_at_least_cycles(20);
    uint32_t t0 = cyc_now();
    gpio_put(PIN_GS_RD, 0);
    while (!gpio_get(PIN_GS_RDY))
        if (((t0 - cyc_now()) & 0xFFFFFF) > ST_LIMIT) { gpio_put(PIN_GS_RD, 1); return -1; }
    uint32_t d = (t0 - cyc_now()) & 0xFFFFFF;
    if (d > st_top_rdy) st_top_rdy = d;
    int v = (int)(gpio_get_all() >> PIN_GS_D0 & 0xFF);
    gpio_put(PIN_GS_RD, 1);
    t0 = cyc_now();
    while (gpio_get(PIN_GS_RDY))
        if (((t0 - cyc_now()) & 0xFFFFFF) > ST_LIMIT) return -2;
    return v;
}

// CPLD пишет: код и данные, строб 125 нс. Пока ядро держит GS_D, PIO на них не выходит
static void st_write(uint code, uint8_t v) {
    const uint32_t d_mask = 0xFFu << PIN_GS_D0;
    st_code(code);
    gpio_init_mask(d_mask);
    gpio_put_masked(d_mask, (uint32_t)v << PIN_GS_D0);
    gpio_set_dir_out_masked(d_mask);
    busy_wait_at_least_cycles(10);
    gpio_put(PIN_GS_WR, 0);
    busy_wait_at_least_cycles(31);                  // 125 нс при 250 МГц
    gpio_put(PIN_GS_WR, 1);
    busy_wait_at_least_cycles(8);
    gpio_set_dir_in_masked(d_mask);
    for (uint i = 0; i < 8; i++) pio_gpio_init(pio, PIN_GS_D0 + i);
}

static unsigned long st_ns(uint32_t cycles) {
    return (unsigned long)((uint64_t)cycles * 1000000u / (clock_get_hz(clk_sys) / 1000));
}

void bus_selftest(void) {
    long bad_status = 0, bad_data = 0, bad_flag = 0, no_answer = 0, bad_33 = 0;
    const uint32_t out_mask = 7u << PIN_GS_C0 | 1u << PIN_GS_WR | 1u << PIN_GS_RD;
    st_top_rdy = st_top_flag = 0;
    gpio_init_mask(out_mask);
    gpio_set_mask(1u << PIN_GS_WR | 1u << PIN_GS_RD);
    gpio_set_dir_out_masked(out_mask);
    for (long i = 0; i < ST_RUNS; i++) {
        uint8_t v = (uint8_t)(i * 37);
        // Статус без последствий: сверка с тем, что знает коре
        int s = st_read(CPLD_CODE_BB);
        if (s < 0) no_answer++;
        else if (s != gs_core_port_peek(GS_CODE_BB)) bad_status++;
        // Запись в #B3 поднимает бит данных: виден через ту же шину
        st_write(CPLD_CODE_B3, v);
        uint32_t t0 = cyc_now(), d = 0;
        for (;;) {
            s = st_read(CPLD_CODE_BB);
            d = (t0 - cyc_now()) & 0xFFFFFF;
            if (s >= 0 && (s & 0x80)) break;
            if (d > ST_LIMIT) { bad_flag++; break; }
        }
        if (d > st_top_flag) st_top_flag = d;
        // Чтение #B3: ответ тот, что отдал бы коре до чтения
        int want = gs_core_port_peek(GS_CODE_B3);
        int r = st_read(CPLD_CODE_B3);
        if (r < 0) no_answer++;
        else if (r != want) bad_data++;
        // Код без ответа: RDY не поднимается
        if (st_read(CPLD_CODE_33 + 1) != -1) bad_33++;
        gs_core_work();
    }
    gpio_set_dir_in_masked(out_mask);
    printf("самотест шины: прогонов %d\n", ST_RUNS);
    printf("статус не тот %ld, данные не те %ld, бит данных не встал %ld, нет ответа %ld, чужой код ответил %ld\n",
           bad_status, bad_data, bad_flag, no_answer, bad_33);
    printf("RDY после GS_RD: самый долгий %lu нс, запись -> флаг %lu нс, обработчик %lu нс\n",
           st_ns(st_top_rdy), st_ns(st_top_flag), st_ns(irq_top));
}
#endif
