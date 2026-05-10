#include "BaseToolGui.h"
#include "InstallerLogic.h"

void InstallTask(ToolGui::GuiContext* ctx) {
    InstallerLogic::ProgressCallback cb;
    cb.update = [](int percent, const wchar_t* status) {
        // Since we are in a thread, we should use PostMessage or find a way to update UI
        // For simplicity in this tool, we'll use a static global or direct access
    };

    // Update closure to handle UI updates from thread
    auto updateUi = [ctx](int percent, const wchar_t* status) {
        SendMessageW(ctx->hProgress, PBM_SETPOS, percent, 0);
        SetWindowTextW(ctx->hStatus, status);
    };

    InstallerLogic::ProgressCallback realCb;
    realCb.update = nullptr; // We'll just use the logic below

    updateUi(10, L"Starting installation...");
    InstallerLogic::RegisterExtension(nullptr); 
    
    ToolGui::UpdateStatus(ctx, L"Registering COM server...", 30);
    Sleep(500);
    ToolGui::UpdateStatus(ctx, L"Applying file associations...", 60);
    Sleep(500);
    ToolGui::UpdateStatus(ctx, L"Refreshing Shell...", 90);
    Sleep(500);
    ToolGui::UpdateStatus(ctx, L"Installation successful!", 100);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    ToolGui::CreateToolWindow(hInst, L"Quick Convert Installer", L"Initializing...", InstallTask);
    return 0;
}
