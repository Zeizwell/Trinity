# ZX Trinity

**Three computers' worth of sound on one card for the ZX Spectrum.**
Real AY chips, the sound of the Amiga and the FM of the MSX, on ZX-BUS / NemoBus and ZX Evolution.

| | Platform | What you get |
|---|---|---|
| 🎹 | **Atari** | Two real YM2203 chips (AY + FM) with timer effects on top: SID, Sync buzzer, FM, sweeps. SNDH player planned |
| 🎸 | **Amiga** | General Sound compatible, with the sound of Paula: A500 and A1200 filters, ProTracker playback |
| 🎺 | **MSX** | OPL4 (Moonsound) in place of the GS mode |

## Where we are

### Done
- ✅ **Board rev A** designed and ordered, on its way
- ✅ **Amiga sound:** Paula-style output, A500 / A1200 / LED filters, presets GS, A500, A1200 and HiFi
- ✅ **ProTracker player:** every Paula register matches the original replayer on hundreds of test modules
- ✅ **Checked against real hardware:** A1200 and MultiSound recordings, the spectrum matches within ±1 dB up to 16 kHz
- ✅ **General Sound:** ports, module and effect loading, commands
- ✅ **Plenty of headroom:** the sound core takes 10-17 % of its time
- ✅ **Driver API for Z80:** check for the card, change settings and presets, see the load and filter curves live. Include one file and write any interface you like

### Working on the bench
- 🔧 **AY side:** bus capture, register shadow and timer effects on a breadboard prototype
- 🔧 **SD card** with exFAT, tested on PC so far

### Next
- ⏳ Assemble the first boards and run the card on a real Spectrum
- ⏳ CPLD firmware and firmware updates from SD
- ⏳ OPL4 sound, SNDH player, NeoGS, XM

---

# ZX Trinity

**Звук трёх компьютеров на одной карте для ZX Spectrum.**
Настоящие чипы AY, звук Амиги и FM от MSX, для ZX-BUS / NemoBus и ZX Evolution.

| | Платформа | Что внутри |
|---|---|---|
| 🎹 | **Atari** | Два настоящих YM2203 (AY + FM) и таймерные эффекты поверх: SID, бузер Sync, FM, свипы. SNDH-плеер в планах |
| 🎸 | **Amiga** | Совместимость с General Sound и звук Paula: фильтры A500 и A1200, воспроизведение ProTracker |
| 🎺 | **MSX** | OPL4 (Moonsound) вместо режима GS |

## Что сейчас

### Готово
- ✅ **Плата рев A** разведена и заказана, едет
- ✅ **Звук Амиги:** вывод как у Paula, фильтры A500 / A1200 / LED, пресеты GS, A500, A1200 и HiFi
- ✅ **Плеер ProTracker:** все регистры Paula совпали с оригинальным реплеером на сотнях тестовых модулей
- ✅ **Сверено с настоящим железом:** записи A1200 и MultiSound, спектр совпал в пределах ±1 дБ до 16 кГц
- ✅ **General Sound:** порты, загрузка модулей и эффектов, команды
- ✅ **Большой запас:** ядро звука занято на 10-17 %
- ✅ **API драйвера для Z80:** проверка карты, настройки и пресеты, нагрузка и кривые фильтров вживую. Подключаешь один файл и пишешь любой интерфейс

### Работает на стенде
- 🔧 **Часть AY:** перехват шины, тень регистров и таймерные эффекты на макете
- 🔧 **SD-карта** с exFAT, пока проверена на ПК

### Дальше
- ⏳ Собрать первые платы и запустить карту на настоящем Спектруме
- ⏳ Прошивка CPLD и обновление прошивок с SD
- ⏳ Звук OPL4, SNDH-плеер, NeoGS, XM
