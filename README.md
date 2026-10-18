# ZX Trinity

Звуковая карта для ZX Spectrum / ZX Evolution — **три платформы в одной (отсюда Trinity): Atari + Amiga + MSX**:
- **Atari** — настоящие AY + таймерные эффекты (SID, Sync, FM, свип) и SNDH-плеер — RP-AY;
- **Amiga** — GS со звуком Paula (BLEP, фильтры A500 / A1200), совместимость с GS / NeoGS — RP-GS;
- **MSX** — OPL4 (Moonsound, YMF278B через ymfm) — RP-GS, режимом вместо GS.
Всё обсуждение и решения — в репозитории FT2: `doc/zx-trinity.md`, протокол обмена — `doc/ay-ports-map.md` (п.5а).

## Прошивки

- `firmware/rp-ay` — RP-AY. Сейчас макет: Spotpear RP2350B-MINI-A в разрыве слота YM2203 на MultiSound
  (схема — FT2 `doc/trinity-proto-wiring.svg`).

Сборка: `firmware\rp-ay\build.bat`. Инструменты — `D:\_Work\Tools\pico` (Pico SDK 2.1.1, ARM GCC 14.2,
CMake, Ninja, pioasm, picotool; в систему не ставятся).

Прошивка: зажать BOOT, воткнуть USB (плата вынута из слота), кинуть `build\rp_ay.uf2` на появившийся диск.

## Найдено (на потом)

- **SNDH-плеер (Atari ST) для RP-AY** — 2026-10-18. Плеер `ref/SndhArchivePlayer` (exe, Leonard / Oxygene; копия из
  `d:\Data\Speccy\Atari\SndhArchivePlayer`; `ref/` не в git). Движок — библиотека **AtariAudio**
  (github.com/arnaud-carre/AtariAudio, MIT): 68000 (Musashi) + MFP + YM2149. Записи в YM — одно место
  (`AtariMachine.cpp`, `#FF8800/#FF8802`) — подменить на живой AY. Разбор — FT2 `doc/trinity-sndh.md`. Пока не делаем.
- **Paula (звук Amiga) для GS-части** — 2026-10-18. Материалы — FT2 `Paula/`, разбор и решения — FT2 `doc/paula.md`:
  берём Paula из **pt2-clone** (BSD-3: BLEP, фильтры A500 / A1200 / LED) + **pocketmod** (MIT, MOD-плеер). Порядок:
  4 канала и MOD-плеер, потом NeoGS и 8+ каналов; 8 / 16 бит, 48 кГц; модуль играет только RP. Замер нагрузки —
  `DWT_CYCCNT`. Оценка: Paula 4 канала ~7 % ядра RP2350, внешний DSP не нужен.
- **OPL4 (Moonsound) — на RP-GS**, режимом вместо GS (вместе не влезут): ymfm (BSD), ROM волн YRW801 с SD
  пользователя. Решение и проверка (PicoVerse 2350) — FT2 `doc/trinity-concept.md`.
- **GS / NeoGS** — руководство GS: FT2 `pentevo/docs/GS/gs_prog.pdf`; **исходники NeoGS целиком** (ПЗУ Z80, ПЛИС,
  руководство программиста) — `D:/_Work/Speccy/ref-neogs/neogs` (зеркало github.com/alfishe/neogs). Опознание GS / NeoGS —
  ПЗУ `#0004` (версия BCD), `#0080` (патч), `#0100` (копирайты) через команду `#17`: наш движок кладёт туда «Trinity».
- **Образцы на RP:** PicoGUS (FT2 `Paula/picogus-main`, GUS / SB на RP2040, шина через PIO, PSRAM, I2S), PiCard3
  (FT2 `schemes/PiCard3`, ПЗУ GS 1.05b в эмуляторе Z80 на Pico) — только справочник (GPL).
- **Драйверы дисков WC** (IDE Nemo / SMUC, SD ZC / NeoGS) — `D:/_Work/Speccy/ref-wc`.
- **Указатель всего найденного** — FT2 `doc/trinity-sources.md`.
