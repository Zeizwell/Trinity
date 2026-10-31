#include <string.h>
#include <math.h>
#include "gs_chan.h"
#include "gs_cfg.h"

#define NO_IDX 0xFFFFFFFFu
#define Q_ONE  4294967296.0     // позиция 32.32
#define CCK_HZ 3546895.0        // такт Paula, PAL

gs_voice_t gs_voice[GS_VOICES_MAX];

static float hold_acc;          // счёт ступенек GS_RS_STEP, GS_RS_ROM
static uint32_t rom_tick_n;     // GS_RS_ROM: счёт тиков кванта
static double pwm_phase;        // счётчик ШИМ Paula, такты CCK, 0..64

static void bus_reset(void);

void gs_chan_reset(void) {
    bus_reset();
    memset(gs_voice, 0, sizeof gs_voice);
    hold_acc = 1.0f;            // первая ступенька - сразу
    rom_tick_n = 0;
    for (int i = 0; i < GS_VOICES_MAX; i++) {
        gs_voice[i].idx = NO_IDX;
        gs_voice[i].rom_old = 0x80, gs_voice[i].rom_quiet = 1;
    }
}

static void bus_edge(int c, int s, float offset, float amp);

void gs_chan_clear(int first, int n) {
    for (int i = first; i < first + n; i++)
        if (gs_voice[i].blep.last != 0.0f) bus_edge(i, 0, 0.0f, gs_voice[i].blep.last);
    memset(&gs_voice[first], 0, (size_t)n * sizeof gs_voice[0]);
    for (int i = first; i < first + n; i++) {
        gs_voice[i].idx = NO_IDX;
        gs_voice[i].rom_old = 0x80, gs_voice[i].rom_quiet = 1;
    }
}

void gs_voice_set_inc(gs_voice_t *v, float inc) {
    uint32_t mix = gs_cfg_mix_hz(), hold = gs_cfg.hold_hz && gs_cfg.hold_hz < mix ? gs_cfg.hold_hz : mix;
    v->inc_f = inc;
    v->inc = (uint64_t)((double)inc * Q_ONE);
    v->step = v->inc * mix / hold;
    v->ratio = inc > 0.0f ? (uint32_t)(65536.0 * hold / ((double)inc * mix)) : 0;
}

void gs_voice_period(gs_voice_t *v, float inc) {
    if (inc == v->inc_f) { v->inc_next = 0.0f; return; }
    if (gs_cfg.resample == GS_RS_BLEP && v->inc && v->on) v->inc_next = inc;
    else gs_voice_set_inc(v, inc), v->inc_next = 0.0f;
}

void gs_chan_rate(uint32_t from, uint32_t to) {
    for (int i = 0; i < GS_VOICES_MAX; i++) {
        gs_voice[i].inc_next = 0.0f;
        if (gs_voice[i].inc) gs_voice_set_inc(&gs_voice[i], gs_voice[i].inc_f * (float)from / (float)to);
    }
}

