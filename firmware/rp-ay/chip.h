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
    volatile uint32_t fx_over;      // запись эффекта перезаписала ещё не ушедшую (чип не успевает за эффектом)
} chip_stat_t;

extern chip_stat_t chip_stat;

void chip_init(void);                     // ножки чипа - выходы, /CS и /WR вверх
void chip_reset_state(void);              // сброс: очередь пуста
bool chip_put(int c, uint8_t reg, uint8_t v);    // в очередь: регистр + данные; false - очередь полна
void chip_put_fx(int c, uint8_t reg, uint8_t v); // запись эффекта (0..15): вне очереди, первой на границе записи; слот на регистр
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
void chip_test(int c, chip_test_t *r);          // только когда chip_idle(); после теста в чипе мусор - вернуть тень
void chip_hold(uint32_t ms);              // замер тестером: A0 = 1, /WR = 0, /CS = 1 держать ms, потом покой
uint16_t chip_pins_check(int c);         // ножки RP к чипу: замыкания (маска: D0..D7, /CS, /WR, A0, /RD)
void chip_probe(int c, uint8_t out[3]);         // чтение R14, R15 (порты) и статуса - без записи: какая линия

// Паузы записи: ритм Z80 на 3.5 МГц (cfg, полутакты) * % набора. Наборы: default - 100 %, tuned - из теста пауз
#define CHIP_TM_N 5                       // установка, /WR, удержание, после адреса, после данных
extern const char *const chip_tm_name[CHIP_TM_N];
uint32_t chip_tm_base_ns(int i);          // пауза i при 100 % (ритм Z80), нс
void chip_timing_pct(const uint16_t pct[CHIP_TM_N]);
void chip_timing_cfg(void);               // набор из cfg: timing_preset 0 - default, 1 - tuned_pct
typedef struct { uint16_t min_pct[CHIP_TM_N], work_pct[CHIP_TM_N]; } chip_tune_t;
void chip_tune(chip_tune_t *t);           // тест пауз (только когда chip_idle): минимум без ошибок, рабочее = + запас

void     cyc_init(void);                  // счётчик тактов RP (SysTick) - на ядре писателя, до первой записи
uint32_t cyc_now(void);                   //   считает вниз, 24 бита
