// Машина Atari на ПК для теста плеера (sndh.h): записи заранее известны. Файл - "MOCK" + число песен.
// Кадр k: тик 0 - R8 = k & 15 (громкость - видно позицию), тик 500 (из таймера) - R0 = k; R13 - только в кадре 0.
#include <string.h>
#include "sndh.h"
#include "sim.h"

uint32_t sndh_lost;
void (*sndh_mark)(int step);
int      sim_sndh_crash_at = -1;        // кадр, где машина «падает» (-1 - нет)
int      sim_sndh_opens;                // сколько раз открывали (перемотка назад - с начала)
static int frame = -1;                  // -1 - не открыта
static int songs;

bool sndh_open(const void *file, uint32_t size, int subsong, sndh_info_t *info) {
    const uint8_t *f = (const uint8_t *)file;
    frame = -1;
    if (!f || size < 5 || memcmp(f, "MOCK", 4) || subsong < 1 || subsong > f[4]) return false;
    songs = f[4], frame = 0, sndh_lost = 0;
    sim_sndh_opens++;
    if (info) strcpy(info->name, "Mock"), strcpy(info->author, "Test"), info->subsongs = songs, info->frames = 100;
    return true;
}

int sndh_frame(sndh_wr_t *out, int max) {
    if (frame < 0 || frame == sim_sndh_crash_at || max < 3) return -1;
    int n = 0;
    out[n].t = 0, out[n].reg = 8, out[n].val = (uint8_t)(frame & 15), n++;
    if (frame == 0) out[n].t = 0, out[n].reg = 13, out[n].val = 0x0E, n++;
    out[n].t = 500 | SNDH_WR_TIMER, out[n].reg = 0, out[n].val = (uint8_t)frame, n++;
    frame++;
    return n;
}

void sndh_close(void) { frame = -1; }
void sndh_arena(void *base, uint32_t size) { (void)base, (void)size; }
