@echo off
rem Сборка прошивки RP-AY: build.bat [build_a - плата рев А | build_st - она же с самотестом шины, только на голой RP]. Инструменты - D:\_Work\Tools\pico (в систему не ставятся).
set TOOLS=D:\_Work\Tools\pico
set PICO_SDK_PATH=%TOOLS%\pico-sdk
set PICO_TOOLCHAIN_PATH=%TOOLS%\arm-gcc
set PATH=%TOOLS%\cmake\cmake-3.31.6-windows-x86_64\bin;%TOOLS%\ninja;%TOOLS%\arm-gcc\bin;%PATH%
cd /d %~dp0
set O=-DBOARD_REV_A=OFF -DBUS_SELFTEST=OFF
set B=build
set N=rp_ay
if "%1"=="build_a" set O=-DBOARD_REV_A=ON -DBUS_SELFTEST=OFF
if "%1"=="build_a" set B=build_a
if "%1"=="build_a" set N=rp_ay_a
if "%1"=="build_st" set O=-DBOARD_REV_A=ON -DBUS_SELFTEST=ON
if "%1"=="build_st" set B=build_st
if "%1"=="build_st" set N=rp_ay_st
cmake -S . -B %B% -G Ninja -DCMAKE_BUILD_TYPE=Release -Dpioasm_DIR=%TOOLS%\sdk-tools\pioasm -Dpicotool_DIR=%TOOLS%\picotool\picotool %O% -DSNDH=ON || exit /b 1
cmake --build %B% || exit /b 1
rem Версия - из config.h (FW_VERSION). Каждая версия - свой файл в bin\, готовый не перезаписываем
for /f "tokens=3" %%v in ('findstr /c:"#define FW_VERSION" config.h') do set VER=%%v
if %VER% LSS 10 set VER=0%VER%
if not exist bin mkdir bin
if exist bin\%N%_v%VER%.uf2 (
  echo bin\%N%_v%VER%.uf2 already exists - raise FW_VERSION in config.h
  exit /b 1
)
copy /y %B%\rp_ay.uf2 bin\%N%_v%VER%.uf2 >nul
echo.
echo Done: %~dp0bin\%N%_v%VER%.uf2
