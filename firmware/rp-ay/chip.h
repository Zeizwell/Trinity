// Чип (AY / YM): тень регистров, очередь записи и единственный писатель в чип (ядро 1).
// Тайминги - только из cfg (config.h).
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define CHIP_Q_SIZE 1024            // очередь записей в чип (степень двойки)

typedef struct {                    // счётчики (USB-консоль)
    volatile uint32_t slot_wr;      // записей из слота (адрес и данные)
    volatile uint32_t chip_wr;      // записей в чип
    volatile uint32_t q_lost;       // очередь была полна - запись потеряна
    volatile uint32_t fifo_full;    // FIFO PIO был полон при разборе (могли потерять записи шины)
    volatile uint32_t resets;       // сбросов /IC
} chip_stat_t;

extern chip_stat_t chip_stat;
extern volatile uint8_t chip_regs[256];   // тень: что Спектрум записал (читается ответом на чтение)
extern volatile uint8_t chip_sel;         // выбранный Спектрумом регистр

void chip_init(void);                     // ножки чипа - выходы, /CS и /WR вверх
void chip_reset_state(void);              // сброс: тень, выбор, очередь
void chip_slot_write(bool a0, uint8_t v); // запись со стороны Спектрума: тень + (в режиме) в очередь чипа
bool chip_put(bool a0, uint8_t v);        // в очередь чипа (эффекты, плеер RP); false - очередь полна
void chip_poll(void);                     // писатель: следующая запись, если чип уже готов (ядро 1, в цикле)
