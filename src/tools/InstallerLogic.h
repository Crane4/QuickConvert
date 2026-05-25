#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <functional>

namespace InstallerLogic {

using ProgressCallback = std::function<void(int, const wchar_t*)>;

// Core operations
bool RegisterExtension(ProgressCallback callback = nullptr);
bool UnregisterExtension(ProgressCallback callback = nullptr);
bool RepairExtension(ProgressCallback callback = nullptr);

// Helpers
bool IsWindows11();
bool RegisterSparsePackage(const std::wstring& manifestPath);
bool UnregisterSparsePackage();
void RestartExplorer();

}
