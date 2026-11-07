// Шина без железного ответа (bus_hw.h): плата рев А (отвечает прерывание, main.c) и модель на ПК - отвечает модель
// (card_answer)
#include "bus_hw.h"

volatile uint32_t bus_hw_irq_cyc, bus_hw_left, bus_hw_check;

void bus_hw_init(void) {}
void bus_hw_reset(void) {}
void bus_hw_value(int c, uint8_t r, uint8_t v) { (void)c, (void)r, (void)v; }
void bus_hw_reply(int c, const uint8_t *r, uint16_t n) { (void)c, (void)r, (void)n; }
void bus_hw_status(int c, uint8_t st) { (void)c, (void)st; }
void bus_hw_unmark(int c) { (void)c; }
void bus_hw_print(void) {}
void bus_hw_jr_clear(void) {}
volatile bool bus_hw_jr_force;
void bus_hw_poll(void) {}
void bus_hw_bench(void) {}
