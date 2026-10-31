#include <math.h>
#include <string.h>
#include "gs_ctl.h"
#include "gs_cfg.h"
#include "gs_core.h"
#include "gs_filter.h"
#include "gs_stat.h"

typedef enum { T_U8, T_I8, T_U32, T_HZ, T_Q, T_RS, T_LED, T_MODE } type_t;
enum { F_KNOB = 1, F_RO = 2 };      // F_KNOB - общая ручка, режим остаётся. Остальное - уже не канон, режим становится Custom. F_RO - только чтение

// Настройка: тег, где лежит, как читать, пределы. Новая настройка - строка здесь
typedef struct {
    char tag[4];
    void *p;
    uint8_t type, flags;
    int32_t min, max;
} item_t;

static const item_t items[] = {
    { "MODE", &gs_cfg.mode, T_MODE, F_RO, 0, 4 },
    { "RATE", &gs_cfg.rate_hz, T_U32, F_RO, 0, 0 },
    { "SYSK", &gs_cfg.sys_khz, T_U32, F_RO, 0, 0 },
    { "OVER", &gs_cfg.oversample, T_U8, F_RO, 0, 0 },
    { "MAST", &gs_cfg.master_pct, T_U8, F_KNOB, 0, 100 },
    { "BASS", &gs_cfg.bass_db, T_I8, F_KNOB, -GS_TONE_DB_MAX, GS_TONE_DB_MAX },
    { "TREB", &gs_cfg.treble_db, T_I8, F_KNOB, -GS_TONE_DB_MAX, GS_TONE_DB_MAX },
    { "RAMK", &gs_cfg.gs_ram_kb, T_U32, F_KNOB, 128, 8192 },
    { "GSON", &gs_cfg.gs_on, T_U8, F_KNOB, 0, 1 },
    { "VOIC", &gs_cfg.voices, T_U8, 0, 4, 32 },
    { "RSMP", &gs_cfg.resample, T_RS, 0, 0, 4 },
    { "HOLD", &gs_cfg.hold_hz, T_U32, 0, 8000, 75000 },
    { "PWM ", &gs_cfg.pwm, T_U8, 0, 0, 1 },
    { "RAMP", &gs_cfg.ramp, T_U8, 0, 0, 1 },
    { "STER", &gs_cfg.stereo_pct, T_U8, 0, 0, 100 },
    { "LED ", &gs_cfg.led, T_LED, 0, 0, 2 },
    { "COMP", &gs_cfg.comp, T_U8, 0, 0, 1 },
    { "CBAS", &gs_cfg.color_bass_db, T_I8, 0, -GS_TONE_DB_MAX, GS_TONE_DB_MAX },
    { "CTRE", &gs_cfg.color_treble_db, T_I8, 0, -GS_TONE_DB_MAX, GS_TONE_DB_MAX },
    { "RCHZ", &gs_cfg.st[GS_ST_RC].hz, T_HZ, 0, 100, 2400000 },
    { "RCON", &gs_cfg.st[GS_ST_RC].on, T_U8, 0, 0, 1 },
    { "LPHZ", &gs_cfg.st[GS_ST_LP].hz, T_HZ, 0, 100, 2400000 },
    { "LPON", &gs_cfg.st[GS_ST_LP].on, T_U8, 0, 0, 1 },
    { "LDHZ", &gs_cfg.st[GS_ST_LED].hz, T_HZ, 0, 100, 2400000 },
    { "LDQ ", &gs_cfg.st[GS_ST_LED].q, T_Q, 0, 100, 10000 },
    { "HPHZ", &gs_cfg.st[GS_ST_HP].hz, T_HZ, 0, 100, 2400000 },
    { "HPON", &gs_cfg.st[GS_ST_HP].on, T_U8, 0, 0, 1 },
};
#define ITEM_N ((int)(sizeof items / sizeof items[0]))

static int (*save_fn)(void);
static uint16_t fw_ver;

void gs_ctl_init(int (*save)(void), uint16_t fw_version) { save_fn = save, fw_ver = fw_version; }

static const item_t *item_find(const uint8_t *tag) {
    for (int i = 0; i < ITEM_N; i++)
        if (!memcmp(items[i].tag, tag, 4)) return &items[i];
    return 0;
}

static int32_t item_get(const item_t *it) {
    switch (it->type) {
    case T_U8: return *(uint8_t *)it->p;
    case T_I8: return *(int8_t *)it->p;
    case T_U32: return (int32_t)*(uint32_t *)it->p;
    case T_HZ: return (int32_t)(*(double *)it->p * 100.0 + 0.5);    // 0.01 Гц
    case T_Q: return (int32_t)(*(double *)it->p * 1000.0 + 0.5);    // 0.001
    case T_RS: return *(gs_resample_t *)it->p;
    case T_LED: return *(gs_led_t *)it->p;
    default: return *(gs_mode_t *)it->p;
    }
}

