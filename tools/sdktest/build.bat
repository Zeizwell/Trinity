@echo off
rem Тест SDK драйвера. Запуск: build.bat, потом %TEMP%\sdktest\sdktest.exe %TEMP%\sdktest\sdktest.bin
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set T=%~dp0
set E=%T%..\..\engine\gs
set FT2=%T%..\..\..\FT2
set W=%TEMP%\sdktest
if not exist "%W%" mkdir "%W%"
pushd "%W%"
"%FT2%\tools\sjasmplus\sjasmplus.exe" --nologo --inc="%FT2%" "%T%test.asm"
popd
cl /nologo /I"%E%\..\third_party" /W3 /utf-8 /O2 /D_CRT_SECURE_NO_WARNINGS "%T%main.c" "%E%\gs_cfg.c" "%E%\gs_chan.c" "%E%\gs_mod.c" "%E%\gs_pt.c" "%E%\gs_ctl.c" "%E%\gs_stat.c" "%E%\gs_play_pt.c" "%E%\gs_play_pocket.c" "%E%\gs_core.c" "%E%\gs_dev_gs.c" "%E%\gs_dev_opl4.c" "%E%\gs_fx.c" "%E%\gs_blep.c" "%E%\gs_filter.c" "%E%\gs_down.c" /Fe:"%W%\sdktest.exe" /Fo:"%W%\\"
