#include "BaseToolGui.h"
#include <dwmapi.h>
#include <uxtheme.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace ToolGui {

static GuiContext g_ctx = { 0 };
static void (*g_task)(GuiContext*) = nullptr;

bool IsDarkMode() {
    HKEY hKey;
    DWORD dwData = 1;
    DWORD cbData = sizeof(dwData);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"AppsUseLightTheme", NULL, NULL, (LPBYTE)&dwData, &cbData);
        RegCloseKey(hKey);
    }
    return dwData == 0;
}

LRESULT CALLBACK ToolWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, GetTextColor(g_ctx.isDarkMode));
        SetBkColor(hdc, GetBgColor(g_ctx.isDarkMode));
        static HBRUSH hbrStatic = NULL;
        if (!hbrStatic) hbrStatic = CreateSolidBrush(GetBgColor(g_ctx.isDarkMode));
        return (LRESULT)hbrStatic;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        HBRUSH hBr = CreateSolidBrush(GetBgColor(g_ctx.isDarkMode));
        FillRect(hdc, &rc, hBr);
        DeleteObject(hBr);
        
        // Draw top accent bar
        RECT accent = { 0, 0, rc.right, 4 };
        HBRUSH hAcc = CreateSolidBrush(GetAccentColor());
        FillRect(hdc, &accent, hAcc);
        DeleteObject(hAcc);
        
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == 1001) { // Close button
            PostQuitMessage(0);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

void CreateToolWindow(HINSTANCE hInst, const wchar_t* title, const wchar_t* initialStatus, void (*task)(GuiContext*)) {
    g_ctx.isDarkMode = IsDarkMode();
    g_task = task;

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = ToolWindowProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"QuickConvertTool";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc);

    int w = 400, h = 220;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;

    g_ctx.hWnd = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, title, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, x, y, w, h, NULL, NULL, hInst, NULL);
    
    BOOL dark = g_ctx.isDarkMode;
    DwmSetWindowAttribute(g_ctx.hWnd, 20, &dark, sizeof(dark));

    HFONT hFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
    HFONT hFontBold = CreateFontW(22, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");

    HWND hTitle = CreateWindowExW(0, L"STATIC", title, WS_CHILD | WS_VISIBLE, 20, 20, 360, 30, g_ctx.hWnd, NULL, hInst, NULL);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)hFontBold, TRUE);

    g_ctx.hStatus = CreateWindowExW(0, L"STATIC", initialStatus, WS_CHILD | WS_VISIBLE, 20, 60, 360, 25, g_ctx.hWnd, NULL, hInst, NULL);
    SendMessageW(g_ctx.hStatus, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_ctx.hProgress = CreateWindowExW(0, PROGRESS_CLASSW, NULL, WS_CHILD | WS_VISIBLE, 20, 100, 345, 15, g_ctx.hWnd, NULL, hInst, NULL);
    SendMessageW(g_ctx.hProgress, PBM_SETBARCOLOR, 0, GetAccentColor());
    SendMessageW(g_ctx.hProgress, PBM_SETBKCOLOR, 0, g_ctx.isDarkMode ? RGB(40, 40, 40) : RGB(230, 230, 230));

    g_ctx.hButton = CreateWindowExW(0, L"BUTTON", L"Finish", WS_CHILD | BS_PUSHBUTTON, 150, 135, 100, 30, g_ctx.hWnd, (HMENU)1001, hInst, NULL);
    SendMessageW(g_ctx.hButton, WM_SETFONT, (WPARAM)hFont, TRUE);

    ShowWindow(g_ctx.hWnd, SW_SHOW);
    UpdateWindow(g_ctx.hWnd);

    std::thread([task]() {
        task(&g_ctx);
        EnableWindow(g_ctx.hButton, TRUE);
        ShowWindow(g_ctx.hButton, SW_SHOW);
    }).detach();

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void UpdateStatus(GuiContext* ctx, const wchar_t* status, int percent) {
    if (percent >= 0) {
        SendMessageW(ctx->hProgress, PBM_SETPOS, percent, 0);
    }
    
    // Set the text
    SetWindowTextW(ctx->hStatus, status);
    
    // Force a full repaint of the status control area to prevent overlapping text
    RECT rc;
    GetWindowRect(ctx->hStatus, &rc);
    ScreenToClient(ctx->hWnd, (LPPOINT)&rc.left);
    ScreenToClient(ctx->hWnd, (LPPOINT)&rc.right);
    
    InvalidateRect(ctx->hWnd, &rc, TRUE);
    UpdateWindow(ctx->hWnd);
}

}
