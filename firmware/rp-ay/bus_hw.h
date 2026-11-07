// Шина слота, ответ на чтение - железом (doc/zx-trinity.md п.14е): что прочтёт Спектрум, выдают PIO и DMA без
// процессора. Ядро только сообщает железу, что поменялось в модели шины (engine/ayfx.c: ayfx_bus_t) - сроков нет.
// Макет: bus_hw.c. Плата рев А (пока старый путь - прерывание) и модель на ПК: bus_hw_none.c
#pragma once
#include <stdint.h>
#include <stdbool.h>

void bus_hw_init(void);                         // (ядро 1) автоматы, цепочки DMA, кольцо записей -> card_slot_write
void bus_hw_reset(void);                        // всё по умолчанию: тень 0, поток пуст, метки нет (шина молчит: сброс)
void bus_hw_value(int c, uint8_t r, uint8_t v); // тень регистра r чипа c - то, что пишет само ядро (тишина, тест)
void bus_hw_reply(int c, const uint8_t *r, uint16_t n);   // ответ в поток #C1 (n = 0 - пусто). Ответ читают целиком
void bus_hw_status(int c, uint8_t st);          // статус ядра (чтение #C0 без метки)
void bus_hw_unmark(int c);                      // снять метку «занята» (ответ уже в потоке)

extern volatile uint32_t bus_hw_irq_cyc;        // самый долгий разбор кольца записей, такты RP
extern volatile uint32_t bus_hw_left;           // ответов, к которым в потоке остался хвост прошлого (недочитали)
// Самопроверка при старте (bus_hw_init): биты - что не прошло, 0 - всё OK
enum { BUS_ST_EXEC = 1, BUS_ST_HANG = 2, BUS_ST_L = 4, BUS_ST_PUTGET = 8, BUS_ST_A = 16, BUS_ST_A_L = 32,
       BUS_ST_D = 64, BUS_ST_D_L = 128 };
extern volatile uint32_t bus_hw_check;
void bus_hw_poll(void);                         // (ядро 1, цикл) журнал шины - отладка
void bus_hw_jr_clear(void);                     // журнал шины - заново (card.c: на SLOT_NEW)
extern volatile bool bus_hw_jr_force;           // консоль j: журнал напечатать в следующий раз
void bus_hw_print(void);                        // (ядро 0, консоль) самопроверка, снимок пути ответа: cur, слова, R
void bus_hw_bench(void);                        // (ядро 0, консоль 'b', Ева ВЫКЛЮЧЕНА) стенд: Спектрум подменой входов, Detect x200
