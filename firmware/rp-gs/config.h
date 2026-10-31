// Настройки RP-GS: ножки и такт. На макете шина - слот YM: A0 = 1 порт #B3, A0 = 0 порт #BB. На рев А ножки другие
#ifndef CONFIG_H
#define CONFIG_H

#define SYS_KHZ 250000          // как RP-AY: ответ на чтение сразу за записью на 14 МГц (бюджет ~0.85 мкс)

#define PIN_SLOT_D0 0           // D0..D7 = GP0..GP7
#define PIN_SLOT_CS 8
#define PIN_SLOT_WR 9
#define PIN_SLOT_RD 10
#define PIN_SLOT_A0 11

#endif
