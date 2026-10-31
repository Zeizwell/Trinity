// Плагин pocketmod (MIT): MOD, которые не берёт gs_pt - 15 сэмплов, больше 4 каналов. Логика строк и команд его, звук - наши каналы
#define POCKETMOD_IMPLEMENTATION
#include "pocketmod/pocketmod.h"
#include "gs_player.h"
#include "gs_chan.h"
#include "gs_cfg.h"
#include "gs_filter.h"

static pocketmod_context pm;

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
    v->vol = (uint8_t)gs_mod_gain(ch->real_volume);
    if (ch->position >= 0.0f) {                         // новая нота: старт DMA, период сразу
        gs_voice_set_inc(v, ch->increment);
        gs_voice_set_pos(v, ch->position);
        ch->position = POS_OURS;
    } else gs_voice_period(v, ch->increment);
}

static int accepts(const void *data, int size) { (void)data, (void)size; return 1; }

static int load(const void *data, int size, uint8_t pos, int writable) {
    (void)writable;
    if (!pocketmod_init(&pm, data, size, (int)gs_cfg_mix_hz())) return 0;
    if (pm.num_channels > GS_VOICES_MAX) return 0;
    pm.pattern = (signed char)(pos < pm.length ? pos : 0);
    return 1;
}

static void rate_sync(void) {
    int to = (int)gs_cfg_mix_hz(), from = pm.samples_per_second;
    if (to == from) return;
    float k = (float)to / (float)from;
    pm.samples_per_second = to;
    pm.samples_per_tick *= k;
    pm.sample *= k;
    for (int i = 0; i < pm.num_channels; i++) pm.channels[i].increment /= k;
}

static int audio(float *out, int n) {
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
                    if (pm.channels[i].effect == 0xE0) gs_filter_led_set(!(pm.channels[i].param & 1));
        }
    }
    return 1;
}

static void set_speed(uint8_t v) {
    if (v < 0x20) pm.ticks_per_line = v;
    else pm.samples_per_tick = pm.samples_per_second / (0.4f * v);
}

static uint8_t song_pos(void) { return (uint8_t)pm.pattern; }
static uint8_t pat_pos(void) { return pm.line >= 0 ? (uint8_t)pm.line : 0; }
static uint8_t speed(void) { return (uint8_t)pm.ticks_per_line; }
static uint8_t tempo(void) { return (uint8_t)(pm.samples_per_second / (0.4f * pm.samples_per_tick) + 0.5f); }

static int period(int chan) {
    if (chan >= pm.num_channels || !pm.channels[chan].sample) return 0;
    return pm.channels[chan].period;
}

static uint8_t vol(int chan) { return chan < pm.num_channels ? pm.channels[chan].real_volume : 0; }

const gs_player_t gs_player_pocket = {
    accepts, load, rate_sync, audio, set_speed, song_pos, pat_pos, speed, tempo, period, vol
};
