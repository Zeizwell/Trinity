// Клавиши: b - режимы, v - голоса, f - формат I2S, x - обмен как у Z80, s - нагрузка, 1..4 - режим, w - сохранить настройки, c - показать
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "config.h"
#include "cfgstore.h"
#include "psram.h"
#include "audio.h"
#include "bus.h"
#include "bench.h"
#include "gs/gs_core.h"
#include "gs/gs_cfg.h"
#include "gs/gs_dev_gs.h"
#include "gs/gs_chan.h"

__asm__(".section .rodata\n.global mod_bin\nmod_bin:\n.incbin \"" TEST_MOD "\"\n.global mod_end\nmod_end:\n.text\n");
extern const uint8_t mod_bin[], mod_end[];
extern const gs_dev_t gs_dev_gs;

static float block[AUDIO_FRAMES * 2];
#define BLOCKS 400

static uint32_t budget_us(void) { return AUDIO_FRAMES * 1000000u / gs_cfg.rate_hz; }

static void bench_mode(const void *mod, int size, gs_mode_t mode) {
    gs_core_init();
    gs_cfg_mode(mode);
    if (!gs_gs_play_mod(mod, size)) { printf("не модуль\n"); return; }
    uint64_t sum = 0;
    uint32_t top = 0;
    for (int i = 0; i < BLOCKS; i++) {
        uint64_t t = time_us_64();
        gs_core_audio(block, AUDIO_FRAMES);
        uint32_t d = (uint32_t)(time_us_64() - t);
        sum += d;
        if (d > top) top = d;
    }
    gs_gs_stop();
    uint32_t avg = (uint32_t)(sum / BLOCKS);
    printf("%-12s среднее %5lu мкс (%3lu%%), худшее %5lu мкс (%3lu%%), бюджет %lu\n", gs_cfg_mode_name(mode),
           (unsigned long)avg, (unsigned long)(avg * 100 / budget_us()), (unsigned long)top, (unsigned long)(top * 100 / budget_us()), (unsigned long)budget_us());
}

// Голоса без модуля: сэмпл по кругу, ~16.8 кГц, тяжёлый случай по фронтам и выборке
static void bench_voices(const uint8_t *src, uint32_t len, gs_mode_t mode) {
    for (int voices = 4; voices <= 32; voices *= 2) {
        gs_core_init();
        gs_cfg_mode(mode);
        gs_cfg.voices = (uint8_t)voices;
        gs_core_claim(&gs_dev_gs);
        for (int c = 0; c < voices; c++) {
            gs_voice_t *v = &gs_voice[c];
            v->len = len, v->loop_start = 0, v->loop_len = len;
            v->vol = 40;
            gs_voice_set_inc(v, 0.35f + c * 0.001f);
            gs_voice_set_pos(v, (float)(c * 3001 % (len / 2)));
            v->data = (const int8_t *)src;
        }
        uint64_t sum = 0;
        for (int i = 0; i < 200; i++) {
            uint64_t t = time_us_64();
            gs_core_audio(block, AUDIO_FRAMES);
            sum += time_us_64() - t;
        }
        uint32_t avg = (uint32_t)(sum / 200);
        printf("%-12s %2d голосов: %5lu мкс (%3lu%%)\n", gs_cfg_mode_name(mode), voices, (unsigned long)avg, (unsigned long)(avg * 100 / budget_us()));
    }
}

// Замеры считают на ядре 0: ядро 1 на это время молчит и движок не трогает. Настройки потом - как были
static gs_cfg_t kept;
static void offline_begin(void) {
    kept = gs_cfg;
    audio_hold = 1;
    sleep_ms(20);
}
static void offline_end(void) {
    gs_cfg = kept;
    gs_core_init();
    audio_hold = 0;
    printf("готово\n");
}

// Проверка формата без ЦАП: счётчик с ножек, как его принял бы ЦАП
static void check_format(void) {
    enum { N = 256 };
    static uint32_t w[N];
    audio_pattern = 1;
    sleep_ms(30);
    int got = audio_check(w, N), bad = 0;
    audio_pattern = 0;
    for (int i = 0; i + 2 < got; i += 2) {
        if (w[i] >> 24 != 0x11 || w[i + 1] >> 24 != 0x22) bad++;
        else if ((w[i] & 0xFFFFFF) != (w[i + 1] & 0xFFFFFF) || ((w[i] + 1) & 0xFFFFFF) != (w[i + 2] & 0xFFFFFF)) bad++;
    }
    printf("формат I2S: слов %d, плохих кадров %d\n", got, bad);
}

// Живой звук: блоки за секунду, частота LCK с ножки, нагрузка
static void live_stat(void) {
    uint32_t b0 = audio_stat.blocks, edges = 0;
    audio_stat.top_us = 0;
    uint64_t end = time_us_64() + 1000000;
    bool was = gpio_get(PIN_LCK);
    while (time_us_64() < end) {
        bool now = gpio_get(PIN_LCK);
        if (now && !was) edges++;
        was = now;
    }
    printf("%-12s блоков/с %lu, LCK %lu Гц, блок %lu мкс (%lu%%), худший %lu мкс (%lu%%), опозданий %lu\n", gs_cfg_mode_name(gs_cfg.mode),
           (unsigned long)(audio_stat.blocks - b0), (unsigned long)edges, (unsigned long)audio_stat.last_us, (unsigned long)(audio_stat.last_us * 100 / budget_us()),
           (unsigned long)audio_stat.top_us, (unsigned long)(audio_stat.top_us * 100 / budget_us()), (unsigned long)audio_stat.late);
}

