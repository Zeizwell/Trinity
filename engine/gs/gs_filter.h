// Фильтры Amiga на стерео-шине (номиналы со схем, как pt2-clone): ФНЧ платы, LED, ФВЧ. Что включено - gs_cfg
#ifndef GS_FILTER_H
#define GS_FILTER_H

#include "gs_cfg.h"

void gs_filter_init(float rate);            // коэффициенты под частоту вывода, состояние - в ноль
void gs_filter_run(float *buf, int n);      // L, R, L, R...
float gs_filter_db(float hz);               // кривая для драйвера: дБ на частоте при текущих настройках. Не на пути звука
// Ступени кривой: GS_ST_* и ручки
enum { GS_CURVE_BASS = GS_ST_COUNT, GS_CURVE_TREBLE, GS_CURVE_COUNT };
float gs_filter_stage_db(int stage, float hz);  // одна ступень, выключенная - 0 дБ
void gs_comp_run(float *buf, int n, float rate);    // компрессор и ограничитель: на частоте вывода, после понижения

extern int gs_led_module;                   // LED-фильтр по команде модуля E0x (gs_cfg.led == GS_LED_MODULE)

#endif
