#include "gs_core.h"
#include "gs_cfg.h"
#include "gs_mod.h"
#include "gs_dev_gs.h"

const gs_dev_t gs_dev_gs;

static int  dev_enabled(void) { return gs_cfg.gs_on; }
static void dev_reset(void) { gs_mod_stop(); }
static void dev_sound_stop(void) { gs_mod_stop(); }
static void dev_port_write(uint8_t code, uint8_t v) { (void)code, (void)v; }    // команды GS - следующий шаг
static uint8_t dev_port_read(uint8_t code) { (void)code; return 0xFF; }
static void dev_work(void) { }
static void dev_audio(float *out, int n) { gs_mod_audio(out, n); }

const gs_dev_t gs_dev_gs = {
    "GS", dev_enabled, dev_reset, dev_sound_stop, dev_port_write, dev_port_read, dev_work, dev_audio
};

int gs_gs_play_mod(const void *data, int size) {
    if (!gs_mod_load(data, size)) return 0;
    gs_core_claim(&gs_dev_gs);
    return 1;
}

void gs_gs_stop(void) { gs_mod_stop(); }
