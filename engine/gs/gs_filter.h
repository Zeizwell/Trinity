// Фильтры Amiga на стерео-шине (номиналы со схем, как pt2-clone): ФНЧ платы, LED, ФВЧ. Что включено - gs_cfg
#ifndef GS_FILTER_H
#define GS_FILTER_H

void gs_filter_init(float rate);            // коэффициенты под частоту вывода, состояние - в ноль
void gs_filter_run(float *buf, int n);      // L, R, L, R...
float gs_filter_db(float hz);               // кривая для драйвера: дБ на частоте при текущих настройках. Не на пути звука
void gs_comp_run(float *buf, int n, float rate);    // компрессор и ограничитель: на частоте вывода, после понижения

extern int gs_led_module;                   // LED-фильтр по команде модуля E0x (gs_cfg.led == GS_LED_MODULE)

#endif
