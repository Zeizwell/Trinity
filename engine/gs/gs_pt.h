// Плеер MOD: логика ProTracker 2.3D по реплееру pt2-clone (doc/paula.md). Только 4 канала. Вывод - регистры как у Паулы.
#ifndef GS_PT_H
#define GS_PT_H

#include <stdint.h>

typedef struct {
    uint16_t period, vol;       // AUDxPER, AUDxVOL
    const int8_t *lc;           // AUDxLC: что пойдёт после текущего блока, NULL - пусто
    uint16_t len;               // AUDxLEN, слова
} gs_pt_reg_t;

extern gs_pt_reg_t gs_pt_reg[4];

int gs_pt_is_mod(const void *data, int size);                       // 4 канала, 31 сэмпл
int gs_pt_load(const void *data, int size, int writable);           // writable - EFx может писать в сэмплы
void gs_pt_tick(void);                                              // один тик
void gs_pt_set_pos(int pos);
void gs_pt_set_speed(int speed);
void gs_pt_set_bpm(int bpm);
int gs_pt_pos(void);                                                // позиция и строка, которые будут читаться следующими
int gs_pt_row(void);
int gs_pt_play_pos(void);                                           // строка, которая сейчас играет
int gs_pt_play_row(void);
int gs_pt_speed(void);
int gs_pt_bpm(void);
int gs_pt_stopped(void);                                            // была команда F00
double gs_pt_tick_sec(void);                                        // длина тика по CIA
int gs_pt_period(int chan);                                         // период ноты без вибрато и арпеджио

// Реализует тот, кто выводит
void gs_pt_dma_start(int chan);                                     // старт DMA, в регистрах уже новые LC, LEN, PER
void gs_pt_led_set(int on);                                         // E0x: 1 - фильтр включён

#endif
