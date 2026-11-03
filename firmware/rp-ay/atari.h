// Самоплеер Atari (SNDH) - сервис 1 «плеер» (doc/trinity-api.md п.5, doc/trinity-sndh.md). Как шаги FX:
//   ядро 0 («Служебное», шаг задания) - машина Atari считает кадр 20 мс вперёд (sndh_frame) в кольцо кадров;
//   ядро 1 (будильник «Звука», режим ATARI) - записи кадра в чип 0 точно по тику 20 мкс.
// Команды (SVC_CALL сервиса 1): PLAY - вход слот SNDH, параметр 0 - песня (0 / 1 - первая); дальше тому же заданию:
// STOP, PAUSE (параметр 0: 1 - пауза, 0 - дальше), SEEK (параметры 0..3 - кадр). Состояние для программы -
// слот выхода 'STAT' (ATARI_ST_*), создаёт сам, если слот выхода 0.
#pragma once
#include <stdint.h>
#include "svc.h"

#define ATARI_SVC     1
enum { ATARI_PLAY = 1, ATARI_STOP, ATARI_PAUSE, ATARI_SEEK };

// Слот 'STAT' (младшим байтом вперёд)
#define ATARI_ST_FRAME   0      // 4: кадров сыграно (позиция)
#define ATARI_ST_FRAMES  4      // 4: длина песни в кадрах (0 - в файле нет)
#define ATARI_ST_SONG    8      // 1: песня; +1 - песен в файле
#define ATARI_ST_FLAGS   10     // 1: бит 0 - пауза, бит 1 - идёт перемотка
#define ATARI_ST_LOAD    12     // 2: мкс счёта последнего кадра (из 20000); +2 - пик с начала / перемотки
#define ATARI_ST_LATE    16     // 4: кадр не успел к сроку (чип ждал)
#define ATARI_ST_LOST    20     // 4: записей не влезло в кадр
#define ATARI_ST_SIZE    32

extern const svc_def_t atari_svc;           // строка таблицы сервисов (svc_t.host)
void     atari_poll(svc_t *s);              // ядро 0, каждый опрос служебного: задание кончилось - плеер стоп
uint32_t atari_sound(uint32_t now);         // ядро 1, будильник в режиме ATARI: записи в срок -> мкс до следующего
