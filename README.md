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
| DAC | PCM5102A, I2S 32 bit |
| Output | 3.5 mm line out: AY through an OPA1656 mixer, DAC directly; narrow stereo - DIP switch |
| Processors | 2 × RP2350B (16 MB flash, 8 MB PSRAM each), CPLD EPM3128 with its own 32 MHz clock |
| Storage | microSD |
| Screen | any SPI TFT module on a cable: scopes, levels, what is playing, chip status, temperature, firmware versions |

## Paula

| | |
|---|---|
| Channels | 4, samples 8 bit signed |
| Rate | own per channel: note period at 3.55 MHz (PAL), no common output rate |
| Signal | steps (zero-order hold), sample never resampled; steps band-limited: minBLEP 16 × 16 |
| Volume | 0..64; 6-bit PWM - option, off by default |
| Stereo | hard: 0 + 3 left, 1 + 2 right |
| Filters (board values) | A500: low-pass 1st order 4.4 kHz; LED: low-pass 2nd order 3.1 kHz, Q 0.66, on the fly (`E0x`); high-pass ~5 Hz; A1200: no 4.4 kHz |
| Modes | Clean, A500, A1200, HiFi (cubic), Custom - on the fly |
| ProTracker | replayer one to one, PT quirks kept; Paula registers = pt2-clone on 80,595 modules |

## Status

### Done
- Board rev A2: schematic, placement and routing done, DRC clean; next - Gerbers and order (rev A1 was made, but unusable: edge connector defect)
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
| ЦАП | PCM5102A, I2S 32 бита |
| Выход | линия 3.5 мм: AY через сумматор на OPA1656, ЦАП напрямую; узкое стерео - DIP-переключатель |
| Процессоры | 2 × RP2350B (флеш 16 МБ, PSRAM 8 МБ у каждого), CPLD EPM3128 со своим генератором 32 МГц |
| Носитель | microSD |
| Экран | любой SPI TFT-модуль на проводах: осциллографы, уровни, что играет, состояние чипов, температура, версии прошивок |

## Paula

| | |
|---|---|
| Каналы | 4, сэмплы 8 бит со знаком |
| Частота | своя у каждого канала: период ноты на 3.55 МГц (PAL), общей частоты вывода нет |
| Сигнал | ступеньки (удержание отсчёта), сэмпл не пересчитывается; ступеньки с ограниченной полосой: minBLEP 16 × 16 |
| Громкость | 0..64; ШИМ 6 бит - опция, по умолчанию выключен |
| Стерео | жёсткое: 0 + 3 слева, 1 + 2 справа |
| Фильтры (номиналы плат) | A500: ФНЧ 1-го порядка 4.4 кГц; LED: ФНЧ 2-го порядка 3.1 кГц, Q 0.66, на лету (`E0x`); ФВЧ ~5 Гц; A1200: без 4.4 кГц |
| Режимы | Clean, A500, A1200, HiFi (кубическая), Custom - на лету |
| ProTracker | реплеер один в один, причуды PT сохранены; регистры Paula = pt2-clone на 80 595 модулях |

## Что сейчас

### Готово
- Плата рев A2: схема, расстановка и разводка готовы, DRC чистый; дальше - гербера и заказ (рев A1 изготовлена, но не годится: брак разъёма шины)
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
