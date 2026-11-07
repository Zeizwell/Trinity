// Сторожевой таймер и метки «где сейчас» (диагностика зависаний). Метки - в регистрах SCRATCH 0..3 сторожа: они
// переживают его перезагрузку, после старта консоль печатает, где висели (main.c). На ПК-модели - пусто
#pragma once
#if PICO_ON_DEVICE
#include "hardware/watchdog.h"
#define WD_AT(n)   (watchdog_hw->scratch[0] = (n))          // ядро 0: место
#define WD_PK(v)   (watchdog_hw->scratch[1] = (v))          // ядро 0: служебный пакет (код), который разбираем
#define WD_AT1(n)  (watchdog_hw->scratch[2] = (n))          // ядро 1: место
#define WD_BEAT1() (watchdog_hw->scratch[3]++)              // ядро 1: круг цикла
#define WD_KICK()  watchdog_update()
#else
#define WD_AT(n)   ((void)0)
#define WD_PK(v)   ((void)0)
#define WD_AT1(n)  ((void)0)
#define WD_BEAT1() ((void)0)
#define WD_KICK()  ((void)0)
#endif
