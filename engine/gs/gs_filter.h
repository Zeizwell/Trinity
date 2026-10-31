// Фильтры Amiga на стерео-шине (номиналы со схем, как pt2-clone): ФНЧ платы, LED, ФВЧ. Что включено - gs_cfg
#ifndef GS_FILTER_H
#define GS_FILTER_H

void gs_filter_init(float rate);            // коэффициенты под частоту вывода, состояние - в ноль
void gs_filter_run(float *buf, int n);      // L, R, L, R...

extern int gs_led_module;                   // LED-фильтр по команде модуля E0x (gs_cfg.led == GS_LED_MODULE)

#endif
