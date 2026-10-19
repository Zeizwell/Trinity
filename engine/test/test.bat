@echo off
rem Engine test on the PC: build with MSVC and run (exit code = number of failures)
set T=%~dp0
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /W3 /utf-8 /I"%T%.." "%T%ayfx_test.c" "%T%..\ayfx.c" /Fe:"%T%ayfx_test.exe" /Fo:"%TEMP%\\" >nul || exit /b 100
chcp 65001 >nul
"%T%ayfx_test.exe"
