# ZX Trinity: найденные материалы — где лежат и что в них

Указатель (2026-10-18). **Сначала смотреть сюда, а не искать заново в интернете.** Пути — от `D:\_Work\Speccy\`.
Материалы пользователя в git не добавлены (лежат в папке проекта, ждут его решения).

## Звук Paula / Amiga — `ref/Paula/` (разбор — `doc/paula.md`)

| Что | Где | Зачем нам | Лицензия |
|---|---|---|---|
| Paula на Verilog (весь чип: DMA, период, ШИМ-громкость, attach) | `ref/Paula/paula.v`, `ref/Paula/amiga_replacement_project-master/paula/Paula.v`, `Better_Audio.v` | как устроен звук в железе | CC BY-NC (только справочник) |
| **Реплеер ProTracker pt2-clone** — эталон, логика перенесена в `engine/gs/gs_pt.c` | `ref/Paula/pt2-clone-master/src/pt2_replayer.c` | эталон, сверка по тикам в `doc/paula.md` | BSD-3 |
| **Эмуляция Paula pt2-clone** (BLEP + фильтры A500 / A1200 / LED, номиналы RC) | `ref/Paula/pt2-clone-master/src/pt2_paula.c`, `pt2_blep.c`, `pt2_rcfilters.c` | **берём в прошивку** (~660 строк) | BSD-3 |
| **pocketmod** — MOD-плеер одним файлом, без зависимостей | `ref/Paula/pocketmod-master/pocketmod.h` | только MOD не на 4 канала; 4 канала играет наш `gs_pt` (`doc/paula.md`) | MIT |
| WinUAE `audio.cpp` (sinc-очередь, модели фильтров) | `ref/Paula/WinUAE-master/audio.cpp` | справочник | GPL |
| MilkyTracker `ResamplerAmiga.h` (BLEP) | `ref/Paula/MilkyTracker-master/src/milkyplay/` | справочник | GPL |
| Заметка «как правильно ресемплить Amiga» (метод BLEP, A. Lankila) | `ref/Paula/milkytracker.txt` | теория | — |
| **PicoGUS** — GUS / SB / OPL на RP2040: шина ISA через PIO, I2S (`pico_audio_i2s`), PSRAM, 32 голоса | `ref/Paula/picogus-main/sw/` | образец: звук + шина + PSRAM на RP | GPL (справочник) |
| Заметка о ЦАП (PCM5102A / PCM5122 / ES9023) | `ref/Paula/ДАКИ.txt`, `adafruit-pcm5122-i2s-dac.pdf` | выбор ЦАП (решено: PCM5102A) | — |
| **SOAMC: записи с настоящей A1200 (Paula 8364R7PL), MP3 + сами модули** | `cdimage.debian.org/mirror/media/Oakvalley/soamc/000/{MP3,AMIGA}/Trackers_Modland/<автор>/` | эталон для сверки звука, результат — `doc/paula.md` | только для сверки |

## General Sound / NeoGS

| Что | Где | Ключевое |
|---|---|---|
| **Руководство программиста GS** v1.04 + правки ROM 1.05a (30 стр.) | `FT2/pentevo/docs/GS/gs_prog.pdf` | порты `#BB` (команда / статус: бит 7 данные, бит 0 команда), `#B3` (данные / вывод); все команды `#00..#FA` (модули `#30..#33`, FX `#38..#49`, позиция `#60..#64`, память `#14 / #15 / #17`); версия — **Peek `#17`: ПЗУ `#0004` BCD, `#0100` копирайты 3×24, `#0080` патч**; `#6A` / `#6B` (1.05a) |
| PiCard3 — карта на Pico: **настоящее ПЗУ GS 1.05b в эмуляторе Z80**, TurboSound, I2S, PS/2 | `FT2/schemes/PiCard3/src/GS/` (`GS.cpp`, `gs105b.rom.h`), `src/PIO`, `src/i2s` | образец интерфейса GS на шине Спектрума через PIO |
| PiCard3 — **шина без CPLD** (разбор 2026-10-19, схемы в проекте нет — только прошивка) | `FT2/schemes/PiCard3/config.h`, `src/PIO/pio_data_z80_bus.cpp`, цикл в `PiCard3.ino` (~стр. 436) | RP2350 (PGA2350), **разгон 351 МГц при 1.60 В**. A0..A15 — GP8..23, M1 / WR / RD / IORQ / MREQ — GP3..7: **прямо в RP** (входы терпят 5 В). Данные D0..D7 — GP24..31 **через один 74…245**: OE / направление — side-set PIO на GP2 (PIO выдаёт байт, пока /RD внизу). Дешифрация портов — **ядро опрашивает `gpio_get_all()` в цикле** (не PIO), WAIT нет, IORQGE нет. ROMCS — GP1 (`PIN_ROM_BLK`), NMI — GP35 (открытый сток сменой направления). Итого: без CPLD и без шифтеров на входах, но буфер на данных есть. **Модуль:** схемы платы в проекте нет, но сборка — под `pimoroni_pga2350` (PiCard3; ранний PiCard — `pimoroni_pico_plus_2`), ножки до GP47 — значит **PGA2350** (RP2350B, 16 МБ флеш + 8 МБ PSRAM, выводы сеткой 2.54 — впаивается в отверстия платы или в панельку) |
| **PI-CARD MUSICA** (Д. Демьяненко, **MIT** — код брать можно), v0.46, разбор 2026-11-07 | `D:/_Work/Speccy/PICARD-MUSICA-main` (`backend/ports/rp2350/hostlink/`: `ports.pio`, `bus.cpp`, `README.md`; `docs/protocol.md`, `docs/gs.md`) | RP2350B, 300 МГц, 1.30 В, FreeRTOS, PSRAM 8 МБ. **Ответ на IN — как у нас:** PIO ловит цикл, адрес ячейки `база | порт << 2` -> цепочка DMA -> автомат выдаёт байт (срок ~825 нс, WAIT нет). Фильтр иголок: IORQ подтверждается 5 раз (~60 нс), снятие — две проверки с выдержкой. **Поток ответа — прерыванием ядра**: `INIR` нельзя, команды и чтение кадра — только на 3.5 МГц (у нас поток железом, `INIR` на 14 МГц проходит). Кадр платы с контрольной суммой, плата не считает прочитанное: команда стоит до подтверждения, перечитывать можно (повтор = «не подтверждать»). Диагностика: `serve_late` (байт не успели выставить — /RD уже вверху), потери записей WR FIFO, печать раз в 10 с. Функции пути шины — только в SRAM (промах XIP до 6.7 мкс). **GS — система команд без Z80, залитый код опознаётся и за него отвечает плата** (Z-Player 4.1 / 5.1, `docs/gs.md`) — ровно наш путь (`trinity-config.md` п.16-19): образец для RP-GS. Ещё: движок трекеров IT / XM / S3M / MOD до 64 голосов, MIDI с SoundFont по частям из PSRAM, DivMMC + Z-Controller на одной SD с арбитром, USB HID |
| **Karabas-MegaBuzz** (andykarpov, скачано 2026-10-19) — карта NemoBus на ПЛИС XC6SLX16: TurboSound FM, GS 2 МБ, OPL3 (YMF262), MIDI SAM2695, SAA1099, SounDrive / Covox / бипер, VS1053 (MP3), PCM5102, SD (Z-Controller / DivMMC) | `D:/_Work/Speccy/ref-cards/karabas-megabuzz/` — `PORTS.md`, `firmware/src/rtl/megabuzz.v` (~стр. 785), `sound/soundrive.v`, схемы `docs/*.pdf` | **порты:** TS `#FFFD` / `#BFFD` (A15, A14, A3..0 = 1101), SAA `#FF` + A8, GS `#B3` / `#BB`, OPL3 `#C4..#C7`, SounDrive `#0F` `#1F` `#3F` `#4F` `#5F`, Covox `#FB`, Z-Controller `#77` / `#57`, DivMMC `#E3` `#E7` `#EB`, служебные — ZXUNO `#FC3B` / `#FD3B` (полный адрес 16 бит). **Шина:** A0–15, D0–7, RD, WR, IORQ, MREQ, M1, RST, F14, WAIT, IORQGE, NMI, ROMCS, DOS, IODOS. MIDI — через порт AY. Конфиг — EEPROM, меню своим ПЗУ (ROMCS) |
| **ZX-MultiSound Max** (Лозовой, 2022; прислал пользователь 2026-10-19) — MultiSound + GS 2 МБ (16 МГц) + OPL3 YMF262 | `FT2/schemes/MultSoundMax/`: `cpld/fw_ms_opl3_cpld_saa_fix/rtl/top.v`, схема и плата PDF, iBOM, ПЗУ GS (AM29F040B 512К), `.pof`; эмулятор — ZXMAK2-Fork (aaydev) | CPLD **EPM3256ATC144** (как MultiSound). Отличия от MultiSound (`top.v`): **OPL3 `#C4..#C7`** (A7..A2 = 110001, `n_opl_cs`, A0 / A1 — на шину чипов, данные — общая `ad`), чтение `#C4..#C7` — статус OPL; **запирание ПЗУ — по фронту выборки кода** (`M1 & MREQ & RD`, A15..14 = 00), а не опросом по такту (это и есть `saa_fix`); `IORQGE` — **по адресу при `M1` = 1** (`#FFFD` полный A15..13, `#BFFD`, `#B3`, `#BB`, OPL), без `IORQ`. Цикл ввода-вывода — как MultiSound: `RD` / `WR` без `MREQ` и `M1`, по такту 32 МГц. Порты: TS-FM `#FFFD` / `#BFFD`, SAA `#FF` (+ A8), GS `#B3` / `#BB`, SounDrive `#0F` `#1F` `#4F` `#5F`, OPL3 `#C4..#C7` (как MoonSound) |
| **ZXM-MoonSound** (micklab, архив alfishe, скачано 2026-10-19) — OPL4 (YMF278B) на Пентагон | `D:/_Work/Speccy/ref-cards/zxm-moonsound/` — `hardware/README.md`, `cpld-sources/`, `doc/`, 240 песен в Furnace | порты `#C4..#C7` (FM), `#7E` / `#7F` (волны); CPLD EPM7032STC44; даташиты OPL2/3/4, форматы MFM / MWM — для OPL4 на RP-GS |
| NeoGS rev C-VS — схема и плата (sch, pcb, гербера, BOM, PDF) | `FT2/schemes/Neogs/revC-VS.r191/` | разбор в `doc/zx-trinity.md` |
| **Исходники NeoGS целиком** (git-зеркало SVN nedopc: github.com/alfishe/neogs, скачано 2026-10-18) | `ref-neogs/neogs/` | `z80/main_rom/main_ngs.a80` — ПЗУ NeoGS (GS-совместимое, v1.11 от 07.02.2026), `z80/gs105a_fix` — исходник ПЗУ GS 1.05a, `fpga/current/sound` (ЦАП, `sound_mulacc.v`), `fpga/current/zxbus/zxbus.v` — шина, `cpld/`, `docs/NGS_prm/ngspgm_01a.pdf` — **руководство программиста NeoGS** (порты GSCTR / GSCOM / GSDAT, 8 каналов, MP3, SD, память 2 МБ), `docs/_old_gs/GS104SRC` — исходник ПЗУ GS 1.04 |
| Опознание NeoGS (из `main_ngs.a80`) | — | **то же, что у GS**: ПЗУ `#0004` — `DW` версия BCD (`#0111` = 1.11), `#0080` — строка патча, `#0100` — 3 строки по 24 («General Sound (tm) ROM», «Copyright 1997 Stinger», «Version 1.11»). Наш движок кладёт туда своё |
| Свободные команды GS (таблица `COMTAB` / `COMTABH` в `main_ngs.a80`, сверка с `gs_prog.pdf`) | — | ПЗУ NeoGS 1.11 их молча игнорирует (только снимает флаг команды): `#1E #1F`, `#24..#29`, `#43 #44`, `#4A..#4F`, `#51..#5F`, `#6C..#7F`, `#B0..#EF`, `#F1 #F2`, `#F8 #F9`, `#FB..#FF`. В руководстве помечены «Зарезервированы»: `#B0..#F0`, `#F1 #F2`, `#F8 #F9`, `#FB..#FF`. Порты: `#xxBB`, `#xxB3`, `#xx33` (сброс и NMI), разбираются только младшие 8 бит адреса (`zxbus.v`) |

