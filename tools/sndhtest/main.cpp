// sndhtest: проверка engine/sndh на ПК. Звук оригинала AtariAudio (YM, без STE) против звука того же эмулятора YM,
// которому подали только наши записи с временем. Совпало до сэмпла - время и порядок записей верные.
// sndhtest файл.sndh... [-s секунд]
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sndh.h"
#include "AtariAudio.h"

#define WR_MAX 8192                     // записей на кадр с запасом (тяжёлые - до ~1000)
#define DEAD_MARK 12345                 // в буфере оригинала: машина упала - AudioRender его не тронул

static sndh_wr_t wr[WR_MAX];
static int16_t   ref[SNDH_FRAME_TICKS];
static uint8_t   *arena;                // область обёртки, как PSRAM на RP (8 МБ)
#define ARENA_SIZE (8u << 20)
static uint32_t  hash;                  // FNV всех записей: сравнить сборки (например, ужатое ядро 68000 и обычное)

static void *load(const char *path, uint32_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *d = malloc(n);
    *size = (uint32_t)fread(d, 1, n, f);
    fclose(f);
    return d;
}

// Записи кадра -> эмулятор YM по тикам, как в машине: записи плеера - до тика, таймеров - после; сравнить с ref.
// -> первый несовпавший тик или -1
static int replay(Ym2149c &ym, int n, uint32_t *top) {
    int i = 0, bad = -1;
    for (int t = 0; t < SNDH_FRAME_TICKS; t++) {
        for (; i < n && wr[i].t == t; i++) ym.WritePort(0, wr[i].reg), ym.WritePort(2, wr[i].val);
        int16_t s = ym.ComputeNextSample();
        if (s != ref[t] && bad < 0) bad = t;
        if (i < n && wr[i].t == (t | SNDH_WR_TIMER)) {
            ym.InsideTimerIrq(true);
            for (; i < n && wr[i].t == (t | SNDH_WR_TIMER); i++) ym.WritePort(0, wr[i].reg), ym.WritePort(2, wr[i].val);
            ym.InsideTimerIrq(false);
        }
    }
    if (i != n) bad = SNDH_FRAME_TICKS;                         // записи не по порядку времени
    if ((uint32_t)n > *top) *top = n;
    return bad;
}

static int test(const char *path, int sec) {
    uint32_t size;
    void *d = load(path, &size);
    const char *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
    if (!d) { printf("%-40s нет файла\n", name); return 1; }
    sndh_info_t info;
    AtariAudioRenderer *orig = AtariAudioRenderer::Create(d, size, SNDH_TICK_HZ);
    if (!orig || !orig->InitSubSong(1, true) || !sndh_open(d, size, 1, &info)) {
        printf("%-40s не открылся\n", name);
        free(d);
        return 1;
    }
    orig->MuteVoices(1 << 3);                                   // STE DMA - мимо: на AY его нет
    Ym2149c ym;
    ym.Reset(SNDH_TICK_HZ, 2000000, true);
    uint32_t top = 0, total = 0;
    hash = 2166136261u;
    int bad_frame = -1, bad_tick = 0;
    int dead = -1;                                              // кадр, где машина упала (-1 - живая)
    for (int f = 0; f < sec * 50 && bad_frame < 0 && dead < 0; f++) {
        for (int t = 0; t < SNDH_FRAME_TICKS; t++) ref[t] = DEAD_MARK;
        orig->AudioRender(ref, SNDH_FRAME_TICKS);
        int n = sndh_frame(wr, WR_MAX);
        if (n < 0) {                                            // упала у нас - оригинал обязан упасть в том же кадре
            dead = f;
            if (ref[SNDH_FRAME_TICKS - 1] != DEAD_MARK) bad_frame = f, bad_tick = -1;
            break;
        }
        total += n;
        for (int i = 0; i < n; i++) hash = (hash ^ (wr[i].t | (uint32_t)wr[i].reg << 16 | (uint32_t)wr[i].val << 24)) * 16777619u;
        int b = replay(ym, n, &top);
        if (b >= 0) bad_frame = f, bad_tick = b;
    }
    if (bad_frame >= 0 && bad_tick < 0)
        printf("%-40s РАЗНОЕ: у нас машина упала в кадре %d, у оригинала нет\n", name, bad_frame);
    else if (dead >= 0)
        printf("%-40s УПАЛ  в AtariAudio, кадр %d (оригинал тоже) - карта видит: sndh_frame = -1\n", name, dead);
    else if (bad_frame < 0)
        printf("%-40s OK    %d с, записей %u, на кадр до %u, потеряно %u, хэш %08x\n", name, sec, total, top, sndh_lost, hash);
    else
        printf("%-40s РАЗНЫЙ звук: кадр %d, тик %d (записей до этого %u)\n", name, bad_frame, bad_tick, total);
    sndh_close();
    AtariAudioRenderer::Destroy(orig);
    free(d);
    return bad_frame >= 0;
}

int main(int argc, char **argv) {
    int sec = 60, fails = 0, files = 0;
    arena = (uint8_t *)malloc(ARENA_SIZE);
    sndh_arena(arena, ARENA_SIZE);                              // обёртка - из своей области, оригинал - из кучи
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-s") && i + 1 < argc) sec = atoi(argv[++i]);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-s")) { i++; continue; }
        fails += test(argv[i], sec);
        files++;
    }
    printf("\n%s: файлов %d, не совпало %d\n", fails ? "FAIL" : "OK", files, fails);
    return fails;
}
