#include "InstallerLogic.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <iostream>
#include <algorithm>

#pragma comment(lib, "shlwapi.lib")

namespace InstallerLogic {

const wchar_t* CLSID_QuickConvert = L"{BF5E6C7D-8B9A-4E3D-BC1F-2E4D3C2B1A0F}";
const wchar_t* PACKAGE_NAME = L"QuickConvertShellExtension";

bool SetRegistryValue(HKEY hRoot, const wchar_t* subKey, const wchar_t* valueName, const wchar_t* data) {
    HKEY hKey;
    if (RegCreateKeyExW(hRoot, subKey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS) {
        return false;
    }
    bool success = (RegSetValueExW(hKey, valueName, 0, REG_SZ, (BYTE*)data, (DWORD)((wcslen(data) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS);
    RegCloseKey(hKey);
    return success;
}

bool DeleteRegistryKey(HKEY hRoot, const wchar_t* subKey) {
    return (RegDeleteTreeW(hRoot, subKey) == ERROR_SUCCESS);
}

bool RunCommand(const std::wstring& cmd) {
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(NULL, (LPWSTR)cmd.c_str(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return exitCode == 0;
    }
    return false;
}

bool IsWindows11() {
    OSVERSIONINFOEXW osvi = { sizeof(osvi) };
    osvi.dwBuildNumber = 22000;
    DWORDLONG mask = VerSetConditionMask(0, VER_BUILDNUMBER, VER_GREATER_EQUAL);
    return VerifyVersionInfoW(&osvi, VER_BUILDNUMBER, mask);
}

void RestartExplorer() {
    system("taskkill /f /im explorer.exe >nul 2>&1");
    system("start explorer.exe");
}

bool RegisterSparsePackage(const std::wstring& manifestPath) {
    std::wstring cmd = L"powershell.exe -NoProfile -NonInteractive -Command \"Add-AppxPackage -Register '" + manifestPath + L"'\"";
    return RunCommand(cmd);
}

bool UnregisterSparsePackage() {
    std::wstring cmd = L"powershell.exe -NoProfile -NonInteractive -Command \"Get-AppxPackage *QuickConvert* | Remove-AppxPackage -ErrorAction SilentlyContinue\"";
    return RunCommand(cmd);
}

bool UnblockDirectory(const std::wstring& path) {
    // This command removes the 'Mark of the Web' (Zone.Identifier) from all files in the directory
    std::wstring cmd = L"powershell.exe -NoProfile -NonInteractive -Command \"Get-ChildItem -Path '" + path + L"' -Recurse | Unblock-File\"";
    return RunCommand(cmd);
}

bool RegisterCLSID(HKEY hRoot, const std::wstring& dllPath) {
    // Register CLSID -> InprocServer32 so Windows can load the DLL
    std::wstring clsidKey = std::wstring(L"Software\\Classes\\CLSID\\") + CLSID_QuickConvert;
    std::wstring inprocKey = clsidKey + L"\\InprocServer32";
    if (!SetRegistryValue(hRoot, clsidKey.c_str(), NULL, L"Quick Convert")) return false;
    if (!SetRegistryValue(hRoot, inprocKey.c_str(), NULL, dllPath.c_str())) return false;
    if (!SetRegistryValue(hRoot, inprocKey.c_str(), L"ThreadingModel", L"Both")) return false;

    // Approve the shell extension (required for it to load)
    std::wstring approvedKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved";
    SetRegistryValue(hRoot, approvedKey.c_str(), CLSID_QuickConvert, L"Quick Convert");
    return true;
}

bool RegisterExtension(ProgressCallback* callback) {
    if (callback) callback->update(5, L"Locating files...");

    wchar_t modulePath[MAX_PATH];
    GetModuleFileNameW(NULL, modulePath, MAX_PATH);
    PathRemoveFileSpecW(modulePath);
    std::wstring basePath = modulePath;

    if (callback) callback->update(10, L"Unblocking files...");
    UnblockDirectory(basePath);

    std::wstring dllPath  = basePath + L"\\QuickConvert.dll";
    std::wstring manifestPath = basePath + L"\\AppxManifest.xml";
    std::wstring iconPath = basePath + L"\\logo.ico";

    if (callback) callback->update(20, L"Registering COM Server (HKLM)...");
    // Try regsvr32 first (needs admin) then fall back to manual registry write
    std::wstring regCmd = L"regsvr32 /s \"" + dllPath + L"\"";
    bool regsvr32Ok = RunCommand(regCmd);

    if (callback) callback->update(35, L"Writing CLSID to Registry...");
    // Always write manually – guarantees the entry exists even if regsvr32 was blocked
    RegisterCLSID(HKEY_LOCAL_MACHINE, dllPath);
    RegisterCLSID(HKEY_CURRENT_USER,  dllPath);

    if (callback) callback->update(50, L"Applying Registry Associations...");
    
    std::vector<std::wstring> extensions = { L".pdf", L".mp4", L".mp3", L".pptx", L".ppt", L".docx", L".doc", L".xlsx", L".xls", L".png", L".jpg", L".jpeg", L".webp", L".bmp", L".ico" };
    
    auto install = [&](HKEY hRoot) {
        for (const auto& ext : extensions) {
            std::wstring key = L"Software\\Classes\\" + ext + L"\\shell\\QuickConvert";
            SetRegistryValue(hRoot, key.c_str(), L"MUIVerb", L"Quick Convert");
            SetRegistryValue(hRoot, key.c_str(), L"Icon", iconPath.c_str());
            SetRegistryValue(hRoot, key.c_str(), L"ExplorerCommandHandler", CLSID_QuickConvert);

            std::wstring sfaKey = L"Software\\Classes\\SystemFileAssociations\\" + ext + L"\\shell\\QuickConvert";
            SetRegistryValue(hRoot, sfaKey.c_str(), L"MUIVerb", L"Quick Convert");
            SetRegistryValue(hRoot, sfaKey.c_str(), L"Icon", iconPath.c_str());
            SetRegistryValue(hRoot, sfaKey.c_str(), L"ExplorerCommandHandler", CLSID_QuickConvert);
        }

        const wchar_t* locations[] = { L"*", L"Directory", L"Directory\\Background", L"Folder", L"SystemFileAssociations\\image" };
        for (const auto& loc : locations) {
            std::wstring key = std::wstring(L"Software\\Classes\\") + loc + L"\\shell\\QuickConvert";
            SetRegistryValue(hRoot, key.c_str(), L"MUIVerb", L"Quick Convert");
            SetRegistryValue(hRoot, key.c_str(), L"Icon", iconPath.c_str());
            SetRegistryValue(hRoot, key.c_str(), L"ExplorerCommandHandler", CLSID_QuickConvert);
        }
    };

    install(HKEY_LOCAL_MACHINE);
    install(HKEY_CURRENT_USER);

    if (IsWindows11()) {
        if (callback) callback->update(75, L"Enabling Windows 11 Primary Menu...");
        std::wstring registerCmd = L"powershell.exe -NoProfile -NonInteractive -Command \"Add-AppxPackage -Path '" + manifestPath + L"' -Register -ExternalLocation '" + basePath + L"'\"";
        RunCommand(registerCmd);
    }

    if (callback) callback->update(90, L"Refreshing Explorer...");
    RestartExplorer();

    if (callback) callback->update(100, L"Installation Complete!");
    return true;
}

bool UnregisterExtension(ProgressCallback* callback) {
    if (callback) callback->update(10, L"Closing Explorer...");
    RestartExplorer();
    Sleep(1000);

    if (callback) callback->update(30, L"Removing Sparse Package...");
    // Use the exact command the user confirmed works
    RunCommand(L"powershell.exe -NoProfile -NonInteractive -Command \"Get-AppxPackage QuickConvertShellExtension | Remove-AppxPackage\"");

    if (callback) callback->update(50, L"Unregistering DLL...");
    wchar_t modulePath[MAX_PATH];
    GetModuleFileNameW(NULL, modulePath, MAX_PATH);
    PathRemoveFileSpecW(modulePath);
    std::wstring dllPath = std::wstring(modulePath) + L"\\QuickConvert.dll";
    std::wstring unregCmd = L"regsvr32 /u /s \"" + dllPath + L"\"";
    RunCommand(unregCmd);

    if (callback) callback->update(70, L"Cleaning Registry...");
    
    // Use reg.exe directly as it's often more reliable for deep deletes
    auto regDelete = [](const wchar_t* root, const wchar_t* subKey) {
        std::wstring cmd = L"reg delete \"" + std::wstring(root) + L"\\" + subKey + L"\" /f";
        RunCommand(cmd);
    };

    std::vector<std::wstring> extensions = { L".pdf", L".mp4", L".mp3", L".pptx", L".ppt", L".docx", L".doc", L".xlsx", L".xls", L".png", L".jpg", L".jpeg", L".webp", L".bmp", L".ico" };
    
    const wchar_t* roots[] = { L"HKLM", L"HKCU" };
    for (const auto& root : roots) {
        for (const auto& ext : extensions) {
            regDelete(root, (L"Software\\Classes\\" + ext + L"\\shell\\QuickConvert").c_str());
            regDelete(root, (L"Software\\Classes\\SystemFileAssociations\\" + ext + L"\\shell\\QuickConvert").c_str());
        }
        regDelete(root, L"Software\\Classes\\*\\shell\\QuickConvert");
        regDelete(root, L"Software\\Classes\\SystemFileAssociations\\image\\shell\\QuickConvert");
        regDelete(root, L"Software\\Classes\\Directory\\shell\\QuickConvert");
        regDelete(root, L"Software\\Classes\\Directory\\background\\shell\\QuickConvert");
        regDelete(root, L"Software\\Classes\\Folder\\shell\\QuickConvert");
        regDelete(root, L"Software\\Classes\\CLSID\\{BF5E6C7D-8B9A-4E3D-BC1F-2E4D3C2B1A0F}");
        
        regDelete(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved\\{BF5E6C7D-8B9A-4E3D-BC1F-2E4D3C2B1A0F}");
        regDelete(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\CommandStore\\shell\\QC_PNG");
        regDelete(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\CommandStore\\shell\\QC_JPG");
        regDelete(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\CommandStore\\shell\\QC_PDF");
    }

    if (callback) callback->update(90, L"Restarting Explorer...");
    RestartExplorer();

    if (callback) callback->update(100, L"Uninstallation Complete!");
    return true;
}

bool RepairExtension(ProgressCallback* callback) {
    if (callback) callback->update(10, L"Cleaning old state...");
    UnregisterExtension(nullptr);
    if (callback) callback->update(50, L"Reinstalling...");
    return RegisterExtension(callback);
}

}
