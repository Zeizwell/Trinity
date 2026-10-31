// Плагин ProTracker: регистры gs_pt -> наши голоса, тики по таймеру CIA
#include "gs_player.h"
#include "gs_pt.h"
#include "gs_chan.h"
#include "gs_cfg.h"
#include "gs_filter.h"
#include "gs_amiga.h"

#define VOICES 4

static double acc;              // отсчётов с начала тика
static int rate;                // частота, по которой посчитаны шаги голосов
static int8_t null_smp[4];      // пустой блок: AUDxLC = NULL или длина 0

// Период Паулы -> шаг на отсчёт смешивания. 0 - это 65536, меньше 113 не бывает (как в pt2-clone)
static float period_inc(int period) {
    if (!period) period = 65536;
    else if (period < 113) period = 113;
    return (float)GS_PAULA_HZ / ((float)period * (float)rate);
}

static const int8_t *block_of(const gs_pt_reg_t *r, uint32_t *len) {
    if (!r->lc || !r->len) { *len = sizeof null_smp; return null_smp; }
    *len = (uint32_t)r->len * 2;
    return r->lc;
}

void gs_pt_dma_start(int chan) {
    uint32_t len;
    const int8_t *d = block_of(&gs_pt_reg[chan], &len);
    gs_voice_start(&gs_voice[chan], d, len, period_inc(gs_pt_reg[chan].period));
}

void gs_pt_led_set(int on) { gs_filter_led_set(on); }

// Регистры -> голос: громкость, период (при BLEP вступит на смене отсчёта), следующий блок
static void regs_to_voices(void) {
    for (int i = 0; i < VOICES; i++) {
        gs_voice_t *v = &gs_voice[i];
        const gs_pt_reg_t *r = &gs_pt_reg[i];
        v->vol = (uint8_t)gs_mod_gain(r->vol);
        gs_voice_period(v, period_inc(r->period));
        v->next_data = block_of(r, &v->next_len);
    }
}

static void tick(void) {
    gs_pt_tick();
    regs_to_voices();
}

static int load(const void *data, int size, uint8_t pos, int writable) {
    if (!gs_pt_load(data, size, writable)) return 0;
    gs_pt_set_pos(pos);
    rate = (int)gs_cfg_mix_hz();
    acc = 0.0;
    tick();
    return 1;
}

static void rate_sync(void) {
    int to = (int)gs_cfg_mix_hz();
    if (to == rate) return;
    acc *= (double)to / rate;
    rate = to;
    regs_to_voices();
}

static int audio(float *out, int n) {
    while (n > 0) {
        double per_tick = gs_pt_tick_sec() * rate;
        int num = (int)(per_tick - acc);                // до конца тика
        if (num < 1) num = 1;
        if (num > n) num = n;
        gs_chan_mix(out, num);
        out += num * 2;
        n -= num;
        acc += num;
        if (acc >= per_tick) {
            acc -= per_tick;
            tick();
            if (gs_pt_stopped()) {                      // F00: песня стоит, голоса доигрывают
                gs_chan_mix(out, n);
                return 0;
            }
        }
    }
    return 1;
}

static void set_speed(uint8_t v) {
    if (v < 0x20) gs_pt_set_speed(v);
    else gs_pt_set_bpm(v);
}

static uint8_t song_pos(void) { return (uint8_t)gs_pt_play_pos(); }
static uint8_t pat_pos(void) { return (uint8_t)gs_pt_play_row(); }
static uint8_t speed(void) { return (uint8_t)gs_pt_speed(); }
static uint8_t tempo(void) { return (uint8_t)gs_pt_bpm(); }
static int period(int chan) { return chan < VOICES ? gs_pt_period(chan) : 0; }
static uint8_t vol(int chan) { return chan < VOICES ? (uint8_t)gs_pt_reg[chan].vol : 0; }

const gs_player_t gs_player_pt = {
    gs_pt_is_mod, load, rate_sync, audio, set_speed, song_pos, pat_pos, speed, tempo, period, vol
};
