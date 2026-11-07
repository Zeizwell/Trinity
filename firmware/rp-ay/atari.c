// Самоплеер Atari (atari.h). Кольцо кадров: кладёт ядро 0 (at_put), выводит ядро 1 (at_get). Целиком кольцо
// (перемотка) ядро 0 правит, только когда ядро 1 стоит: at_hold -> ядро 1 глушит чип и ставит at_held.
#include <string.h>
#include <stdio.h>
#include "atari.h"
#include "sndh.h"
#include "card.h"
#include "chip.h"
#include "modes.h"
#include "psram.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "wd.h"

#define AT_FRAMES   3                       // кадров в кольце: один играет, два посчитаны вперёд
#define AT_WR_MAX   1536                    // записей на кадр (по архиву - до ~1400)
#define AT_TICK_US  (1000000 / SNDH_TICK_HZ)        // тик записи - 20 мкс
#define AT_FRAME_US (AT_TICK_US * SNDH_FRAME_TICKS) // кадр - 20 мс
#define AT_LATE_US  2000                    // кадр пришёл позже срока больше чем на это - время от «сейчас»
#define AT_SEEK_US  10000                   // перемотка: счёт кадров за один шаг задания, мкс
#define AT_PHIM_HZ  4000000                 // такт чипа: YM Atari 2 МГц = SSG YM2203 на φM / 2
#define AT_STAT     0x54415453u             // 'STAT' - слот состояния плеера
#define AT_SNDH     0x48444E53u             // 'SNDH' - слот входа

// ---- кольцо: ядро 0 -> ядро 1
static sndh_wr_t at_wr[AT_FRAMES][AT_WR_MAX];
static uint16_t  at_n[AT_FRAMES];
static volatile uint32_t at_put, at_get;    // кадров положено / выведено
static volatile bool at_hold = true;        // ядро 0: стой (пауза, перемотка, стоп, ждём режим)
static volatile bool at_held;               // ядро 1: стою, чип молчит
static volatile uint32_t at_late;           // кадр не успел к сроку

// ---- ядро 1
static bool     at_run;                     // кадр at_get выводится
static bool     at_sync;                    // время кадра - от «сейчас» (первый кадр, после стоянки)
static uint32_t at_t0;                      // мкс: начало кадра
static uint16_t at_i;                       // следующая запись кадра
static uint8_t  at_reg[16];                 // что записали в чип (вернуть после паузы)

// ---- ядро 0
enum { AT_WAIT, AT_PLAY };
static bool     at_on;                      // плеер идёт (с PLAY до конца задания)
static uint8_t  at_phase;                   // AT_WAIT - ждём режим ATARI, AT_PLAY - играем
static uint32_t at_frame;                   // кадров посчитано (позиция машины, впереди вывода на кольцо)
static uint32_t at_seek;                    // перемотка: на какой кадр (UINT32_MAX - нет)
static bool     at_pause;
static uint8_t  at_sreg[16];                // перемотка: последние значения регистров
static uint16_t at_sset;                    //   биты - какие записаны
static uint8_t  at_song;
static sndh_info_t at_info;
static uint16_t at_load, at_peak;           // мкс счёта кадра: последний, пик

// ================= ядро 1: вывод =================

// мкс до срока, 1..CARD_SOUND_MAX_US
static uint32_t at_wait_us(uint32_t due, uint32_t now) {
    int32_t d = (int32_t)(due - now);
    if (d < 1) return 1;
    return d > CARD_SOUND_MAX_US ? CARD_SOUND_MAX_US : (uint32_t)d;
}

uint32_t atari_sound(uint32_t now) {
    if (at_hold) {                                              // стоим: громкости 0, кадр - заново
        if (!at_held) {
            chip_put(0, 8, 0), chip_put(0, 9, 0), chip_put(0, 10, 0);
            at_run = false, at_sync = true;
            at_held = true;
        }
        return CARD_SOUND_MAX_US;
    }
    if (at_held) {                                              // дальше: регистры как были (R13 - нет: перезапуск
        for (uint8_t r = 0; r < 13; r++) chip_put(0, r, at_reg[r]);   //   огибающей)
        at_held = false;
    }
    for (;;) {
        if (!at_run) {                                          // следующий кадр
            if (at_get == at_put) return CARD_SOUND_MAX_US;     // не готов (опоздание - когда придёт)
            __dmb();                                            // кадр читать после at_put
            if (at_sync || (int32_t)(now - at_t0) > AT_LATE_US) {
                if (!at_sync) at_late++;
                at_t0 = now, at_sync = false;
            }
            at_run = true, at_i = 0;
        }
        uint32_t f = at_get % AT_FRAMES;
        const sndh_wr_t *w = at_wr[f];
        while (at_i < at_n[f]) {                                // записи, чей срок пришёл
            uint32_t due = at_t0 + (uint32_t)(w[at_i].t & ~SNDH_WR_TIMER) * AT_TICK_US;
            if ((int32_t)(now - due) < 0) return at_wait_us(due, now);
            if (w[at_i].reg < 14) {                             // 14, 15 - порты, не звук
                at_reg[w[at_i].reg] = w[at_i].val;
                chip_put(0, w[at_i].reg, w[at_i].val);
            }
            at_i++;
        }
        uint32_t end = at_t0 + AT_FRAME_US;                     // кадр выведен - ждём его конца
        if ((int32_t)(now - end) < 0) return at_wait_us(end, now);
        at_t0 = end, at_run = false;
        at_get = at_get + 1;
    }
}

