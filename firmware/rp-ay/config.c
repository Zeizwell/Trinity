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
    cfg.timing_preset    = 0;           // default: ритм Z80 (tuned - после теста пауз, клавиша p)
    for (int i = 0; i < 5; i++) cfg.tuned_pct[i] = 100;
    cfg.timing_margin_pct = 50;
    cfg.status_answer    = 0x00;        // никогда не занят: очередь в RP сама держит паузы чипа
    cfg.mode_after_reset = MODE_AY;
    cfg.silence_on_reset = true;
    cfg.boot_beep_ms     = 0;           // писк при старте не нужен: тест чипа проверяет RP -> чип сам
    cfg.boot_test        = true;        // зелёный 3 с - все регистры пишутся и читаются, красный насовсем - ошибки
    cfg.fx_timeout_s     = 5;
    cfg.chips            = 1;           // макет: чип TurboSound выбирает CPLD MultiSound, в слот U4 - только его записи
    cfg.ay_clock_hz      = 1750000;     // такт AY Спектрума (YM2203 на макете - проверить делитель SSG)
    cfg.frame_hz         = 50;
    cfg.sys_khz          = 250000;      // 250 МГц: на 150 разбор шины не успевает за пачкой записей (fifo, FT2 не видит карту) - до архитектуры 14б
    cfg.vreg             = VREG_VOLTAGE_1_15;     // под 250 МГц
    cfg.led_bright       = 16;
    cfg.log_period_ms    = 1000;
#ifdef BOARD_REV_A
    cfg.chips            = 2;           // плата: два YM2203, выбор TurboSound - RP
    cfg.sys_khz          = 252000;      // такт чипов 3.5 МГц - делитель 72 без дрожания (250 / 3.5 - дробный)
#endif
#ifdef BUS_SELFTEST
    cfg.boot_test        = false;       // на голой RP чипов нет
    cfg.log_period_ms    = 0;
#endif
}
