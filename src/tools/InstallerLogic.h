#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace InstallerLogic {

struct ProgressCallback {
    void (*update)(int percent, const wchar_t* status);
};

// Core operations
bool RegisterExtension(ProgressCallback* callback = nullptr);
bool UnregisterExtension(ProgressCallback* callback = nullptr);
bool RepairExtension(ProgressCallback* callback = nullptr);

// Helpers
bool IsWindows11();
bool RegisterSparsePackage(const std::wstring& manifestPath);
bool UnregisterSparsePackage();
void RestartExplorer();

}
