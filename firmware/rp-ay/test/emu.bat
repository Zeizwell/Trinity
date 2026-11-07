@echo off
rem Карта Trinity для эмулятора: прошивка на модели железа (как test.bat), плеер Atari - настоящий (AtariAudio, как
rem в прошивке) -> trinity_card.dll рядом с Unreal FT2 (FT2/tools/unreal)
set T=%~dp0
set F=%T%..
set E=%F%\..\..\engine
set AA=%E%\third_party\atariaudio
set OUT=%T%..\..\..\..\FT2\tools\unreal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /LD /O2 /Zi /W3 /utf-8 /EHsc /DSIM_HOST /DSNDH /DNDEBUG /DTRINITY_M68K_COMPACT /D_CRT_SECURE_NO_WARNINGS /wd4100 /wd4244 /wd4267 /wd4018 /wd4146 /wd4305 /wd4996 /I"%T%mock" /I"%F%" /I"%E%" /I"%E%\sndh" /I"%AA%" "%T%trin_dll.c" "%T%mock\sim.c" "%F%\atari.c" "%F%\chip.c" "%F%\card.c" "%T%mock\bus_hw_model.c" "%F%\modes.c" "%F%\config.c" "%E%\ayfx.c" "%E%\svc.c" "%E%\sndh\sndh.cpp" "%AA%\*.cpp" "%AA%\external\*.cpp" "%AA%\external\Musashi\*.cpp" /Fe:"%OUT%\trinity_card.dll" /Fo:"%TEMP%\\" /link /DEBUG || exit /b 1
