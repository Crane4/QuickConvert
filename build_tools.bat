@echo off
setlocal
cd /d "%~dp0"

:: Set paths
set "VC_VARS=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VC_VARS%" set "VC_VARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"

call "%VC_VARS%" x64

if not exist build mkdir build
if not exist dist mkdir dist

set "FLAGS=/EHsc /MT /O2 /std:c++17 /I src\tools /Fo:build\"
set "LIBS=user32.lib shell32.lib advapi32.lib comctl32.lib dwmapi.lib uxtheme.lib ole32.lib shlwapi.lib gdi32.lib"

echo [*] Compiling shared logic...
cl /c %FLAGS% src\tools\InstallerLogic.cpp
cl /c %FLAGS% src\tools\BaseToolGui.cpp

echo [*] Building Install.exe...
cl %FLAGS% src\tools\Install.cpp build\InstallerLogic.obj build\BaseToolGui.obj /Fe:dist\Install.exe /link /subsystem:windows %LIBS% /MANIFESTUAC:"level='requireAdministrator' uiAccess='false'"

echo [*] Building Uninstall.exe...
cl %FLAGS% src\tools\Uninstall.cpp build\InstallerLogic.obj build\BaseToolGui.obj /Fe:dist\Uninstall.exe /link /subsystem:windows %LIBS% /MANIFESTUAC:"level='requireAdministrator' uiAccess='false'"

echo [*] Building Repair.exe...
cl %FLAGS% src\tools\Repair.cpp build\InstallerLogic.obj build\BaseToolGui.obj /Fe:dist\Repair.exe /link /subsystem:windows %LIBS% /MANIFESTUAC:"level='requireAdministrator' uiAccess='false'"

echo [^] Tools built successfully in 'dist' folder.