// ================= ядро 0: машина и команды =================

static void wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, v), wr16(p + 2, v >> 16); }

// Открыть песню из слота входа: машина - в PSRAM
static void at_mark(int step) { WD_AT(30 + step); }        // метка сторожа: шаг sndh_open (wd.h)

static bool at_open(svc_t *s, svc_job_t *j) {
    sndh_mark = at_mark;
    sndh_arena(PSRAM_BASE + PSRAM_ATARI, PSRAM_ATARI_SIZE);
    return sndh_open(svc_slot_data(s, j->in), svc_slot_size(s, j->in), at_song, &at_info);
}

// Позиция для программы: посчитано минус лежит в кольце (в перемотке - куда дошла машина)
static uint32_t at_pos(void) {
    return at_seek != UINT32_MAX ? at_frame : at_frame - (at_put - at_get);
}

// Слот 'STAT' и прогресс задания
static void at_state(svc_t *s, svc_job_t *j) {
    uint32_t pos = at_pos();
    if (at_info.frames) j->progress = (uint8_t)(pos >= at_info.frames ? 100 : (uint64_t)pos * 100 / at_info.frames);
    uint8_t *p = svc_slot_data(s, j->out);
    if (!p || svc_slot_size(s, j->out) < ATARI_ST_SIZE) return;
    wr32(p + ATARI_ST_FRAME, pos);
    wr32(p + ATARI_ST_FRAMES, at_info.frames);
    p[ATARI_ST_SONG] = at_song, p[ATARI_ST_SONG + 1] = (uint8_t)at_info.subsongs;
    p[ATARI_ST_FLAGS] = (uint8_t)((at_pause ? 1 : 0) | (at_seek != UINT32_MAX ? 2 : 0));
    wr16(p + ATARI_ST_LOAD, at_load), wr16(p + ATARI_ST_LOAD + 2, at_peak);
    wr32(p + ATARI_ST_LATE, at_late);
    wr32(p + ATARI_ST_LOST, sndh_lost);
}

// Машина упала (sndh_frame = -1): в лог
static int at_crash(svc_t *s) {
    char t[48];
    snprintf(t, sizeof t, "player: Atari machine crashed, frame %u", (unsigned)at_frame);
    svc_log(s, t);
    return -1;
}

// Кадр вперёд в кольцо, если есть место. После перемотки - сначала регистры, как их оставила машина (тик 0)
static int at_fill(svc_t *s) {
    if (at_put - at_get >= AT_FRAMES) return 0;
    uint32_t f = at_put % AT_FRAMES;
    sndh_wr_t *w = at_wr[f];
    int k = 0;
    for (uint8_t r = 0; r < 14; r++)
        if (at_sset >> r & 1) w[k].t = 0, w[k].reg = r, w[k].val = at_sreg[r], k++;
    at_sset = 0;
    uint32_t t = time_us_32();
    int n = sndh_frame(w + k, AT_WR_MAX - k);
    t = time_us_32() - t;
    at_load = (uint16_t)(t > 0xFFFF ? 0xFFFF : t);
    if (at_load > at_peak) at_peak = at_load;
    if (n < 0) return at_crash(s);
    at_n[f] = (uint16_t)(k + n);
    at_frame++;
    __dmb();                                                    // кадр записан - потом at_put
    at_put = at_put + 1;
    return 0;
}

