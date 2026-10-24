@echo off
rem Сборка прошивки RP-AY. Инструменты - D:\_Work\Tools\pico (в систему не ставятся).
set TOOLS=D:\_Work\Tools\pico
set PICO_SDK_PATH=%TOOLS%\pico-sdk
set PICO_TOOLCHAIN_PATH=%TOOLS%\arm-gcc
set PATH=%TOOLS%\cmake\cmake-3.31.6-windows-x86_64\bin;%TOOLS%\ninja;%TOOLS%\arm-gcc\bin;%PATH%
cd /d %~dp0
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -Dpioasm_DIR=%TOOLS%\sdk-tools\pioasm -Dpicotool_DIR=%TOOLS%\picotool\picotool || exit /b 1
cmake --build build || exit /b 1
rem Версия - из config.h (FW_VERSION). Каждая версия - свой файл в bin\, готовый не перезаписываем
for /f "tokens=3" %%v in ('findstr /c:"#define FW_VERSION" config.h') do set VER=%%v
if %VER% LSS 10 set VER=0%VER%
if not exist bin mkdir bin
if exist bin\rp_ay_v%VER%.uf2 (
  echo bin\rp_ay_v%VER%.uf2 already exists - raise FW_VERSION in config.h
  exit /b 1
)
copy /y build\rp_ay.uf2 bin\rp_ay_v%VER%.uf2 >nul
echo.
echo Done: %~dp0bin\rp_ay_v%VER%.uf2
