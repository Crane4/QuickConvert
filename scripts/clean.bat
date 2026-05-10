@echo off
setlocal
cd /d "%~dp0"

echo [*] Removing build artifacts...
if exist "..\build" rd /s /q "..\build"
if exist "..\dist" rd /s /q "..\dist"

:: Remove any stray build files in root if they exist
del /q "..\*.obj" 2>nul
del /q "..\*.res" 2>nul
del /q "..\*.exp" 2>nul
del /q "..\*.lib" 2>nul
del /q "..\*.old" 2>nul
del /q "..\QuickConvert.exe" 2>nul
del /q "..\QuickConvert.dll" 2>nul

echo [^] Done.
