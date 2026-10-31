// Конвертер Bitphase (.btp) -> трек FT2 (контейнер FT2C). Переносимый C: консольная утилита на ПК и сервис RP-AY
// (doc/zx-trinity.md п.14в). Работает потоком: .btp распаковывается кусками (окно 32К), JSON разбирается на
// лету (json.h), в памяти - только то, что идёт в FT2 (~60К). Вход читается ДВАЖДЫ:
//   проход 1 - ордер, длины паттернов, таблицы (орнаменты), инструменты, заголовок;
//   проход 2 - паттерны: каждый сразу в блоки PATT.
// Что не легло в FT2 - строкой в лог (log_fn): «потеряно ...», «приближено ...».
// Форматы: вход - FT2/doc/formats/btp.md, выход - FT2/doc/formats/ft2.md, команды - FT2/doc/commands.md.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "json.h"

#define BTP_MAC     512             // значений в макросе (как Bitphase INSTRUMENT_MACRO_MAX_LENGTH)
#define BTP_PATS     64             // паттернов FT2
#define BTP_ROWS     64             // роу в паттерне FT2
#define BTP_SRC_PATS 256            // паттернов Bitphase (номера)
#define BTP_INS      32
#define BTP_ORN      16
#define BTP_ORDER    256

typedef void (*btp_out_fn)(void *user, const uint8_t *p, uint32_t n);   // байты .ft2 подряд
typedef void (*btp_log_fn)(void *user, const char *line);               // строка лога

typedef struct {                    // инструмент FT2 (строки по 5 байт - FT2/doc/instruments.md)
    uint8_t rows, loop;             // строк (0 - нет инструмента), цикл
    uint8_t row[64][5];
    char    name[17];
} btp_ins_t;

typedef struct {                    // FX FT2 (FT2/doc/fx-row.md): строки по 16 байт, заголовок
    uint8_t rows, loop;             // строк (0 - нет FX), цикл
    uint8_t row[32][16];
    uint8_t shape, phase, keep;     // свип ШИМ: форма (таблица 1..5), стартовая фаза 0..255, не перезапускать
} btp_fx_t;

typedef struct {                    // орнамент FT2 (строки по 2 байта)
    uint8_t rows, loop;
    uint8_t row[64][2];
    char    name[17];
} btp_orn_t;

typedef struct {
    // выход
    btp_out_fn out;
    btp_log_fn log;
    void      *user;
    uint16_t   crc;
    // разбор
    json_t     js;
    int        pass;                // 1 / 2
    // проход 1: трек
    char       name[33], author[33];
    uint8_t    speed, tune, chip, stereo, songs;
    uint16_t   order_n, loop;
    uint8_t    order[BTP_ORDER];    // номера паттернов Bitphase
    uint16_t   src_len[BTP_SRC_PATS];   // длина паттерна Bitphase по номеру (0 - нет)
    btp_ins_t  ins[BTP_INS + 1];    // [1..32]
    btp_orn_t  orn[BTP_ORN + 1];    // [1..16]
    btp_fx_t   fx[BTP_INS + 1];     // FX инструмента n - номер n
    uint8_t    tab[256][16], tab_n[256];   // таблицы FX: значения, длина (0..5 - пресет нового трека, свои - с 6)
    int        tabs;                // таблиц занято (следующая свободная)
    uint8_t    pwm_max, pwm_min, pwm_speed, pwm_shape, pwm_phase, pwm_keep;   // ШИМ инструмента (уже в наших единицах)
    // ордер FT2: паттерн Bitphase -> первый номер FT2 (куски по 64 роу подряд); 0xFF - не влез
    uint8_t    first[BTP_SRC_PATS];
    uint8_t    pats;                // паттернов FT2 занято
    // текущий объект разбора (проход 1 - инструмент / таблица, проход 2 - паттерн)
    int        cur_id;              // номер инструмента / таблицы / паттерна Bitphase
    uint16_t   cur_len;
    int16_t    mac_v[24][BTP_MAC];  // макросы инструмента: значения (0..11 - обычные, 12..22 - таймерные)
    uint16_t   mac_n[24], mac_loop[24];
    // проход 2: паттерн Bitphase целиком (до 256 роу), канал - 16 байт, заголовок чипа - 8
    uint8_t    row[256][112];
    uint8_t    eff_n;               // команд в текущей клетке
    bool       warned[32];          // о чём уже писали в лог (одна строка на вид потери)
    uint8_t    seen[BTP_SRC_PATS];  // паттерн с этим номером уже был (у Bitphase номер бывает дважды - берём первый)
} btp_conv_t;

void btp_init(btp_conv_t *c, btp_out_fn out, btp_log_fn log, void *user);
void btp_pass(btp_conv_t *c, int pass);                       // начать проход: дальше btp_feed по всему входу
void btp_feed(btp_conv_t *c, const uint8_t *p, uint32_t n);   // распакованные байты JSON
bool btp_finish(btp_conv_t *c);                               // после прохода 2: хвост файла; false - ошибка разбора

// Распаковка .btp (gzip) потоком: вход целиком в памяти (сжатый - десятки К), выход - кусками в fn
typedef void (*btp_chunk_fn)(void *user, const uint8_t *p, uint32_t n);
bool btp_gunzip(const uint8_t *src, uint32_t len, btp_chunk_fn fn, void *user);
