@echo off
rem Сборка прошивки RP-GS: build.bat файл_модуля [build_a - плата рев А , build_st - она же с самотестом шины, только на голой RP]. Инструменты - D:\_Work\Tools\pico (как у RP-AY)
set TOOLS=D:\_Work\Tools\pico
set PICO_SDK_PATH=%TOOLS%\pico-sdk
set PICO_TOOLCHAIN_PATH=%TOOLS%\arm-gcc
set PATH=%TOOLS%\cmake\cmake-3.31.6-windows-x86_64\bin;%TOOLS%\ninja;%TOOLS%\arm-gcc\bin;%PATH%
cd /d %~dp0
set MOD=%~f1
set MOD=%MOD:\=/%
set O=-DBOARD_REV_A=OFF -DBUS_SELFTEST=OFF
set B=build
if not "%2"=="" set B=%2
if "%2"=="build_a" set O=-DBOARD_REV_A=ON -DBUS_SELFTEST=OFF
if "%2"=="build_st" set O=-DBOARD_REV_A=ON -DBUS_SELFTEST=ON
cmake -S . -B %B% -G Ninja -DCMAKE_BUILD_TYPE=Release -DTEST_MOD=%MOD% -Dpioasm_DIR=%TOOLS%\sdk-tools\pioasm -Dpicotool_DIR=%TOOLS%\picotool\picotool %O% || exit /b 1
cmake --build %B% || exit /b 1
echo Done: %~dp0%B%\rp_gs.uf2
