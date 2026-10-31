#include "gs_mod.h"
#include "gs_player.h"
#include "gs_chan.h"
#include "gs_filter.h"
#include <math.h>

#define MOD_VOICES 4    // дальше - голоса эффектов

static const gs_player_t *const players[] = { &gs_player_pt, &gs_player_pocket };

static const gs_player_t *player;   // NULL - ничего не загружено
static int playing;
static int vol_master = 64, vol_module = 64;
static const int8_t *paused[MOD_VOICES];

int gs_mod_gain(int vol) { return vol * vol_module / 64 * vol_master / 64; }

int gs_mod_play(const void *data, int size, uint8_t pos, gs_mod_mem_t mem) {
    playing = 0;
    player = 0;
    gs_chan_clear(0, MOD_VOICES);
    gs_filter_led_set(0);
    for (unsigned i = 0; i < sizeof players / sizeof players[0]; i++) {
        if (!players[i]->accepts(data, size)) continue;
        if (!players[i]->load(data, size, pos, mem == GS_MOD_RAM)) return 0;
        player = players[i];
        playing = 1;
        return 1;
    }
    return 0;
}

void gs_mod_stop(void) {
    playing = 0;
    gs_chan_clear(0, MOD_VOICES);
}

void gs_mod_pause(void) {
    playing = 0;
    for (int i = 0; i < MOD_VOICES; i++) {
        paused[i] = gs_voice[i].data;
        gs_voice[i].data = 0;                   // позицию держим, данные вернёт gs_mod_resume
    }
}

int gs_mod_resume(void) {
    if (!player) return 0;
    for (int i = 0; i < MOD_VOICES; i++) gs_voice[i].data = paused[i];
    playing = 1;
    return 1;
}

int gs_mod_playing(void) { return playing; }

void gs_mod_audio(float *out, int n) {
    if (player) player->rate_sync();
    if (!playing) { gs_chan_mix(out, n); return; }  // эффекты звучат и без модуля
    if (!player->audio(out, n)) playing = 0;
}

void gs_mod_set_vol(uint8_t master, uint8_t module) { vol_master = master, vol_module = module; }

void gs_mod_set_speed(uint8_t v) {
    if (player && v) player->set_speed(v);
}

uint8_t gs_mod_song_pos(void) { return player ? player->song_pos() : 0; }
uint8_t gs_mod_pat_pos(void) { return player ? player->pat_pos() : 0; }
uint8_t gs_mod_speed(void) { return player ? player->speed() : 0; }
uint8_t gs_mod_tempo(void) { return player ? player->tempo() : 0; }

uint8_t gs_mod_note(int chan) {
    if (!playing) return 127;
    int period = player->period(chan);
    if (!period || !gs_voice[chan].on) return 127;
    int n = 48 + (int)lround(12.0 * log2(428.0 / period));
    return (uint8_t)(n < 0 ? 0 : n > 95 ? 95 : n);
}

uint8_t gs_mod_vol(int chan) { return playing ? player->vol(chan) : 0; }
