// Чип (AY / YM): очередь записей и единственный писатель в чип (ядро 1). Запись очереди - 16 бит:
// регистр << 8 | данные. Писатель на каждую запись выставляет регистр, потом данные (всегда оба цикла)
// и держит паузы занятости чипа. Тайминги - только из cfg (config.h).
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define CHIP_Q_SIZE 1024            // записей в очереди (степень двойки)

typedef struct {                    // счётчики (USB-консоль)
    volatile uint32_t slot_wr;      // записей из слота (адрес и данные)
    volatile uint32_t chip_wr;      // записей в чип (циклов: адрес и данные)
    volatile uint32_t q_lost;       // очередь была полна - запись потеряна
    volatile uint32_t fifo_full;    // FIFO PIO был полон при разборе (могли потерять записи шины)
    volatile uint32_t resets;       // сбросов /IC
} chip_stat_t;

extern chip_stat_t chip_stat;

void chip_init(void);                     // ножки чипа - выходы, /CS и /WR вверх
void chip_reset_state(void);              // сброс: очередь пуста
bool chip_put(uint8_t reg, uint8_t v);    // в очередь: регистр + данные; false - очередь полна
void chip_poll(void);                     // писатель: следующий цикл, если чип уже готов (ядро 1, в цикле)
bool chip_idle(void);                     // очередь пуста, запись не начата

// Тест чтения (режим TEST, провод /RD чипа -> GP39): пишет и читает регистры SSG мимо очереди
typedef struct {
    uint16_t checks, errors;              // сколько сравнений, сколько не совпало
    uint8_t  bad_reg, bad_wrote, bad_read;// первое несовпадение
} chip_test_t;
void chip_test(chip_test_t *r);           // только когда chip_idle(); после теста в чипе мусор - вернуть тень
