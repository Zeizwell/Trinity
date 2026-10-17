@echo off
rem Сборка прошивки RP-AY. Инструменты - D:\_Work\Tools\pico (в систему не ставятся).
set TOOLS=D:\_Work\Tools\pico
set PICO_SDK_PATH=%TOOLS%\pico-sdk
set PICO_TOOLCHAIN_PATH=%TOOLS%\arm-gcc
set PATH=%TOOLS%\cmake\cmake-3.31.6-windows-x86_64\bin;%TOOLS%\ninja;%TOOLS%\arm-gcc\bin;%PATH%
cd /d %~dp0
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -Dpioasm_DIR=%TOOLS%\sdk-tools\pioasm -Dpicotool_DIR=%TOOLS%\picotool\picotool || exit /b 1
cmake --build build || exit /b 1
echo.
echo Done: %~dp0build\rp_ay.uf2
