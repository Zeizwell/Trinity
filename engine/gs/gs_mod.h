// Плеер MOD: логика строк и команд - pocketmod (MIT), звук - наши каналы (gs_chan)
#ifndef GS_MOD_H
#define GS_MOD_H

#include <stdint.h>

int  gs_mod_load(const void *data, int size);   // 1 - годится, с начала. Данные должны жить, пока играет
int  gs_mod_play(const void *data, int size, uint8_t pos);  // то же, с позиции pos
void gs_mod_stop(void);
void gs_mod_pause(void);                        // с места продолжит gs_mod_resume
int  gs_mod_resume(void);                       // 1 - есть что продолжать
int  gs_mod_playing(void);
void gs_mod_audio(float *out, int n);           // добавить звук к буферу

void gs_mod_set_vol(uint8_t master, uint8_t module);    // 0..64 каждая
void gs_mod_set_speed(uint8_t v);               // 1..#1F скорость, #20..#FF темп, как команда F
uint8_t gs_mod_song_pos(void);
uint8_t gs_mod_pat_pos(void);
uint8_t gs_mod_speed(void);
uint8_t gs_mod_tempo(void);
uint8_t gs_mod_note(int chan);                  // нота GS (C-2 Амиги = 48), 127 - молчит
uint8_t gs_mod_vol(int chan);                   // 0..64

#endif
