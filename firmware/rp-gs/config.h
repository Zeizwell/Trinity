// Ножки RP-GS (значения настроек - gs_cfg). На макете шина - слот YM: A0 = 1 порт #B3, A0 = 0 порт #BB. На рев А ножки другие
#ifndef CONFIG_H
#define CONFIG_H

#define FW_VERSION 1            // версия прошивки: поднимать при выпуске, драйвер показывает

#define PIN_SLOT_D0 0           // D0..D7 = GP0..GP7
#define PIN_SLOT_CS 8
#define PIN_SLOT_WR 9
#define PIN_SLOT_RD 10
#define PIN_SLOT_A0 11

// I2S: doc/zx-trinity.md, «PCM5102A на макете»
#define PIN_DIN 14
#define PIN_BCK 15
#define PIN_LCK 16              // сразу за BCK

// SD: рев А, SPI1 (trinity-board-checklist). На макете эти ножки - шина слота, трогать нельзя
#define BOARD_HAS_SD 0
#define PIN_SD_MISO 28
#define PIN_SD_CS   29
#define PIN_SD_SCK  30
#define PIN_SD_MOSI 31
#define PIN_SD_DET  32

#endif
