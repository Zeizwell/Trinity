# ZX Trinity

**Sound card for the ZX Spectrum: Atari, Amiga and MSX on one board.** For ZX-BUS / NemoBus and ZX Evolution.

| | |
|---|---|
| AY | 2 × AY-3-8910 / YM2149F in sockets, TurboSound (chip select `#FF` / `#FE`), ports `#FFFD` / `#BFFD` |
| AYFX | timer effects on the real AY chips: SID, Sync, FM, Env FM, PWM (16 steps 0..50 %, sweep) |
| SNDH | Atari ST music player: 68000 at 8 MHz on the card, sound on the real AY chips |
| GS | General Sound interface (`#B3` / `#BB`); NeoGS - in progress |
| Tracker modules | MOD (ProTracker), S3M, XM up to 32 channels; IT - in progress |
| Amiga | Paula A500 / A1200, BLEP, LED filter; presets Clean, A500, A1200, HiFi, Custom |
| MSX | Moonsound OPL4, MIDI (GM) - in progress |
| DAC | PCM5102A, I2S 32 bit, 48 kHz |
| Output | 3.5 mm line out: AY through an OPA1656 mixer, DAC directly; narrow stereo - DIP switch |
| Processors | 2 × RP2350B (16 MB flash, 8 MB PSRAM each), CPLD EPM3128 with its own 32 MHz clock |
| Storage | microSD |
| Screen | any SPI TFT module on a cable: scopes, levels, what is playing, chip status, temperature, firmware versions |

## Paula

**What Paula is.** The sound chip of the Amiga: 4 channels, 8-bit samples. What makes its sound unique:

- **Every channel at its own frequency.** A channel plays its sample at the rate set by the note period and holds every sample value until the next one: clean steps, the sample is never recalculated. The images of the steps lie above the music, and the board's RC filters cut them gently. This is the bright, "glassy" Amiga sound.
- **Volume does not touch the sample:** it is a 6-bit PWM on top of it, the 8-bit sample stays whole.
- **Hard stereo:** channels 0 + 3 left, 1 + 2 right.
- **Filters on the board:** A500 - a fixed low-pass at 4.4 kHz (the "warm" A500), plus the "LED" filter at 3.1 kHz that music switches as an effect (`E0x`); A1200 - without the 4.4 kHz filter, brighter.

**How Trinity does it.** Each channel is computed like Paula - its own frequency, its own steps - and the steps are rendered band-limited (minBLEP 16 × 16, as in pt2-clone): the character of the steps stays, and no aliasing of our own is added when output at 48 kHz.

**What is done:**

- 4 channels, each at its own frequency (Paula period at 3.55 MHz), 8-bit samples, Amiga stereo: channels 0 + 3 left, 1 + 2 right.
- Amiga board filters modelled from the real part values: A500 low-pass 4.4 kHz; LED filter, 2nd order, 3.1 kHz, switched on the fly (`E0x`); high-pass ~5 Hz. A1200 - without the 4.4 kHz filter.
- Volume 0..64; Paula's real 6-bit PWM volume is a setting, off by default (on Elysium the difference is -34 dB, not audible).
- Modes switch on the fly: Clean, A500, A1200, HiFi (cubic interpolation), Custom.
- ProTracker player: the replayer logic one to one, with all PT quirks; Paula registers match pt2-clone on 80,595 modules.
- A1200 mode matches recordings of a real A1200 within ±0.9 dB from 20 Hz to 16 kHz.
- Load: A1200 mode, 4 channels - 16 % of one RP2350 core.

## Status

### Done
- Board rev A2: schematic, placement and routing done, DRC clean; next - Gerbers and order (rev A1 was made, but unusable: edge connector defect)
- Sound on PC: A1200 mode matches recordings of a real A1200, GS mode matches MultiSound, both within ±1 dB up to 16 kHz
- S3M player: matches OpenMPT on 8,394 of 8,459 modules (99.2 %); XM player (up to 32 channels): 35,160 of 35,458 (99.2 %) (on PC, every song played to the end)
- General Sound: ports, loading modules and effects, commands (tested without a Spectrum)
- Z80 API and SDK: find the card, settings, presets, CPU load, filter curves, versions
- CPLD firmware, first version: 91 of 128 macrocells, testbench passes; not yet on hardware

### Works on a test setup (ZX Evolution + RP2350 board)
- AY part: registers and timer effects; the FT2 tracker finds the card and uses the effects
- SNDH player: the Spectrum only sends the file. Wild Commander plugin: play, pause, seek, next track

### To do
- Build the first boards and test on a real Spectrum, CPLD on hardware
- Firmware updates from SD, driver program for the Spectrum
- OPL4, MIDI, NeoGS, IT player; XM: filters (`Zxx`), more than 32 channels
- CPU clock modes Economy / Normal / Turbo
- Output up to 96 kHz
- Trinity player program: all card modes, channel and pattern view, bass / treble

---

# ZX Trinity

**Звуковая карта для ZX Spectrum: Atari, Amiga и MSX на одной плате.** Для ZX-BUS / NemoBus и ZX Evolution.

