@echo off
rem Engine tests on the PC: build with MSVC and run (exit code = number of failures)
rem   ayfx_test - effects and the bus; svc_test - service API (slots, services, jobs, status)
set T=%~dp0
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /W3 /utf-8 /I"%T%.." "%T%ayfx_test.c" "%T%..\ayfx.c" /Fe:"%T%ayfx_test.exe" /Fo:"%TEMP%\\" >nul || exit /b 100
cl /nologo /W3 /utf-8 /I"%T%.." "%T%svc_test.c" "%T%..\svc.c" "%T%..\ayfx.c" /Fe:"%T%svc_test.exe" /Fo:"%TEMP%\\" >nul || exit /b 100
chcp 65001 >nul
"%T%ayfx_test.exe"
set E=%ERRORLEVEL%
"%T%svc_test.exe"
set /a E=E+%ERRORLEVEL%
exit /b %E%
