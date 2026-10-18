// Режимы и переходы (modes.h). Таблица allow[из][в]: 1 - можно.
#include "modes.h"

volatile mode_t mode = MODE_AY;

const char *const mode_name[MODE_COUNT] = { "AY", "FX", "ATARI", "UPDATE", "TEST" };

// из \ в                      AY  FX  ATARI UPDATE TEST
static const uint8_t allow[MODE_COUNT][MODE_COUNT] = {
    /* AY     */              { 0,  1,  1,    1,     1 },
    /* FX     */              { 1,  0,  0,    0,     0 },   // в ATARI - только через AY (сначала выключить FX)
    /* ATARI  */              { 1,  0,  0,    0,     0 },   // только STOP -> AY
    /* UPDATE */              { 1,  0,  0,    0,     0 },
    /* TEST   */              { 1,  0,  0,    0,     0 },
};

// Идут ли записи Спектрума в чип: в AY и FX - да; в ATARI / UPDATE / TEST - чип у RP
static const bool slot_to_chip[MODE_COUNT] = { true, true, false, false, false };

mode_res_t mode_set(mode_t to) {
    if (to >= MODE_COUNT) return MODE_DENIED;
    if (to == mode) return MODE_SAME;
    if (!allow[mode][to]) return MODE_DENIED;
    mode = to;
    return MODE_OK;
}

void mode_reset(void) {
    mode = cfg.mode_after_reset;
}

bool mode_chip_from_slot(void) {
    return slot_to_chip[mode];
}
