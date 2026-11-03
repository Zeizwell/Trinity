// SNDH-плеер (музыка Atari ST) на живом AY: AtariAudio крутит 68000 и таймеры MFP, звук не считает - отдаёт записи
// в YM с временем (doc/trinity-sndh.md). Тот же код - в прошивке RP-AY (ядро 0) и в тесте на ПК (tools/sndhtest).
//   sndh_open  - файл в память машины, инициализация песни (её записи в YM - в начале первого кадра);
//   sndh_frame - следующий кадр 20 мс: список записей по порядку, время - тик 20 мкс внутри кадра;
//   sndh_close - освободить машину;
//   sndh_arena - где брать большие блоки (память Atari 4 МБ, файл): на RP - PSRAM; не звали - malloc.
// Порядок внутри тика как в машине: записи плеера кадра (флаг SNDH_WR_TIMER = 0) - до тика, записи обработчиков
// таймеров (флаг 1) - после.
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SNDH_TICK_HZ     50000          // тик таймеров MFP: 20 мкс
#define SNDH_FRAME_TICKS 1000           // тиков в кадре 20 мс
#define SNDH_WR_TIMER    0x8000         // в t: запись из обработчика таймера

typedef struct {
    uint16_t t;                         // тик в кадре 0..999, бит 15 - SNDH_WR_TIMER
    uint8_t  reg, val;                  // регистр YM (номер как записали), значение
} sndh_wr_t;

typedef struct {
    char     name[64], author[64];      // из тегов файла
    int      subsongs;                  // песен в файле (1..)
    uint32_t frames;                    // длина песни в кадрах 20 мс (0 - в файле нет)
} sndh_info_t;

bool     sndh_open(const void *file, uint32_t size, int subsong, sndh_info_t *info);   // subsong 1..; false - не наш файл / упал
int      sndh_frame(sndh_wr_t *out, int max);  // -> записей в out (лишние сверх max - в sndh_lost); -1 - машина упала
void     sndh_close(void);
void     sndh_arena(void *base, uint32_t size);    // область под машину; блоки по порядку, освобождает sndh_close
extern uint32_t sndh_lost;                     // записей не влезло в out (с открытия)

#ifdef __cplusplus
}
#endif
