// Заводские настройки RP-AY (config.h). Менять числа - только здесь.
#include "config.h"
#include "hardware/vreg.h"

cfg_t cfg;

void cfg_defaults(void) {
    cfg.z80_hz           = 3500000;     // ритм записи - как Z80 3.5 МГц
    cfg.setup_ht         = 2;           // 1 такт   = 286 нс
    cfg.pulse_ht         = 5;           // 2.5 такта = 714 нс
    cfg.hold_ht          = 1;           // 0.5 такта = 143 нс: чип защёлкивает по подъёму /WR при /CS внизу
    cfg.gap_addr_ht      = 22;          // 11 тактов = 3.1 мкс
    cfg.gap_ssg_ht       = 22;          // 11 тактов = 3.1 мкс
    cfg.phim_hz          = 3500000;     // макет: такт YM2203 из слота MultiSound (уточнить замером φM)
    cfg.wait_fm_clk      = 83;          // YM2203 FM
    cfg.rd_pulse_ns      = 500;         // тест чтения: с запасом
    cfg.status_answer    = 0x00;        // никогда не занят: очередь в RP сама держит паузы чипа
    cfg.mode_after_reset = MODE_AY;
    cfg.silence_on_reset = true;
    cfg.boot_beep_ms     = 1000;        // макет: проверка RP -> чип без Спектрума
    cfg.boot_test        = true;        // макет: зелёный 5 с - чип пишется и читается, красный - ошибки
    cfg.fx_timeout_s     = 5;
    cfg.chips            = 1;           // макет: чип TurboSound выбирает CPLD MultiSound, в слот U4 - только его записи
    cfg.ay_clock_hz      = 1750000;     // такт AY Спектрума (YM2203 на макете - проверить делитель SSG)
    cfg.frame_hz         = 50;
    cfg.sys_khz          = 250000;      // 250 МГц: PiCard3 гоняет RP2350 на 351 (1.6 В), у нас - с запасом
    cfg.vreg             = VREG_VOLTAGE_1_15;
    cfg.led_bright       = 16;
    cfg.log_period_ms    = 1000;
}
