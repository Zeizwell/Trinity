// Заводские настройки RP-AY (config.h). Менять числа - только здесь.
#include "config.h"

cfg_t cfg;

void cfg_defaults(void) {
    cfg.phim_hz          = 3500000;     // макет: такт YM2203 из слота MultiSound (уточнить замером φM)
    cfg.wr_pulse_ns      = 200;
    cfg.setup_ns         = 50;
    cfg.wait_addr_clk    = 17;          // YM2203: адрес -> данные
    cfg.wait_ssg_clk     = 17;          // YM2203 SSG: по даташиту 0, берём как адрес - с запасом
    cfg.wait_fm_clk      = 83;          // YM2203 FM
    cfg.status_answer    = 0x00;        // никогда не занят: очередь в RP сама держит паузы чипа
    cfg.mode_after_reset = MODE_AY;
    cfg.silence_on_reset = true;
    cfg.fx_timeout_s     = 5;
    cfg.chips            = 2;
    cfg.wr_sample_delay  = 8;
    cfg.led_bright       = 16;
    cfg.log_period_ms    = 1000;
}
