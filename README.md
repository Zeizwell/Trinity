# ZX Trinity

**Sound card for the ZX Spectrum: Atari, Amiga and MSX on one board.**

For ZX-BUS / NemoBus and ZX Evolution.

| Sound | How |
|---|---|
| **Atari** | Two real AY / YM2149 chips in sockets (TurboSound) and timer effects: SID, Sync, FM, sweep |
| **Amiga** | General Sound compatible, with Paula sound and A500 / A1200 filters; MOD, S3M and XM players |
| **MSX** | OPL4 (Moonsound), used instead of General Sound |

On the board (rev A2):
- CPLD on the Spectrum bus, own 32 MHz clock
- Two RP2350: one drives the AY chips and timer effects, the other plays General Sound / Paula / modules
- PCM5102A DAC, microSD slot
- Line out: AY through an op-amp mixer, DAC directly
- Headphone amplifier TPA6120A2 to the case front panel (HD Audio header)
- 2" 320×240 screen (ST7789) on a cable: scopes, levels, what is playing, card status

## Status

### Done
- Board rev A1: made, but unusable (edge connector defect); replaced by rev A2
- Board rev A2: schematic and part placement done (two AY in sockets, own 32 MHz clock for the CPLD, headphone output); routing in progress
- Amiga sound: Paula output, A500 / A1200 / LED filters, presets Clean, A500, A1200, HiFi
- ProTracker player: Paula registers match the pt2-clone replayer on 80,595 real modules and 200 stress tests
- Sound on PC: A1200 mode matches recordings of a real A1200, GS mode matches MultiSound, both within ±1 dB up to 16 kHz
- S3M player: matches OpenMPT on 8,389 of 8,459 modules (99.2 %); XM player (up to 32 channels): 35,060 of 35,458 (98.9 %) (on PC, first 3 minutes of each song)
- General Sound: ports, loading modules and effects, commands (tested without a Spectrum)
- Output: 32-bit I2S to a PCM5102A DAC. all presets 48 kHz
- Z80 API: find the card, settings, presets, CPU load, filter curves, versions (tested without a Spectrum)
- Card protocol and Z80 SDK: data slots, jobs, status byte, service log
- CPLD firmware, first version: builds with the rev A2 pins (91 of 128 macrocells), testbench passes; not yet on hardware
- Card emulator: the card firmware runs inside the Unreal Speccy emulator, card programs are tested there before the hardware; the General Sound part too

### Works on a test setup (ZX Evolution + RP2350 board)
- AY part: bus, registers and timer effects; the FT2 tracker finds the card and uses the effects
- Atari SNDH player on the card: the Atari machine runs on the card, the Spectrum only sends the file. Wild Commander plugin: play, pause, seek, next track
- SD card with exFAT: tested on PC only

### To do
- Build the first boards and test on a real Spectrum, CPLD on hardware
- Firmware updates from SD, driver program for the Spectrum
- OPL4 sound, NeoGS, IT player; XM: filters (`Zxx`), more than 32 channels
- CPU clock modes Economy / Normal / Turbo, chosen in the driver per music type
- Output up to 96 kHz: Paula (4 channels) has room for it; for more channels the rate is chosen on the real card by CPU load
- Trinity player program: all card modes, channel and pattern view, bass / treble

---

# ZX Trinity

**Звуковая карта для ZX Spectrum: Atari, Amiga и MSX на одной плате.**

Для ZX-BUS / NemoBus и ZX Evolution.

| Звук | Как |
|---|---|
| **Atari** | Два настоящих AY / YM2149 на панельках (TurboSound) и таймерные эффекты: SID, Sync, FM, свип |
| **Amiga** | Совместимость с General Sound, звук Paula, фильтры A500 / A1200; плееры MOD, S3M и XM |
| **MSX** | OPL4 (Moonsound), вместо General Sound |

На плате (рев A2):
- CPLD на шине Спектрума, свой генератор 32 МГц
- Два RP2350: один ведёт AY и таймерные эффекты, другой играет General Sound / Paula / модули
- ЦАП PCM5102A, гнездо microSD
- Линейный выход: AY через сумматор на ОУ, ЦАП напрямую
- Усилитель для наушников TPA6120A2 на переднюю панель корпуса (гребёнка HD Audio)
- Экранчик 2" 320×240 (ST7789) на проводах: осциллографы, уровни, что играет, состояние карты

## Что сейчас

### Готово
- Плата рев A1: изготовлена, но в работу не годится (брак разъёма шины); вместо неё рев A2
- Плата рев A2: схема и расстановка готовы (два AY на панельках, свой генератор 32 МГц у CPLD, выход на наушники); идёт разводка
- Звук Амиги: вывод Paula, фильтры A500 / A1200 / LED, пресеты Clean, A500, A1200, HiFi
- Плеер ProTracker: регистры Paula совпали с реплеером pt2-clone на 80 595 настоящих модулях и 200 стресс-тестах
- Звук на ПК: режим A1200 совпал с записями настоящей A1200, режим GS с MultiSound, оба в пределах ±1 дБ до 16 кГц
- Плеер S3M: совпал с OpenMPT на 8 389 из 8 459 модулей (99.2 %); плеер XM (до 32 каналов): 35 060 из 35 458 (98.9 %) (на ПК, первые 3 минуты каждой песни)
- General Sound: порты, загрузка модулей и эффектов, команды (проверено без Спектрума)
- Выход: I2S 32 бита на ЦАП PCM5102A. все пресеты 48 кГц
- API для Z80: поиск карты, настройки, пресеты, нагрузка, кривые фильтров, версии (проверено без Спектрума)
- Протокол карты и SDK для Z80: слоты данных, задания, байт статуса, лог сервиса
- Прошивка CPLD, первая версия: собирается с ножками рев A2 (91 из 128 макроячеек), тест на модели проходит; на железе ещё нет
- Эмулятор карты: прошивка карты работает внутри эмулятора Unreal Speccy, программы для карты проверяются там до железа; часть General Sound тоже

### Работает на макете (ZX Evolution + плата RP2350)
- Часть AY: шина, регистры и таймерные эффекты; трекер FT2 находит карту и пользуется эффектами
- Плеер Atari SNDH на карте: машина Atari работает на карте, Спектрум только отдаёт файл. Плагин для Wild Commander: игра, пауза, перемотка, следующий трек
- SD-карта с exFAT: проверена только на ПК

### Дальше
- Собрать первые платы и проверить на настоящем Спектруме, CPLD на железе
- Обновление прошивок с SD, программа-драйвер для Спектрума
- Звук OPL4, NeoGS, плеер IT; XM: фильтры (`Zxx`), больше 32 каналов
- Режимы частоты процессора Эконом / Нормал / Турбо, выбор в драйвере по типу музыки
- Вывод до 96 кГц: Paula (4 канала) это тянет; для большего числа каналов частоту выберем на настоящей карте по нагрузке процессора
- Программа-плеер Trinity: все режимы карты, вид каналов и паттерна, НЧ / ВЧ
