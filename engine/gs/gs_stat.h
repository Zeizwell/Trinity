// Нагрузка ядра звука: одна глобальная структура на всю карту, какое бы устройство ни играло (doc/trinity-config.md, п.46б).
// Пишет ядро звука, читают все. Цифры уже готовые, чтение звук не трогает
#ifndef GS_STAT_H
#define GS_STAT_H

#include <stdint.h>

typedef struct {
    volatile uint32_t blocks;       // посчитано блоков
    volatile uint32_t late;         // блок не успел к выводу
    volatile uint32_t last_us;      // последний блок
    volatile uint32_t top_us;       // худший блок с последнего сброса
    volatile uint16_t load_x10;     // нагрузка за последние полсекунды, 0.1 %
    volatile uint16_t top_x10;      // худший блок, 0.1 %
} gs_stat_t;

extern gs_stat_t gs_stat;

void gs_stat_block(uint32_t us, uint32_t budget_us);    // ядро звука: блок посчитан за us при бюджете budget_us
void gs_stat_late(void);                                // ядро звука: опоздание
void gs_stat_reset(void);                               // любое ядро: пик и опоздания с нуля, выполнит ядро звука на следующем блоке

#endif
