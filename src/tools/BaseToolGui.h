#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <thread>
#include "InstallerLogic.h"

namespace ToolGui {

struct GuiContext {
    HWND hWnd;
    HWND hStatus;
    HWND hProgress;
    HWND hButton;
    bool isDarkMode;
};

// Colors matching QuickConvert main app
inline COLORREF GetBgColor(bool dark) { return dark ? RGB(11, 15, 20) : RGB(247, 248, 250); }
inline COLORREF GetTextColor(bool dark) { return dark ? RGB(245, 247, 250) : RGB(17, 24, 39); }
inline COLORREF GetAccentColor() { return RGB(15, 98, 254); }

void CreateToolWindow(HINSTANCE hInst, const wchar_t* title, const wchar_t* initialStatus, void (*task)(GuiContext*));
void UpdateStatus(GuiContext* ctx, const wchar_t* status, int percent = -1);

}
