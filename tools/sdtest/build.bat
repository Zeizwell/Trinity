@echo off
rem sdtest: PC test of the SD protocol and FAT against a card model
set T=%~dp0
set E=%T%..\..\engine
set F=%E%\third_party\fatfs
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /W3 /utf-8 /O2 /D_CRT_SECURE_NO_WARNINGS /DFF_TEST_MKFS /I"%F%" /I"%E%\sd" "%T%main.c" "%E%\sd\sd_card.c" "%E%\sd\sd_disk.c" "%F%\ff.c" "%F%\ffunicode.c" /Fe:"%T%sdtest.exe" /Fo:"%TEMP%\\"
