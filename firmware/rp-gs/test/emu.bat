@echo off
rem RP-GS для эмулятора: движок GS (как в прошивке) -> trinity_gs.dll рядом с Unreal FT2 (FT2/tools/unreal), GSType=TRIN
set T=%~dp0
set E=%T%..\..\..\engine
set G=%E%\gs
set OUT=%T%..\..\..\..\FT2\tools\unreal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /LD /O2 /W3 /utf-8 /D_CRT_SECURE_NO_WARNINGS /wd4244 /I"%E%" /I"%E%\third_party" "%T%gs_dll.c" "%G%\gs_cfg.c" "%G%\gs_chan.c" "%G%\gs_mod.c" "%G%\gs_pt.c" "%G%\gs_ctl.c" "%G%\gs_stat.c" "%G%\gs_play_pt.c" "%G%\gs_play_pocket.c" "%G%\gs_core.c" "%G%\gs_dev_gs.c" "%G%\gs_dev_opl4.c" "%G%\gs_fx.c" "%G%\gs_blep.c" "%G%\gs_filter.c" "%G%\gs_down.c" /Fe:"%OUT%\trinity_gs.dll" /Fo:"%TEMP%\\" || exit /b 1