| | |
|---|---|
| AY | 2 × AY-3-8910 / YM2149F на панельках, TurboSound (выбор чипа `#FF` / `#FE`), порты `#FFFD` / `#BFFD` |
| AYFX | таймерные эффекты на настоящих AY: SID, Sync, FM, Env FM, PWM (16 ступеней 0..50 %, свип) |
| SNDH | плеер музыки Atari ST: 68000 на 8 МГц на карте, звук на настоящих AY |
| GS | интерфейс General Sound (`#B3` / `#BB`); NeoGS - в работе |
| Трекерные модули | MOD (ProTracker), S3M, XM до 32 каналов; IT - в работе |
| Amiga | Paula A500 / A1200, BLEP, LED-фильтр; пресеты Clean, A500, A1200, HiFi, Custom |
| MSX | Moonsound OPL4, MIDI (GM) - в работе |
| ЦАП | PCM5102A, I2S 32 бита, 48 кГц |
| Выход | линия 3.5 мм: AY через сумматор на OPA1656, ЦАП напрямую; узкое стерео - DIP-переключатель |
| Процессоры | 2 × RP2350B (флеш 16 МБ, PSRAM 8 МБ у каждого), CPLD EPM3128 со своим генератором 32 МГц |
| Носитель | microSD |
| Экран | любой SPI TFT-модуль на проводах: осциллографы, уровни, что играет, состояние чипов, температура, версии прошивок |

## Paula

**Что такое Paula.** Звуковой чип Амиги: 4 канала, сэмплы 8 бит. Чем уникален её звук:

- **У каждого канала своя частота.** Канал играет сэмпл со скоростью, которую задаёт период ноты, и держит каждое значение до следующего: честные ступеньки, сэмпл никогда не пересчитывается. Образы ступенек лежат выше музыки, и их мягко срезают RC-фильтры платы. Это и есть яркий, «стеклянный» звук Амиги.
- **Громкость не трогает сэмпл:** это 6-битный ШИМ поверх него, 8-битный сэмпл остаётся целым.
- **Жёсткое стерео:** каналы 0 + 3 слева, 1 + 2 справа.
- **Фильтры на плате:** A500 - постоянный ФНЧ 4.4 кГц («тёплая» A500) и LED-фильтр 3.1 кГц, который музыка включает как эффект (`E0x`); A1200 - без фильтра 4.4 кГц, ярче.

**Как это сделано в Trinity.** Каждый канал считается как у Paula - своя частота, свои ступеньки, - а ступеньки выводятся с ограниченной полосой (minBLEP 16 × 16, как в pt2-clone): характер ступенек остаётся, а своего алиасинга при выводе на 48 кГц не добавляется.

**Что сделано:**

- 4 канала, у каждого своя частота (период Paula на 3.55 МГц), сэмплы 8 бит, стерео как на Амиге: каналы 0 + 3 слева, 1 + 2 справа.
- Фильтры плат Амиги по номиналам настоящих деталей: A500 - ФНЧ 4.4 кГц; LED-фильтр 2-го порядка 3.1 кГц, включается на лету (`E0x`); ФВЧ ~5 Гц. A1200 - без фильтра 4.4 кГц.
- Громкость 0..64; настоящая ШИМ-громкость Paula (6 бит) - настройкой, по умолчанию выключена (на Elysium разница -34 дБ, на слух не слышно).
- Режимы переключаются на лету: Clean, A500, A1200, HiFi (кубическая интерполяция), Custom.
- Плеер ProTracker: логика реплеера один в один, со всеми причудами PT; регистры Paula совпали с pt2-clone на 80 595 модулях.
- Режим A1200 совпал с записями настоящей A1200 в пределах ±0.9 дБ от 20 Гц до 16 кГц.
- Нагрузка: режим A1200, 4 канала - 16 % одного ядра RP2350.

## Что сейчас

### Готово
- Плата рев A2: схема, расстановка и разводка готовы, DRC чистый; дальше - гербера и заказ (рев A1 изготовлена, но не годится: брак разъёма шины)
- Звук на ПК: режим A1200 совпал с записями настоящей A1200, режим GS с MultiSound, оба в пределах ±1 дБ до 16 кГц
- Плеер S3M: совпал с OpenMPT на 8 394 из 8 459 модулей (99.2 %); плеер XM (до 32 каналов): 35 160 из 35 458 (99.2 %) (на ПК, каждая песня до конца)
- General Sound: порты, загрузка модулей и эффектов, команды (проверено без Спектрума)
- API и SDK для Z80: поиск карты, настройки, пресеты, нагрузка, кривые фильтров, версии
- Прошивка CPLD, первая версия: 91 из 128 макроячеек, тест на модели проходит; на железе ещё нет

### Работает на макете (ZX Evolution + плата RP2350)
- Часть AY: регистры и таймерные эффекты; трекер FT2 находит карту и пользуется эффектами
- Плеер SNDH: Спектрум только отдаёт файл. Плагин для Wild Commander: игра, пауза, перемотка, следующий трек

### Дальше
- Собрать первые платы и проверить на настоящем Спектруме, CPLD на железе
- Обновление прошивок с SD, программа-драйвер для Спектрума
- OPL4, MIDI, NeoGS, плеер IT; XM: фильтры (`Zxx`), больше 32 каналов
- Режимы частоты процессора Эконом / Нормал / Турбо
- Вывод до 96 кГц
- Программа-плеер Trinity: все режимы карты, вид каналов и паттерна, НЧ / ВЧ
