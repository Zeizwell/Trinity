#include <string.h>
#include "gs_chan.h"
#include "gs_cfg.h"

#define NO_IDX 0xFFFFFFFFu
#define Q_ONE  4294967296.0     // позиция 32.32

gs_voice_t gs_voice[GS_VOICES_MAX];

static float hold_acc;          // счёт ступенек GS_RS_STEP

void gs_chan_reset(void) {
    memset(gs_voice, 0, sizeof gs_voice);
    hold_acc = 1.0f;            // первая ступенька - сразу
    for (int i = 0; i < GS_VOICES_MAX; i++) gs_voice[i].idx = NO_IDX;
}

void gs_voice_set_inc(gs_voice_t *v, float inc) {
    uint32_t mix = gs_cfg_mix_hz(), hold = gs_cfg.hold_hz && gs_cfg.hold_hz < mix ? gs_cfg.hold_hz : mix;
    v->inc_f = inc;
    v->inc = (uint64_t)((double)inc * Q_ONE);
    v->step = v->inc * mix / hold;
}

void gs_voice_set_pos(gs_voice_t *v, float pos) {
    v->pos = (uint64_t)((double)pos * Q_ONE);
    v->on = 1;
}

// Позиция вперёд, цикл, конец сэмпла. 1 - сэмпл ещё звучит
static int voice_advance(gs_voice_t *v, uint64_t step) {
    if (!v->data || !v->on) return 0;
    v->pos += step;
    if (v->loop_len) {
        uint64_t end = (uint64_t)(v->loop_start + v->loop_len) << 32;
        if (v->pos >= end) v->pos -= (uint64_t)v->loop_len << 32;
    }
    if ((uint32_t)(v->pos >> 32) >= v->len) { v->on = 0; return 0; }
    return 1;
}

// Ступенька GS: новый уровень на своём такте, отсчёт сэмпла - текущий
static void voice_step(gs_voice_t *v) {
    if (!v->data || !v->on || (uint32_t)(v->pos >> 32) >= v->len) { v->held = 0.0f; v->on = 0; return; }
    v->held = v->data[v->pos >> 32] * (1.0f / 128.0f) * v->vol * (1.0f / 64.0f);
    voice_advance(v, v->step);
}

// BLEP (как pt2-clone): ступенька на своей частоте канала, фронт - с поправкой. Громкость берётся на смене отсчёта, как у Paula
static float voice_blep(gs_voice_t *v) {
    float level = 0.0f, offset = 0.0f;
    if (voice_advance(v, v->inc)) {
        uint32_t i = (uint32_t)(v->pos >> 32);
        if (i == v->idx) level = v->held;
        else {
            level = v->data[i] * (1.0f / 128.0f) * v->vol * (1.0f / 64.0f);
            offset = v->inc_f > 0.0f ? (float)(uint32_t)v->pos * (1.0f / 4294967296.0f) / v->inc_f : 0.0f;
            if (offset >= 1.0f) offset = 0.0f;
            v->idx = i;
        }
    } else v->idx = NO_IDX;
    gs_blep_t *b = &v->blep;
    if (level != b->last) {
        gs_blep_add(b, offset, b->last - level);
        b->last = level;
    }
    v->held = level;
    return b->left > 0 ? gs_blep_run(b, level) : level;
}

// Линейная интерполяция + плавная громкость (HiFi)
static float voice_interp(gs_voice_t *v, float ramp_k) {
    float target = 0.0f, x = 0.0f;
    if (v->data && v->on && (uint32_t)(v->pos >> 32) < v->len) {    // сначала читаем, потом шаг: первый отсчёт ноты не теряем
        uint32_t i = (uint32_t)(v->pos >> 32), j = i + 1;
        if (v->loop_len && j >= v->loop_start + v->loop_len) j = v->loop_start;
        float a = v->data[i], b = j < v->len ? v->data[j] : 0.0f;
        x = (a + (b - a) * ((float)(uint32_t)v->pos * (1.0f / 4294967296.0f))) * (1.0f / 128.0f);
        target = v->vol * (1.0f / 64.0f);
        voice_advance(v, v->inc);
    } else v->on = 0;
    v->vol_now = ramp_k > 0.0f ? v->vol_now + (target - v->vol_now) * ramp_k : target;
    return x * v->vol_now;
}

// Сведение: каналы L R R L, как Amiga. Ширина: 100 % - жёстко, 0 - моно. На сторону два канала: половина, чтобы не вылезти за 1.
// n отсчётов на частоте смешивания (gs_cfg_mix_hz)
void gs_chan_mix(float *out, int n) {
    int voices = gs_cfg.voices < GS_VOICES_MAX ? gs_cfg.voices : GS_VOICES_MAX;
    float side = 0.5f + gs_cfg.stereo_pct * 0.005f;     // 0.5..1 - своя сторона
    float rate = (float)gs_cfg_mix_hz();
    float hold = gs_cfg.hold_hz && (float)gs_cfg.hold_hz < rate ? (float)gs_cfg.hold_hz : rate;
    float hold_add = hold / rate;                       // доля ступеньки на отсчёт
    float ramp_k = gs_cfg.ramp ? 1000.0f / (1.5f * rate) : 0.0f;    // рампа ~1.5 мс
    gs_resample_t rs = gs_cfg.resample;
    for (int s = 0; s < n; s++) {
        int tick = 0;
        if (rs == GS_RS_STEP) {
            hold_acc += hold_add;
            if (hold_acc >= 1.0f) hold_acc -= 1.0f, tick = 1;
        }
        float l = 0.0f, r = 0.0f;
        for (int c = 0; c < voices; c++) {
            gs_voice_t *v = &gs_voice[c];
            float x;
            if (rs == GS_RS_BLEP) x = voice_blep(v);
            else if (rs == GS_RS_INTERP) x = voice_interp(v, ramp_k);
            else {                                      // ступеньки GS: фронт - точно в свой момент, без лишней грязи от частоты вывода
                gs_blep_t *b = &v->blep;
                if (tick) {
                    voice_step(v);
                    if (v->held != b->last) {
                        float offset = hold < rate ? hold_acc / hold_add : 0.0f;
                        gs_blep_add(b, offset < 1.0f ? offset : 0.0f, b->last - v->held);
                        b->last = v->held;
                    }
                }
                x = b->left > 0 ? gs_blep_run(b, v->held) : v->held;
            }
            x *= 0.5f;
            int right = ((c + 1) >> 1) & 1;             // 0 L, 1 R, 2 R, 3 L
            l += x * (right ? 1.0f - side : side);
            r += x * (right ? side : 1.0f - side);
        }
        *out++ += l;
        *out++ += r;
    }
}
