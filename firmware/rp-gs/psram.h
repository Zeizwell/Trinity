// PSRAM на QMI CS1 (APS6404, 8 МБ). Доступ - окно XIP, с кэшем
#ifndef PSRAM_H
#define PSRAM_H

#include <stdint.h>

#define PSRAM_BASE ((uint8_t *)0x11000000)

uint32_t psram_init(void);      // ID чипа. После него окно читается и пишется
uint32_t psram_test(uint32_t size);     // сколько байт не совпало

#endif
