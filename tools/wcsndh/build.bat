@echo off
rem SNDH.WMF - Wild Commander plugin: Atari SNDH on the ZX Trinity card. Output next to this file
set T=%~dp0
cd /d %T%
"%T%..\sjasmplus\sjasmplus.exe" --nologo --inc="%T%..\.." "%T%SNDHPLAY.ASM" || exit /b 1
