#include "BaseToolGui.h"
#include "InstallerLogic.h"

void RepairTask(ToolGui::GuiContext* ctx) {
    auto updateUi = [ctx](int percent, const wchar_t* status) {
        SendMessageW(ctx->hProgress, PBM_SETPOS, percent, 0);
        SetWindowTextW(ctx->hStatus, status);
    };

    ToolGui::UpdateStatus(ctx, L"Resetting configuration...", 20);
    InstallerLogic::UnregisterExtension(nullptr);
    Sleep(500);
    ToolGui::UpdateStatus(ctx, L"Restoring associations...", 60);
    InstallerLogic::RegisterExtension(nullptr);
    ToolGui::UpdateStatus(ctx, L"Repair complete! Everything is back to normal.", 100);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    ToolGui::CreateToolWindow(hInst, L"Quick Convert Repair", L"Initializing...", RepairTask);
    return 0;
}
