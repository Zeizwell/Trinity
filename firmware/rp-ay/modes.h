// Режимы RP-AY и переходы между ними (doc/zx-trinity.md, 14а). Правила - таблицей (modes.c),
// не кодом: поменять разрешение перехода - одна клетка таблицы.
#pragma once
#include "config.h"

typedef enum {
    MODE_OK = 0,
    MODE_DENIED,        // переход запрещён таблицей из текущего режима
    MODE_SAME,          // уже в этом режиме
} mode_res_t;

extern volatile mode_t mode;            // текущий режим (читают оба ядра)
extern const char *const mode_name[MODE_COUNT];

mode_res_t mode_set(mode_t to);         // переход по таблице; сам режим ничего не включает - это делают хуки
void mode_reset(void);                  // сброс (/IC, включение): cfg.mode_after_reset, без таблицы
bool mode_chip_from_slot(void);         // идут ли записи Спектрума в чип в текущем режиме
