#define POCKETMOD_IMPLEMENTATION
#include "../third_party/pocketmod/pocketmod.h"
#include "gs_mod.h"
#include "gs_chan.h"
#include "gs_cfg.h"
#include "gs_filter.h"
#include "gs_pt.h"
#include <math.h>

#define MOD_VOICES 4    // дальше - голоса эффектов
#define CCK_HZ 3546895.0f

static pocketmod_context pm;    // только не-ProTracker: 15 сэмплов, больше 4 каналов
static int use_pt;              // 4 канала, 31 сэмпл - наш gs_pt, он один в один с PT
static int playing, loaded;
static int vol_master = 64, vol_module = 64;
static double pt_acc;           // отсчётов с начала тика
static int pt_rate;             // частота, по которой посчитаны шаги голосов
static const int8_t *pt_paused[MOD_VOICES];
static int8_t null_smp[4];      // пустой блок: AUDxLC = NULL и длина 0

// Период Паулы -> шаг на отсчёт смешивания. 0 - это 65536, меньше 113 не бывает (как в pt2-clone)
static float pt_inc(int period) {
    if (!period) period = 65536;
    else if (period < 113) period = 113;
    return CCK_HZ / ((float)period * (float)pt_rate);
}

static const int8_t *pt_block(const gs_pt_reg_t *r, uint32_t *len) {
    if (!r->lc || !r->len) { *len = sizeof null_smp; return null_smp; }
    *len = (uint32_t)r->len * 2;
    return r->lc;
}

void gs_pt_dma_start(int chan) {
    uint32_t len;
    const int8_t *d = pt_block(&gs_pt_reg[chan], &len);
    gs_voice_start(&gs_voice[chan], d, len, pt_inc(gs_pt_reg[chan].period));
}

// Регистры плеера -> голос: громкость, период (при BLEP вступит на смене отсчёта), следующий блок
static void pt_apply(void) {
    for (int i = 0; i < MOD_VOICES; i++) {
        gs_voice_t *v = &gs_voice[i];
        const gs_pt_reg_t *r = &gs_pt_reg[i];
        v->vol = (uint8_t)(r->vol * vol_module / 64 * vol_master / 64);
        gs_voice_period(v, pt_inc(r->period));
        v->next_data = pt_block(r, &v->next_len);
    }
}

static void pt_tick(void) {
    gs_pt_tick();
    int led = gs_pt_led();
    if (led >= 0) gs_led_module = led;
    if (gs_pt_stopped()) playing = 0;                   // F00: песня стоит, голоса доигрывают
    pt_apply();
}

static int load(const void *data, int size, int writable) {
    playing = loaded = 0;
    gs_chan_clear(0, MOD_VOICES);
    gs_led_module = 0;
    use_pt = gs_pt_is_mod(data, size);
    if (use_pt) {
        if (!gs_pt_load(data, size, writable)) return 0;
        pt_rate = (int)gs_cfg_mix_hz();
        pt_acc = 0.0;
    } else {
        if (!pocketmod_init(&pm, data, size, (int)gs_cfg_mix_hz())) return 0;
        if (pm.num_channels > GS_VOICES_MAX) return 0;
    }
    playing = loaded = 1;
    return 1;
}

int gs_mod_load(const void *data, int size) {
    if (!load(data, size, 0)) return 0;                 // модуль может лежать во флеше
    if (use_pt) pt_tick();
    return 1;
}

int gs_mod_play(const void *data, int size, uint8_t pos) {
    if (!load(data, size, 1)) return 0;                 // память GS
    if (use_pt) {
        gs_pt_set_pos(pos);
        pt_tick();
    } else pm.pattern = (signed char)(pos < pm.length ? pos : 0);
    return 1;
}

void gs_mod_stop(void) {
    playing = 0;
    gs_chan_clear(0, MOD_VOICES);
}

void gs_mod_pause(void) {
    playing = 0;
    for (int i = 0; i < MOD_VOICES; i++) {
        pt_paused[i] = gs_voice[i].data;
        gs_voice[i].data = 0;                           // позицию держим, вернёт resume или chan_to_voice
    }
}

int gs_mod_resume(void) {
    if (!loaded) return 0;
    if (use_pt) for (int i = 0; i < MOD_VOICES; i++) gs_voice[i].data = pt_paused[i];
    playing = 1;
    return 1;
}

int gs_mod_playing(void) { return playing; }

// Состояние канала pocketmod -> наш канал. Позицию ведём мы (32.32); от него берём только новую: при ноте он пишет >= 0,
// мы оставляем ему POS_OURS. Его float на длинных сэмплах плывёт по высоте
#define POS_OURS (-2.0f)
static void chan_to_voice(int i) {
    _pocketmod_chan *ch = &pm.channels[i];
    gs_voice_t *v = &gs_voice[i];
    if (!ch->sample) { v->data = 0; return; }
    _pocketmod_sample *smp = &pm.samples[ch->sample - 1];
    unsigned char *h = POCKETMOD_SAMPLE(&pm, ch->sample);
    uint32_t loop_start = (uint32_t)((h[4] << 8) | h[5]) << 1;
    uint32_t loop_len = (uint32_t)((h[6] << 8) | h[7]) << 1;
    v->data = smp->data;
    v->len = smp->length;
    v->loop_start = loop_start;
    v->loop_len = loop_len > 2 && loop_start + loop_len <= smp->length ? loop_len : 0;
    v->vol = (uint8_t)(ch->real_volume * vol_module / 64 * vol_master / 64);
    if (ch->position >= 0.0f) {                         // новая нота: старт DMA, период сразу
        gs_voice_set_inc(v, ch->increment);
        gs_voice_set_pos(v, ch->position);
        ch->position = POS_OURS;
    } else gs_voice_period(v, ch->increment);
}