void gs_voice_set_pos(gs_voice_t *v, float pos) {
    v->pos = (uint64_t)((double)pos * Q_ONE);
    v->on = 1;
    v->inc_next = 0.0f;
    v->rom_n = v->rom_k = 0, v->rom_quiet = 0;
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

#define ROM_AVG(a, b) ((uint8_t)(((uint32_t)(a) + (b)) >> 1))   // ADD A,(HL) : RRA

// Тики одного отсчёта по SGEN (main_ngs.a80): сам отсчёт s, потом точки к следующему y. e - целых тиков, extra - лишний
static void rom_points(gs_voice_t *v, uint8_t s, uint8_t y, int e, int extra) {
    uint8_t *b = v->rom_buf;
    uint8_t h = ROM_AVG(s, y), q = ROM_AVG(s, h), t = ROM_AVG(h, y), z = ROM_AVG(t, y);
    int n = 0;
    b[n++] = s;
    switch (e) {
    case 1:                                                 // S1: сам отсчёт; лишний тик - середина
        if (extra) b[n++] = h;
        break;
    case 2:                                                 // S2: середина; с лишним - трети (DIVTAB3)
        if (extra) { uint8_t a = (uint8_t)(s + ((int)y - s) / 3); b[n++] = a; b[n++] = ROM_AVG(a, y); }
        else b[n++] = h;
        break;
    case 3:                                                 // S3: две середины; с лишним - четверти
        if (extra) { b[n++] = q; b[n++] = h; b[n++] = t; }
        else { b[n++] = h; b[n++] = h; }
        break;
    case 4:                                                 // S4: четверти; лишний - повтор отсчёта
        if (extra) b[n++] = s;
        b[n++] = q; b[n++] = h; b[n++] = t;
        break;
    case 5:                                                 // S5: q h t z; лишний - повтор отсчёта
        if (extra) b[n++] = s;
        b[n++] = q; b[n++] = h; b[n++] = t; b[n++] = z;
        break;
    default:                                                // S6..S8 (звук ниже ~6 кГц): равномерно, ветки по точкам не разобраны
        for (int i = 1, m = e - 1 + extra; i <= m; i++) b[n++] = (uint8_t)(s + ((int)y - s) * i / (m + 1));
        break;
    }
    v->rom_n = (uint8_t)n, v->rom_k = 0;
}

// Спад к 0x80 после конца сэмпла (GENZERO)
static void rom_fade(gs_voice_t *v) {
    uint8_t c = 0x80, o = v->rom_old, h = ROM_AVG(c, o), l = ROM_AVG(h, o), m = ROM_AVG(h, c);
    uint8_t *b = v->rom_buf;
    b[0] = ROM_AVG(l, o), b[1] = l, b[2] = ROM_AVG(l, h), b[3] = h;
    b[4] = ROM_AVG(m, h), b[5] = m, b[6] = ROM_AVG(m, c);
    v->rom_n = 7, v->rom_k = 0, v->rom_quiet = 1;
}

// Один тик ЦАП GS: байт 0..255
static uint8_t rom_tick(gs_voice_t *v) {
    if (v->rom_k >= v->rom_n) {
        uint32_t i = (uint32_t)(v->pos >> 32);
        if (v->data && v->on && v->rom_ratio && i < v->len) {
            uint32_t j = i + 1, r = v->rom_ratio;
            int e = (int)(r >> 16), extra;
            uint16_t f = (uint16_t)r;
            if (e < 1) e = 1, f = 0;                        // S0: быстрее ЦАП - отсчёт на тик
            if (e > 8) e = 8, f = 0xFFFF;                   // S9
            v->rom_acc = (uint16_t)(v->rom_acc + f);
            extra = v->rom_acc < f;
            if (v->loop_len && j >= v->loop_start + v->loop_len) j = v->loop_start;
            rom_points(v, (uint8_t)(v->data[i] ^ 0x80), j < v->len ? (uint8_t)(v->data[j] ^ 0x80) : 0x80, e, extra);
            voice_advance(v, (uint64_t)1 << 32);
        } else {
            v->on = 0;
            if (v->rom_quiet) return v->rom_old = 0x80;
            rom_fade(v);
        }
    }
    return v->rom_old = v->rom_buf[v->rom_k++];
}

// ШИМ громкости Paula: канал открыт vol тактов CCK из 64 (счётчик общий, 64 - открыт всегда), отсчёт и громкость меняются
// на смене отсчёта. Каждый фронт - и отсчёта, и ШИМ - через BLEP. d - тактов CCK на отсчёт смешивания
static float pwm_level(float s, int vol, double phase) {
    return vol >= 64 || fmod(phase, 64.0) < vol ? s : 0.0f;
}

static float voice_pwm(gs_voice_t *v, double p0, double d) {
    double ev[4], te = d;                           // время от начала отсчёта смешивания, такты CCK
    float s_new = v->pwm_s;
    int vol_new = v->pwm_v, n = 0;
    if (voice_advance(v, v->inc)) {
        uint32_t i = (uint32_t)(v->pos >> 32);
        if (i != v->idx) {
            float o = v->inc_f > 0.0f ? (float)(uint32_t)v->pos * (1.0f / 4294967296.0f) / v->inc_f : 0.0f;
            if (o >= 1.0f) o = 0.0f;
            s_new = v->data[i] * (1.0f / 128.0f), vol_new = v->vol;
            v->idx = i;
            if (v->inc_next != 0.0f) {
                gs_voice_set_inc(v, v->inc_next);
                v->inc_next = 0.0f;
            }
            te = d * (1.0 - o);
            ev[n++] = te;
        }
    } else {
        if (v->idx != NO_IDX) te = 0.0, ev[n++] = 0.0;
        s_new = 0.0f, v->idx = NO_IDX;
    }
    double on = floor(p0 / 64.0 + 1.0) * 64.0 - p0;     // следующее открытие
    if (on < d) ev[n++] = on;
    for (int k = 0; k < 2; k++) {                       // закрытия: по старой и по новой громкости
        int vol = k ? vol_new : v->pwm_v;
        double off = floor(p0 / 64.0) * 64.0 + vol - p0;
        if (off <= 0.0) off += 64.0;
        if (vol < 64 && off < d) ev[n++] = off;
    }
    for (int a = 1; a < n; a++)                         // по времени
        for (int b = a; b > 0 && ev[b] < ev[b - 1]; b--) { double x = ev[b]; ev[b] = ev[b - 1]; ev[b - 1] = x; }
    gs_blep_t *b = &v->blep;
    for (int k = 0; k < n; k++) {
        double t = ev[k];
        float level = t >= te ? pwm_level(s_new, vol_new, p0 + t) : pwm_level(v->pwm_s, v->pwm_v, p0 + t);
        if (level != b->last) {
            float o = (float)((d - t) / d);
            gs_blep_add(b, o < 1.0f ? o : 0.0f, b->last - level);
            b->last = level;
        }
    }
    v->pwm_s = s_new, v->pwm_v = vol_new;
    v->held = b->last;
    return b->left > 0 ? gs_blep_run(b, b->last) : b->last;
}

// Отсчёт сэмпла с учётом цикла, за краем - тишина
static float smp_at(const gs_voice_t *v, int64_t i) {
    if (v->loop_len && i >= (int64_t)v->loop_start + v->loop_len) i -= v->loop_len;
    return i >= 0 && i < (int64_t)v->len ? v->data[i] : 0.0f;
}

// Интерполяция (линейная или кубическая Catmull-Rom) + плавная громкость (HiFi)

// Сведение: каналы L R R L, как Amiga. Ширина: 100 % - жёстко, 0 - моно. На сторону два канала: половина, чтобы не вылезти за 1.
// Шина: фронты всех голосов в одном кольце. Цена - от числа фронтов, а не каналов и отсчётов
#define BUS_MASK 2047
static float bus_corr[2][BUS_MASK + 1];     // поправки BLEP
static float bus_step[2][BUS_MASK + 1];     // скачки уровня
static float bus_lvl[2];
static uint32_t bus_pos;
static float gain[GS_VOICES_MAX][2];        // панорама голоса

static void gains_set(int voices) {
    float side = 0.5f + gs_cfg.stereo_pct * 0.005f;     // 0.5..1 - своя сторона
    for (int c = 0; c < voices; c++) {
        int right = ((c + 1) >> 1) & 1;                 // 0 L, 1 R, 2 R, 3 L
        gain[c][0] = 0.5f * (right ? 1.0f - side : side);
        gain[c][1] = 0.5f * (right ? side : 1.0f - side);
    }
}

// Фронт голоса в отсчёте s блока: amp - прошлый уровень минус новый
static void bus_edge(int c, int s, float offset, float amp) {
    uint32_t p = (bus_pos + (uint32_t)s) & BUS_MASK;
    for (int k = 0; k < 2; k++) {
        float g = gain[c][k];
        if (g == 0.0f) continue;
        gs_blep_ring(bus_corr[k], BUS_MASK, p, offset, amp * g);
        bus_step[k][p] -= amp * g;
    }
}

// Голос за блок, кубика / линейная: многочлен считается один раз на отсчёт сэмпла, на выход - только Хорнер
static void voice_interp_block(gs_voice_t *v, int c, float *out, int n, float ramp_k, int cubic) {
    float gl = gain[c][0], gr = gain[c][1];
    for (int s = 0; s < n; s++) {
        float target = 0.0f, x = 0.0f;
        if (v->data && v->on && (uint32_t)(v->pos >> 32) < v->len) {    // сначала читаем, потом шаг: первый отсчёт ноты не теряем
            uint32_t i32 = (uint32_t)(v->pos >> 32);
            if (i32 != v->idx) {
                int64_t i = i32;
                float a = smp_at(v, i), b = smp_at(v, i + 1);
                if (cubic) {
                    float p = smp_at(v, i - 1), q = smp_at(v, i + 2);
                    v->cf[0] = a, v->cf[1] = 0.5f * (b - p), v->cf[2] = 0.5f * (2.0f * p - 5.0f * a + 4.0f * b - q);
                    v->cf[3] = 0.5f * (3.0f * (a - b) + q - p);
                } else v->cf[0] = a, v->cf[1] = b - a, v->cf[2] = v->cf[3] = 0.0f;
                v->idx = i32;
            }
            float t = (float)(uint32_t)v->pos * (1.0f / 4294967296.0f);
            x = (v->cf[0] + t * (v->cf[1] + t * (v->cf[2] + t * v->cf[3]))) * (1.0f / 128.0f);
            target = v->vol * (1.0f / 64.0f);
            uint32_t was = (uint32_t)v->pos;
            uint64_t inc = v->inc;
            if (inc >> 32 || was + (uint32_t)inc < was) voice_advance(v, inc);  // отсчёт сменился: цикл, конец
            else v->pos += inc;
        } else v->on = 0, v->idx = NO_IDX;
        v->vol_now = ramp_k > 0.0f ? v->vol_now + (target - v->vol_now) * ramp_k : target;
        x *= v->vol_now;
        out[s * 2] += x * gl;
        out[s * 2 + 1] += x * gr;
    }
}

// Голос за блок: считаем только на смене отсчёта сэмпла, между ними - один шаг умножением
static void voice_block(gs_voice_t *v, int c, int n) {
    gs_blep_t *b = &v->blep;
    for (int s = 0; s < n; s++) {
        float level = 0.0f, offset = 0.0f;
        if (voice_advance(v, v->inc)) {
            uint32_t i = (uint32_t)(v->pos >> 32);
            if (i == v->idx) level = v->held;
            else {
                level = v->data[i] * (1.0f / 128.0f) * v->vol * (1.0f / 64.0f);
                offset = v->inc_f > 0.0f ? (float)(uint32_t)v->pos * (1.0f / 4294967296.0f) / v->inc_f : 0.0f;
                if (offset >= 1.0f) offset = 0.0f;
                v->idx = i;
                if (v->inc_next != 0.0f) {              // Паула берёт период здесь, на смене отсчёта
                    gs_voice_set_inc(v, v->inc_next);
                    v->inc_next = 0.0f;
                }
            }
        } else v->idx = NO_IDX;
        if (level != b->last) {
            bus_edge(c, s, offset, b->last - level);
            b->last = level;
        }
        v->held = level;
        if (!v->data || !v->on) {                       // молчит: до конца блока ничего не будет
            if (level == 0.0f) return;
            continue;
        }
        if (v->inc >> 32 || !v->inc) {                  // быстрее отсчёта на шаг или стоит на месте
            if (!v->inc) return;
            continue;
        }
        uint32_t inc = (uint32_t)v->inc;
        uint32_t skip = (0xFFFFFFFFu - (uint32_t)v->pos) / inc;     // шагов без смены отсчёта
        if (skip > (uint32_t)(n - 1 - s)) skip = (uint32_t)(n - 1 - s);
        v->pos += (uint64_t)skip * inc;
        s += (int)skip;
    }
}

// Тики генератора GS: как ЦАП GS и NeoGS: на тике голоса складываются, уровень держится до следующего тика, без BLEP
static void ticks_block(int voices, int n, gs_resample_t rs, float hold_add) {
    for (int s = 0; s < n; s++) {
        hold_acc += hold_add;
        if (hold_acc < 1.0f) continue;
        hold_acc -= 1.0f;
        int quantum = (rom_tick_n++ & 255) == 0;        // GS: громкость и шаг - раз в квант 256 тиков
        float amp[2] = { 0.0f, 0.0f };
        for (int c = 0; c < voices; c++) {
            gs_voice_t *v = &gs_voice[c];
            gs_blep_t *b = &v->blep;
            if (rs == GS_RS_ROM) {
                if (quantum) v->rom_vol = v->vol, v->rom_ratio = v->ratio;
                if (!v->data && v->rom_quiet && v->rom_k >= v->rom_n && b->last == 0.0f) continue;  // молчит
                v->held = ((int)rom_tick(v) - 128) * (1.0f / 128.0f) * v->rom_vol * (1.0f / 64.0f);
            } else voice_step(v);
            if (v->held != b->last) {
                float d = b->last - v->held;
                amp[0] += d * gain[c][0];
                amp[1] += d * gain[c][1];
                b->last = v->held;
            }
        }
        uint32_t p = (bus_pos + (uint32_t)s) & BUS_MASK;
        for (int k = 0; k < 2; k++) {
            bus_step[k][p] -= amp[k];
        }
    }
}

// Шину - в буфер. В конце блока уровень - точно по голосам: ошибка float не копится
static void bus_out(float *out, int n, int voices) {
    for (int k = 0; k < 2; k++) {
        float *corr = bus_corr[k], *step = bus_step[k], lvl = bus_lvl[k];
        uint32_t p = bus_pos;
        for (int s = 0; s < n; s++) {
            lvl += step[p];
            out[s * 2 + k] += lvl + corr[p];
            step[p] = 0.0f, corr[p] = 0.0f;
            p = (p + 1) & BUS_MASK;
        }
        float exact = 0.0f;
        for (int c = 0; c < voices; c++) exact += gs_voice[c].blep.last * gain[c][k];
        bus_lvl[k] = exact;
    }
    bus_pos = (bus_pos + (uint32_t)n) & BUS_MASK;
}

// n отсчётов на частоте смешивания (gs_cfg_mix_hz)
void gs_chan_mix(float *out, int n) {
    int voices = gs_cfg.voices < GS_VOICES_MAX ? gs_cfg.voices : GS_VOICES_MAX;
    float rate = (float)gs_cfg_mix_hz();
    float hold = gs_cfg.hold_hz && (float)gs_cfg.hold_hz < rate ? (float)gs_cfg.hold_hz : rate;
    gs_resample_t rs = gs_cfg.resample;
    int use_pwm = rs == GS_RS_BLEP && gs_cfg.pwm;
    gains_set(voices);
    if (rs == GS_RS_BLEP && !use_pwm) {
        for (int c = 0; c < voices; c++) {
            gs_voice_t *v = &gs_voice[c];
            if (v->data || v->blep.last != 0.0f) voice_block(v, c, n);
        }
    } else if (rs == GS_RS_STEP || rs == GS_RS_ROM) {
        ticks_block(voices, n, rs, hold / rate);
    } else {                                            // на каждый отсчёт: кубика, рампа, ШИМ
        float ramp_k = gs_cfg.ramp ? 1000.0f / (1.5f * rate) : 0.0f;    // рампа ~1.5 мс
        double cck = CCK_HZ / rate;                     // тактов CCK на отсчёт
        for (int c = 0; c < voices; c++)                // пришли из режима с фронтами: уровни на шине снять
            if (gs_voice[c].blep.last != 0.0f) bus_edge(c, 0, 0.0f, gs_voice[c].blep.last), gs_voice[c].blep.last = 0.0f;
        if (use_pwm) {
            for (int s = 0; s < n; s++) {
                float l = 0.0f, r = 0.0f;
                for (int c = 0; c < voices; c++) {
                    float x = voice_pwm(&gs_voice[c], pwm_phase, cck);
                    l += x * gain[c][0];
                    r += x * gain[c][1];
                }
                pwm_phase = fmod(pwm_phase + cck, 64.0);
                out[s * 2] += l;
                out[s * 2 + 1] += r;
            }
        } else {
            for (int c = 0; c < voices; c++) {
                gs_voice_t *v = &gs_voice[c];
                if (v->data || v->vol_now != 0.0f) voice_interp_block(v, c, out, n, ramp_k, rs == GS_RS_CUBIC);
            }
        }
    }
    bus_out(out, n, voices);
}

static void bus_reset(void) {
    memset(bus_corr, 0, sizeof bus_corr);
    memset(bus_step, 0, sizeof bus_step);
    bus_lvl[0] = bus_lvl[1] = 0.0f;
    bus_pos = 0;
}
