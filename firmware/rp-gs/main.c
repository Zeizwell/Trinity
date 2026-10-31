// RP-GS, шаг 1: замер движка. Ножки шины не трогаем: все входы, на плате макета они заняты RP-AY
// Клавиши в консоли: b - замер, модуль в ОЗУ; p - PSRAM (ID, тест, замер, модуль там)
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "psram.h"
#include "gs/gs_core.h"
#include "gs/gs_cfg.h"
#include "gs/gs_dev_gs.h"
#include "gs/gs_filter.h"

__asm__(".section .rodata\n.global mod_bin\nmod_bin:\n.incbin \"" TEST_MOD "\"\n.global mod_end\nmod_end:\n.text\n");
extern const uint8_t mod_bin[], mod_end[];

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

static void bench_all(const void *mod, int size) {
    printf("частота %lu МГц, модуль %d байт\n", (unsigned long)(clock_get_hz(clk_sys) / 1000000), size);
    for (int m = GS_MODE_GS; m <= GS_MODE_HIFI; m++) bench(mod, size, (gs_mode_t)m);
    printf("готово\n");
}

int main(void) {
    stdio_init_all();
    int size = (int)(mod_end - mod_bin);
    for (;;) {
        int c = getchar_timeout_us(1000000);
        if (c == 'b') bench_all(mod_bin, size);
        else if (c == 'p') {
            printf("PSRAM id %06lX\n", (unsigned long)psram_init());
            printf("тест 1 МБ: не совпало %lu\n", (unsigned long)psram_test(1u << 20));
            memcpy(PSRAM_BASE, mod_bin, (size_t)size);
            bench_all(PSRAM_BASE, size);
        } else if (c == PICO_ERROR_TIMEOUT) printf("rp-gs: b, p\n");
    }
}