// Режим сменил частоту: темп и высота должны остаться те же
static void rate_check(void) {
    int to = (int)gs_cfg_mix_hz();
    if (use_pt) {
        if (to == pt_rate) return;
        pt_acc *= (double)to / pt_rate;
        pt_rate = to;
        pt_apply();
        return;
    }
    int from = pm.samples_per_second;
    if (to == from) return;
    float k = (float)to / (float)from;
    pm.samples_per_second = to;
    pm.samples_per_tick *= k;
    pm.sample *= k;
    for (int i = 0; i < pm.num_channels; i++) pm.channels[i].increment /= k;
}

static void audio_pt(float *out, int n) {
    while (n > 0) {
        double per_tick = gs_pt_tick_sec() * pt_rate;
        int num = (int)(per_tick - pt_acc);             // до конца тика
        if (num < 1) num = 1;
        if (num > n) num = n;
        gs_chan_mix(out, num);
        out += num * 2;
        n -= num;
        pt_acc += num;
        if (pt_acc >= per_tick) {
            pt_acc -= per_tick;
            pt_tick();
            if (!playing) { gs_chan_mix(out, n); return; }
        }
    }
}

void gs_mod_audio(float *out, int n) {
    if (loaded) rate_check();
    if (!playing) { gs_chan_mix(out, n); return; }  // эффекты звучат и без модуля
    if (use_pt) { audio_pt(out, n); return; }
    while (n > 0) {
        int num = (int)(pm.samples_per_tick - pm.sample);   // до конца тика
        if (num < 1) num = 1;
        if (num > n) num = n;
        for (int i = 0; i < pm.num_channels; i++) chan_to_voice(i);
        gs_chan_mix(out, num);
        out += num * 2;
        n -= num;
        pm.sample += (float)num;
        if (pm.sample >= pm.samples_per_tick) {
            pm.sample -= pm.samples_per_tick;
            _pocketmod_next_tick(&pm);
            if (pm.tick == 0)                               // E0x: LED-фильтр, 0 - вкл, 1 - выкл
                for (int i = 0; i < pm.num_channels; i++)
                    if (pm.channels[i].effect == 0xE0) gs_led_module = !(pm.channels[i].param & 1);
        }
    }
}

void gs_mod_set_vol(uint8_t master, uint8_t module) { vol_master = master, vol_module = module; }

void gs_mod_set_speed(uint8_t v) {
    if (!loaded || !v) return;
    if (use_pt) {
        if (v < 0x20) gs_pt_set_speed(v);
        else gs_pt_set_bpm(v);
        return;
    }
    if (v < 0x20) pm.ticks_per_line = v;
    else pm.samples_per_tick = pm.samples_per_second / (0.4f * v);
}

uint8_t gs_mod_song_pos(void) { return !loaded ? 0 : use_pt ? (uint8_t)gs_pt_play_pos() : (uint8_t)pm.pattern; }
uint8_t gs_mod_pat_pos(void) { return !loaded ? 0 : use_pt ? (uint8_t)gs_pt_play_row() : pm.line >= 0 ? (uint8_t)pm.line : 0; }
uint8_t gs_mod_speed(void) { return !loaded ? 0 : use_pt ? (uint8_t)gs_pt_speed() : (uint8_t)pm.ticks_per_line; }
uint8_t gs_mod_tempo(void) {
    if (!loaded) return 0;
    return use_pt ? (uint8_t)gs_pt_bpm() : (uint8_t)(pm.samples_per_second / (0.4f * pm.samples_per_tick) + 0.5f);
}

uint8_t gs_mod_note(int chan) {
    int period;
    if (!playing) return 127;
    if (use_pt) {
        if (chan >= MOD_VOICES) return 127;
        period = gs_pt_period(chan);
    } else {
        if (chan >= pm.num_channels) return 127;
        _pocketmod_chan *ch = &pm.channels[chan];
        if (!ch->sample) return 127;
        period = ch->period;
    }
    if (!period || !gs_voice[chan].on) return 127;
    int n = 48 + (int)lround(12.0 * log2(428.0 / period));
    return (uint8_t)(n < 0 ? 0 : n > 95 ? 95 : n);
}

uint8_t gs_mod_vol(int chan) {
    if (!playing) return 0;
    if (use_pt) return chan < MOD_VOICES ? (uint8_t)gs_pt_reg[chan].vol : 0;
    return chan < pm.num_channels ? pm.channels[chan].real_volume : 0;
}