Текст PDF (`gs_prog.pdf`, `ngspgm_01a.pdf`) вытаскивается `python` + `pypdf` (поставлен 2026-10-18).

## Диск / драйверы устройств

| Что | Где | Ключевое |
|---|---|---|
| tslabs/zx-evo целиком (разреженный клон, только дерево) | `ref-zxevo/zx-evo/` | `pentevo/soft/WC/source/core32/drivers/SD_NGS.ASM`, ПЗУ `pentevo/unreal/Unreal/cfg/rom/gs105a.rom`; файлы берутся `git checkout` по пути |
| FatFs R0.15a (ChaN, BSD-подобная, без GPL): на RP-GS, exFAT + FAT32, CP866, карты до 2 ТБ (SDXC) | `engine/third_party/fatfs/`, порт `engine/sd/`, тест `tools/sdtest/` | взят из `ref/Paula/picogus-main/sw/fatfs`; на живой карте пока не проверен |
| AtariAudio 1.27 (Leonard / Oxygene, MIT; внутри Musashi 68000 MIT): эмуляция Atari ST для SNDH | `engine/third_party/atariaudio/` (SOURCE.txt — коммит) | без правок; замеры и план — `doc/trinity-sndh.md` |
| Наш FAT32 (из WC Improved): ядро без железа, устройство — таблица `init / read512 / write512 / sync` | `FT2/drivers/fat32/` (README) | порт сейчас один: `ports/sdzc.asm` (SD Z-Controller, номер карты 0 / 1 = SD / SD2) |
| **Исходники Wild Commander Improved** (скачаны 2026-10-18) | `ref-wc/Wild-Commander-Improved/source/` | плагины WMF: формат и весь API — `docs/02-plugins-and-public-api.md`, образцы — `source/plugins/` (`gsplayer` — модуль в GS); драйверы устройств: `DIDENEMO.ASM` (IDE Nemo), `DIDESMUC.ASM` (IDE SMUC), `DSDZC.ASM` (SD ZC), `DSDNGS.ASM` (SD NeoGS); список «Select Device» — `WCVLS.ASM` (`THDD`) |