// Перемотка: ядро 1 встало -> (назад - машина с начала) кадры без вывода, регистры запомнить -> кольцо пустое
static int at_seek_step(svc_t *s, svc_job_t *j) {
    if (!at_held) { at_hold = true; return 0; }                 // ждём, пока ядро 1 встанет
    if (at_seek < at_frame) {
        if (!at_open(s, j)) return at_crash(s);
        at_frame = 0, at_sset = 0;
    }
    uint32_t t = time_us_32();
    while (at_frame < at_seek && time_us_32() - t < AT_SEEK_US) {
        int n = sndh_frame(at_wr[0], AT_WR_MAX);                // кольцо стоит - его кадр как черновик
        if (n < 0) return at_crash(s);
        for (int i = 0; i < n; i++)
            if (at_wr[0][i].reg < 14) at_sreg[at_wr[0][i].reg] = at_wr[0][i].val, at_sset |= 1u << at_wr[0][i].reg;
        at_frame++;
    }
    if (at_frame < at_seek) return 0;
    at_put = at_get = 0;                                        // ядро 1 стоит - кольцо его не трогает
    at_seek = UINT32_MAX, at_peak = 0;
    at_hold = at_pause;                                         // дальше (если не пауза)
    return 0;
}

// PLAY: открыть песню (распаковка, машина Atari - Спектрум ждёт ответа, карта «занята»), кольцо пустое, ядро 1
// стоит; просьба ядру 1 - режим ATARI. Прошлый плеер мог ещё не отпустить режим - его просьба выполнится раньше
static bool at_start(svc_t *s, svc_job_t *j) {
    if (j->cmd != ATARI_PLAY) { svc_log(s, "player: not playing"); return false; }
    at_song = j->par[0] ? j->par[0] : 1;
    uint32_t t0 = time_us_32();
    bool opened = at_open(s, j);
    char t[160];
    snprintf(t, sizeof t, "player: open %lu ms", (unsigned long)((time_us_32() - t0) / 1000));   // замер: долго ли
    svc_log(s, t);
    if (!opened) { svc_log(s, "player: not SNDH, no such song or Atari machine crashed"); return false; }
    snprintf(t, sizeof t, "%s - %s, song %d of %d", at_info.author, at_info.name, at_song, at_info.subsongs);
    svc_log(s, t);
    if (!j->out) j->out = svc_slot_new(s, AT_STAT, ATARI_ST_SIZE);
    at_hold = true;                                             // ядро 1 стоит, пока не наш режим (at_step)
    at_put = at_get = 0, at_late = 0;
    at_run = false, at_sync = true;
    memset(at_reg, 0, sizeof at_reg), at_reg[7] = 0x3F;         // после стоянки ядро 1 вернёт это - тишина
    at_frame = 0, at_seek = UINT32_MAX, at_pause = false, at_sset = 0;
    at_load = at_peak = 0;
    at_on = true, at_phase = AT_WAIT;
    card_atari(true);
    return true;
}

static int at_step(svc_t *s, svc_job_t *j) {
    if (at_phase == AT_WAIT) {                                  // ядро 1 выполнило просьбы (стоп прошлого, наш PLAY)
        if (card_atari_pending()) return 0;
        if (mode != MODE_ATARI) { svc_log(s, "player: card busy (UPDATE / TEST)"); return -1; }
        phim_set(AT_PHIM_HZ);
        at_phase = AT_PLAY;
        at_hold = at_pause;                                     // играть (пауза могла прийти, пока ждали)
        return 0;
    }
    if (mode != MODE_ATARI) { svc_log(s, "player: stopped by MODE AY / reset"); return 1; }
    int r = at_seek != UINT32_MAX ? at_seek_step(s, j) : at_fill(s);
    at_state(s, j);
    return r;
}

// Команды идущему плееру
static uint8_t at_cmd(svc_t *s, svc_job_t *j, uint8_t cmd, const uint8_t *par) {
    switch (cmd) {
    case ATARI_STOP:                                            // задание готово - atari_poll остановит
        svc_log(s, "player: stop");
        j->state = SVC_JOB_DONE, j->progress = 100;
        return 0;
    case ATARI_PAUSE:
        at_pause = par[0] != 0;
        if (at_phase == AT_PLAY && at_seek == UINT32_MAX) at_hold = at_pause;   // ждём режим / перемотка - потом
        at_state(s, j);
        return 0;
    case ATARI_SEEK:
        at_seek = par[0] | par[1] << 8 | par[2] << 16 | (uint32_t)par[3] << 24;
        if (at_seek == UINT32_MAX) at_seek--;
        at_state(s, j);
        return 0;
    }
    return 4;
}

const svc_def_t atari_svc = { ATARI_SVC, 1, "player", AT_SNDH, AT_STAT, at_start, at_step, at_cmd };

// Задание кончилось (STOP, JOB_STOP, ошибка, MODE AY) - машину закрыть, карту - по умолчанию, такт - Спектрума
void atari_poll(svc_t *s) {
    if (!at_on || (s->job.state == SVC_JOB_RUN && s->job.svc == ATARI_SVC)) return;
    at_on = false;
    at_hold = true;                                             // ядро 1 - тишина, пока режим не сменится
    card_atari(false);                                          // ядро 1: был ATARI - карту по умолчанию
    phim_set(0);
    sndh_close();
}
