@echo off
setlocal
cd /d "%~dp0.."

set "VC_VARS=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VC_VARS%" set "VC_VARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VC_VARS%" set "VC_VARS=D:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VC_VARS%" set "VC_VARS=D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"

call "%VC_VARS%" x64

echo [*] Preparing directories...
if not exist build mkdir build
if not exist dist mkdir dist

taskkill /f /im QuickConvert.exe >nul 2>&1

echo [*] Compiling resources...
rc /fo build\QuickConvert.res assets\QuickConvert.rc

set "COMMON_FLAGS=/EHsc /MT /O2 /std:c++17 /I include /Fo:build\"
set "LIBS=windowscodecs.lib shlwapi.lib ole32.lib user32.lib advapi32.lib shell32.lib windowsapp.lib"

echo [*] Building QuickConvert.exe (GUI/Logic)...
cl %COMMON_FLAGS% src\QuickConvertExe.cpp src\Converter.cpp build\QuickConvert.res /Fe:dist\QuickConvert.exe /link /subsystem:windows %LIBS% comctl32.lib gdi32.lib

echo [*] Building QuickConvert.dll (Shell Extension)...
cl %COMMON_FLAGS% src\dllmain.cpp src\CommandProvider.cpp src\Converter.cpp build\QuickConvert.res /Fe:dist\QuickConvert.dll /LD /link /def:assets\QuickConvert.def %LIBS%

echo [*] Copying assets to dist...
copy /y assets\AppxManifest.xml dist\ >nul
copy /y assets\logo.ico dist\ >nul
if exist assets\logo.png copy /y assets\logo.png dist\ >nul
if exist ffmpeg.exe copy /y ffmpeg.exe dist\ >nul

echo [^] Build Complete! Check the 'dist' folder.

