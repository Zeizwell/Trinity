// Проверка служебного API на ПК (doc/trinity-api.md): Спектрум пишет и читает регистры AY через шину
// (ayfx_bus_t), «ядро 0» разбирает пакеты (svc_t). Каждая проверка - строка OK / FAIL, код выхода = число ошибок.
// Сборка и запуск - test.bat.
#include <stdio.h>
#include <string.h>
#include "../ayfx.h"
#include "../svc.h"

static int fails;
static void check(const char *what, int ok) {
    printf("%s  %s\n", ok ? "OK  " : "FAIL", what);
    if (!ok) fails++;
}

static ayfx_bus_t bus;
static svc_t svc;
static uint8_t pool[64 * 1024];

// «Ядро 0»: служебный пакет из шины -> ответ, статус в шину; core0_step - ещё и кусок задания
static void core0(void) {
    if (bus.sbusy && !bus.pend) {
        uint16_t n = svc_packet(&svc, bus.sbuf);
        ayfx_bus_reply(&bus, svc.reply, n);
    }
    bus.status = svc_status(&svc);
}
static void core0_step(void) { core0(); svc_step(&svc); bus.status = svc_status(&svc); }

// Спектрум: OUT (#FFFD), reg; OUT (#BFFD), v; IN (#FFFD)
static void sel(uint8_t r) { ayfx_bus_select(&bus, r); }
static void out(uint8_t v) { ayfx_bus_write(&bus, v); }
static uint8_t in(void) { uint8_t v = 0xFF; ayfx_bus_peek(&bus, &v); ayfx_bus_read_done(&bus); return v; }

static void key(void) { sel(0xC0); out('T'); out('R'); out('I'); out('N'); sel(0xC1); }
static void close_(void) { sel(0xC0); out(0); }

// Служебный пакет: код, данные -> ждать (как Z80: опрос статуса), прочитать ответ в r; -> байт данных ответа
static int call(uint8_t code, const uint8_t *d, uint16_t n, uint8_t *r) {
    key();
    out(code); out((uint8_t)n); out((uint8_t)(n >> 8));
    for (uint16_t i = 0; i < n; i++) out(d[i]);
    sel(0xC0);
    for (int k = 0; k < 10 && (in() & 0x80); k++) core0();  // занято - ждём ядро 0
    uint8_t st = in();
    int len = -1;
    if (st & 0x40) {                                        // ответ готов
        sel(0xC1);
        len = in(); len |= in() << 8;
        for (int i = 0; i < len; i++) r[i] = in();
    }
    close_();
    return len;
}

int main(void) {
    svc_init(&svc, pool, sizeof pool);
    core0();

    printf("--- статус и INFO\n");
    sel(0xC0);
    check("#C0 без ключа - статус: AY, задания нет (00)", in() == 0x00);
    key(); out(AYFX_P_INFO);
    uint8_t inf[5];
    for (int i = 0; i < 5; i++) inf[i] = in();
    close_();
    check("INFO: TRIN + версия 3", !memcmp(inf, "TRIN", 4) && inf[4] == AYFX_VER);

    printf("--- слоты\n");
    uint8_t r[600], d[600];
    uint8_t nw[9] = { 'F', 'T', '2', 'C', 0x00, 0x10, 0, 0, 0 };   // 4096 байт, временный
    check("SLOT_NEW 4096 -> слот 1", call(SVC_P_SLOT_NEW, nw, 9, r) == 1 && r[0] == 1);
    d[0] = 1; d[1] = 0x10; d[2] = d[3] = d[4] = 0;          // слот 1, смещение 16
    for (int i = 0; i < 300; i++) d[5 + i] = (uint8_t)i;
    check("SLOT_WRITE 300 байт - без ответа", call(SVC_P_SLOT_WRITE, d, 305, r) == -1);
    uint8_t rd[7] = { 1, 0x10, 0, 0, 0, 0x2C, 0x01 };       // 300 байт со смещения 16
    int k = call(SVC_P_SLOT_READ, rd, 7, r);
    int same = k == 300;
    for (int i = 0; same && i < 300; i++) same = r[i] == (uint8_t)i;
    check("SLOT_READ: те же 300 байт", same);
    uint8_t one = 1;
    check("SLOT_INFO: FT2C, 4096", call(SVC_P_SLOT_INFO, &one, 1, r) == 9 && !memcmp(r, "FT2C", 4) && r[5] == 0x10);
    nw[5] = 0x00; nw[6] = 0x01;                             // 65536 - больше свободного
    check("SLOT_NEW больше пула -> 0", call(SVC_P_SLOT_NEW, nw, 9, r) == 1 && r[0] == 0);
    check("SLOT_LIST -> {1}", call(SVC_P_SLOT_LIST, NULL, 0, r) == 1 && r[0] == 1);

    printf("--- сервисы и задание\n");
    k = call(SVC_P_LIST, NULL, 0, r);
    check("SVC_LIST: 1 сервис, номер 4, имя test", k == 27 && r[0] == 1 && r[1] == 4 && !strcmp((char *)r + 3, "test"));
    uint8_t cl[4] = { 4, 0, 1, 0 };                         // тест, команда 0, вход слот 1
    check("SVC_CALL тест -> 0 (пошло)", call(SVC_P_CALL, cl, 4, r) == 1 && r[0] == 0);
    sel(0xC0);
    check("статус: задание идёт (бит 2)", (in() & 0x0C) == 0x04);
    for (int i = 0; i < 5; i++) core0_step();
    sel(0xC0);
    check("статус: задание готово (биты 3..2 = 2)", (in() & 0x0C) == 0x08);
    k = call(SVC_P_JOB_STATUS, NULL, 0, r);
    uint8_t log = r[2];
    check("JOB_STATUS: готово, 100 %, слот лога 2, в логе 27 байт", k == 7 && r[0] == SVC_JOB_DONE && r[1] == 100 &&
          log == 2 && r[3] == 27);
    k = call(SVC_P_JOB_LOG, NULL, 0, r);
    r[k > 0 ? k : 0] = 0;
    printf("  лог: %s", (char *)r);
    check("JOB_LOG: CRC32 ..., 4096 bytes", k == 27 && strstr((char *)r, "4096 bytes\n"));
    uint8_t lr[7] = { log, 0, 0, 0, 0, 27, 0 };
    check("лог целиком - SLOT_READ слота лога", call(SVC_P_SLOT_READ, lr, 7, r) == 27 && !memcmp(r, "CRC32", 5));
    uint8_t bad[4] = { 9, 0, 1, 0 };
    check("SVC_CALL нет сервиса -> 2", call(SVC_P_CALL, bad, 4, r) == 1 && r[0] == 2);
    check("SLOT_FREE 1, SLOT_LIST - только лог", call(SVC_P_SLOT_FREE, &one, 1, r) == -1 &&
          call(SVC_P_SLOT_LIST, NULL, 0, r) == 1 && r[0] == log);

    printf("--- реалтайм не задет\n");
    key(); out(AYFX_P_RESET);
    int ev = ayfx_bus_write(&bus, 7);
    close_();
    check("RESET (код 3) - как раньше: событие пакета", ev == AYFX_EV_PACKET);

    printf("%s: %d ошибок\n", fails ? "FAIL" : "OK", fails);
    return fails;
}
