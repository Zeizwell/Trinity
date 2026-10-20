// Карта со стороны Спектрума (card.h). Все числа - из cfg.
#include <string.h>
#include "card.h"
#include "config.h"
#include "modes.h"
#include "chip.h"
#include "ayfx.h"
#include "pico/stdlib.h"

static uint8_t  regs[CARD_CHIPS][256];      // тень: что Спектрум записал (ответ на чтение)
static uint8_t  sel[CARD_CHIPS];            // выбранный регистр каждого чипа
static uint8_t  cur;                        // выбранный чип TurboSound
static ayfx_t   fx[CARD_CHIPS];             // движок эффектов - на каждый чип
static uint32_t t_last, t_frame, t_act;     // мкс: прошлый опрос, прошлый кадр, последняя запись Спектрума
static uint64_t tick_acc;                   // остаток для пересчёта мкс -> такты тона
static uint8_t  test_err, test_bits;        // итог теста чипа: ошибок, плохие биты (#E0 / #E1 тени)

// Сколько чипов включено (cfg, не больше CARD_CHIPS)
static int chips(void) { return cfg.chips < CARD_CHIPS ? cfg.chips : CARD_CHIPS; }

// Выход движка: запись в чип. Настоящий чип на макете - только 0, второй "в никуда"
static void fx_out(void *user, uint8_t reg, uint8_t val) {
    if ((uintptr_t)user == 0) chip_put(reg, val);
}

// Регистр от Спектрума: тень, движок (регистр может быть у эффекта), чип
static void reg_set(int c, uint8_t r, uint8_t v) {
    regs[c][r] = v;
    if (ayfx_reg_write(&fx[c], r, v)) return;  // у эффекта - движок уже записал с поправкой
    if (c == 0) chip_put(r, v);
}

// Все эффекты выкл, поток закрыт, режим AY
static void fx_stop(void) {
    for (int c = 0; c < chips(); c++) {
        ayfx_off(&fx[c]);
        fx[c].on = 0;
    }
    mode_set(MODE_AY);
}

// Режим - по протоколу: поток открыт хоть у одного чипа - FX, закрыт у всех - AY
static void fx_mode(void) {
    for (int c = 0; c < chips(); c++)
        if (fx[c].on) { mode_set(MODE_FX); return; }
    if (mode == MODE_FX) fx_stop();
}

// Адрес (#FFFD): выбор чипа TurboSound или регистра
static void addr_write(uint8_t v) {
    if (chips() > 1 && v >= 0xFE) { cur = (uint8_t)(0xFF - v); return; }   // #FF - чип 0, #FE - чип 1
    sel[cur] = v;
    ayfx_select(&fx[cur], v);
}

// Данные (#BFFD) в выбранный регистр
static void data_write(uint8_t v) {
    uint8_t r = sel[cur];
    if (!mode_chip_from_slot()) { regs[cur][r] = v; return; }   // ATARI / UPDATE / TEST: только тень
    if (r == 0xC0 || r == 0xC1) {                               // протокол AY FX - в чип не идёт
        regs[cur][r] = v;
        ayfx_port_write(&fx[cur], r, v);
        fx_mode();
        return;
    }
    reg_set(cur, r, v);
}

void card_slot_write(bool a0, uint8_t v) {
    chip_stat.slot_wr++;
    t_act = time_us_32();
    if (a0) data_write(v); else addr_write(v);
}

void card_test_result(uint8_t errors, uint8_t bad_bits) {
    test_err = errors, test_bits = bad_bits;
    regs[0][0xE0] = test_err, regs[0][0xE1] = test_bits;
}

uint8_t card_answer(void) {
    uint8_t v;
    if (ayfx_port_peek(&fx[cur], sel[cur], &v)) return v;     // #C0 / #C1 при открытом потоке
    return regs[cur][sel[cur]];
}

void card_read_done(void) {
    ayfx_port_read_done(&fx[cur], sel[cur]);
}

uint8_t card_sel(void) { return sel[cur]; }

void card_resend(void) {
    for (uint8_t r = 0; r < 14; r++) chip_put(r, regs[0][r]);
}

void card_reset(void) {
    memset(regs, 0, sizeof regs);
    regs[0][0xE0] = test_err, regs[0][0xE1] = test_bits;   // итог теста чипа сброс не стирает
    memset(sel, 0, sizeof sel);
    cur = 0;
    for (int c = 0; c < CARD_CHIPS; c++) ayfx_init(&fx[c], fx_out, (void *)(uintptr_t)c);
    chip_reset_state();
    if (cfg.silence_on_reset) {                                 // тишина: тон и шум выкл, громкости 0
        reg_set(0, 7, 0x3F);
        reg_set(0, 8, 0), reg_set(0, 9, 0), reg_set(0, 10, 0);
    }
    t_last = t_frame = t_act = time_us_32();
    tick_acc = 0;
}

void card_poll(void) {
    uint32_t now = time_us_32(), dt = now - t_last;
    t_last = now;
    if (mode != MODE_FX) { tick_acc = 0; t_frame = now; return; }
    if (cfg.fx_timeout_s && now - t_act >= cfg.fx_timeout_s * 1000000u) { fx_stop(); return; }   // Спектрум молчит
    // такты тона за dt мкс: dt * (такт AY / 8) / 1e6, остаток копится
    tick_acc += (uint64_t)dt * (cfg.ay_clock_hz / 8);
    uint32_t n = (uint32_t)(tick_acc / 1000000u);
    tick_acc %= 1000000u;
    if (n)
        for (int c = 0; c < chips(); c++)
            if (ayfx_active(&fx[c])) ayfx_tick(&fx[c], n);
    // кадр: свип ШИМ
    if (cfg.frame_hz && now - t_frame >= 1000000u / cfg.frame_hz) {
        t_frame += 1000000u / cfg.frame_hz;
        for (int c = 0; c < chips(); c++) ayfx_frame(&fx[c]);
    }
}
