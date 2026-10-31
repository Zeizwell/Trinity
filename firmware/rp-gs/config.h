// Ножки RP-GS (значения настроек - gs_cfg). На макете шина - слот YM: A0 = 1 порт #B3, A0 = 0 порт #BB. На рев А ножки другие
#ifndef CONFIG_H
#define CONFIG_H

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
