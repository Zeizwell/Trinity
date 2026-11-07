// Проверка прошивки RP-AY на ПК: та же прошивка (main.c, chip.c, card.c, ...) на модели железа (mock/sim.c).
// Сборка и запуск - test.bat. Каждая проверка - строка OK / FAIL, код выхода = число ошибок.
#define main fw_main
#include "../main.c"
#undef main
#include "../atari.h"
#include <string.h>
#include "ayfx.h"                                      // AYFX_P_INFO, AYFX_VER

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

void hwm_write(bool a0, uint8_t v);
uint8_t hwm_read(void);
// MODE AY как SDK: ключ, #C1: [C0][1][0][0], ждать ответ (ядро 0), прочитать [длина 2][итог], закрыть -> итог
static int mode_ay_packet(void) {
    const char *key = "TRIN";
    hwm_write(0, 0xC0);
    for (int i = 0; i < 4; i++) hwm_write(1, (uint8_t)key[i]);
    hwm_write(0, 0xC1);
    hwm_write(1, 0xC0); hwm_write(1, 1); hwm_write(1, 0); hwm_write(1, 0);
    card_svc_poll(false);
    hwm_write(0, 0xC0);
    if ((hwm_read() & 0xC0) != 0x40) return -1;
    hwm_write(0, 0xC1);
    uint8_t a[3];
    for (int i = 0; i < 3; i++) { a[i] = hwm_read(); }
    hwm_write(0, 0xC0); hwm_write(1, 0);
    return a[0] == 1 && a[1] == 0 ? a[2] : -2;
}

// Служебный пакет как SDK: ключ, #C1: код, длина 2, данные; ядро 0 отвечает -> данные ответа в ans, закрыть.
// -> байт в ответе (-1 - пакет без ответа)
static int svc_req(uint8_t code, const uint8_t *d, int n, uint8_t *ans, int max) {
    const char *key = "TRIN";
    hwm_write(0, 0xC0);
    for (int i = 0; i < 4; i++) hwm_write(1, (uint8_t)key[i]);
    hwm_write(0, 0xC1);
    hwm_write(1, code); hwm_write(1, (uint8_t)n); hwm_write(1, (uint8_t)(n >> 8));
    for (int i = 0; i < n; i++) hwm_write(1, d[i]);
    card_svc_poll(false);
    hwm_write(0, 0xC0);
    int k = -1;
    if ((hwm_read() & 0xC0) == 0x40) {
        hwm_write(0, 0xC1);
        uint8_t l[2];
        for (int i = 0; i < 2; i++) { l[i] = hwm_read(); }
        k = l[0] | l[1] << 8;
        for (int i = 0; i < k; i++) { uint8_t v = hwm_read(); if (i < max) ans[i] = v; }
    }
    hwm_write(0, 0xC0); hwm_write(1, 0);
    return k;
}

// Путь Z80 точно как SDK (Ayfx_SvcSend, Ayfx_SvcState, Ayfx_SvcReply) и плагин WC: выбор чипа #FF, ключ, поток, байты,
// закрыть, метка «занята» #80 в #C0 (п.14е-5); статус - по одному чтению, пока занята - время идёт; ответ - #C1 заново,
// длина и байты - целиком. Спектрум ходит через модель железа (mock/bus_hw_model.c): hwm_write / hwm_read
static void run_ms(uint32_t ms);
static bool sdk_busy_seen;                                          // sdk_req: метка видна сразу после пакета
static void sdk_key(void) {
    hwm_write(0, 0xC0);
    for (int i = 0; i < 4; i++) hwm_write(1, (uint8_t)"TRIN"[i]);
}
static uint8_t sdk_rd(void) { return hwm_read(); }          // читает железо (модель п.14е), ядру чтения не видны
static int sdk_req(uint8_t code, const uint8_t *d, int n, uint8_t *ans, int max) {
    hwm_write(0, 0xFF);                                       // Chip0
    sdk_key();
    hwm_write(0, 0xC1);
    hwm_write(1, code); hwm_write(1, (uint8_t)n); hwm_write(1, (uint8_t)(n >> 8));
    for (int i = 0; i < n; i++) hwm_write(1, d[i]);
    hwm_write(0, 0xC0); hwm_write(1, 0);                // Ayfx_Close
    hwm_write(1, 0x80);                                       //   метка: занята, пока ответ не лёг в поток
    sdk_busy_seen = (hwm_read() & 0x80) != 0;                         // сразу после пакета - занята (ядро 0 не успело)
    uint8_t st;
    for (;;) {                                                      // Ayfx_SvcState в цикле программы
        hwm_write(0, 0xC0);
        st = sdk_rd();
        if (st == 0xFF) return -2;
        if (!(st & 0x80)) break;
        run_ms(1);
    }
    if (!(st & 0x40)) return 0;
    hwm_write(0, 0xC1);                                       // Ayfx_SvcReply: ответ целиком
    int k = sdk_rd();
    k |= sdk_rd() << 8;
    for (int i = 0; i < k; i++) { uint8_t v = sdk_rd(); if (i < max) ans[i] = v; }
    return k;
}

