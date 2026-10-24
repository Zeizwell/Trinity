// Карта со стороны Спектрума (card.h): «Шина» в прерывании, «Звук» в фоне, между ними очередь событий.
#include <string.h>
#include "card.h"
#include "config.h"
#include "modes.h"
#include "chip.h"
#include "ayfx.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"

// ---- Очередь событий «Шина» -> «Звук»: слово = тип << 24 | чип << 16 | регистр << 8 | значение.
// Пакет - в своём слоте (pk), в слове - номер слота
#define EV_Q    256                         // событий (степень двойки)
#define PK_Q    16                          // слотов пакетов (степень двойки)
static uint32_t ev[EV_Q];
static volatile uint32_t ev_head, ev_tail;  // head пишет шина, tail - звук
static uint8_t  pk[PK_Q][AYFX_PKT];
static volatile uint32_t pk_head, pk_tail;  // сколько пакетов положено / разобрано
volatile uint32_t card_ev_lost;
volatile uint32_t card_pk[5];
volatile uint32_t card_late_max, card_late_sum, card_late_n;
static uint32_t  snd_due;                   // мкс: срок ближайшего шага эффекта (0 - нет)

// ---- «Шина»
static uint8_t    regs[CARD_CHIPS][256];    // тень: что Спектрум записал (ответ на чтение)
static uint8_t    cur;                      // выбранный чип TurboSound
static ayfx_bus_t bus[CARD_CHIPS];          // протокол AY FX каждого чипа
static volatile uint32_t t_act;             // мкс: последняя запись Спектрума (таймаут FX)
static uint8_t    test_res[16];             // итог теста чипа (#E0..#EF тени)

// ---- «Звук»
static ayfx_t   fx[CARD_CHIPS];             // движок эффектов каждого чипа
static uint32_t t_last, t_frame;            // мкс: прошлый вызов card_sound, прошлый кадр
static uint32_t tick_acc;                   // остаток для пересчёта мкс -> такты тона

// Сколько чипов включено (cfg, не больше CARD_CHIPS)
static int chips(void) { return cfg.chips < CARD_CHIPS ? cfg.chips : CARD_CHIPS; }

// ================= «Шина» =================

// Событие в очередь (полна - потеряно, счётчик)
static void ev_put(uint8_t type, uint8_t c, uint8_t r, uint8_t v) {
    uint32_t h = ev_head;
    if (((h + 1) & (EV_Q - 1)) == ev_tail) { card_ev_lost++; return; }
    ev[h] = (uint32_t)type << 24 | (uint32_t)c << 16 | (uint32_t)r << 8 | v;
    ev_head = (h + 1) & (EV_Q - 1);
    sound_kick();                                               // «Звук» - сразу, не ждать будильника
}

// Пакет целиком - в слот и событие (слотов нет - потерян)
static void pk_put(uint8_t c, const uint8_t *p) {
    if (pk_head - pk_tail >= PK_Q) { card_ev_lost++; return; }
    uint32_t s = pk_head & (PK_Q - 1);
    memcpy(pk[s], p, AYFX_PKT);
    pk_head++;
    ev_put(AYFX_EV_PACKET, c, 0, (uint8_t)s);
}

// Ключ: режим FX. Закрытие потока режим не меняет (ay-ports-map.md п.4: «закрыто» - закрыт доступ к регистрам, а не
// выключены эффекты; плеер закрывает поток каждый кадр). Назад в AY - только таймаут или сброс
static void bus_open(void) {
    if (mode == MODE_AY) mode_set(MODE_FX);
}

// Адрес (#FFFD): выбор чипа TurboSound или регистра
static void addr_write(uint8_t v) {
    if (chips() > 1 && v >= 0xFE) { cur = (uint8_t)(0xFF - v); return; }   // #FF - чип 0, #FE - чип 1
    ayfx_bus_select(&bus[cur], v);
}

// Данные (#BFFD) в выбранный регистр: тень, протокол, событие для «Звука»
static void data_write(uint8_t v) {
    ayfx_bus_t *b = &bus[cur];
    regs[cur][b->sel] = v;
    if (!mode_chip_from_slot()) return;                         // ATARI / UPDATE / TEST: только тень
    switch (ayfx_bus_write(b, v)) {
    case AYFX_EV_REG:    if (b->sel < 16) ev_put(AYFX_EV_REG, cur, b->sel, v); break;
    case AYFX_EV_OPEN:   bus_open(); break;              // закрытие (AYFX_EV_CLOSE) - только шина, эффекты играют
    case AYFX_EV_PACKET: pk_put(cur, b->pkt); break;
    }
}

void card_slot_write(bool a0, uint8_t v) {
    chip_stat.slot_wr++;
    t_act = time_us_32();
    if (a0) data_write(v); else addr_write(v);
}

uint8_t card_answer(void) {
    uint8_t v;
    if (ayfx_bus_peek(&bus[cur], &v)) return v;                 // #C0 / #C1 при открытом потоке
    return regs[cur][bus[cur].sel];
}

void card_read_done(void) { ayfx_bus_read_done(&bus[cur]); }

uint8_t card_sel(void) { return bus[cur].sel; }

// ================= «Звук» - прерывание будильника (main.c): card_sound =================

// Выход движка: запись эффекта в чип - вне очереди (chip_put_fx). Настоящий чип на макете - только 0
static void fx_out(void *user, uint8_t reg, uint8_t val) {
    if ((uintptr_t)user == 0) chip_put_fx(reg, val);
}

