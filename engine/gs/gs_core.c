#include <string.h>
#include "gs_core.h"
#include "gs_cfg.h"
#include "gs_filter.h"
#include "gs_down.h"
#include "gs_chan.h"
#include "gs_dev_gs.h"
#include "gs_dev_opl4.h"

// Устройства прошивки: новое - строка здесь
static const gs_dev_t *const dev_list[] = { &gs_dev_gs, &gs_dev_opl4 };
#define DEV_N ((int)(sizeof dev_list / sizeof dev_list[0]))

// Код порта -> устройство и его порт. Индекс - GS_CODE_*
static const struct { const gs_dev_t *dev; uint8_t port; } route[GS_CODE_COUNT] = {
    { &gs_dev_gs, GS_PORT_B3 }, { &gs_dev_gs, GS_PORT_BB },
    { &gs_dev_opl4, OPL4_PORT_C4 }, { &gs_dev_opl4, OPL4_PORT_C5 }, { &gs_dev_opl4, OPL4_PORT_C6 }, { &gs_dev_opl4, OPL4_PORT_C7 },
};

void gs_core_port_write(uint8_t code, uint8_t v) { if (code < GS_CODE_COUNT) route[code].dev->port_write(route[code].port, v); }
uint8_t gs_core_port_read(uint8_t code) { return code < GS_CODE_COUNT ? route[code].dev->port_read(route[code].port) : 0xFF; }
uint8_t gs_core_port_peek(uint8_t code) { return code < GS_CODE_COUNT ? route[code].dev->port_peek(route[code].port) : 0xFF; }

static const gs_dev_t *active;
static uint32_t rate_now;
static void (*lock_fn)(void), (*unlock_fn)(void);

void gs_core_set_lock(void (*lock)(void), void (*unlock)(void)) { lock_fn = lock, unlock_fn = unlock; }
void gs_core_set_mode(int mode) {
    gs_core_lock();
    gs_cfg_mode((gs_mode_t)mode);
    gs_core_unlock();
}

void gs_core_lock(void) { if (lock_fn) lock_fn(); }
void gs_core_unlock(void) { if (unlock_fn) unlock_fn(); }

void gs_core_init(void) {
    active = 0;
    rate_now = gs_cfg_mix_hz();
    gs_filter_init((float)rate_now);
    gs_down_reset();
    for (int i = 0; i < DEV_N; i++) dev_list[i]->reset();
}

void gs_core_claim(const gs_dev_t *dev) {
    if (active && active != dev) active->sound_stop();
    active = dev;
}

const gs_dev_t *gs_core_active(void) { return active; }

void gs_core_work(void) {
    for (int i = 0; i < DEV_N; i++) dev_list[i]->work();
}

// Кусок на частоте смешивания: звук устройства и фильтры
static void render(float *buf, int n) {
    memset(buf, 0, (size_t)n * 2 * sizeof(float));
    if (active && active->enabled()) active->audio(buf, n);
    gs_filter_run(buf, n);
}

#define CHUNK 512
void gs_core_audio(float *out, int n) {
    static float mix[CHUNK * 2 * 2];
    if (gs_cfg_mix_hz() != rate_now) {              // режим сменил частоту ЦАП
        gs_chan_rate(rate_now, gs_cfg_mix_hz());
        rate_now = gs_cfg_mix_hz();
        gs_filter_init((float)rate_now);
    }
    float *p = out;
    for (int left = n; left > 0; ) {
        int k = left < CHUNK ? left : CHUNK;
        if (gs_cfg.oversample == 2) {
            render(mix, k * 2);
            gs_down_run(mix, p, k);
        } else render(p, k);
        p += k * 2;
        left -= k;
    }
    gs_comp_run(out, n, (float)gs_cfg.rate_hz);
    float g = gs_cfg.master_pct * 0.01f;
    for (int i = 0; i < n * 2; i++) {
        float x = out[i] * g;
        out[i] = x > 1.0f ? 1.0f : x < -1.0f ? -1.0f : x;
    }
}