// Обмен как у Z80, через коре и его таблицу кодов: те же действия шины, ядро 0 работает между ними, звук идёт на ядре 1
static void pump(void) { gs_core_work(); bus_answer(); }
static void wait_cmd(void) { while (gs_core_port_peek(GS_CODE_BB) & 1) pump(); }
static void z80_sc(uint8_t c) { bus_inject_wr(GS_CODE_BB, c); wait_cmd(); }
static void z80_sd(uint8_t v) { bus_inject_wr(GS_CODE_B3, v); }
static int z80_gd(void) { while (!(gs_core_port_peek(GS_CODE_BB) & 0x80)) pump(); return bus_inject_rd(GS_CODE_B3); }

static void exchange(void) {
    const uint8_t *mod = mod_bin;
    int size = (int)(mod_end - mod_bin);
    uint32_t late0 = audio_stat.late;
    z80_sc(0x30);
    int h = bus_inject_rd(GS_CODE_B3);
    z80_sc(0xD1);
    uint64_t t = time_us_64();
    uint32_t waits = 0;
    for (int i = 0; i < size; i++) {
        while (gs_core_port_peek(GS_CODE_BB) & 0x80) { waits++; pump(); }
        z80_sd(mod[i]);
        if ((i & 255) == 255) pump();
    }
    z80_sc(0xD2);
    uint32_t us = (uint32_t)(time_us_64() - t);
    int bad = 0;
    for (int i = 0; i < size; i++) if (PSRAM_BASE[i] != mod[i]) bad++;
    printf("загрузка: номер %d, %d байт, %lu нс на байт, ждал %lu раз, не совпало %d\n", h, size,
           (unsigned long)((uint64_t)us * 1000 / (unsigned)size), (unsigned long)waits, bad);
    z80_sd(0);
    z80_sc(0x31);
    printf("#31 ответ %d\n", bus_inject_rd(GS_CODE_B3));
    sleep_ms(1500);
    z80_sc(0x60); int song = z80_gd();
    z80_sc(0x61); int row = z80_gd();
    z80_sc(0x20); int r0 = z80_gd(), r1 = z80_gd(), r2 = z80_gd();
    printf("позиция %d, строка %d, память %d, опозданий %lu\n", song, row, r0 | r1 << 8 | r2 << 16, (unsigned long)(audio_stat.late - late0));
    printf("обработчик шины: самый долгий %lu тактов (%lu нс), бюджет 850 нс\n", (unsigned long)bus_irq_top(), (unsigned long)(bus_irq_top() * 1000 / (gs_cfg.sys_khz / 1000)));
    printf("OPL4: порт #C4 отвечает %02X\n", gs_core_port_peek(OPL4_CODE_C4));
    printf("готово\n");
}

static void show_cfg(void) {
    printf("настройки: режим %s, вывод %lu Гц, такт %lu кГц, память GS %lu КБ, НЧ %d ВЧ %d, громкость %d%%\n", gs_cfg_mode_name(gs_cfg.mode), (unsigned long)gs_cfg.rate_hz,
           (unsigned long)gs_cfg.sys_khz, (unsigned long)gs_cfg.gs_ram_kb, gs_cfg.bass_db, gs_cfg.treble_db, gs_cfg.master_pct);
    printf("готово\n");
}

// Сохранение: проверить, что PSRAM пережила запись во флеш (та же шина QSPI)
static void save_cfg(void) {
    static uint8_t keep[4096];                      // это память GS: вернуть как было
    volatile uint8_t *p = PSRAM_BASE + (7u << 20);
    for (int i = 0; i < 4096; i++) keep[i] = p[i], p[i] = (uint8_t)(i * 7);
    uint32_t late0 = audio_stat.late;
    uint64_t t = time_us_64();
    int ok = cfgstore_save();
    uint32_t us = (uint32_t)(time_us_64() - t);
    int bad = 0;
    for (int i = 0; i < 4096; i++) { if (p[i] != (uint8_t)(i * 7)) bad++; p[i] = keep[i]; }
    printf("сохранение: %s, %lu мс, PSRAM не совпало %d, опозданий звука %lu\n", ok ? "да" : "нет", (unsigned long)(us / 1000), bad, (unsigned long)(audio_stat.late - late0));
    printf("готово\n");
}

void bench_key(void) {
    int c = getchar_timeout_us(0);
    if (c == PICO_ERROR_TIMEOUT) return;
    int size = (int)(mod_end - mod_bin);
    switch (c) {
    case 'b':
        offline_begin();
        printf("такт %lu МГц, модуль %d байт\n", (unsigned long)(clock_get_hz(clk_sys) / 1000000), size);
        for (int m = GS_MODE_GS; m <= GS_MODE_HIFI; m++) bench_mode(mod_bin, size, (gs_mode_t)m);
        offline_end();
        break;
    case 'v':
        offline_begin();
        for (int m = GS_MODE_GS; m <= GS_MODE_HIFI; m++) bench_voices(mod_bin + 1084, 90000, (gs_mode_t)m);
        memcpy(PSRAM_BASE, mod_bin, (size_t)size);
        printf("из PSRAM:\n");
        for (int m = GS_MODE_GS; m <= GS_MODE_HIFI; m++) bench_voices(PSRAM_BASE + 1084, 90000, (gs_mode_t)m);
        offline_end();
        break;
    case 'f': check_format(); printf("готово\n"); break;
    case 'x': exchange(); break;
    case 's': live_stat(); printf("готово\n"); break;
    case '1': case '2': case '3': case '4': gs_core_set_mode((gs_mode_t)(c - '1')); show_cfg(); break;
    case 'w': save_cfg(); break;
    case 'c': show_cfg(); break;
    default: break;
    }
}