## AY / Atari / прочее

| Что | Где |
|---|---|
| SNDH-плеер (AtariAudio, MIT) | разбор `doc/trinity-sndh.md`, исходник — `D:\Data\Speccy\Atari\SndhArchivePlayer` (копия — в Trinity, см. там) |
| AYX-32 (STM32, прошивки, KiCad) | `FT2/schemes/AYX-32/` |
| ZX Evolution rev D+ | `FT2/schemes/rev_d+.r1368/`, разбор `FT2/doc/evo-rev-d.md` |
| MultiSound, схемы карт | `FT2/schemes/`, разбор `doc/zx-trinity.md` |
| Карта портов AY (кто какие номера занял) | `doc/ay-ports-map.md` |
| Verilog TS-Config, доки Evo | `FT2/pentevo/` (`fpga/current`, `docs/`) |

## Сборка прошивок и находки (перенесено из README)

## Прошивки

- `firmware/rp-ay` — RP-AY. Сейчас макет: Spotpear RP2350B-MINI-A в разрыве слота YM2203 на MultiSound
  (схема — `doc/trinity-proto-wiring.svg`).
- `firmware/rp-gs` — RP-GS. Сейчас макет: тот же модуль в слоте YM, SD выключена (`BOARD_HAS_SD 0`).
  Сборка: `firmware\rp-gs\build.bat <модуль для стенда>`.

