#include <math.h>
#include <string.h>
#include "gs_fx.h"
#include "gs_chan.h"
#include "gs_cfg.h"

#define FX_VOICE 4              // эффекты играют в голосах после модуля
#define CCK_HZ 3546894.6        // такт Paula, PAL, как в плеере модулей
#define NOTE_C2 48              // C-2 Амиги: период 428

static gs_fx_t fx[GS_FX_MAX];
static int fx_count;
static uint8_t chan_prio[GS_FX_CHANS], chan_vol[GS_FX_CHANS];
static uint8_t master = 64;

void gs_fx_reset(void) {
    fx_count = 0;
    master = 64;
    gs_chan_clear(FX_VOICE, GS_FX_CHANS);
}

void gs_fx_stop(uint8_t chan_mask) {
    for (int c = 0; c < GS_FX_CHANS; c++)
        if (chan_mask >> c & 1) gs_voice[FX_VOICE + c].data = 0, gs_voice[FX_VOICE + c].on = 0;
}

gs_fx_t *gs_fx_get(int handle) { return handle >= 1 && handle <= fx_count ? &fx[handle - 1] : 0; }

int gs_fx_new(const void *data) {
    if (fx_count >= GS_FX_MAX) return 0;
    gs_fx_t *f = &fx[fx_count++];
    f->data = (const int8_t *)data, f->len = 0;
    f->loop_begin = GS_FX_NO_LOOP, f->loop_end = 0;
    f->note = 60, f->vol = 0x40, f->finetune = 0, f->prio = 0x80, f->seek_first = 0x0F, f->seek_last = 0x0F;
    return fx_count;
}

void gs_fx_end(int handle, uint32_t len) {
    gs_fx_t *f = gs_fx_get(handle);
    if (f) f->len = len, f->loop_end = len;
}

int gs_fx_count(void) { return fx_count; }

static int chan_free(int c) {
    gs_voice_t *v = &gs_voice[FX_VOICE + c];
    return !v->data || !v->on;
}

static int seek_free(uint8_t mask) {
    for (int c = 0; c < GS_FX_CHANS; c++)
        if ((mask >> c & 1) && chan_free(c)) return c;
    return -1;
}

void gs_fx_play(int handle) {
    gs_fx_t *f = gs_fx_get(handle);
    if (!f) return;
    int c = seek_free(f->seek_first);
    if (c < 0) c = seek_free(f->seek_last);
    if (c < 0) {                                    // все заняты: вытесняем самый младший, если он младше нашего
        int low = 0;
        for (int i = 1; i < GS_FX_CHANS; i++) if (chan_prio[i] < chan_prio[low]) low = i;
        if (chan_prio[low] >= f->prio) return;
        c = low;
    }
    gs_fx_play_chan(handle, c, -1, -1);
}

static void voice_vol(int c) { gs_voice[FX_VOICE + c].vol = (uint8_t)(chan_vol[c] * master / 64); }

void gs_fx_play_chan(int handle, int chan, int note, int vol) {
    gs_fx_t *f = gs_fx_get(handle);
    if (!f || chan < 0 || chan >= GS_FX_CHANS) return;
    gs_voice_t *v = &gs_voice[FX_VOICE + chan];
    int fine = f->finetune < 8 ? f->finetune : (int)f->finetune - 16;   // как finetune MOD: 1/8 полутона
    double semi = (note < 0 ? f->note : note) - NOTE_C2 + fine / 8.0;
    v->data = 0;
    v->len = f->len;
    v->loop_start = f->loop_begin;
    v->loop_len = f->loop_begin >> 16 != 0xFF && f->loop_begin < f->loop_end && f->loop_end <= f->len ? f->loop_end - f->loop_begin : 0;
    gs_voice_set_inc(v, (float)(CCK_HZ / (428.0 * pow(2.0, -semi / 12.0) * gs_cfg_mix_hz())));
    gs_voice_set_pos(v, 0.0f);
    chan_prio[chan] = f->prio;
    chan_vol[chan] = (uint8_t)(vol < 0 ? f->vol : vol > 64 ? 64 : vol);
    voice_vol(chan);
    v->data = f->data;                              // последним: голос читает другое ядро
}

void gs_fx_set_master(uint8_t vol) {
    master = vol;
    for (int c = 0; c < GS_FX_CHANS; c++) voice_vol(c);
}

int gs_fx_active(void) {
    for (int c = 0; c < GS_FX_CHANS; c++) if (!chan_free(c)) return 1;
    return 0;
}
