// Тест SDK драйвера: настоящий код Z80 из FT2 sdk/trinity_gs.asm выполняется здесь и говорит с настоящим движком карты через порты.
// Процессор - только те команды, что нужны SDK: встретится другая - тест падает и называет её
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "../../engine/gs/gs_cfg.h"
#include "../../engine/gs/gs_core.h"
#include "../../engine/gs/gs_ctl.h"
#include "../../engine/gs/gs_dev_gs.h"
#include "../../engine/gs/gs_stat.h"

static uint8_t mem[65536];
static uint8_t r[8];            // B C D E H L (HL) A, как в опкоде
enum { B, C, D, E, H, L, M, A };
static uint16_t pc, sp;
static int fc, fz;
static int no_card;             // карты нет: шина отдаёт FF, записи в никуда
static long steps;

#define HL ((uint16_t)(r[H] << 8 | r[L]))
#define DE ((uint16_t)(r[D] << 8 | r[E]))

static uint8_t bus_in(uint8_t port) {
    if (no_card) return 0xFF;
    return gs_dev_gs.port_read(port == 0xBB ? GS_PORT_BB : GS_PORT_B3);
}

static void bus_out(uint8_t port, uint8_t v) {
    if (!no_card) gs_dev_gs.port_write(port == 0xBB ? GS_PORT_BB : GS_PORT_B3, v);
}

static uint8_t get8(int i) { return i == M ? mem[HL] : r[i]; }
static void set8(int i, uint8_t v) { if (i == M) mem[HL] = v; else r[i] = v; }
static uint8_t imm(void) { return mem[pc++]; }
static uint16_t imm16(void) { uint16_t v = mem[pc] | mem[pc + 1] << 8; pc += 2; return v; }
static void push16(uint16_t v) { mem[--sp] = (uint8_t)(v >> 8); mem[--sp] = (uint8_t)v; }
static uint16_t pop16(void) { uint16_t v = mem[sp] | mem[sp + 1] << 8; sp += 2; return v; }
static void logic(uint8_t v) { r[A] = v, fz = v == 0, fc = 0; }

