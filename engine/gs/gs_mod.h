// Плеер MOD: логика строк и команд - pocketmod (MIT), звук - наши каналы (gs_chan)
#ifndef GS_MOD_H
#define GS_MOD_H

#include <stdint.h>

int  gs_mod_load(const void *data, int size);   // 1 - годится, с начала. Данные должны жить, пока играет
void gs_mod_stop(void);
int  gs_mod_playing(void);
void gs_mod_audio(float *out, int n);           // добавить звук к буферу

#endif
