#include <math.h>
#include <string.h>
#include "gs_cfg.h"

#ifndef GS_RATE_GS
#define GS_RATE_GS 75000     // ЦАП режима GS: 2 x 37500, как NeoGS (24 МГц / 5 / 64). Тик ровно на два отсчёта, без пересчёта
#endif
#define RATE_PAULA 48000

gs_cfg_t gs_cfg;

void gs_cfg_defaults(void) {
    gs_cfg.sys_khz = 250000;                    // как RP-AY: ответ на чтение сразу за записью на 14 МГц (бюджет ~0.85 мкс)
    gs_cfg.gs_ram_kb = 8192;
    gs_cfg.gs_on = 1;
    gs_cfg.hold_hz = 37500;
    gs_cfg.oversample = 1;
    gs_cfg.voices = 8;                          // 4 модуль, 4 эффекты
    gs_cfg.master_pct = 80;                     // запас: выбросы BLEP на громких модулях шли за 16 бит
    gs_cfg_mode(GS_MODE_A1200);
}

uint32_t gs_cfg_mix_hz(void) { return gs_cfg.rate_hz * gs_cfg.oversample; }

// Числа со схем, все ступени выключены: включает пресет
#define GS_PI 3.14159265358979
#define RC_HZ(r, c) (1.0 / (2.0 * GS_PI * (r) * (c)))
static void stages_schematic(void) {
    double r1 = 1e4, r2 = 1e4, c1 = 6.8e-9, c2 = 3.9e-9;        // LED, Саллен-Ки: одинаков на A500 и A1200
    double sq = sqrt(r1 * r2 * c1 * c2);
    gs_stage_t *st = gs_cfg.st;
    memset(st, 0, sizeof gs_cfg.st);
    st[GS_ST_RC].hz = RC_HZ(1000.0, 1e-8);                      // ~15.9 кГц: R45/C32, R50/C47 MultiSound
    st[GS_ST_LP].hz = RC_HZ(360.0, 1e-7);                       // ~4421 Гц: A500
    st[GS_ST_LED].hz = 1.0 / (2.0 * GS_PI * sq);                // ~3091 Гц
    st[GS_ST_LED].q = sq / (c2 * (r1 + r2));                    // ~0.66
    st[GS_ST_HP].hz = RC_HZ(1360.0, 2.2e-5);                    // ~5.3 Гц: A1200
}

// Пересчёт руками: уже не канон. Частота вывода - не настройка, следует за пересчётом: ROM GS - свой ЦАП, остальные 48 кГц
void gs_cfg_set_resample(gs_resample_t r) {
    gs_cfg.resample = r;
    gs_cfg.rate_hz = r == GS_RS_ROM ? GS_RATE_GS : RATE_PAULA;
    gs_cfg.mode = GS_MODE_CUSTOM;
}

// Режим - только значения переменных
void gs_cfg_mode(gs_mode_t mode) {
    gs_cfg.mode = mode;
    if (mode == GS_MODE_CUSTOM) return;         // переменные - как есть
    stages_schematic();
    gs_cfg.color_bass_db = gs_cfg.color_treble_db = 0, gs_cfg.comp = 0;
    switch (mode) {
    case GS_MODE_GS:                            // оригинальный GS: генератор ROM на 37500 Гц прямо в ЦАП, RC после ЦАП
        gs_cfg.resample = GS_RS_ROM, gs_cfg.hold_hz = 37500, gs_cfg.ramp = 0, gs_cfg.stereo_pct = 100;
        gs_cfg.led = GS_LED_OFF, gs_cfg.st[GS_ST_RC].on = 1;
        break;
    case GS_MODE_A500:
        gs_cfg.resample = GS_RS_BLEP, gs_cfg.pwm = 0, gs_cfg.ramp = 0, gs_cfg.stereo_pct = 100;
        gs_cfg.led = GS_LED_MODULE, gs_cfg.st[GS_ST_LP].on = 1, gs_cfg.st[GS_ST_HP].on = 1;
        gs_cfg.st[GS_ST_HP].hz = RC_HZ(1390.0, 2.233e-5);    // ~5.1 Гц
        break;
    case GS_MODE_A1200:
        gs_cfg.resample = GS_RS_BLEP, gs_cfg.pwm = 0, gs_cfg.ramp = 0, gs_cfg.stereo_pct = 100;
        gs_cfg.led = GS_LED_MODULE, gs_cfg.st[GS_ST_HP].on = 1;     // ФНЧ A1200 ~34 кГц: не слышно, не считаем (как pt2-clone)
        break;
    case GS_MODE_HIFI:
        gs_cfg.resample = GS_RS_CUBIC, gs_cfg.ramp = 1, gs_cfg.stereo_pct = 70;
        gs_cfg.led = GS_LED_OFF, gs_cfg.st[GS_ST_HP].on = 1;
        gs_cfg.color_bass_db = 4, gs_cfg.color_treble_db = 5, gs_cfg.comp = 1;
        break;
    default:
        break;
    }
    gs_cfg_set_resample(gs_cfg.resample);
    gs_cfg.mode = mode;
}

const char *gs_cfg_mode_name(gs_mode_t mode) {
    static const char *const name[] = { "GS", "Paula A500", "Paula A1200", "HiFi", "Custom" };
    return name[mode];
}
