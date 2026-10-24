// Чип (AY / YM): очередь записей и единственный писатель в чип (ядро 1). Запись очереди - 16 бит:
// регистр << 8 | данные. На каждую запись - регистр, потом данные (всегда оба цикла) с паузами занятости чипа;
// ножки ставит ядро шагами по времени (chip_poll), не ждёт. Тайминги - только из cfg (config.h).
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
    volatile uint32_t reads;        // чтений данных Спектрумом (метки PIO)
    volatile uint8_t  rd_sel[4], rd_ans[4]; // первые 4 чтения данных: выбранный регистр, что было на шине
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
    uint8_t  bad_bits;                    // все биты, что хоть раз не совпали (линия данных)
    uint16_t got_zero, got_float, got_old, got_other;// ошибки по виду: прочитали 00 (статус или регистр не записан), обратное
                                          //   ожидаемому (чип не выдал данные), прошлое значение (запись не прошла), значение другого регистра
    uint16_t reg_err[14];                 // ошибок по регистрам
} chip_test_t;
void chip_test(chip_test_t *r);
void chip_hold(uint32_t ms);              // замер тестером: A0 = 1, /WR = 0, /CS = 1 держать ms, потом покой
uint16_t chip_pins_check(void);         // ножки RP к чипу: замыкания (маска: D0..D7, /CS, /WR, A0, /RD)
void chip_probe(uint8_t out[3]);         // чтение R14, R15 (порты) и статуса - без записи: какая линия           // только когда chip_idle(); после теста в чипе мусор - вернуть тень
#define CHIP_TIMING_RUN 2                 // макет: в работе - самые длинные паузы (x4), подобрать потом тестом
void chip_timing(int set);                // паузы записи: 0 - cfg (ритм Z80), 1 - даташит YM2203 (17 тактов чипа
                                          //   после адреса и данных), 2 - набор 0 в 4 раза медленнее (тест)