static void item_set(const item_t *it, int32_t v) {
    switch (it->type) {
    case T_U8: *(uint8_t *)it->p = (uint8_t)v; break;
    case T_I8: *(int8_t *)it->p = (int8_t)v; break;
    case T_U32: *(uint32_t *)it->p = (uint32_t)v; break;
    case T_HZ: *(double *)it->p = v / 100.0; break;
    case T_Q: *(double *)it->p = v / 1000.0; break;
    case T_RS: gs_cfg_set_resample((gs_resample_t)v); break;
    case T_LED: *(gs_led_t *)it->p = (gs_led_t)v; break;
    default: break;
    }
    if (!(it->flags & F_KNOB)) gs_cfg.mode = GS_MODE_CUSTOM;
}

static void put32(uint8_t *o, uint32_t v) { o[0] = (uint8_t)v, o[1] = (uint8_t)(v >> 8), o[2] = (uint8_t)(v >> 16), o[3] = (uint8_t)(v >> 24); }
static void put16(uint8_t *o, uint32_t v) { o[0] = (uint8_t)v, o[1] = (uint8_t)(v >> 8); }
static uint32_t get32(const uint8_t *i) { return (uint32_t)i[0] | (uint32_t)i[1] << 8 | (uint32_t)i[2] << 16 | (uint32_t)i[3] << 24; }

static int answer(uint8_t *out, uint8_t v) { out[0] = v; return 1; }

static int cfg_get(const uint8_t *in, int len, uint8_t *out) {
    const item_t *it = len == 4 ? item_find(in) : 0;
    if (!it) return answer(out, GS_CTL_ERR);
    gs_core_lock();
    int32_t v = item_get(it);
    gs_core_unlock();
    put32(out, (uint32_t)v);
    return 4;
}

static int cfg_set(const uint8_t *in, int len, uint8_t *out) {
    const item_t *it = len == 8 ? item_find(in) : 0;
    if (!it || (it->flags & F_RO)) return answer(out, GS_CTL_ERR);
    int32_t v = (int32_t)get32(in + 4);
    if (v < it->min || v > it->max) return answer(out, GS_CTL_ERR);
    gs_core_lock();
    item_set(it, v);
    gs_core_unlock();
    gs_stat_reset();
    return answer(out, GS_CTL_OK);
}

static int preset(const uint8_t *in, int len, uint8_t *out) {
    if (len != 1 || in[0] > GS_MODE_HIFI) return answer(out, GS_CTL_ERR);
    gs_core_set_mode(in[0]);
    gs_stat_reset();
    return answer(out, GS_CTL_OK);
}

static int stat(uint8_t *out) {
    const gs_dev_t *d = gs_core_active();
    const char *name = d ? d->name : "";
    put16(out + GS_CTL_ST_LOAD, gs_stat.load_x10), put16(out + GS_CTL_ST_TOP, gs_stat.top_x10);
    put32(out + GS_CTL_ST_LATE, gs_stat.late), put32(out + GS_CTL_ST_BLOCKS, gs_stat.blocks);
    put32(out + GS_CTL_ST_RATE, gs_cfg.rate_hz), put32(out + GS_CTL_ST_SYSK, gs_cfg.sys_khz);
    out[GS_CTL_ST_MODE] = (uint8_t)gs_cfg.mode;
    for (int i = 0, end = 0; i < 4; i++) {              // имя устройства, 4 знака, дополнено пробелами
        if (!name[i]) end = 1;
        out[GS_CTL_ST_DEV + i] = end ? ' ' : (uint8_t)name[i];
    }
    return GS_CTL_ST_SIZE;
}

// Каждая точка под своим замком: звук ждёт не дольше одной точки
static int curve(const uint8_t *in, int len, uint8_t *out) {
    if (len != 1 || (in[0] != GS_CTL_CURVE_ALL && in[0] >= GS_CURVE_COUNT)) return answer(out, GS_CTL_ERR);
    for (int i = 0; i < GS_CTL_POINTS; i++) {
        float hz = 20.0f * powf(1000.0f, (float)i / (GS_CTL_POINTS - 1));
        gs_core_lock();
        float db = in[0] == GS_CTL_CURVE_ALL ? gs_filter_db(hz) : gs_filter_stage_db(in[0], hz);
        gs_core_unlock();
        if (db < -320.0f) db = -320.0f;
        if (db > 320.0f) db = 320.0f;
        put16(out + i * 2, (uint16_t)(int16_t)lroundf(db * 100.0f));
    }
    return GS_CTL_POINTS * 2;
}

int gs_ctl_packet(uint8_t code, const uint8_t *in, int len, uint8_t *out) {
    switch (code) {
    case GS_CTL_CFG_GET: return cfg_get(in, len, out);
    case GS_CTL_CFG_SET: return cfg_set(in, len, out);
    case GS_CTL_CFG_SAVE: return answer(out, save_fn && save_fn() ? GS_CTL_OK : GS_CTL_ERR);
    case GS_CTL_PRESET: return preset(in, len, out);
    case GS_CTL_STAT: return stat(out);
    case GS_CTL_STAT_RESET: gs_stat_reset(); return answer(out, GS_CTL_OK);
    case GS_CTL_CURVE: return curve(in, len, out);
    case GS_CTL_VERSION:
        out[0] = GS_CTL_PROTO;
        put16(out + 1, fw_ver);
        return 3;
    default: return answer(out, GS_CTL_ERR);
    }
}
