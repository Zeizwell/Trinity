#include <string.h>
#include "gs_core.h"
#include "gs_cfg.h"

extern const gs_dev_t gs_dev_gs;

// Устройства прошивки: новое - строка здесь
static const gs_dev_t *const dev_list[] = { &gs_dev_gs };
#define DEV_N ((int)(sizeof dev_list / sizeof dev_list[0]))

static const gs_dev_t *active;

void gs_core_init(void) {
    gs_cfg_defaults();
    active = 0;
    for (int i = 0; i < DEV_N; i++) dev_list[i]->reset();
}

void gs_core_claim(const gs_dev_t *dev) {
    if (active && active != dev) active->sound_stop();
    active = dev;
}

const gs_dev_t *gs_core_active(void) { return active; }

void gs_core_audio(float *out, int n) {
    memset(out, 0, (size_t)n * 2 * sizeof(float));
    if (active && active->enabled()) active->audio(out, n);
    float k = gs_cfg.master_pct * 0.01f;
    for (int i = 0; i < n * 2; i++) {
        float x = out[i] * k;
        out[i] = x > 1.0f ? 1.0f : x < -1.0f ? -1.0f : x;
    }
}
