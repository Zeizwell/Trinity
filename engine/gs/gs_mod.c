#define POCKETMOD_IMPLEMENTATION
#include "../third_party/pocketmod/pocketmod.h"
#include "gs_mod.h"
#include "gs_chan.h"
#include "gs_cfg.h"

static pocketmod_context pm;
static int playing;

int gs_mod_load(const void *data, int size) {
    playing = 0;
    gs_chan_reset();
    if (!pocketmod_init(&pm, data, size, (int)gs_cfg.rate_hz)) return 0;
    if (pm.num_channels > GS_VOICES_MAX) return 0;
    playing = 1;
    return 1;
}

void gs_mod_stop(void) {
    playing = 0;
    gs_chan_reset();
}

int gs_mod_playing(void) { return playing; }

// Состояние канала pocketmod -> наш канал. Позицию ведёт он при ноте, мы - при звуке
static void chan_to_voice(int i) {
    _pocketmod_chan *ch = &pm.channels[i];
    gs_voice_t *v = &gs_voice[i];
    if (!ch->sample || ch->position < 0.0f) { v->data = 0; return; }
    _pocketmod_sample *smp = &pm.samples[ch->sample - 1];
    unsigned char *h = POCKETMOD_SAMPLE(&pm, ch->sample);
    uint32_t loop_start = (uint32_t)((h[4] << 8) | h[5]) << 1;
    uint32_t loop_len = (uint32_t)((h[6] << 8) | h[7]) << 1;
    v->data = smp->data;
    v->len = smp->length;
    v->loop_start = loop_start;
    v->loop_len = loop_len > 2 && loop_start + loop_len <= smp->length ? loop_len : 0;
    v->pos = ch->position;
    v->inc = ch->increment;
    v->vol = ch->real_volume;
}

void gs_mod_audio(float *out, int n) {
    if (!playing) return;
    while (n > 0) {
        int num = (int)(pm.samples_per_tick - pm.sample);   // до конца тика
        if (num < 1) num = 1;
        if (num > n) num = n;
        for (int i = 0; i < pm.num_channels; i++) chan_to_voice(i);
        gs_chan_mix(out, num);
        for (int i = 0; i < pm.num_channels; i++)           // позиция - обратно
            if (gs_voice[i].data) pm.channels[i].position = gs_voice[i].pos;
        out += num * 2;
        n -= num;
        pm.sample += (float)num;
        if (pm.sample >= pm.samples_per_tick) {
            pm.sample -= pm.samples_per_tick;
            _pocketmod_next_tick(&pm);
        }
    }
}
