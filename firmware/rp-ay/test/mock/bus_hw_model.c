// Модель железного ответа на ПК (doc/zx-trinity.md п.14е): то же API, что bus_hw.c, и то, что делают PIO и DMA.
// Чип 0. Спектрум ходит через hwm_write / hwm_read: сначала железо (A / D - сразу), потом ядро (card_slot_write).
// Что моделируем: таблица слов ans, слово у R защёлкивается при выборе или записи (цепочка L), метку #80 пишет D
// сразу, поток - очередь как TX FIFO (старый хвост - впереди нового), статус ядра - сразу
#include "bus_hw.h"
#include "card.h"
#include <string.h>

#define K_STATUS 0x100
#define K_STREAM 0x200

volatile uint32_t bus_hw_irq_cyc, bus_hw_left, bus_hw_check;

static uint32_t ans[256];                   // слово: значение, род
static uint8_t  sel;                        // выбранный регистр (A)
static uint32_t latch;                      // слово у R (rxfifo[0])
static uint8_t  st;                         // статус ядра (rxfifo[1])
static uint8_t  q[1024];                    // поток (TX FIFO + то, что S ещё дольёт)
static int      qh, qn;

void bus_hw_init(void) { bus_hw_reset(); }

void bus_hw_reset(void) {
    memset(ans, 0, sizeof ans);
    ans[0xC0] = K_STATUS, ans[0xC1] = K_STREAM;
    sel = 0, latch = ans[0], st = 0, qh = qn = 0;
}

void bus_hw_value(int c, uint8_t r, uint8_t v) { if (!c) ans[r] = (ans[r] & ~0xFFu) | v; }

void bus_hw_reply(int c, const uint8_t *r, uint16_t n) {
    if (c) return;
    if (qn) bus_hw_left++;                  // хвост прошлого остаётся впереди (S не чистит TX FIFO)
    for (int i = 0; i < n && qn < (int)sizeof q; i++) q[(qh + qn++) % sizeof q] = r[i];
}

void bus_hw_status(int c, uint8_t s) { if (!c) st = s; }
void bus_hw_unmark(int c) { if (!c) ans[0xC0] &= ~0xFFu; }
void bus_hw_print(void) {}
void bus_hw_bench(void) {}
void bus_hw_jr_clear(void) {}
volatile bool bus_hw_jr_force;

// Спектрум пишет: a0 = 0 - выбор (A: слово заново), 1 - данные (D: байт в слово, слово заново); потом ядро
void hwm_write(bool a0, uint8_t v) {
    if (!a0) sel = v;
    else ans[sel] = (ans[sel] & ~0xFFu) | v;
    latch = ans[sel];
    card_slot_write(a0, v);
}

// Спектрум читает #FFFD (данные выбранного регистра) - как автомат R
uint8_t hwm_read(void) {
    uint8_t v = (uint8_t)latch;
    if (v & 0x80) return v;                 // значение с битом 7 (метка «занята» у #C0)
    if (latch & K_STATUS) return st;
    if (latch & K_STREAM) {
        if (!qn) return v;                  // пусто - тень
        uint8_t b = q[qh];
        qh = (qh + 1) % sizeof q, qn--;
        return b;
    }
    return v;
}

void bus_hw_poll(void) {}
