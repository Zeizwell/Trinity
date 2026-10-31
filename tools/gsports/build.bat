@echo off
rem gsports: PC test of the GS ports (Z80 exchange by FULGSCOM.TXT)
set T=%~dp0
set E=%T%..\..\engine\gs
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /W3 /utf-8 /O2 /D_CRT_SECURE_NO_WARNINGS "%T%main.c" "%E%\gs_cfg.c" "%E%\gs_chan.c" "%E%\gs_mod.c" "%E%\gs_core.c" "%E%\gs_dev_gs.c" "%E%\gs_dev_opl4.c" "%E%\gs_fx.c" "%E%\gs_blep.c" "%E%\gs_filter.c" "%E%\gs_down.c" /Fe:"%T%gsports.exe" /Fo:"%TEMP%\\"
