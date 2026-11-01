# ZX Trinity

**Sound card for the ZX Spectrum: Atari, Amiga and MSX on one board.**

For ZX-BUS / NemoBus and ZX Evolution.

| Sound | How |
|---|---|
| **Atari** | Two real YM2203 chips (AY and FM) and timer effects: SID, Sync, FM, sweep |
| **Amiga** | General Sound compatible, with Paula sound and A500 / A1200 filters |
| **MSX** | OPL4 (Moonsound), used instead of General Sound |

## Status

### Done
- Board rev A is designed and ordered
- Amiga sound: Paula output, A500 / A1200 / LED filters, presets GS, A500, A1200, HiFi
- ProTracker player: Paula registers match the pt2-clone replayer on 80,595 real modules and 200 stress tests
- Sound on PC: A1200 mode matches recordings of a real A1200, GS mode matches MultiSound, both within ±1 dB up to 16 kHz
- General Sound: ports, loading modules and effects, commands (tested without a Spectrum)
- Output: 32-bit I2S to a PCM5102A DAC. Paula and HiFi 48 kHz, GS 37.5 kHz like the original (each sample sent twice, DAC at 75 kHz)
- Sound takes 10-17 % of one CPU core
- Z80 API: find the card, settings, presets, CPU load, filter curves, versions (tested without a Spectrum)

### Works on a test setup
- AY part: bus, registers and timer effects
- SD card with exFAT: tested on PC only

### To do
- Build the first boards and test on a real Spectrum
- CPLD firmware, firmware updates from SD, driver program for the Spectrum
- OPL4 sound, SNDH player, NeoGS, XM

---

# ZX Trinity

**Звуковая карта для ZX Spectrum: Atari, Amiga и MSX на одной плате.**

Для ZX-BUS / NemoBus и ZX Evolution.

| Звук | Как |
|---|---|
| **Atari** | Два настоящих YM2203 (AY и FM) и таймерные эффекты: SID, Sync, FM, свип |
| **Amiga** | Совместимость с General Sound, звук Paula, фильтры A500 / A1200 |
| **MSX** | OPL4 (Moonsound), вместо General Sound |

## Что сейчас

### Готово
- Плата рев A разведена и заказана
- Звук Амиги: вывод Paula, фильтры A500 / A1200 / LED, пресеты GS, A500, A1200, HiFi
- Плеер ProTracker: регистры Paula совпали с реплеером pt2-clone на 80 595 настоящих модулях и 200 стресс-тестах
- Звук на ПК: режим A1200 совпал с записями настоящей A1200, режим GS с MultiSound, оба в пределах ±1 дБ до 16 кГц
- General Sound: порты, загрузка модулей и эффектов, команды (проверено без Спектрума)
- Выход: I2S 32 бита на ЦАП PCM5102A. Paula и HiFi 48 кГц, GS 37.5 кГц как в оригинале (каждый отсчёт дважды, ЦАП на 75 кГц)
- Звук занимает 10-17 % одного ядра процессора
- API для Z80: поиск карты, настройки, пресеты, нагрузка, кривые фильтров, версии (проверено без Спектрума)

### Работает на макете
- Часть AY: шина, регистры и таймерные эффекты
- SD-карта с exFAT: проверена только на ПК

### Дальше
- Собрать первые платы и проверить на настоящем Спектруме
- Прошивка CPLD, обновление прошивок с SD, программа-драйвер для Спектрума
- Звук OPL4, SNDH-плеер, NeoGS, XM
