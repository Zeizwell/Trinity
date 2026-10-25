@echo off
rem f2lcmp for the PC (MSVC)
set T=%~dp0
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /W3 /O2 /D_CRT_SECURE_NO_WARNINGS "%T%main.c" /Fe:"%T%f2lcmp.exe" /Fo:"%TEMP%\\"
