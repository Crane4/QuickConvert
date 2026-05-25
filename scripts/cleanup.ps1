# Quick Convert ULTIMATE Cleanup Script
# MUST RUN AS ADMINISTRATOR

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole] "Administrator")) {
    Write-Host "ERROR: You MUST run this script as Administrator!" -ForegroundColor Red
    pause
    exit
}

Write-Host "--- Quick Convert Deep Cleanup ---" -ForegroundColor Cyan

# 1. Kill Explorer to unlock files/registry
Write-Host "[*] Closing Explorer..."
Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2

# 2. Unregister DLL
Write-Host "[*] Unregistering DLL..."
$projectRoot = Split-Path $PSScriptRoot -Parent
$dllPath = Join-Path $projectRoot "dist\QuickConvert.dll"
if (Test-Path $dllPath) {
    regsvr32.exe /u /s $dllPath
}

# 3. Targeted Registry Nuke
Write-Host "[*] Cleaning Registry..."
$keysToDelete = @(
    "HKLM:\SOFTWARE\Classes\*\shell\QuickConvert",
    "HKLM:\SOFTWARE\Classes\Directory\shell\QuickConvert",
    "HKLM:\SOFTWARE\Classes\Directory\Background\shell\QuickConvert",
    "HKLM:\SOFTWARE\Classes\Folder\shell\QuickConvert",
    "HKLM:\SOFTWARE\Classes\SystemFileAssociations\image\shell\QuickConvert",
    "HKLM:\SOFTWARE\Classes\CLSID\{BF5E6C7D-8B9A-4E3D-BC1F-2E4D3C2B1A0F}",
    "HKCU:\SOFTWARE\Classes\*\shell\QuickConvert",
    "HKCU:\SOFTWARE\Classes\CLSID\{BF5E6C7D-8B9A-4E3D-BC1F-2E4D3C2B1A0F}"
)

# Add all extension-based keys
$exts = @(".pdf", ".mp4", ".mp3", ".pptx", ".ppt", ".docx", ".doc", ".xlsx", ".xls", ".png", ".jpg", ".jpeg", ".webp", ".bmp", ".ico")
foreach ($ext in $exts) {
    $keysToDelete += "HKLM:\SOFTWARE\Classes\$ext\shell\QuickConvert"
    $keysToDelete += "HKCU:\SOFTWARE\Classes\$ext\shell\QuickConvert"
    $keysToDelete += "HKLM:\SOFTWARE\Classes\SystemFileAssociations\$ext\shell\QuickConvert"
}

foreach ($key in $keysToDelete) {
    if (Test-Path $key) {
        Write-Host "Deleting $key" -ForegroundColor Yellow
        Remove-Item -Path $key -Recurse -Force -ErrorAction SilentlyContinue
    }
}

# 4. Modern Menu (Sparse Package) Nuke
Write-Host "[*] Removing Windows 11 Modern Menu Package..."
Get-AppxPackage -AllUsers *QuickConvert* | Remove-AppxPackage -AllUsers -ErrorAction SilentlyContinue

# 5. Restart Explorer
Write-Host "[*] Restarting Explorer..."
Start-Process explorer.exe

Write-Host "`n--- CLEANUP COMPLETE ---" -ForegroundColor Green
Write-Host "If the menu is still there, please RESTART your computer."
pause
