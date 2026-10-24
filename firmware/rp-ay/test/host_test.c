// Проверка прошивки RP-AY на ПК: та же прошивка (main.c, chip.c, card.c, ...) на модели железа (mock/sim.c).
// Сборка и запуск - test.bat. Каждая проверка - строка OK / FAIL, код выхода = число ошибок.
#define main fw_main
#include "../main.c"
#undef main

static int fails;
static void check(const char *what, int ok) {
    printf("%s  %s\n", ok ? "OK  " : "FAIL", what);
    if (!ok) fails++;
}

// Дать записям дойти до чипа (ядро 1 в цикле крутит chip_poll), не дольше limit_ms модельного времени
static bool drain(uint32_t limit_ms) {
    uint64_t end = sim_now_ns() + (uint64_t)limit_ms * 1000000;
    while (sim_now_ns() < end) { card_poll(); card_sound(); chip_poll(); if (chip_idle()) return true; }   // «Звук»: события, чип
    return false;
}

int main(void) {
    // 1. Старт целиком: белый -> писк -> тест чипа -> зелёный 5 с -> обычная работа
    printf("--- старт прошивки (модель, 8 с)\n");
    sim_run_main(fw_main, 8000);
    check("старт: ядро 1 дошло до цикла, тест чипа прошёл (boot_result = 0)", boot_result == 0);
    check("старт: в чипе после теста тишина (R8 = 0; ядро 1 дописало очередь)", drain(100) && sim_ym_regs[8] == 0);

    // 2. Запись в чип: значения доходят, тайминги - по тактам Z80 3.5 МГц
    printf("--- запись в чип\n");
    chip_put(0, 0x5A); chip_put(1, 0x0C); chip_put(13, 0x0E);
    check("очередь дошла до чипа (chip_idle стал true)", drain(100));
    check("R0 = 5A, R1 = 0C, R13 = 0E", sim_ym_regs[0] == 0x5A && sim_ym_regs[1] == 0x0C && sim_ym_regs[13] == 0x0E);
    check("chip_idle повторно, когда всё записано, - сразу true", chip_idle() && chip_idle());
    printf("  /WR самый короткий %u нс (нужно >= 714), адрес -> данные %u нс (нужно >= 3140)\n",
           sim_wr_min_ns, sim_gap_addr_min_ns);
    check("/WR >= 714 нс (2.5 такта Z80)", sim_wr_min_ns >= 714);
    check("адрес -> данные >= 3140 нс (11 тактов Z80)", sim_gap_addr_min_ns >= 3140);

    // 3. Тест чтения чипа: исправный - 0 ошибок, склеенные D4 / D6 - ловит
    printf("--- тест чтения чипа\n");
    chip_test_t r;
    chip_test(&r);
    check("исправный чип: 0 ошибок из 3584 (14 регистров x 256)", r.errors == 0 && r.checks == 3584);
    sim_fault_tie = 0x50;
    chip_test(&r);
    check("склеенные D4 / D6: ошибки найдены", r.errors > 0);
    check("склеенные D4 / D6: плохие биты = 50", r.bad_bits == 0x50);
    sim_no_chip = true;                          // чипа нет (или не туда воткнут): тест не должен давать ложный OK
    chip_test(&r);
    printf("  чипа нет: %u ошибок из %u\n", r.errors, r.checks);
    check("чипа нет: ошибки во всех проверках, биты FF", r.errors == r.checks && r.bad_bits == 0xFF);
    sim_no_chip = false;
    sim_fault_tie = 0;
    check("после теста запись в чип идёт", (chip_put(2, 0x33), drain(100)) && sim_ym_regs[2] == 0x33);

    // 4. Спектрум: #FFFD (выбор регистра) и #BFFD (данные) через card.c -> чип и ответ на чтение
    printf("--- запись Спектрума через card.c\n");
    card_slot_write(0, 8); card_slot_write(1, 0x0F);
    card_slot_write(0, 7); card_slot_write(1, 0x3E);
    check("дошло до чипа", drain(100) && sim_ym_regs[8] == 0x0F && sim_ym_regs[7] == 0x3E);
    card_slot_write(0, 8);
    check("ответ на чтение #FFFD = 0F", card_answer() == 0x0F);

    static const uint8_t tr[6] = { 0x23, 0x01, 0x50, 4, 0x10, 0x00 };
    card_test_bytes(tr, 6);                      // итог теста - в #E0.., и сброс его не стирает
    card_reset();
    uint8_t t[6];
    for (int i = 0; i < 6; i++) { card_slot_write(0, (uint8_t)(0xE0 + i)); t[i] = card_answer(); }
    check("итог теста читается после сброса: 23 01 50 04 10 00", t[0] == 0x23 && t[1] == 0x01 && t[2] == 0x50 && t[3] == 4 && t[4] == 0x10 && t[5] == 0);

    // 5. Ключ FX, как FT2 (Fx_Detect): #C0, "TRIN", и сразу чтение - ответ 'T'
    card_slot_write(0, 0xC0);
    const char *key = "TRIN";
    for (int i = 0; i < 3; i++) card_slot_write(1, (uint8_t)key[i]);
    check("после \"TRI\" поток ещё закрыт: ответ #C0 - тень, не 'T'", card_answer() != 'T');
    card_slot_write(1, (uint8_t)key[3]);         // шина: та же запись - и режим, и ответ (без фона)
    check("ключ TRIN: сразу режим FX и чтение #C0 = 'T' (уровень «Шина»)", mode == MODE_FX && card_answer() == 'T');

    // 6. Пакет через очередь: TABLE + ROW (SID канал A) - «Звук» пишет громкость в чип
    card_slot_write(0, 8); card_slot_write(1, 15);   // громкость A = 15 (поток открыт - регистры как обычно)
    card_slot_write(0, 0); card_slot_write(1, 100);  // тон A = 100
    card_slot_write(0, 0xC1);
    static const uint8_t tab[18] = { 2, 1, 15, 0 };  // TABLE 1: уровни 15, 0
    for (int i = 0; i < 18; i++) card_slot_write(1, tab[i]);
    static const uint8_t row[19] = { 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0 };  // ROW: SID, табл. 1, 2 шага
    for (int i = 0; i < 19; i++) card_slot_write(1, row[i]);
    check("«Звук» разобрал очередь: SID вкл, громкость A в чипе = 15", drain(100) && sim_ym_regs[8] == 15);
    check("событий не потеряно", card_ev_lost == 0);

    card_slot_write(0, 0xC0);
    card_slot_write(1, 0);                       // закрыть
    check("закрыть (0 в #C0): режим FX остаётся, SID играет (закрыт доступ, не эффекты)", mode == MODE_FX && drain(100) && sim_ym_regs[8] == 15);

    printf("\n%s: %d ошибок\n", fails ? "FAIL" : "OK", fails);
    return fails;
}
