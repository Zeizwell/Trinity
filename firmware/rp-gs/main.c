// Прошивка RP-GS: запуск и рабочий цикл ядра 0. Ядро 1 - звук (audio.c), шина - прерывание (bus.c), устройства - в коре
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "hardware/vreg.h"
#include "config.h"
#include "cfgstore.h"
#include "psram.h"
#include "audio.h"
#include "bus.h"
#include "bench.h"
#include "gs/gs_core.h"
#include "gs/gs_cfg.h"
#include "gs/gs_dev_gs.h"
#include "gs/gs_ctl.h"

#if BOARD_HAS_SD
#include "ff.h"
#include "sdport.h"
static FATFS sd_fs;
#endif

static spin_lock_t *engine_lock;
static void lock_on(void) { spin_lock_unsafe_blocking(engine_lock); }
static void lock_off(void) { spin_unlock_unsafe(engine_lock); }

int main(void) {
    gs_cfg_defaults();
    cfgstore_load();                                // сохранённое поверх умолчаний
    vreg_set_voltage(VREG_VOLTAGE_1_15);            // под 250 МГц, как RP-AY
    sleep_ms(2);
    set_sys_clock_khz(gs_cfg.sys_khz, true);
    stdio_init_all();
    engine_lock = spin_lock_init(spin_lock_claim_unused(true));
    gs_core_set_lock(lock_on, lock_off);
    psram_init();
    gs_gs_set_ram(PSRAM_BASE, PSRAM_SIZE);
    gs_ctl_init(cfgstore_save, FW_VERSION);
    gs_core_init();
#if BOARD_HAS_SD
    sdport_init();
    if (sdport_present()) f_mount(&sd_fs, "0:", 1);     // нет карты - работаем без неё (п.42)
#endif
    audio_start();
    bus_start();
    for (;;) {                                      // спит, будят шина и USB
        gs_core_work();
        bus_answer();
        bench_key();
        __wfe();
    }
}
