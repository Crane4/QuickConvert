#include "BaseToolGui.h"
#include "InstallerLogic.h"

void RepairTask(ToolGui::GuiContext* ctx) {
    auto updateUi = [ctx](int percent, const wchar_t* status) {
        ToolGui::UpdateStatus(ctx, status, percent);
    };

    updateUi(5, L"Starting repair...");
    InstallerLogic::RepairExtension(updateUi);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    ToolGui::CreateToolWindow(hInst, L"Quick Convert Repair", L"Initializing...", RepairTask);
    return 0;
}
