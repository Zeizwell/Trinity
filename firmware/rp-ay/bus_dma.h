// Ответ на чтение железом (doc/zx-trinity.md п.14е): таблица ответов и цепочки DMA. Пока - только замер цепочек
// без Z80 (консоль, клавиша d): за сколько тактов RP после записи Спектрума готов ответ
#pragma once
#include <stdbool.h>

void bus_dma_test(void);                    // (ядро 0) замер: 1000 выборов и 1000 записей данных, итог - в консоль
extern volatile bool bus_dma_load;         // замер просит нагрузку ядра 1
void bus_dma_hammer(void);                  // (ядро 1) нагрузка, пока bus_dma_load
