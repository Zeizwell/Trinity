@echo off
rem btp2ft2 for the PC (MSVC): engine/btp + uzlib
set T=%~dp0
set E=%T%..\..\engine
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /W3 /utf-8 /O2 /D_CRT_SECURE_NO_WARNINGS "%T%main.c" "%E%\btp\btp2ft2.c" "%E%\btp\json.c" "%E%\third_party\uzlib\tinflate.c" "%E%\third_party\uzlib\tinfgzip.c" "%E%\third_party\uzlib\crc32.c" "%E%\third_party\uzlib\adler32.c" /Fe:"%T%btp2ft2.exe" /Fo:"%TEMP%\\"
