#include "BaseToolGui.h"
#include "InstallerLogic.h"

void UninstallTask(ToolGui::GuiContext* ctx) {
    auto updateUi = [ctx](int percent, const wchar_t* status) {
        ToolGui::UpdateStatus(ctx, status, percent);
    };

    updateUi(5, L"Starting uninstallation...");
    InstallerLogic::UnregisterExtension(updateUi);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    ToolGui::CreateToolWindow(hInst, L"Quick Convert Uninstaller", L"Initializing...", UninstallTask);
    return 0;
}
