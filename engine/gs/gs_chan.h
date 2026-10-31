// Каналы: движок звука GS, NeoGS и плеера модулей (doc/trinity-config.md, п.26). Метод пересчёта - gs_cfg.resample.
#ifndef GS_CHAN_H
#define GS_CHAN_H

#include <stdint.h>

#define GS_VOICES_MAX 32

typedef struct {
    const int8_t *data;         // сэмпл, NULL - канал молчит
    uint32_t len;               // длина, байт
    uint32_t loop_start;
    uint32_t loop_len;          // 0 - без цикла
    float    pos;               // позиция; < 0 - сэмпл кончился
    float    inc;               // шаг на отсчёт вывода
    uint8_t  vol;               // 0..64
    float    held;              // текущая ступенька, -1..1 (GS_RS_STEP)
} gs_voice_t;

extern gs_voice_t gs_voice[GS_VOICES_MAX];

void gs_chan_reset(void);
// Добавить звук каналов к буферу (L, R, L, R...), n отсчётов
void gs_chan_mix(float *out, int n);

#endif