// Ayfx_Detect как SDK: ключ, INFO, закрыть, метка; статус - не занята, ответ есть; #C1: T R I N версия -> версия, -1 нет
static int sdk_detect(void) {
    hwm_write(0, 0xFF);
    sdk_key();
    hwm_write(0, 0xC1); hwm_write(1, AYFX_P_INFO);
    hwm_write(0, 0xC0); hwm_write(1, 0); hwm_write(1, 0x80);
    hwm_write(0, 0xC0);                                             // Ayfx_SvcState
    uint8_t st = sdk_rd();
    if (st == 0xFF || (st & 0xC0) != 0x40) return -1;               // INFO отвечает сразу: метка уже снята
    hwm_write(0, 0xC1);
    uint8_t a[5];
    for (int i = 0; i < 5; i++) a[i] = sdk_rd();
    return memcmp(a, "TRIN", 4) ? -1 : a[4];
}

// Оба ядра на ms модельного времени: ядро 0 - служебное раз в мс, ядро 1 - «Звук» и чип.
// R8 (у подставной машины - номер кадра) сменился: сколько раз, через сколько мкс (самый короткий / длинный)
static uint32_t r8_changes, r8_min_us, r8_max_us;
static void run_ms(uint32_t ms) {
    uint64_t end = sim_now_ns() + (uint64_t)ms * 1000000, next = 0, t_prev = 0;
    uint8_t last = sim_ym_regs[8];
    r8_changes = 0, r8_min_us = UINT32_MAX, r8_max_us = 0;
    while (sim_now_ns() < end) {
        if (sim_now_ns() >= next) card_svc_poll(false), next = sim_now_ns() + 1000000;
        card_poll(); card_sound(); chip_poll();
        if (sim_ym_regs[8] != last) {
            uint64_t now = sim_now_ns();
            if (t_prev) {
                uint32_t d = (uint32_t)((now - t_prev) / 1000);
                if (d < r8_min_us) r8_min_us = d;
                if (d > r8_max_us) r8_max_us = d;
            }
            t_prev = now, last = sim_ym_regs[8], r8_changes++;
        }
    }
}

// Плеер Atari: SVC_CALL сервиса 1 (команда, слот входа, параметры 0..3) -> итог
static int player(uint8_t cmd, uint8_t in, uint32_t par) {
    uint8_t d[8] = { ATARI_SVC, cmd, in, 0, (uint8_t)par, (uint8_t)(par >> 8), (uint8_t)(par >> 16), (uint8_t)(par >> 24) };
    uint8_t a[4];
    return svc_req(0x91, d, 8, a, 4) == 1 ? a[0] : -1;
}

static int job_state(void) {
    uint8_t a[8];
    return svc_req(0xA0, NULL, 0, a, 8) >= 1 ? a[0] : -1;
}

// Слот 'STAT' плеера (ищем по типу) -> st; false - нет
static bool player_stat(uint8_t *st) {
    for (uint8_t n = 1; n <= SVC_SLOTS; n++) {
        uint8_t a[9];
        if (svc_req(0x83, &n, 1, a, 9) != 9 || memcmp(a, "STAT", 4)) continue;
        uint8_t d[7] = { n, 0, 0, 0, 0, ATARI_ST_SIZE, 0 };
        return svc_req(0x82, d, 7, st, ATARI_ST_SIZE) == ATARI_ST_SIZE;
    }
    return false;
}
static uint32_t st32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

