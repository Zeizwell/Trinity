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

| | |
|---|---|
| Каналы | 4, сэмплы 8 бит со знаком |
| Частота | своя у каждого канала: период ноты на 3.55 МГц (PAL), общей частоты вывода нет |
| Сигнал | ступеньки (удержание отсчёта), сэмпл не пересчитывается; ступеньки с ограниченной полосой: minBLEP 16 × 16 -> 48 кГц |
| Громкость | 0..64; ШИМ 6 бит - опция, по умолчанию выключен |
| Стерео | жёсткое: 0 + 3 слева, 1 + 2 справа |
| Фильтры (номиналы плат) | A500: ФНЧ 1-го порядка 4.4 кГц; LED: ФНЧ 2-го порядка 3.1 кГц, Q 0.66, на лету (`E0x`); ФВЧ ~5 Гц; A1200: без 4.4 кГц |
| Режимы | Clean, A500, A1200, HiFi (кубическая), Custom - на лету |
| ProTracker | реплеер один в один, причуды PT сохранены; регистры Paula = pt2-clone на 80 595 модулях |
| Точность | режим A1200 против настоящей A1200: ±0.9 дБ, 20 Гц - 16 кГц |
| Нагрузка | A1200, 4 канала: 16 % одного ядра RP2350 |

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
