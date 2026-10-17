# ZX Trinity

Звуковая карта для ZX Spectrum / ZX Evolution: настоящие AY + таймерные эффекты (RP2350), потом GS.
Всё обсуждение и решения — в репозитории FT2: `doc/zx-trinity.md`, протокол обмена — `doc/ay-ports-map.md` (п.5а).

## Прошивки

- `firmware/rp-ay` — RP-AY. Сейчас макет: Spotpear RP2350B-MINI-A в разрыве слота YM2203 на MultiSound
  (схема — FT2 `doc/trinity-proto-wiring.svg`).

Сборка: `firmware\rp-ay\build.bat`. Инструменты — `D:\_Work\Tools\pico` (Pico SDK 2.1.1, ARM GCC 14.2,
CMake, Ninja, pioasm, picotool; в систему не ставятся).

Прошивка: зажать BOOT, воткнуть USB (плата вынута из слота), кинуть `build\rp_ay.uf2` на появившийся диск.
