@echo off
setlocal
cd /d "%~dp0"

echo [*] Starting full release build...

:: Ensure no files are locked
taskkill /f /im explorer.exe >nul 2>&1
taskkill /f /im QuickConvert.exe >nul 2>&1
echo [*] Building main application...
call full_build.bat

:: 2. Build tools
echo [*] Building installer tools...
call build_tools.bat

:: 3. Prepare release folder
echo [*] Assembling release folder...
if exist release rd /s /q release
mkdir release
mkdir release\assets

:: Copy binaries from dist
copy /y dist\QuickConvert.exe release\ >nul
copy /y dist\QuickConvert.dll release\ >nul
copy /y dist\Install.exe release\ >nul
copy /y dist\Uninstall.exe release\ >nul
copy /y dist\Repair.exe release\ >nul

:: Copy assets
copy /y assets\AppxManifest.xml release\ >nul
copy /y assets\AppxManifest.xml release\assets\ >nul
copy /y assets\logo.ico release\ >nul
copy /y assets\logo.ico release\assets\ >nul
if exist ffmpeg.exe copy /y ffmpeg.exe release\ >nul

:: Copy documentation
copy /y README.md release\ >nul
copy /y LICENSE release\ >nul

echo [^] Release folder created: %CD%\release

:: 4. Create ZIP
echo [*] Creating ZIP archive...
powershell -Command "Expand-Archive -Path '.' -DestinationPath '.' " >nul 2>&1
powershell -Command "Compress-Archive -Path 'release\*' -DestinationPath 'QuickConvert_v1.0.zip' -Force"

echo [^] SUCCESS! Package ready: QuickConvert_v1.0.zip

:: Restart explorer
start explorer.exe

pause
