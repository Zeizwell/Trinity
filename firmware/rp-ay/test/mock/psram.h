// PSRAM карты на ПК: массив в модели (sim.c)
#pragma once
#include <stdint.h>

#define PSRAM_SIZE (8u * 1024u * 1024u)
extern uint8_t sim_psram[PSRAM_SIZE];
#define PSRAM_BASE sim_psram

uint32_t psram_init(void);
