// RP-GS, шаг 1: замер движка. Ножки шины не трогаем: все входы, на плате макета они заняты RP-AY
// Клавиши в консоли: b - замер, модуль в ОЗУ; p - PSRAM (ID, тест, замер, модуль там), v - голоса, ОЗУ и PSRAM; i - живой звук на двух ядрах
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "psram.h"
#include "audio.h"
#include "hardware/gpio.h"
#include "gs/gs_core.h"
#include "gs/gs_cfg.h"
#include "gs/gs_dev_gs.h"
#include "gs/gs_filter.h"
#include "gs/gs_chan.h"

__asm__(".section .rodata\n.global mod_bin\nmod_bin:\n.incbin \"" TEST_MOD "\"\n.global mod_end\nmod_end:\n.text\n");
extern const uint8_t mod_bin[], mod_end[];
extern const gs_dev_t gs_dev_gs;

#define FRAMES 256              // блок DMA: 5.33 мс при 48 кГц
#define BLOCKS 400
static float block[FRAMES * 2];

// Один режим: среднее и худшее время блока, доля бюджета
static void bench(const void *mod, int size, gs_mode_t mode) {
    gs_core_init();
    gs_cfg_mode(mode);
    if (!gs_gs_play_mod(mod, size)) { printf("не модуль\n"); return; }
    uint64_t sum = 0;
    uint32_t top = 0;
    for (int i = 0; i < BLOCKS; i++) {
        uint64_t t = time_us_64();
        gs_core_audio(block, FRAMES);
        uint32_t d = (uint32_t)(time_us_64() - t);
        sum += d;
        if (d > top) top = d;
    }
    gs_gs_stop();
    uint32_t avg = (uint32_t)(sum / BLOCKS), budget = FRAMES * 1000000u / gs_cfg.rate_hz;
    printf("%-12s среднее %5lu мкс (%3lu%%), худшее %5lu мкс (%3lu%%), бюджет %lu\n", gs_cfg_mode_name(mode),
           (unsigned long)avg, (unsigned long)(avg * 100 / budget), (unsigned long)top, (unsigned long)(top * 100 / budget), (unsigned long)budget);
}

// Голоса без модуля: сэмпл по кругу, ~16.8 кГц, худший случай по фронтам и выборке
static void bench_voices(const uint8_t *src, uint32_t len, int mode) {
    for (int voices = 4; voices <= 32; voices *= 2) {
        gs_core_init();
        gs_cfg_mode((gs_mode_t)mode);
        gs_cfg.voices = voices;
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
            gs_core_audio(block, FRAMES);
            sum += time_us_64() - t;
        }
        uint32_t avg = (uint32_t)(sum / 200);
        printf("%-12s %2d голосов: %5lu мкс (%3lu%%)\n", gs_cfg_mode_name((gs_mode_t)mode), voices, (unsigned long)avg, (unsigned long)(avg * 100 / (FRAMES * 1000000u / gs_cfg.rate_hz)));
    }
}

static void bench_all(const void *mod, int size) {
    printf("частота %lu МГц, модуль %d байт\n", (unsigned long)(clock_get_hz(clk_sys) / 1000000), size);
    for (int m = GS_MODE_GS; m <= GS_MODE_HIFI; m++) bench(mod, size, (gs_mode_t)m);
    printf("готово\n");
}

// Проверка формата без ЦАП: счётчик с ножек, как его принял бы ЦАП
static void check_format(void) {
    enum { N = 256 };
    static uint32_t w[N];
    audio_pattern = 1;
    sleep_ms(30);                                   // уйти от буферов со звуком
    int got = audio_check(w, N), bad = 0;
    audio_pattern = 0;
    for (int i = 0; i + 2 < got; i += 2) {
        if (w[i] >> 24 != 0x11 || w[i + 1] >> 24 != 0x22) bad++;
        else if ((w[i] & 0xFFFFFF) != (w[i + 1] & 0xFFFFFF) || ((w[i] + 1) & 0xFFFFFF) != (w[i + 2] & 0xFFFFFF)) bad++;
    }
    printf("формат I2S: слов %d, плохих кадров %d, первое L %08lX R %08lX\n", got, bad, (unsigned long)w[0], (unsigned long)w[1]);
}

// Живой звук: сколько блоков за секунду, частота LCK с ножки, нагрузка
static void live_stat(void) {
    uint32_t b0 = audio_stat.blocks, edges = 0;
    audio_stat.top_us = 0;
    uint64_t end = time_us_64() + 1000000;
    bool was = gpio_get(16);
    while (time_us_64() < end) {
        bool now = gpio_get(16);
        if (now && !was) edges++;
        was = now;
    }
    uint32_t budget = AUDIO_FRAMES * 1000000u / gs_cfg.rate_hz;
    printf("%-12s блоков/с %lu, LCK %lu Гц, блок %lu мкс (%lu%%), худший %lu мкс (%lu%%), опозданий %lu\n", gs_cfg_mode_name(gs_cfg.mode),
           (unsigned long)(audio_stat.blocks - b0), (unsigned long)edges, (unsigned long)audio_stat.last_us, (unsigned long)(audio_stat.last_us * 100 / budget),
           (unsigned long)audio_stat.top_us, (unsigned long)(audio_stat.top_us * 100 / budget), (unsigned long)audio_stat.late);
}

// Модуль из PSRAM на двух ядрах. Назад в замеры - только перезапуск
static void live(const uint8_t *mod, int size) {
    printf("PSRAM id %06lX\n", (unsigned long)psram_init());
    memcpy(PSRAM_BASE, mod, (size_t)size);
    gs_core_init();
    gs_gs_play_mod(PSRAM_BASE, size);
    audio_start();
    sleep_ms(100);
    check_format();
    for (int m = GS_MODE_GS; m <= GS_MODE_HIFI; m++) {
        gs_cfg_mode((gs_mode_t)m);
        sleep_ms(100);
        live_stat();
    }
    gs_cfg_mode(GS_MODE_A1200);
    printf("готово\n");
    for (;;) { live_stat(); sleep_ms(4000); }
}

int main(void) {
    stdio_init_all();
    int size = (int)(mod_end - mod_bin);
    for (;;) {
        int c = getchar_timeout_us(1000000);
        if (c == 'i') live(mod_bin, size);
        if (c == 'b') bench_all(mod_bin, size);
        else if (c == 'v') {
            int s0 = size > 90000 ? 90000 : size;
            for (int m = GS_MODE_GS; m <= GS_MODE_HIFI; m++) bench_voices(mod_bin + 1084, (uint32_t)s0, m);
            printf("PSRAM id %06lX\n", (unsigned long)psram_init());
            memcpy(PSRAM_BASE, mod_bin, (size_t)size);
            for (int m = GS_MODE_GS; m <= GS_MODE_HIFI; m++) bench_voices(PSRAM_BASE + 1084, (uint32_t)s0, m);
            printf("готово\n");
        }
        else if (c == 'p') {
            printf("PSRAM id %06lX\n", (unsigned long)psram_init());
            printf("тест 1 МБ: не совпало %lu\n", (unsigned long)psram_test(1u << 20));
            memcpy(PSRAM_BASE, mod_bin, (size_t)size);
            bench_all(PSRAM_BASE, size);
        } else if (c == PICO_ERROR_TIMEOUT) printf("rp-gs: b, p, v, i\n");
    }
}
