#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "hardware/structs/systick.h"
#include "config.h"
#include "gs/gs_core.h"
#include "bus.h"
#include "bus.pio.h"

static PIO pio = pio1;
static uint sm_wr, sm_rd;
static volatile uint32_t irq_top;

// Макет: код порта по A0. Рев А: код на ножках от CPLD (п.12), ответ на чтение по коду - другая программа PIO
static uint8_t code_of(int a0) { return a0 ? GS_CODE_B3 : GS_CODE_BB; }

// Готовый образ: A0 = 1 данные, A0 = 0 статус
static void answer_now(void) {
    bus_rd_set(pio, sm_rd, gs_core_port_peek(GS_CODE_B3), gs_core_port_peek(GS_CODE_BB));
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
        uint32_t w = pio_sm_get(pio, sm_wr);        // снимок ножек от D0
        if (w >> (PIN_SLOT_CS - PIN_SLOT_D0) & 1) continue;
        on_write(code_of((int)(w >> (PIN_SLOT_A0 - PIN_SLOT_D0) & 1)), (uint8_t)w);
    }
    while (!pio_sm_is_rx_fifo_empty(pio, sm_rd)) {  // метка PIO: данные прочитали
        pio_sm_get(pio, sm_rd);
        on_read_data();
    }
    uint32_t d = (t0 - cyc_now()) & 0xFFFFFF;
    if (d > irq_top) irq_top = d;
    __sev();                                        // ядро 0 ждёт в wfe
}

void bus_start(void) {
    systick_hw->rvr = 0xFFFFFF;
    systick_hw->csr = 5;                            // такт ядра
    sm_wr = pio_claim_unused_sm(pio, true);
    sm_rd = pio_claim_unused_sm(pio, true);
    bus_wr_init(pio, sm_wr, bus_add_program(pio, &bus_wr_program, PIN_SLOT_WR));
    bus_rd_init(pio, sm_rd, bus_add_program(pio, &bus_rd_program, PIN_SLOT_RD));
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