static void step(void) {
    uint8_t op = imm();
    int cc_nz = op == 0x20 || op == 0xC0, cc_z = op == 0x28 || op == 0xC8, cc_nc = op == 0x30 || op == 0xD0, cc_c = op == 0x38 || op == 0xD8;
    int taken = (cc_nz && !fz) || (cc_z && fz) || (cc_nc && !fc) || (cc_c && fc);
    if (op >= 0x40 && op < 0x80 && op != 0x76) { set8(op >> 3 & 7, get8(op & 7)); return; }     // ld r,r
    if ((op & 0xC7) == 0x06) { set8(op >> 3 & 7, imm()); return; }                              // ld r,n
    if ((op & 0xC7) == 0x04) { uint8_t v = (uint8_t)(get8(op >> 3 & 7) + 1); set8(op >> 3 & 7, v); fz = !v; return; }   // inc r
    if ((op & 0xC7) == 0x05) { uint8_t v = (uint8_t)(get8(op >> 3 & 7) - 1); set8(op >> 3 & 7, v); fz = !v; return; }   // dec r
    if (op >= 0xB0 && op < 0xB8) { logic(r[A] | get8(op & 7)); return; }                        // or r
    if (op >= 0xA8 && op < 0xB0) { logic(r[A] ^ get8(op & 7)); return; }                        // xor r
    switch (op) {
    case 0x00: break;
    case 0x01: { uint16_t v = imm16(); r[B] = (uint8_t)(v >> 8), r[C] = (uint8_t)v; break; }
    case 0x11: { uint16_t v = imm16(); r[D] = (uint8_t)(v >> 8), r[E] = (uint8_t)v; break; }
    case 0x21: { uint16_t v = imm16(); r[H] = (uint8_t)(v >> 8), r[L] = (uint8_t)v; break; }
    case 0x0B: { uint16_t v = (uint16_t)((r[B] << 8 | r[C]) - 1); r[B] = (uint8_t)(v >> 8), r[C] = (uint8_t)v; break; }
    case 0x13: { uint16_t v = (uint16_t)(DE + 1); r[D] = (uint8_t)(v >> 8), r[E] = (uint8_t)v; break; }
    case 0x23: { uint16_t v = (uint16_t)(HL + 1); r[H] = (uint8_t)(v >> 8), r[L] = (uint8_t)v; break; }
    case 0x2B: { uint16_t v = (uint16_t)(HL - 1); r[H] = (uint8_t)(v >> 8), r[L] = (uint8_t)v; break; }
    case 0x12: mem[DE] = r[A]; break;
    case 0x07: fc = r[A] >> 7, r[A] = (uint8_t)(r[A] << 1 | fc); break;
    case 0x0F: fc = r[A] & 1, r[A] = (uint8_t)(r[A] >> 1 | fc << 7); break;
    case 0x37: fc = 1; break;
    case 0x10: { int8_t d = (int8_t)imm(); if (--r[B]) pc = (uint16_t)(pc + d); break; }
    case 0x18: { int8_t d = (int8_t)imm(); pc = (uint16_t)(pc + d); break; }
    case 0x20: case 0x28: case 0x30: case 0x38: { int8_t d = (int8_t)imm(); if (taken) pc = (uint16_t)(pc + d); break; }
    case 0xC0: case 0xC8: case 0xD0: case 0xD8: if (taken) pc = pop16(); break;
    case 0xC3: pc = imm16(); break;
    case 0xC9: pc = pop16(); break;
    case 0xCD: { uint16_t a = imm16(); push16(pc); pc = a; break; }
    case 0xC5: push16((uint16_t)(r[B] << 8 | r[C])); break;
    case 0xD5: push16(DE); break;
    case 0xE5: push16(HL); break;
    case 0xC1: { uint16_t v = pop16(); r[B] = (uint8_t)(v >> 8), r[C] = (uint8_t)v; break; }
    case 0xD1: { uint16_t v = pop16(); r[D] = (uint8_t)(v >> 8), r[E] = (uint8_t)v; break; }
    case 0xE1: { uint16_t v = pop16(); r[H] = (uint8_t)(v >> 8), r[L] = (uint8_t)v; break; }
    case 0xEB: { uint8_t a = r[D], b = r[E]; r[D] = r[H], r[E] = r[L], r[H] = a, r[L] = b; break; }
    case 0xFE: { uint8_t n = imm(); fz = r[A] == n, fc = r[A] < n; break; }
    case 0xD3: bus_out(imm(), r[A]); break;
    case 0xDB: r[A] = bus_in(imm()); break;
    default:
        printf("команды %02X в процессоре теста нет, адрес %04X\n", op, pc - 1);
        exit(2);
    }
}

// Вызов процедуры SDK. Карта работает между командами Z80, как ядро 0 рядом со Спектрумом
#define ENTRY 0x8000
#define DONE  0xFFF0
static void call(int entry) {
    sp = 0xF000;
    push16(DONE);
    pc = (uint16_t)(ENTRY + entry);
    while (pc != DONE) {
        step();
        steps++;
        if (!no_card && !(steps & 15)) gs_dev_gs.work();
    }
}

enum { E_DETECT = 0, E_CLOSE = 3, E_GET = 6, E_SET = 9, E_SAVE = 12, E_PRESET = 15, E_STAT = 18, E_STAT_RES = 21, E_CURVE = 24, E_VERSION = 27 };

#define TAG 0xC000
#define VAL 0xC010
#define BUF 0xD000

static void tag(const char *t) { memcpy(mem + TAG, t, 4); r[H] = TAG >> 8, r[L] = (uint8_t)TAG; }
static void de_to(uint16_t a) { r[D] = (uint8_t)(a >> 8), r[E] = (uint8_t)a; }
static int mem32(uint16_t a) { return mem[a] | mem[a + 1] << 8 | mem[a + 2] << 16 | mem[a + 3] << 24; }
static int mem16s(uint16_t a) { return (int16_t)(mem[a] | mem[a + 1] << 8); }

static int cfg_set(const char *t, int v) {
    tag(t);
    mem[VAL] = (uint8_t)v, mem[VAL + 1] = (uint8_t)(v >> 8), mem[VAL + 2] = (uint8_t)(v >> 16), mem[VAL + 3] = (uint8_t)(v >> 24);
    de_to(VAL);
    call(E_SET);
    return fc;
}

static int cfg_get(const char *t, int *v) {
    tag(t);
    de_to(BUF);
    call(E_GET);
    *v = mem32(BUF);
    return fc;
}

