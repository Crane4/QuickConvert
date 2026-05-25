#include "BaseToolGui.h"
#include "InstallerLogic.h"

void InstallTask(ToolGui::GuiContext* ctx) {
    auto updateUi = [ctx](int percent, const wchar_t* status) {
        ToolGui::UpdateStatus(ctx, status, percent);
    };

    updateUi(5, L"Starting installation...");
    InstallerLogic::RegisterExtension(updateUi);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    ToolGui::CreateToolWindow(hInst, L"Quick Convert Installer", L"Initializing...", InstallTask);
    return 0;
}
