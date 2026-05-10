@echo off
setlocal
cd /d "%~dp0"

:: Check for Admin
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [!] PLEASE RUN AS ADMINISTRATOR!
    pause
    exit /b
)

echo [*] Cleaning previous configuration...
taskkill /f /im explorer.exe >nul 2>&1
taskkill /f /im QuickConvert.exe >nul 2>&1

:: Clean HKCU entries to avoid overrides
reg delete "HKEY_CURRENT_USER\Software\Classes\*\shell\QuickConvert" /f >nul 2>&1
reg delete "HKEY_CURRENT_USER\Software\Classes\.pdf\shell\QuickConvert" /f >nul 2>&1
reg delete "HKEY_CURRENT_USER\Software\Classes\.mp4\shell\QuickConvert" /f >nul 2>&1
reg delete "HKEY_CURRENT_USER\Software\Classes\.mp3\shell\QuickConvert" /f >nul 2>&1
reg delete "HKEY_CURRENT_USER\Software\Classes\.pptx\shell\QuickConvert" /f >nul 2>&1
reg delete "HKEY_CURRENT_USER\Software\Classes\.ppt\shell\QuickConvert" /f >nul 2>&1
reg delete "HKEY_CURRENT_USER\Software\Classes\.docx\shell\QuickConvert" /f >nul 2>&1
reg delete "HKEY_CURRENT_USER\Software\Classes\.doc\shell\QuickConvert" /f >nul 2>&1
reg delete "HKEY_CURRENT_USER\Software\Classes\.xlsx\shell\QuickConvert" /f >nul 2>&1
reg delete "HKEY_CURRENT_USER\Software\Classes\.xls\shell\QuickConvert" /f >nul 2>&1
reg delete "HKEY_CURRENT_USER\Software\Classes\SystemFileAssociations\image\shell\QuickConvert" /f >nul 2>&1
reg delete "HKEY_CURRENT_USER\Software\Classes\SystemFileAssociations\.pdf\shell\QuickConvert" /f >nul 2>&1
reg delete "HKEY_CURRENT_USER\Software\Classes\SystemFileAssociations\.mp4\shell\QuickConvert" /f >nul 2>&1
reg delete "HKEY_CURRENT_USER\Software\Classes\SystemFileAssociations\.mp3\shell\QuickConvert" /f >nul 2>&1

:: Clean HKLM Command Store (Legacy entries)
set "CS_PATH=HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\CommandStore\shell"
reg delete "%CS_PATH%\QC_PNG" /f >nul 2>&1
reg delete "%CS_PATH%\QC_JPG" /f >nul 2>&1
reg delete "%CS_PATH%\QC_PDF" /f >nul 2>&1
reg delete "%CS_PATH%\QC_ICO" /f >nul 2>&1
reg delete "%CS_PATH%\QC_PDF_PNG" /f >nul 2>&1
reg delete "%CS_PATH%\QC_PDF_JPG" /f >nul 2>&1
reg delete "%CS_PATH%\QC_MP4_MP3" /f >nul 2>&1
reg delete "%CS_PATH%\QC_MP4_MOV" /f >nul 2>&1
reg delete "%CS_PATH%\QC_MP3_OGG" /f >nul 2>&1
reg delete "%CS_PATH%\QC_PPT_PDF" /f >nul 2>&1
reg delete "%CS_PATH%\QC_DOC_PDF" /f >nul 2>&1
reg delete "%CS_PATH%\QC_XLS_PDF" /f >nul 2>&1

echo [*] Registering Shell Extension DLL...
regsvr32 /s "..\dist\QuickConvert.dll"

echo [*] Applying file associations...
reg import register.reg

echo [*] Restarting Explorer...
start explorer.exe

echo [^] SUCCESS! 
echo [^] Quick Convert is now registered for Images, PDFs, Office Documents, and Media.
echo [^] If you are on Windows 11, the menu should appear in the primary context menu.
pause