static int fails;
static void check(const char *what, int got, int want) {
    int ok = got == want;
    printf("%-34s %6d (надо %d) %s\n", what, got, want, ok ? "ok" : "ОШИБКА");
    if (!ok) fails++;
}

static int saves;
static int save_stub(void) { saves++; return 1; }

int main(int argc, char **argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    if (argc < 2) { printf("sdktest sdktest.bin\n"); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { printf("нет %s\n", argv[1]); return 1; }
    size_t n = fread(mem + ENTRY, 1, 0x4000, f);
    fclose(f);
    printf("SDK: %d байт\n", (int)n - 30);

    gs_cfg_defaults();
    gs_core_init();
    gs_ctl_init(save_stub, 7);
    int v;

    // Карты нет
    no_card = 1;
    call(E_DETECT);
    check("нет карты: NC", fc, 0);
    check("нет карты: код", r[A], 1);
    no_card = 0;

    // Канал закрыт: карта не отвечает, программа не виснет
    check("закрыт: запись настройки - NC", cfg_set("MAST", 50), 0);
    check("закрыт: настройка не изменилась", gs_cfg.master_pct, 80);
    gs_core_init();

    call(E_DETECT);
    check("карта есть: C", fc, 1);
    check("карта есть: протокол", r[A], GS_CTL_PROTO);

    de_to(BUF);
    call(E_VERSION);
    check("версия: C", fc, 1);
    check("версия: длина", HL, 3);
    check("версия: прошивка", mem[BUF + 1] | mem[BUF + 2] << 8, 7);

    check("чтение MAST: C", cfg_get("MAST", &v), 1);
    check("чтение MAST", v, 80);
    check("чтение XXXX: NC", cfg_get("XXXX", &v), 0);

    check("запись MAST 50: C", cfg_set("MAST", 50), 1);
    check("запись MAST 50 дошла", gs_cfg.master_pct, 50);
    check("запись MAST 101: NC", cfg_set("MAST", 101), 0);
    check("запись MAST 101: отказ", r[A], 0xFF);
    check("запись RATE: NC", cfg_set("RATE", 44100), 0);
    check("BASS -6: C", cfg_set("BASS", -6), 1);
    check("BASS -6 дошло", gs_cfg.bass_db, -6);
    check("чтение BASS со знаком", (cfg_get("BASS", &v), v), -6);
    check("RCHZ 4 байта: C", cfg_set("RCHZ", 1000000), 1);
    check("чтение RCHZ", (cfg_get("RCHZ", &v), v), 1000000);

    r[A] = GS_MODE_A500;
    call(E_PRESET);
    check("пресет A500: C", fc, 1);
    check("пресет A500 дошёл", gs_cfg.mode, GS_MODE_A500);
    r[A] = 9;
    call(E_PRESET);
    check("пресет 9: NC", fc, 0);

    r[A] = 0xFF;
    de_to(BUF);
    call(E_CURVE);
    check("кривая: C", fc, 1);
    check("кривая: длина", HL, GS_CTL_POINTS * 2);
    check("кривая: A500 завален на верху", mem16s(BUF + 28 * 2) < -100, 1);
    r[A] = 77;
    de_to(BUF);
    call(E_CURVE);
    check("кривая ступени 77: NC", fc, 0);
    check("кривая ступени 77: отказ", r[A], 0xFF);

    gs_stat_reset();
    gs_stat_block(100, 1000);
    gs_stat_block(500, 1000);
    gs_stat_late();
    de_to(BUF);
    call(E_STAT);
    check("статистика: длина", HL, GS_CTL_ST_SIZE);
    check("статистика: пик", mem16s(BUF + GS_CTL_ST_TOP), 500);
    check("статистика: опоздание", mem32(BUF + GS_CTL_ST_LATE), 1);
    check("статистика: режим", mem[BUF + GS_CTL_ST_MODE], GS_MODE_A500);
    call(E_STAT_RES);
    check("сброс статистики: C", fc, 1);

    call(E_SAVE);
    check("запись во флеш: C", fc, 1);
    check("запись во флеш вызвана", saves, 1);

    call(E_CLOSE);
    gs_dev_gs.work();
    check("закрыли: MAST не пишется", cfg_set("MAST", 10), 0);
    check("закрыли: нет ответа", r[A], 1);
    check("закрыли: как было", gs_cfg.master_pct, 50);

    printf(fails ? "ОШИБОК: %d\n" : "ВСЁ OK\n", fails);
    return fails != 0;
}
