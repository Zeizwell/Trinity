#include "gs_cfg.h"

gs_cfg_t gs_cfg;

void gs_cfg_defaults(void) {
    gs_cfg.gs_on = 1;
    gs_cfg.rate_hz = 48000;
    gs_cfg.oversample = 2;
    gs_cfg.voices = 4;
    gs_cfg.master_pct = 100;
    gs_cfg_mode(GS_MODE_A1200);
}

uint32_t gs_cfg_mix_hz(void) { return gs_cfg.rate_hz * gs_cfg.oversample; }

// Режим - только значения переменных
void gs_cfg_mode(gs_mode_t mode) {
    gs_cfg.mode = mode;
    gs_cfg.rc16k = 0;
    switch (mode) {
    case GS_MODE_GS:                            // оригинальный GS: генератор ROM на 37500 Гц, RC после ЦАП
        gs_cfg.resample = GS_RS_ROM, gs_cfg.hold_hz = 37500, gs_cfg.ramp = 0, gs_cfg.stereo_pct = 100;
        gs_cfg.lp = GS_LP_OFF, gs_cfg.led = GS_LED_OFF, gs_cfg.hp = 0, gs_cfg.rc16k = 1;
        break;
    case GS_MODE_A500:
        gs_cfg.resample = GS_RS_BLEP, gs_cfg.pwm = 0, gs_cfg.ramp = 0, gs_cfg.stereo_pct = 100;
        gs_cfg.lp = GS_LP_A500, gs_cfg.led = GS_LED_MODULE, gs_cfg.hp = 1;
        break;
    case GS_MODE_A1200:
        gs_cfg.resample = GS_RS_BLEP, gs_cfg.pwm = 0, gs_cfg.ramp = 0, gs_cfg.stereo_pct = 100;
        gs_cfg.lp = GS_LP_A1200, gs_cfg.led = GS_LED_MODULE, gs_cfg.hp = 1;
        break;
    case GS_MODE_HIFI:
        gs_cfg.resample = GS_RS_CUBIC, gs_cfg.ramp = 1, gs_cfg.stereo_pct = 70;
        gs_cfg.lp = GS_LP_OFF, gs_cfg.led = GS_LED_OFF, gs_cfg.hp = 1;
        break;
    case GS_MODE_CUSTOM:                        // переменные - как есть
        break;
    }
}

const char *gs_cfg_mode_name(gs_mode_t mode) {
    static const char *const name[] = { "GS", "Paula A500", "Paula A1200", "HiFi", "Custom" };
    return name[mode];
}
