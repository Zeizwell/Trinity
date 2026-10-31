@echo off
rem Стенд: gs_pt против настоящего реплеера ProTracker из pt2-clone (регистры Паулы на каждом тике).
rem Чужой код в git не лежит: берём его из PT2_SRC (по умолчанию ref/Paula), собираем во временной папке.
rem Запуск: %TEMP%\ptcmp\cmp_pt.exe a.mod b.mod ...
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set T=%~dp0
if "%PT2_SRC%"=="" set PT2_SRC=%T%..\..\ref\Paula\pt2-clone-master\src
set W=%TEMP%\ptcmp
if not exist "%W%" mkdir "%W%"
for %%h in (audio config helpers paula posed scopes tables visuals visuals_sync) do echo #include "stub.h"> "%W%\pt2_%%h.h"
copy /b "%PT2_SRC%\pt2_replayer.c" + "%T%tail.inc" "%W%\pt2_replayer.c" >nul
cl /nologo /W1 /D_CRT_SECURE_NO_WARNINGS "%T%tabcut.c" /Fe:"%W%\tabcut.exe" /Fo:"%W%\\" >nul
"%W%\tabcut.exe" "%PT2_SRC%\pt2_tables.c" "%W%\tables.c"
cl /nologo /W1 /utf-8 /O2 /D_CRT_SECURE_NO_WARNINGS /FI"stdint.h" /I"%T%." "%T%cmp_pt.c" "%W%\pt2_replayer.c" "%W%\tables.c" "%T%..\..\engine\gs\gs_pt.c" /Fe:"%W%\cmp_pt.exe" /Fo:"%W%\\"
