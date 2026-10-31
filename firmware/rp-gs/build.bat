@echo off
rem Сборка прошивки RP-GS: build.bat файл_модуля. Инструменты - D:\_Work\Tools\pico (как у RP-AY)
set TOOLS=D:\_Work\Tools\pico
set PICO_SDK_PATH=%TOOLS%\pico-sdk
set PICO_TOOLCHAIN_PATH=%TOOLS%\arm-gcc
set PATH=%TOOLS%\cmake\cmake-3.31.6-windows-x86_64\bin;%TOOLS%\ninja;%TOOLS%\arm-gcc\bin;%PATH%
cd /d %~dp0
set MOD=%~f1
set MOD=%MOD:\=/%
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DTEST_MOD=%MOD% -Dpioasm_DIR=%TOOLS%\sdk-tools\pioasm -Dpicotool_DIR=%TOOLS%\picotool\picotool || exit /b 1
cmake --build build || exit /b 1
echo Done: %~dp0build\rp_gs.uf2
