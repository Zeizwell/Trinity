#define POCKETMOD_IMPLEMENTATION
#include "../third_party/pocketmod/pocketmod.h"
#include "gs_mod.h"
#include "gs_chan.h"
#include "gs_cfg.h"
#include "gs_filter.h"
#include <math.h>

#define MOD_VOICES 4    // дальше - голоса эффектов

static pocketmod_context pm;
static int playing, loaded;
static int vol_master = 64, vol_module = 64;

int gs_mod_load(const void *data, int size) {
    playing = loaded = 0;
    gs_chan_clear(0, MOD_VOICES);
    gs_led_module = 0;
    if (!pocketmod_init(&pm, data, size, (int)gs_cfg_mix_hz())) return 0;
    if (pm.num_channels > GS_VOICES_MAX) return 0;
    playing = loaded = 1;
    return 1;
}

int gs_mod_play(const void *data, int size, uint8_t pos) {
    if (!gs_mod_load(data, size)) return 0;
    pm.pattern = (signed char)(pos < pm.length ? pos : 0);
    return 1;
}

void gs_mod_stop(void) {
    playing = 0;
    gs_chan_clear(0, MOD_VOICES);
}

void gs_mod_pause(void) {
    playing = 0;
    for (int i = 0; i < MOD_VOICES; i++) gs_voice[i].data = 0;  // позицию держим, вернёт chan_to_voice
}

int gs_mod_resume(void) {
    if (!loaded) return 0;
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
    int to = (int)gs_cfg_mix_hz(), from = pm.samples_per_second;
    if (to == from) return;
    float k = (float)to / (float)from;
    pm.samples_per_second = to;
    pm.samples_per_tick *= k;
    pm.sample *= k;
    for (int i = 0; i < pm.num_channels; i++) pm.channels[i].increment /= k;
}

void gs_mod_audio(float *out, int n) {
    if (loaded) rate_check();
    if (!playing) { gs_chan_mix(out, n); return; }  // эффекты звучат и без модуля
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
    if (v < 0x20) pm.ticks_per_line = v;
    else pm.samples_per_tick = pm.samples_per_second / (0.4f * v);
}

uint8_t gs_mod_song_pos(void) { return loaded ? (uint8_t)pm.pattern : 0; }
uint8_t gs_mod_pat_pos(void) { return loaded && pm.line >= 0 ? (uint8_t)pm.line : 0; }
uint8_t gs_mod_speed(void) { return loaded ? (uint8_t)pm.ticks_per_line : 0; }
uint8_t gs_mod_tempo(void) { return loaded ? (uint8_t)(pm.samples_per_second / (0.4f * pm.samples_per_tick) + 0.5f) : 0; }

uint8_t gs_mod_note(int chan) {
    if (!playing || chan >= pm.num_channels) return 127;
    _pocketmod_chan *ch = &pm.channels[chan];
    if (!ch->sample || !ch->period || !gs_voice[chan].on) return 127;
    int n = 48 + (int)lround(12.0 * log2(428.0 / ch->period));
    return (uint8_t)(n < 0 ? 0 : n > 95 ? 95 : n);
}

uint8_t gs_mod_vol(int chan) {
    if (!playing || chan >= pm.num_channels) return 0;
    return pm.channels[chan].real_volume;
}