int main(void) {
    // 1. Старт целиком: белый -> писк -> тест чипа -> зелёный 5 с -> обычная работа
    printf("--- старт прошивки (модель, 8 с)\n");
    sim_run_main(fw_main, 8000);
    check("старт: ядро 1 дошло до цикла, тест чипа прошёл (boot_result = 0)", boot_result == 0);
    check("старт: в чипе после теста тишина (R8 = 0; ядро 1 дописало очередь)", drain(100) && sim_ym_regs[8] == 0);

    // 2. Запись в чип: значения доходят, тайминги - по тактам Z80 3.5 МГц
    printf("--- запись в чип\n");
    chip_put(0, 0, 0x5A); chip_put(0, 1, 0x0C); chip_put(0, 13, 0x0E);
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
    chip_test(0, &r);
    check("исправный чип: 0 ошибок из 3584 (14 регистров x 256)", r.errors == 0 && r.checks == 3584);
    sim_fault_tie = 0x50;
    chip_test(0, &r);
    check("склеенные D4 / D6: ошибки найдены", r.errors > 0);
    check("склеенные D4 / D6: плохие биты = 50", r.bad_bits == 0x50);
    sim_no_chip = true;                          // чипа нет (или не туда воткнут): тест не должен давать ложный OK
    chip_test(0, &r);
    printf("  чипа нет: %u ошибок из %u\n", r.errors, r.checks);
    check("чипа нет: ошибки во всех проверках, биты FF", r.errors == r.checks && r.bad_bits == 0xFF);
    sim_no_chip = false;
    sim_fault_tie = 0;
    check("после теста запись в чип идёт", (chip_put(0, 2, 0x33), drain(100)) && sim_ym_regs[2] == 0x33);

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

    // 5. Ключ FX, как FT2: #C0, "TRIN"; чтение #C0 - байт статуса (doc/trinity-api.md п.3)
    hwm_write(0, 0xC0);
    card_svc_poll(false);
    check("#C0 до ключа - статус: AY, задания нет (00)", hwm_read() == 0x00);
    const char *key = "TRIN";
    for (int i = 0; i < 4; i++) hwm_write(1, (uint8_t)key[i]);
    check("ключ TRIN: сразу режим FX (уровень «Шина»)", mode == MODE_FX);
    card_svc_poll(false); card_poll();           // ядро 0: статус освежился, ядро 1 донесло его до железа
    check("статус: FX включены (бит 4) = 10", hwm_read() == 0x10);
    hwm_write(0, 0xC1);                    // служебный пакет: SVC_LIST -> ответ из #C1
    hwm_write(1, 0x90); hwm_write(1, 0); hwm_write(1, 0);
    hwm_write(0, 0xC0);
    check("SVC_LIST принят: статус занято (бит 7)", (hwm_read() & 0x80) != 0);
    card_svc_poll(false);
    hwm_write(0, 0xC0);                    // опрос статуса, как SDK: выбор - шина забирает ответ из ящика
    check("ядро 0 ответило: статус ответ готов (бит 6)", (hwm_read() & 0xC0) == 0x40);
    hwm_write(0, 0xC1);
    uint8_t sl[55];                              // ответ - целиком (п.14е-4): длина 2 + 53
    for (int i = 0; i < 55; i++) sl[i] = hwm_read();
    check("ответ: длина 53, 2 сервиса (4 тест, 1 плеер Atari), первый - 4", sl[0] == 53 && sl[1] == 0 && sl[2] == 2 && sl[3] == 4);

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

    // 7. MODE AY (как SDK Ayfx_ModeAy): карта по умолчанию - плагины выкл, чип и тень - тишина
    printf("--- MODE AY\n");
    check("из FX: ответ 0", mode_ay_packet() == 0);
    check("из FX: режим AY", drain(100) && mode == MODE_AY);
    check("тишина в чипе: R7 = 3F, громкости 0 (SID выкл - за 100 мс громкость не вернул)", drain(100) && sim_ym_regs[7] == 0x3F && sim_ym_regs[8] == 0 && sim_ym_regs[9] == 0 && sim_ym_regs[10] == 0);
    card_slot_write(0, 8);
    check("тень тоже тишина: чтение R8 = 0", card_answer() == 0);
    card_slot_write(1, 5);
    check("дальше чип у Спектрума: R8 = 5 дошло", drain(100) && sim_ym_regs[8] == 5);
    mode_set(MODE_ATARI);                        // самоплеер играет: записи Спектрума до чипа не доходят
    card_slot_write(0, 8); card_slot_write(1, 9);
    check("ATARI: запись Спектрума - только в тень (чип стоит молча - плеер не играет)", drain(100) && sim_ym_regs[8] == 0 && card_answer() == 9);
    check("из ATARI: протокол работает, ответ 0", mode_ay_packet() == 0);
    check("из ATARI: режим AY, тишина в чипе", drain(100) && mode == MODE_AY && sim_ym_regs[7] == 0x3F && sim_ym_regs[8] == 0);
    mode_set(MODE_TEST);
    check("из TEST: нельзя (FF), режим TEST", mode_ay_packet() == 0xFF && drain(100) && mode == MODE_TEST);
    mode_set(MODE_AY);

    // 8. Самоплеер Atari (сервис 1, atari.c): машина подставная (mock/sndh_mock.c) - кадр k пишет R8 = k & 15
    printf("--- самоплеер Atari\n");
    uint8_t a[64], st[ATARI_ST_SIZE];
    check("SVC_LIST: второй сервис - 1 player, вход SNDH, выход STAT", svc_req(0x90, NULL, 0, a, 64) == 53 && a[0] == 2
          && a[27] == 1 && !memcmp(a + 29, "player", 6) && !memcmp(a + 45, "SNDHSTAT", 8));
    static const uint8_t sn[9] = { 'S', 'N', 'D', 'H', 5, 0, 0, 0, 0 };
    int slot = svc_req(0x80, sn, 9, a, 1) == 1 ? a[0] : 0;
    uint8_t wr[10] = { (uint8_t)slot, 0, 0, 0, 0, 'M', 'O', 'C', 'K', 2 };
    svc_req(0x81, wr, 10, a, 1);
    check("слот SNDH создан и записан", slot > 0);
    check("PLAY: пошло (0), карта была в FX (ключ его включил)", mode == MODE_FX && player(ATARI_PLAY, (uint8_t)slot, 1) == 0);
    run_ms(5);
    check("режим ATARI, такт φM 4 МГц, задание идёт", mode == MODE_ATARI && sim_phim_hz == 4000000 && job_state() == SVC_JOB_RUN);
    run_ms(200);
    printf("  R8 (кадр) сменился %u раз, шаг %u..%u мкс\n", r8_changes, r8_min_us, r8_max_us);
    check("кадры в чип ровно через 20 мс (+-50 мкс)", r8_changes >= 9 && r8_min_us >= 19950 && r8_max_us <= 20050);
    check("R13 кадра 0 дошёл, R0 из таймера дошёл", sim_ym_regs[13] == 0x0E && sim_ym_regs[0] != 0);
    check("STAT: позиция ~10 кадров, длина 100, песня 1 из 2, опозданий 0", player_stat(st) && st32(st) >= 9 && st32(st) <= 12
          && st32(st + ATARI_ST_FRAMES) == 100 && st[ATARI_ST_SONG] == 1 && st[ATARI_ST_SONG + 1] == 2 && st32(st + ATARI_ST_LATE) == 0);
    card_slot_write(0, 8); card_slot_write(1, 0x1F);            // у машины R8 до 0F
    check("запись Спектрума в R8 - только в тень", (run_ms(2), sim_ym_regs[8] != 0x1F) && card_answer() == 0x1F);

    check("PAUSE: принята", player(ATARI_PAUSE, 0, 1) == 0);
    run_ms(50);
    uint32_t pos = player_stat(st) ? st32(st) : 0;
    run_ms(60);
    check("пауза: громкость 0, позиция стоит, флаг паузы", sim_ym_regs[8] == 0 && player_stat(st) && st32(st) == pos && (st[ATARI_ST_FLAGS] & 1));
    check("PAUSE 0: дальше", player(ATARI_PAUSE, 0, 0) == 0);
    run_ms(100);
    check("после паузы позиция идёт", player_stat(st) && st32(st) >= pos + 4);

    int opens = sim_sndh_opens;
    check("SEEK 50: принята", player(ATARI_SEEK, 0, 50) == 0);
    run_ms(30);
    check("SEEK 50: позиция 50..52, машина заново не открывалась", player_stat(st) && st32(st) >= 50 && st32(st) <= 52 && sim_sndh_opens == opens);
    check("после перемотки R8 = позиция (регистры машины дошли)", (sim_ym_regs[8] - (st32(st) & 15) + 16) % 16 <= 1);
    check("SEEK 10: назад", player(ATARI_SEEK, 0, 10) == 0);
    run_ms(30);
    check("SEEK 10: позиция 10..12, машина открыта с начала", player_stat(st) && st32(st) >= 10 && st32(st) <= 12 && sim_sndh_opens == opens + 1);

    check("STOP: принята", player(ATARI_STOP, 0, 0) == 0);
    run_ms(5);
    check("STOP: режим AY, тишина, φM Спектрума, задание готово", mode == MODE_AY && sim_ym_regs[7] == 0x3F && sim_ym_regs[8] == 0
          && sim_phim_hz == 0 && job_state() == SVC_JOB_DONE);

    check("PLAY снова (песня 2)", player(ATARI_PLAY, (uint8_t)slot, 2) == 0 && (run_ms(50), mode == MODE_ATARI));
    check("STOP и сразу PLAY (песня 1): режим ATARI, задание идёт, кадры идут", player(ATARI_STOP, 0, 0) == 0
          && player(ATARI_PLAY, (uint8_t)slot, 1) == 0 && (run_ms(50), mode == MODE_ATARI) && job_state() == SVC_JOB_RUN
          && player_stat(st) && st32(st) >= 1);
    check("MODE AY во время игры: 0", mode_ay_packet() == 0);
    run_ms(5);
    check("MODE AY: плеер встал, режим AY, задание готово, φM Спектрума", mode == MODE_AY && job_state() == SVC_JOB_DONE && sim_phim_hz == 0);

    check("песни 3 нет: задание - ошибка, режим не ATARI", player(ATARI_PLAY, (uint8_t)slot, 3) == 0 && (run_ms(5), job_state() == SVC_JOB_ERR) && mode != MODE_ATARI);
    sim_sndh_crash_at = 20;
    check("машина упала в кадре 20: ошибка, режим AY, тишина, φM Спектрума", player(ATARI_PLAY, (uint8_t)slot, 1) == 0
          && (run_ms(600), mode == MODE_AY) && sim_ym_regs[8] == 0 && sim_phim_hz == 0 && job_state() == SVC_JOB_ERR);
    sim_sndh_crash_at = -1;
    wr[5] = 'X';
    svc_req(0x81, wr, 10, a, 1);
    check("не SNDH: ошибка, режим не ATARI", player(ATARI_PLAY, (uint8_t)slot, 1) == 0 && (run_ms(5), job_state() == SVC_JOB_ERR) && mode != MODE_ATARI);
    wr[5] = 'M';
    svc_req(0x81, wr, 10, a, 1);
    mode_set(MODE_AY);
    mode_set(MODE_TEST);
    check("из TEST: ошибка, режим TEST", player(ATARI_PLAY, (uint8_t)slot, 1) == 0 && (run_ms(80), job_state() == SVC_JOB_ERR) && mode == MODE_TEST);

    // 9. Тот же плеер путём SDK / плагина WC: слоты, запись, PLAY с входом и выходом, STOP
    printf("--- путь SDK (плагин WC)\n");
    mode_set(MODE_AY);
    run_ms(5);
    uint8_t ra[16];
    static const uint8_t sn2[9] = { 'S', 'N', 'D', 'H', 5, 0, 0, 0, 0 };
    static const uint8_t ss2[9] = { 'S', 'T', 'A', 'T', ATARI_ST_SIZE, 0, 0, 0, 0 };
    check("SDK: Detect - INFO через поток, версия", sdk_detect() == AYFX_VER);
    int si = sdk_req(0x80, sn2, 9, ra, 16) == 1 ? ra[0] : 0;
    check("SDK: после пакета сразу занята (метка #80)", sdk_busy_seen);
    int so = sdk_req(0x80, ss2, 9, ra, 16) == 1 ? ra[0] : 0;
    check("SDK: слоты SNDH и STAT", si > 0 && so > 0);
    uint8_t w2[10] = { (uint8_t)si, 0, 0, 0, 0, 'M', 'O', 'C', 'K', 2 };
    check("SDK: SLOT_WRITE - без ответа (0)", sdk_req(0x81, w2, 10, ra, 16) == 0);
    uint8_t c2[8] = { ATARI_SVC, ATARI_PLAY, (uint8_t)si, (uint8_t)so, 1, 0, 0, 0 };
    int k2 = sdk_req(0x91, c2, 8, ra, 16);
    printf("  PLAY: ответ %d байт, итог %d\n", k2, k2 > 0 ? ra[0] : -1);
    check("SDK: PLAY - ответ 1 байт, 0", k2 == 1 && ra[0] == 0);
    run_ms(50);
    check("SDK: режим ATARI, задание идёт", mode == MODE_ATARI && job_state() == SVC_JOB_RUN);
    c2[1] = ATARI_STOP;
    check("SDK: STOP - 0", sdk_req(0x91, c2, 8, ra, 16) == 1 && ra[0] == 0);
    run_ms(5);
    printf("  после STOP: режим %s\n", mode_name[mode]);
    check("SDK: плеер встал, карта в AY (ответ читают без ключа - FX не включается)", mode == MODE_AY && job_state() == SVC_JOB_DONE);
    mode_set(MODE_AY);

    printf("\n%s: %d ошибок\n", fails ? "FAIL" : "OK", fails);
    return fails;
}
