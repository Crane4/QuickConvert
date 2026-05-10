#include <windows.h>
#include "CommandProvider.h"

HINSTANCE g_hInst = NULL;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        g_hInst = hModule;
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (IsEqualCLSID(rclsid, CLSID_QuickConvert)) {
        return CreateCommandProvider(riid, ppv);
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

STDAPI DllCanUnloadNow() {
    return S_OK;
}

STDAPI DllRegisterServer() {
    // Sparse Package registration handles this on Win11, 
    // but we can keep registry fallback for Win10.
    wchar_t szPath[MAX_PATH];
    GetModuleFileNameW(g_hInst, szPath, MAX_PATH);

    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, L"CLSID\\{BF5E6C7D-8B9A-4E3D-BC1F-2E4D3C2B1A0F}", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, NULL, 0, REG_SZ, (BYTE*)L"Quick Convert", (DWORD)(wcslen(L"Quick Convert") + 1) * 2);
        HKEY hInProc;
        if (RegCreateKeyExW(hKey, L"InprocServer32", 0, NULL, 0, KEY_WRITE, NULL, &hInProc, NULL) == ERROR_SUCCESS) {
            RegSetValueExW(hInProc, NULL, 0, REG_SZ, (BYTE*)szPath, (DWORD)(wcslen(szPath) + 1) * 2);
            RegSetValueExW(hInProc, L"ThreadingModel", 0, REG_SZ, (BYTE*)L"Both", (DWORD)(wcslen(L"Both") + 1) * 2);
            RegCloseKey(hInProc);
        }
        RegCloseKey(hKey);
    }
    return S_OK;
}

STDAPI DllUnregisterServer() {
    RegDeleteTreeW(HKEY_CLASSES_ROOT, L"CLSID\\{BF5E6C7D-8B9A-4E3D-BC1F-2E4D3C2B1A0F}");
    return S_OK;
}
