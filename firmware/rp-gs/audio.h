// Звук: PIO I2S + DMA двойным буфером, прерывание DMA будит ядро 1, оно считает свободный буфер
#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

#define AUDIO_FRAMES 256        // блок: 5.33 мс при 48 кГц

extern volatile int audio_pattern;      // 1 - вместо звука счётчик: L 0x11nnnnnn, R 0x22nnnnnn (проверка формата)
extern volatile int audio_hold;         // 1 - ядро 1 выводит тишину и не трогает движок

void audio_start(void);                 // запускает ядро 1 и DMA
int  audio_check(uint32_t *words, int n);   // читает n слов с ножек, как ЦАП (с начала кадра, L, R, L, R...)

#endif
