@echo off
rem RP-AY firmware on the PC (hardware model): build with MSVC and run (exit code = number of failures)
set T=%~dp0
set F=%T%..
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /W3 /utf-8 /DSIM_HOST /wd4100 /I"%T%mock" /I"%F%" /I"%F%\..\..\engine" "%T%host_test.c" "%T%mock\sim.c" "%F%\chip.c" "%F%\card.c" "%F%\modes.c" "%F%\config.c" "%F%\..\..\engine\ayfx.c" "%F%\..\..\engine\svc.c" /Fe:"%T%host_test.exe" /Fo:"%TEMP%\\" || exit /b 100
chcp 65001 >nul
"%T%host_test.exe"
