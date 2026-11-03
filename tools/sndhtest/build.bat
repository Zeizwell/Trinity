@echo off
rem sndhtest: PC test of engine/sndh - AtariAudio YM sound vs the same YM fed only with our timed writes
set T=%~dp0
set E=%T%..\..\engine
set A=%E%\third_party\atariaudio
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /W3 /utf-8 /O2 /EHsc /std:c++20 /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /I"%E%\sndh" /I"%A%" "%T%main.cpp" "%E%\sndh\sndh.cpp" "%A%\*.cpp" "%A%\external\*.cpp" "%A%\external\Musashi\*.cpp" /Fe:"%T%sndhtest.exe" /Fo:"%TEMP%\\" > "%TEMP%\sndhtest_build.txt" || (type "%TEMP%\sndhtest_build.txt" & exit /b 1)
