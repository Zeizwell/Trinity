@echo off
rem gsplay for the PC (MSVC): engine/gs + pocketmod, sound via waveOut
set T=%~dp0
set E=%T%..\..\engine\gs
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if exist "%~dp0gsplay.exe" del "%~dp0gsplay.exe"
cl /nologo /I"%E%\..\third_party" /W3 /utf-8 /O2 /D_CRT_SECURE_NO_WARNINGS "%T%main.c" "%E%\gs_cfg.c" "%E%\gs_chan.c" "%E%\gs_mod.c" "%E%\gs_pt.c" "%E%\gs_ctl.c" "%E%\gs_stat.c" "%E%\gs_play_pt.c" "%E%\gs_play_pocket.c" "%E%\gs_core.c" "%E%\gs_dev_gs.c" "%E%\gs_dev_opl4.c" "%E%\gs_fx.c" "%E%\gs_blep.c" "%E%\gs_filter.c" "%E%\gs_down.c" winmm.lib /Fe:"%T%gsplay.exe" /Fo:"%TEMP%\\"
