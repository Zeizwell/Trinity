// Ножки RP-GS (значения настроек - gs_cfg). На макете шина - слот YM: A0 = 1 порт #B3, A0 = 0 порт #BB. На рев А ножки другие
#ifndef CONFIG_H
#define CONFIG_H

#define FW_VERSION 1            // версия прошивки: поднимать при выпуске, драйвер показывает

#ifdef BOARD_REV_A
// Плата рев А: KiCad/trinity/gen/gs_gen.py
#define PIN_GS_C0   0           // код порта от CPLD, GP0..GP5
#define PIN_GS_WR   8
#define PIN_GS_D0   9           // данные, GP9..GP16
#define PIN_GS_RD   17
#define PIN_GS_RDY  18
#define PIN_CPLD_CLK 21

#define PIN_BCK  40
#define PIN_LCK  41             // сразу за BCK
#define PIN_DIN  42
#define PIN_XSMT 43
#else
#define PIN_SLOT_D0 0           // D0..D7 = GP0..GP7
#define PIN_SLOT_CS 8
#define PIN_SLOT_WR 9
#define PIN_SLOT_RD 10
#define PIN_SLOT_A0 11

// I2S: doc/zx-trinity.md, «PCM5102A на макете»
#define PIN_DIN 14
#define PIN_BCK 15
#define PIN_LCK 16              // сразу за BCK
#endif

// SD: рев А, SPI1 (trinity-board-checklist). На макете эти ножки - шина слота, трогать нельзя
#define BOARD_HAS_SD 0
#define PIN_SD_MISO 28
#define PIN_SD_CS   29
#define PIN_SD_SCK  30
#define PIN_SD_MOSI 31
#define PIN_SD_DET  32

#endif
