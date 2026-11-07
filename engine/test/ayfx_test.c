// Проверка движка AY FX на ПК (без железа). Сборка и запуск - test.bat (MSVC).
// Каждая проверка - одна строка OK / FAIL; в конце - итог, код выхода = число ошибок.
#include <stdio.h>
#include <string.h>
#include "ayfx.h"

// ---- выход движка: журнал записей в чип
static uint8_t chip[16];                // что сейчас в чипе
static int     outs;                    // записей с прошлой очистки
static void out(void *user, uint8_t reg, uint8_t val) { (void)user; chip[reg] = val; outs++; }

static int fails;
static void check(const char *what, int ok) {
    printf("%s  %s\n", ok ? "OK  " : "FAIL", what);
    if (!ok) fails++;
}

// ---- Спектрум: как его записи приходят в RP (card.c)
static ayfx_t a;                                // звук
static ayfx_bus_t b;                            // шина
static void sel(uint8_t r) { ayfx_bus_select(&b, r); }
static bool wr(uint8_t r, uint8_t v) {          // true - в чип не пропускать (протокол или регистр у эффекта)
    sel(r);
    switch (ayfx_bus_write(&b, v)) {            // шина -> событие -> звук (как карта и эмулятор)
    case AYFX_EV_REG:
        if (ayfx_reg_write(&a, r, v)) { ayfx_tick(&a, 0); return true; }
        chip[r] = v;                            // не у эффекта - прямо в чип
        return false;
    case AYFX_EV_PACKET: ayfx_packet(&a, b.pkt); break;
    }
    ayfx_tick(&a, 0);                           // шаги: просьбы (restart / kick) - сразу, как сделало бы прерывание
    return true;
}
static void c1(const uint8_t *p, int n) { for (int i = 0; i < n; i++) wr(0xC1, p[i]); }
static uint8_t rd(uint8_t r) { uint8_t v = 0xEE; sel(r); if (ayfx_bus_peek(&b, &v)) ayfx_bus_read_done(&b); return v; }

static void open_stream(void) { wr(0xC0, 'T'); wr(0xC0, 'R'); wr(0xC0, 'I'); wr(0xC0, 'N'); }

// Пакет ROW: канал, строка (FLAGS, PWM, SWEEP, 3 x (SEMI, RAW, TAB), LEN1, LEN2), форма, старт, keep
static void row(uint8_t ch, uint8_t flags, uint8_t tab_mix, uint8_t tab_fm, uint8_t len1) {
    uint8_t p[AYFX_PKT] = { AYFX_P_ROW, ch, flags, 0, 0,  0, 0, tab_mix,  0, 0, tab_fm,  0, 0, 0,  len1, 0,  0, 0, 0 };
    c1(p, sizeof p);
}
static void table(uint8_t n, const uint8_t *steps) {
    uint8_t p[18] = { AYFX_P_TABLE, n };
    memcpy(p + 2, steps, 16);
    c1(p, sizeof p);
}

int main(void) {
    ayfx_init(&a, out, NULL);
    ayfx_bus_reset(&b);

    // 1. Ключ
    check("закрыт после init", !b.on);
    wr(0xC0, 'T'); wr(0xC0, 'X');
    check("неверный ключ - закрыт", !b.on);
    open_stream();
    check("TRIN - открыт", b.on);

    // 2. INFO: T R I N версия, дальше #C1 - не наш (тень)
    { uint8_t p = AYFX_P_INFO; c1(&p, 1); }
    uint8_t info[6];
    for (int i = 0; i < 6; i++) info[i] = rd(0xC1);
    check("INFO = T R I N версия, потом тень", info[0] == 'T' && info[1] == 'R' && info[2] == 'I' && info[3] == 'N' &&
                                info[4] == AYFX_VER && info[5] == 0xEE);
    { uint8_t v; sel(8); check("чтение AY-регистра - не наше", !ayfx_bus_peek(&b, &v)); }

    // 3. SID на канале A: таблица 1 = уровни 15, 0; 2 шага; тон 100
    wr(0, 100); wr(1, 0); wr(8, 15);
    static const uint8_t t1[16] = { 15, 0 };
    table(1, t1);
    outs = 0;
    row(0, 1 /*SID*/, 1, 0, 1 /*LEN1: 2 шага*/);
    check("SID вкл - шаг 0: громкость 15", chip[8] == 15 && outs > 0);
    ayfx_tick(&a, 99);
    check("99 тактов - ещё шаг 0", chip[8] == 15);
    ayfx_tick(&a, 1);
    check("100 тактов - шаг 1: громкость 0", chip[8] == 0);
    ayfx_tick(&a, 100);
    check("200 тактов - снова шаг 0", chip[8] == 15);
    check("R8 у эффекта: запись Спектрума не в чип", wr(8, 8) == true);
    check("  ... движок пересчитал: 8 * 15 / 15 = 8", chip[8] == 8);
    check("R0 не у эффекта - в чип", wr(0, 100) == false && chip[0] == 100);

    // 4. FM на канале B: таблица 2 = +12 полутонов (период / 2), 1 шаг; тон 200
    wr(2, 200); wr(3, 0);
    static const uint8_t t2[16] = { 12 };
    table(2, t2);
    row(1, 4 /*FM*/, 0, 2, 0);
    check("FM: тон B = 200 / 2 = 100", chip[2] == 100 && chip[3] == 0);
    check("R2 у эффекта", wr(2, 100) == true && chip[2] == 50);

    // 5. Выключение: регистры - как у Спектрума
    ayfx_off(&a);
    ayfx_tick(&a, 0);
    check("off: громкость A = 8 (Спектрум)", chip[8] == 8);
    check("off: тон B = 100 (Спектрум)", chip[2] == 100);
    check("off: эффектов нет", !ayfx_active(&a));
    check("off: R8 снова не у эффекта", wr(8, 5) == false && chip[8] == 5);

    // 6. Закрытие потока: 0 в #C0
    wr(0xC0, 0);
    check("0 в C0 - закрыт", !b.on);
    { uint8_t p[2] = { AYFX_P_RESET, 7 }; outs = 0; c1(p, 2); }
    check("закрыт - пакеты не идут", outs == 0);

    // 7. Закрытие эффекты не выключает (плеер закрывает поток каждый кадр)
    open_stream();
    row(0, 1 /*SID*/, 1, 0, 1);
    wr(0xC0, 0);
    check("SID включили, поток закрыли - эффект играет", ayfx_active(&a));
    ayfx_tick(&a, 100);
    check("  ... и таймер идёт: шаг 1, громкость 0", chip[8] == 0);
    check("ayfx_next: до следующего шага SID - период тона 100", ayfx_next(&a) == 100);
    ayfx_tick(&a, 30);
    check("  ... через 30 тактов - 70", ayfx_next(&a) == 70);

    printf("\n%s: %d ошибок\n", fails ? "FAIL" : "OK", fails);
    return fails;
}
