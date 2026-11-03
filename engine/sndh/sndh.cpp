// SNDH-плеер на живом AY (sndh.h): AtariAudio без синтеза, записи в YM ловит крючок g_ym2149WriteHook
#include <string.h>
#include "sndh.h"
#include "AtariAudio.h"

#define SNDH_REGS  16                   // регистры YM: инициализация песни - последнее значение каждого
#define SNDH_ALIVE 1                    // метка в буфере сэмпла: живая машина без синтеза пишет туда 0

uint32_t sndh_lost;

static AtariAudioRenderer *ar;          // машина открытого файла (nullptr - нет)
static uint8_t   init_val[SNDH_REGS];   // записи инициализации, ещё не отданные: значение регистра
static uint32_t  init_set;              // биты - какие регистры записаны
static bool      in_init;               // идёт инициализация песни
static sndh_wr_t *sink;                 // куда крючок кладёт записи кадра
static int       sink_n, sink_max;
static uint16_t  tick;                  // текущий тик в кадре
static uint8_t   *arena;                // большие блоки (sndh_arena): база, размер, занято
static uint32_t  arena_size, arena_used;
static void *(*old_alloc)(size_t);     // крючки памяти до своего вызова
static void (*old_free)(void *);

// Большие блоки AtariAudio - подряд из области (освобождать по одному не нужно: всё уходит в sndh_close)
static void *arena_alloc(size_t size) {
    size = (size + 15) & ~(size_t)15;
    if (arena_used + size > arena_size) return nullptr;
    void *p = arena + arena_used;
    arena_used += (uint32_t)size;
    return p;
}

static void arena_free(void *p) { (void)p; }

void sndh_arena(void *base, uint32_t size) {
    arena = (uint8_t *)base, arena_size = size, arena_used = 0;
}

// Большие блоки - из области только на время своих вызовов (машина берёт память лениво, в InitSubSong): на ПК рядом
// может работать обычная AtariAudio из кучи (тест). Область не задана - куча.
static void mem_on(void) {
    old_alloc = g_aaBigAlloc, old_free = g_aaBigFree;
    if (arena) g_aaBigAlloc = arena_alloc, g_aaBigFree = arena_free;
}

static void mem_off(void) {
    g_aaBigAlloc = old_alloc, g_aaBigFree = old_free;
}

// Крючок AtariAudio: запись в YM -> в список с временем (из таймера - флаг)
static void on_write(int reg, uint8_t value, bool inTimer) {
    if (in_init) {                                              // до звука: важно только последнее значение
        if ((unsigned)reg < SNDH_REGS) init_val[reg] = value, init_set |= 1u << reg;
        return;
    }
    if (sink_n >= sink_max) { sndh_lost++; return; }
    sink[sink_n].t = (uint16_t)(tick | (inTimer ? SNDH_WR_TIMER : 0));
    sink[sink_n].reg = (uint8_t)reg;
    sink[sink_n].val = value;
    sink_n++;
}

// Крючок и «без синтеза» - только на время своих вызовов: на ПК рядом может играть обычная AtariAudio (тест)
static void hook_on(sndh_wr_t *out, int max) {
    sink = out, sink_n = 0, sink_max = max;
    g_ym2149WriteHook = on_write;
    g_atariNoSynth = true;
}

static void hook_off(void) {
    g_ym2149WriteHook = nullptr;
    g_atariNoSynth = false;
}

static void copy_str(char *d, const char *s, int n) {
    strncpy(d, s ? s : "", n - 1);
    d[n - 1] = 0;
}

bool sndh_open(const void *file, uint32_t size, int subsong, sndh_info_t *info) {
    sndh_close();
    sndh_lost = 0;
    mem_on();
    ar = AtariAudioRenderer::Create(file, size, SNDH_TICK_HZ);
    mem_off();
    if (!ar) return false;
    const AtariAudioRenderer::SongInfo &si = ar->GetSongInfo();
    if (subsong < 1 || subsong > si.subsongCount) { sndh_close(); return false; }
    in_init = true;                                             // записи инициализации - в начало первого кадра
    hook_on(nullptr, 0);
    mem_on();                                                   // память Atari машина берёт здесь
    bool ok = ar->InitSubSong(subsong, true);
    mem_off();
    hook_off();
    in_init = false;
    if (!ok) { sndh_close(); return false; }
    if (info) {
        copy_str(info->name, si.musicName, sizeof info->name);
        copy_str(info->author, si.musicAuthor, sizeof info->author);
        info->subsongs = si.subsongCount;
        info->frames = ar->GetSubsongDurationSample(subsong) / SNDH_FRAME_TICKS;
    }
    return true;
}

int sndh_frame(sndh_wr_t *out, int max) {
    if (!ar) return -1;
    int n = 0;
    for (int r = 0; r < SNDH_REGS; r++)                         // инициализация - тик 0, до плеера кадра
        if (init_set >> r & 1) {
            if (n < max) out[n].t = 0, out[n].reg = (uint8_t)r, out[n].val = init_val[r], n++;
            else sndh_lost++;
        }
    init_set = 0;
    hook_on(out + n, max - n);
    bool alive = true;
    for (tick = 0; tick < SNDH_FRAME_TICKS && alive; tick++) {   // по тику: плеер кадра (если пора), тик таймеров
        int16_t s = SNDH_ALIVE;
        ar->AudioRender(&s, 1);
        alive = s != SNDH_ALIVE;                                  // машина упала - AudioRender ничего не пишет
    }
    n += sink_n;
    hook_off();
    return alive ? n : -1;
}

void sndh_close(void) {
    mem_on();
    if (ar) AtariAudioRenderer::Destroy(ar);
    mem_off();
    ar = nullptr;
    init_set = 0;
    arena_used = 0;                                             // область снова пустая
}
