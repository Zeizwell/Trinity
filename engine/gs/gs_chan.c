#include <string.h>
#include "gs_chan.h"
#include "gs_cfg.h"

gs_voice_t gs_voice[GS_VOICES_MAX];

static float hold_acc;          // счёт ступенек GS_RS_STEP

void gs_chan_reset(void) {
    memset(gs_voice, 0, sizeof gs_voice);
    hold_acc = 1.0f;            // первая ступенька - сразу
}

// Шаг канала: новая ступенька и позиция. step - сдвиг позиции
static void voice_step(gs_voice_t *v, float step) {
    if (!v->data || v->pos < 0.0f) { v->held = 0.0f; return; }
    uint32_t i = (uint32_t)v->pos;
    if (i >= v->len) { v->pos = -1.0f; v->held = 0.0f; return; }
    v->held = v->data[i] * (1.0f / 128.0f) * v->vol * (1.0f / 64.0f);
    v->pos += step;
    if (v->loop_len) {
        float end = (float)(v->loop_start + v->loop_len);
        if (v->pos >= end) v->pos -= (float)v->loop_len;
    }
}

// Сведение: каналы L R R L, как Amiga. Ширина: 100 % - жёстко, 0 - моно. На сторону два канала: половина, чтобы не вылезти за 1
void gs_chan_mix(float *out, int n) {
    int voices = gs_cfg.voices < GS_VOICES_MAX ? gs_cfg.voices : GS_VOICES_MAX;
    float side = 0.5f + gs_cfg.stereo_pct * 0.005f;     // 0.5..1 - своя сторона
    float rate = (float)gs_cfg.rate_hz;
    float hold = gs_cfg.hold_hz && gs_cfg.hold_hz < gs_cfg.rate_hz ? (float)gs_cfg.hold_hz : rate;
    float hold_add = hold / rate;                       // доля ступеньки на отсчёт вывода
    float step_mul = rate / hold;                       // сдвиг позиции за ступеньку
    for (int s = 0; s < n; s++) {
        int tick = 0;
        hold_acc += hold_add;
        if (hold_acc >= 1.0f) hold_acc -= 1.0f, tick = 1;
        float l = 0.0f, r = 0.0f;
        for (int c = 0; c < voices; c++) {
            gs_voice_t *v = &gs_voice[c];
            if (tick) voice_step(v, v->inc * step_mul);
            float x = v->held * 0.5f;
            int right = ((c + 1) >> 1) & 1;             // 0 L, 1 R, 2 R, 3 L
            l += x * (right ? 1.0f - side : side);
            r += x * (right ? side : 1.0f - side);
        }
        *out++ += l;
        *out++ += r;
    }
}
