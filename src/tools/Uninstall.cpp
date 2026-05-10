#include "BaseToolGui.h"
#include "InstallerLogic.h"

void UninstallTask(ToolGui::GuiContext* ctx) {
    auto updateUi = [ctx](int percent, const wchar_t* status) {
        SendMessageW(ctx->hProgress, PBM_SETPOS, percent, 0);
        SetWindowTextW(ctx->hStatus, status);
    };

    ToolGui::UpdateStatus(ctx, L"Removing associations...", 10);
    Sleep(500);
    ToolGui::UpdateStatus(ctx, L"Unregistering components...", 50);
    InstallerLogic::UnregisterExtension(nullptr);
    ToolGui::UpdateStatus(ctx, L"Cleaning up...", 90);
    Sleep(500);
    ToolGui::UpdateStatus(ctx, L"Quick Convert has been removed.", 100);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    ToolGui::CreateToolWindow(hInst, L"Quick Convert Uninstaller", L"Initializing...", UninstallTask);
    return 0;
}
