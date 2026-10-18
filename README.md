# ZX Trinity

Звуковая карта для ZX Spectrum / ZX Evolution: настоящие AY + таймерные эффекты (RP2350), потом GS.
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
