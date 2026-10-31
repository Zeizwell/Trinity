# ZX Trinity

Звуковая карта для ZX Spectrum / ZX Evolution — **три платформы в одной (отсюда Trinity): Atari + Amiga + MSX**:
- **Atari** — настоящие AY + таймерные эффекты (SID, Sync, FM, свип) и SNDH-плеер — RP-AY;
- **Amiga** — GS со звуком Paula (BLEP, фильтры A500 / A1200), совместимость с GS / NeoGS — RP-GS;
- **MSX** — OPL4 (Moonsound, YMF278B через ymfm) — RP-GS, режимом вместо GS.
Всё обсуждение и решения — в репозитории FT2: `doc/zx-trinity.md`, протокол обмена — `doc/ay-ports-map.md` (п.5а).

## Что готово (2026-11-01)

### Плата
- **Рев A:** схема и разводка готовы, гербер в `KiCad/trinity/fab/`, платы и ЦАП на руках.
- **В пути:** модули RP и остальная комплектация. На собранной плате ещё ничего не запускалось.

### RP-GS: Amiga и GS (`firmware/rp-gs`, `engine/gs`)
- **Устройства:** коре с таблицей устройств и арбитром, GS, заглушка OPL4 (порты живут, звука нет).
- **GS:** порты `#BB` / `#B3`, загрузка модулей и эффектов в PSRAM 8 МБ, команды модулей и эффектов.
- **Плееры — плагины:**
  - `gs_pt` — плеер ProTracker: 5 модулей и 200 случайных, регистры совпали с оригиналом, 0 расхождений (`tools/ptcmp`);
  - pocketmod — остальные MOD.
- **Звук:** BLEP как у Paula, фильтры A500 / A1200 / LED, ручки НЧ и ВЧ, компрессор, вывод I2S на PCM5102A.
- **Режимы:** GS, A500, A1200, HiFi и Custom. Режим — набор значений, любую настройку можно менять отдельно.
- **Настройки:** одна структура `gs_cfg`, сохраняются во флеш; запись останавливает звук на 13 мс.
- **Канал драйвера:** настройки, пресеты, нагрузка, кривая фильтров, версии. Порты GS за ключом `TRINITY`
  (FT2 `doc/trinity-api.md`, раздел 7; `sdk/trinity_gs.asm`).
- **Нагрузка ядра звука (замер на макете):** 10–16 %.
- **Проверено:** на макете без Спектрума, обмен подавался в обработчик шины из самой прошивки: опозданий 0,
  обработчик шины 484 нс при бюджете 850 нс. Тесты на ПК: `tools/gsports`.

### RP-AY: Atari (`firmware/rp-ay`)
- **Работает на макете в слоте YM2203:** перехват шины, тень регистров, таймерные эффекты, сервисы карты
  (FT2 `doc/trinity-api.md`). Что и как проверять — `firmware/rp-ay/CHECK.md`.

### Общее
- **SD и FatFs (с exFAT):** протокол проверен на ПК на модели карты, на настоящей SD ещё не запускалось.
- **SDK для Z80:** `sdk/trinity_ay.asm`, `sdk/trinity_gs.asm` в репозитории FT2. `trinity_gs.asm` собирается,
  на Z80 ещё не запускался.

### Ещё нет
- **Прошивка CPLD** (порты AY / GS) и её обновление по SVF.
- **Связь RP-GS и RP-AY** по UART, самообновление RP-AY.
- **Программа-драйвер** на Спектруме (протокол готов) и обновление прошивок с SD.
- **Звук OPL4**, SNDH-плеер, NeoGS, XM.
- **Модули больше 4 каналов:** остался известный вопрос с голосами.

## Прошивки

- `firmware/rp-ay` — RP-AY. Сейчас макет: Spotpear RP2350B-MINI-A в разрыве слота YM2203 на MultiSound
  (схема — FT2 `doc/trinity-proto-wiring.svg`).
- `firmware/rp-gs` — RP-GS. Сейчас макет: тот же модуль в слоте YM, SD выключена (`BOARD_HAS_SD 0`).
  Сборка: `firmware\rp-gs\build.bat <модуль для стенда>`.

Сборка: `firmware\rp-ay\build.bat`. Инструменты — `D:\_Work\Tools\pico` (Pico SDK 2.1.1, ARM GCC 14.2,
CMake, Ninja, pioasm, picotool; в систему не ставятся).

Прошивка: зажать BOOT, воткнуть USB (плата вынута из слота), кинуть `build\rp_ay.uf2` на появившийся диск.

## Найдено (на потом)

- **SNDH-плеер (Atari ST) для RP-AY** — 2026-10-18. Плеер `ref/SndhArchivePlayer` (exe, Leonard / Oxygene; копия из
  `d:\Data\Speccy\Atari\SndhArchivePlayer`; `ref/` не в git). Движок — библиотека **AtariAudio**
  (github.com/arnaud-carre/AtariAudio, MIT): 68000 (Musashi) + MFP + YM2149. Записи в YM — одно место
  (`AtariMachine.cpp`, `#FF8800/#FF8802`) — подменить на живой AY. Разбор — FT2 `doc/trinity-sndh.md`. Пока не делаем.
- **Paula (звук Amiga) для GS-части** — 2026-10-18. Материалы — FT2 `Paula/`, разбор и решения — FT2 `doc/paula.md`:
  сделано на основе **pt2-clone** (BSD-3) и **pocketmod** (MIT), см. «Что готово».
- **OPL4 (Moonsound) — на RP-GS**, режимом вместо GS (вместе не влезут): ymfm (BSD), ROM волн YRW801 с SD
  пользователя. Решение и проверка (PicoVerse 2350) — FT2 `doc/trinity-concept.md`.
- **GS / NeoGS** — руководство GS: FT2 `pentevo/docs/GS/gs_prog.pdf`; **исходники NeoGS целиком** (ПЗУ Z80, ПЛИС,
  руководство программиста) — `D:/_Work/Speccy/ref-neogs/neogs` (зеркало github.com/alfishe/neogs). Опознание GS / NeoGS —
  ПЗУ `#0004` (версия BCD), `#0080` (патч), `#0100` (копирайты) через команду `#17`: наш движок кладёт туда «Trinity».
- **Образцы на RP:** PicoGUS (FT2 `Paula/picogus-main`, GUS / SB на RP2040, шина через PIO, PSRAM, I2S), PiCard3
  (FT2 `schemes/PiCard3`, ПЗУ GS 1.05b в эмуляторе Z80 на Pico) — только справочник (GPL).
- **Драйверы дисков WC** (IDE Nemo / SMUC, SD ZC / NeoGS) — `D:/_Work/Speccy/ref-wc`.
- **Указатель всего найденного** — FT2 `doc/trinity-sources.md`.
