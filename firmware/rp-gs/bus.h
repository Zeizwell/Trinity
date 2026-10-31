// Шина: PIO + FIFO, прерывание на ядре 0. Ответ на чтение отдаёт PIO сам, из готового образа
#ifndef BUS_H
#define BUS_H

#include <stdint.h>

void bus_start(void);
void bus_answer(void);          // ядро 0: устройство что-то поменяло - обновить образ для PIO
void bus_inject_wr(int a0, uint8_t v);  // проверка без Спектрума: те же действия, что в прерывании
int  bus_inject_rd(int a0);     // прочитать как Z80, чтение данных снимает бит
uint32_t bus_irq_top(void);     // самый долгий обработчик, такты

#endif
