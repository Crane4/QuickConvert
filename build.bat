@echo off
setlocal
cd /d "%~dp0"

:: Check for compiler
where cl.exe >nul 2>nul
if %errorlevel% neq 0 (
    echo [!] cl.exe not found in PATH. Please run this from a Developer Command Prompt.
    exit /b 1
)

if not exist build mkdir build
if not exist dist mkdir dist

echo [*] Compiling resources...
rc /fo build\QuickConvert.res assets\QuickConvert.rc

set "COMMON_FLAGS=/EHsc /O2 /std:c++17 /I include /Fo:build\"
set "LIBS=windowscodecs.lib shlwapi.lib ole32.lib user32.lib advapi32.lib shell32.lib windowsapp.lib"

echo [*] Building QuickConvert.exe...
cl %COMMON_FLAGS% src\QuickConvertExe.cpp src\Converter.cpp build\QuickConvert.res /Fe:dist\QuickConvert.exe /link /subsystem:windows %LIBS% comctl32.lib gdi32.lib

echo [*] Building QuickConvert.dll...
cl %COMMON_FLAGS% src\dllmain.cpp src\CommandProvider.cpp src\Converter.cpp build\QuickConvert.res /Fe:dist\QuickConvert.dll /LD /link /def:assets\QuickConvert.def %LIBS%

if %errorlevel% equ 0 (
    echo [^] Build successful! Binaries are in 'dist' folder.
) else (
    echo [!] Build failed.
)

