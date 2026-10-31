#include <string.h>
#include "gs_core.h"
#include "gs_cfg.h"
#include "gs_filter.h"
#include "gs_down.h"

extern const gs_dev_t gs_dev_gs;

// Устройства прошивки: новое - строка здесь
static const gs_dev_t *const dev_list[] = { &gs_dev_gs };
#define DEV_N ((int)(sizeof dev_list / sizeof dev_list[0]))

static const gs_dev_t *active;

void gs_core_init(void) {
    gs_cfg_defaults();
    active = 0;
    gs_filter_init((float)gs_cfg_mix_hz());
    gs_down_reset();
    for (int i = 0; i < DEV_N; i++) dev_list[i]->reset();
}

void gs_core_claim(const gs_dev_t *dev) {
    if (active && active != dev) active->sound_stop();
    active = dev;
}

const gs_dev_t *gs_core_active(void) { return active; }

// Кусок на частоте смешивания: звук устройства и фильтры
static void render(float *buf, int n) {
    memset(buf, 0, (size_t)n * 2 * sizeof(float));
    if (active && active->enabled()) active->audio(buf, n);
    gs_filter_run(buf, n);
}

#define CHUNK 512
void gs_core_audio(float *out, int n) {
    static float mix[CHUNK * 2 * 2];
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