Сборка: `firmware\rp-ay\build.bat`. Инструменты — `D:\_Work\Tools\pico` (Pico SDK 2.1.1, ARM GCC 14.2,
CMake, Ninja, pioasm, picotool; в систему не ставятся).

Прошивка: зажать BOOT, воткнуть USB (плата вынута из слота), кинуть `build\rp_ay.uf2` на появившийся диск.

## Найдено (на потом)

- **SNDH-плеер (Atari ST) для RP-AY** — 2026-10-18. Плеер `ref/SndhArchivePlayer` (exe, Leonard / Oxygene; копия из
  `d:\Data\Speccy\Atari\SndhArchivePlayer`; `ref/` не в git). Движок — библиотека **AtariAudio**
  (github.com/arnaud-carre/AtariAudio, MIT): 68000 (Musashi) + MFP + YM2149. Записи в YM — одно место
  (`AtariMachine.cpp`, `#FF8800/#FF8802`) — подменить на живой AY. Разбор — `doc/trinity-sndh.md`. Пока не делаем.
- **Paula (звук Amiga) для GS-части** — 2026-10-18. Материалы — `ref/Paula/`, разбор и решения — `doc/paula.md`:
  сделано на основе **pt2-clone** (BSD-3) и **pocketmod** (MIT), см. «Что готово».
- **OPL4 (Moonsound) — на RP-GS**, режимом вместо GS (вместе не влезут): ymfm (BSD), ROM волн YRW801 с SD
  пользователя. Решение и проверка (PicoVerse 2350) — `doc/trinity-concept.md`.
- **GS / NeoGS** — руководство GS: FT2 `pentevo/docs/GS/gs_prog.pdf`; **исходники NeoGS целиком** (ПЗУ Z80, ПЛИС,
  руководство программиста) — `D:/_Work/Speccy/ref-neogs/neogs` (зеркало github.com/alfishe/neogs). Опознание GS / NeoGS —
  ПЗУ `#0004` (версия BCD), `#0080` (патч), `#0100` (копирайты) через команду `#17`: наш движок кладёт туда «Trinity».
- **Образцы на RP:** PicoGUS (`ref/Paula/picogus-main`, GUS / SB на RP2040, шина через PIO, PSRAM, I2S), PiCard3
  (FT2 `schemes/PiCard3`, ПЗУ GS 1.05b в эмуляторе Z80 на Pico) — только справочник (GPL).
- **Драйверы дисков WC** (IDE Nemo / SMUC, SD ZC / NeoGS) — `D:/_Work/Speccy/ref-wc`.
- **Указатель всего найденного** — `doc/trinity-sources.md`.