// Событие шины -> движок и чип
static void ev_do(uint32_t e) {
    uint8_t type = (uint8_t)(e >> 24), c = (uint8_t)(e >> 16), r = (uint8_t)(e >> 8), v = (uint8_t)e;
    switch (type) {
    case AYFX_EV_REG:                                           // регистр у эффекта - движок записал с поправкой
        if (!ayfx_reg_write(&fx[c], r, v) && c == 0) chip_put(r, v);
        break;
    case AYFX_EV_PACKET:
        if (pk[v][0] < 5) card_pk[pk[v][0]]++;
        ayfx_packet(&fx[c], pk[v]);
        pk_tail++;                                              // слот свободен
        break;
    }
}

// Таймаут FX: Спектрум молчит - потоки закрыть (шина - при выключенных прерываниях), эффекты выкл
static void fx_timeout(void) {
    uint32_t s = save_and_disable_interrupts();
    for (int c = 0; c < CARD_CHIPS; c++) bus[c].on = 0, bus[c].info = 0;
    if (mode == MODE_FX) mode_set(MODE_AY);
    restore_interrupts(s);
    for (int c = 0; c < chips(); c++) ayfx_off(&fx[c]);
}

// Всё «звуковое» - здесь, из одного места (движок без гонок): события шины, шаги эффектов точно в срок, свип раз в
// кадр, таймаут. Время эффектов - такты тона AY (cfg.ay_clock_hz / 8). -> через сколько мкс звать снова: до ближайшего
// шага эффекта, не реже раза в CARD_SOUND_MAX_US (события, кадр, таймаут)
uint32_t card_sound(void) {
    while (ev_tail != ev_head) {                                // события шины - по порядку
        ev_do(ev[ev_tail]);
        ev_tail = (ev_tail + 1) & (EV_Q - 1);
    }
    uint32_t now = time_us_32(), dt = now - t_last;
    t_last = now;
    if (snd_due && (int32_t)(now - snd_due) >= 0) {             // пришли к сроку шага: насколько позже
        uint32_t late = now - snd_due;
        if (late > card_late_max) card_late_max = late;
        card_late_sum += late, card_late_n++;
        snd_due = 0;
    }
    uint32_t hz = cfg.ay_clock_hz / 8;                          // тактов тона в секунду
    if (mode != MODE_FX) { tick_acc = 0; t_frame = now; return CARD_SOUND_MAX_US; }   // TEST и др.: эффекты стоят
    // t_act пишет прерывание шины: оно могло прийти после чтения now - разница со знаком
    if (cfg.fx_timeout_s && (int32_t)(now - t_act) >= (int32_t)(cfg.fx_timeout_s * 1000000u)) {
        fx_timeout();
        return CARD_SOUND_MAX_US;
    }
    // такты тона за dt мкс: остаток копится в единицах 1/1e6 такта (dt до ~19 с не переполнит 32 бита)
    tick_acc += dt * hz;
    uint32_t n = tick_acc / 1000000u;
    tick_acc -= n * 1000000u;
    if (n)
        for (int c = 0; c < chips(); c++)
            if (ayfx_active(&fx[c])) ayfx_tick(&fx[c], n);
    if (cfg.frame_hz && now - t_frame >= 1000000u / cfg.frame_hz) {   // кадр: свип ШИМ
        t_frame += 1000000u / cfg.frame_hz;
        for (int c = 0; c < chips(); c++) ayfx_frame(&fx[c]);
    }
    uint32_t next = UINT32_MAX;                                 // ближайший шаг эффекта, такты тона
    for (int c = 0; c < chips(); c++) {
        uint32_t k = ayfx_next(&fx[c]);
        if (k < next) next = k;
    }
    if (next == UINT32_MAX) return CARD_SOUND_MAX_US;
    // мкс до него: (next * 1e6 - накопленный остаток) / hz, вверх
    uint64_t us = ((uint64_t)next * 1000000u - tick_acc + hz - 1) / hz;
    if (us < 1) us = 1;
    if (us > CARD_SOUND_MAX_US) return CARD_SOUND_MAX_US;
    snd_due = now + (uint32_t)us;                               // срок шага - для замера опоздания
    return (uint32_t)us;
}

void card_test_bytes(const uint8_t *b, int n) {
    memcpy(test_res, b, n > 16 ? 16 : n);
    memcpy(&regs[0][0xE0], test_res, 16);
}

void card_resend(void) {
    for (uint8_t r = 0; r < 14; r++) chip_put(r, regs[0][r]);
}

void card_reset(void) {
    memset(regs, 0, sizeof regs);
    memcpy(&regs[0][0xE0], test_res, 16);       // итог теста чипа сброс не стирает
    cur = 0;
    ev_head = ev_tail = 0;
    pk_head = pk_tail = 0;
    for (int c = 0; c < CARD_CHIPS; c++) {
        ayfx_bus_reset(&bus[c]);
        ayfx_init(&fx[c], fx_out, (void *)(uintptr_t)c);
    }
    chip_reset_state();
    if (cfg.silence_on_reset) {                                 // тишина: тон и шум выкл, громкости 0
        chip_put(7, 0x3F);
        chip_put(8, 0), chip_put(9, 0), chip_put(10, 0);
        regs[0][7] = 0x3F;
    }
    t_last = t_frame = t_act = time_us_32();
    tick_acc = 0;
}
