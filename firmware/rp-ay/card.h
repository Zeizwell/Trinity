// Карта со стороны Спектрума (ядро 1): записи слота -> тень, TurboSound, протокол AY FX, что уходит в чип,
// таймеры эффектов, режим AY <-> FX. Чип пишет только chip.c (очередь "регистр + данные").
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define CARD_CHIPS 2                        // чипов TurboSound максимум (cfg.chips - сколько включено)

void    card_reset(void);                   // сброс (/IC, включение): тень, эффекты, очередь; тишина - по cfg
void    card_slot_write(bool a0, uint8_t v);// запись Спектрума: A0 = 0 - адрес (#FFFD), 1 - данные (#BFFD)
uint8_t card_answer(void);                  // что отдать на чтение данных сейчас
void    card_read_done(void);               // данные прочитали (метка PIO) - ответ-поток сдвинуть
void    card_poll(void);                    // в цикле ядра 1: таймеры эффектов, кадр, таймаут FX
uint8_t card_sel(void);                     // выбранный регистр текущего чипа (лог)
void    card_resend(void);                  // тень Спектрума (SSG 0..13) - снова в чип (после теста)
