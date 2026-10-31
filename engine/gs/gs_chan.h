// Каналы: движок звука GS, NeoGS и плеера модулей (doc/trinity-config.md, п.26). Метод пересчёта - gs_cfg.resample.
#ifndef GS_CHAN_H
#define GS_CHAN_H

#include <stdint.h>
#include "gs_blep.h"

#define GS_VOICES_MAX 32

typedef struct {
    const int8_t *data;         // сэмпл, NULL - канал молчит
    uint32_t len;               // длина, байт
    uint32_t loop_start;
    uint32_t loop_len;          // 0 - без цикла
    uint64_t pos;               // позиция 32.32
    uint64_t inc;               // шаг на отсчёт смешивания, 32.32
    uint64_t step;              // шаг на ступеньку GS_RS_STEP
    float    inc_f;             // тот же шаг в float: только для доли фронта BLEP
    uint8_t  on;                // 0 - сэмпл кончился
    uint8_t  vol;               // 0..64
    float    held;              // текущая ступенька, -1..1
    uint32_t idx;               // GS_RS_BLEP: какой отсчёт сэмпла звучит
    gs_blep_t blep;             // GS_RS_BLEP
    float    vol_now;           // GS_RS_INTERP: громкость с рампой, 0..1
    float    pwm_s;             // gs_cfg.pwm: текущий отсчёт, -1..1
    uint8_t  pwm_v;             // gs_cfg.pwm: громкость, взятая на смене отсчёта
    // GS_RS_ROM: генератор SGEN из ROM GS (неизменяемые на квант - rom_ratio, rom_vol)
    uint32_t ratio;             // тиков ЦАП на отсчёт сэмпла, 8.16
    uint32_t rom_ratio;
    uint8_t  rom_vol;
    uint16_t rom_acc;           // накопитель лишнего тика
    uint8_t  rom_buf[10];       // тики текущего отсчёта
    uint8_t  rom_n, rom_k;
    uint8_t  rom_old;           // последний выход (CHOLDV)
    uint8_t  rom_quiet;         // спад к 0x80 после конца сэмпла уже был
} gs_voice_t;

extern gs_voice_t gs_voice[GS_VOICES_MAX];

void gs_chan_reset(void);
void gs_chan_clear(int first, int n);        // только эти голоса: модуль не трогает эффекты
void gs_voice_set_inc(gs_voice_t *v, float inc);   // шаг на отсчёт смешивания
void gs_voice_set_pos(gs_voice_t *v, float pos);
// Добавить звук каналов к буферу (L, R, L, R...), n отсчётов
void gs_chan_mix(float *out, int n);

#endif
