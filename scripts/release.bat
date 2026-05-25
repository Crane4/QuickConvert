@echo off
setlocal
cd /d "%~dp0.."

echo [*] Starting full release build...

:: 1. Ensure ffmpeg.exe is present (download if missing)
if not exist ffmpeg.exe (
    echo [*] ffmpeg.exe not found. Downloading FFmpeg release essentials...
    curl -L -o ffmpeg-essentials.zip https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip
    if %errorlevel% equ 0 (
        echo [*] Extracting ffmpeg.exe...
        powershell -Command "Expand-Archive -Path ffmpeg-essentials.zip -DestinationPath temp_ffmpeg -Force"
        powershell -Command "Get-ChildItem -Path temp_ffmpeg -Filter ffmpeg.exe -Recurse | Select-Object -First 1 | ForEach-Object { Copy-Item $_.FullName -Destination '.' }"
        del ffmpeg-essentials.zip
        rd /s /q temp_ffmpeg
        if exist ffmpeg.exe (
            echo [^] FFmpeg downloaded and extracted successfully!
        ) else (
            echo [!] Extraction failed or ffmpeg.exe not found in zip archive.
        )
    ) else (
        echo [!] Failed to download FFmpeg. Media features will require manual setup.
    )
)

:: Ensure no files are locked
taskkill /f /im explorer.exe >nul 2>&1
taskkill /f /im QuickConvert.exe >nul 2>&1
echo [*] Building main application...
call "%~dp0full_build.bat"

:: 2. Build tools
echo [*] Building installer tools...
call "%~dp0build_tools.bat"

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
if exist dist\ffmpeg.exe copy /y dist\ffmpeg.exe release\ >nul

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
