#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <winrt/base.h>
#include <string>
#include <vector>
#include <thread>
#include <algorithm>
#include <cwchar>
#include "Converter.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

HWND g_hWnd = NULL;
HWND g_hTitle = NULL;
HWND g_hSubtitle = NULL;
HWND g_hStatus = NULL;
HWND g_hProgress = NULL;
HWND g_hErrorTitle = NULL;
HWND g_hErrorBody = NULL;
HWND g_hPrimaryButton = NULL;
bool g_Cancelled = false;
bool g_IsDarkMode = false;
COLORREF g_bgColor = RGB(247, 248, 250);
COLORREF g_panelColor = RGB(255, 255, 255);
COLORREF g_headerColor = RGB(17, 24, 39);
COLORREF g_textColor = RGB(17, 24, 39);
COLORREF g_mutedTextColor = RGB(102, 112, 133);
COLORREF g_accentColor = RGB(15, 98, 254);
COLORREF g_errorColor = RGB(196, 38, 54);
COLORREF g_borderColor = RGB(220, 224, 230);
HBRUSH g_hbrBkgnd = NULL;
HFONT g_hFontTitle = NULL;
HFONT g_hFontSubtitle = NULL;
HFONT g_hFontStatus = NULL;
HFONT g_hFontErrorTitle = NULL;
HFONT g_hFontButton = NULL;
std::vector<std::wstring> g_FilesToConvert;
std::wstring g_ErrorMessage;
std::wstring g_WindowSubtitle;

#define WM_UPDATE_PROGRESS (WM_USER + 1)
#define WM_CONVERSION_DONE (WM_USER + 2)
#define IDT_AUTOCLOSE 100
#define IDC_PRIMARY_BUTTON 2

enum class SelectionKind {
    Image,
    Pdf,
    Mp4,
    Mp3,
    Ppt,
    Doc,
    Excel,
    Mixed
};



enum class UiState {
    Working,
    Success,
    Error
};

enum class CompletionState {
    Success = 1,
    Error = 2,
    Cancelled = 3
};

struct ThreadParams {
    std::vector<std::wstring> files;
    TargetFormat format;
};

UiState g_UiState = UiState::Working;

bool IsMediaFormat(TargetFormat format) {
    return format == TargetFormat::Mp4ToMp3
        || format == TargetFormat::Mp4ToMov
        || format == TargetFormat::Mp3ToOgg;
}

std::wstring DescribeConversionError(HRESULT hr, TargetFormat format) {
    const std::wstring& details = Converter::GetLastErrorDetails();
    if (!details.empty()) {
        return details;
    }

    if (IsMediaFormat(format) && hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
        return L"ffmpeg.exe was not found. Put ffmpeg.exe next to QuickConvert.exe or add it to PATH.";
    }


    wchar_t* buffer = nullptr;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD length = FormatMessageW(flags, nullptr, HRESULT_CODE(hr), 0, (LPWSTR)&buffer, 0, nullptr);
    if (length > 0 && buffer) {
        std::wstring message = buffer;
        LocalFree(buffer);
        while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
            message.pop_back();
        }
        return message;
    }

    wchar_t hex[32] = {};
    swprintf_s(hex, L"0x%08X", static_cast<unsigned int>(hr));
    return std::wstring(L"Conversion failed with HRESULT ") + hex + L".";
}

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

void ApplyTheme(bool dark) {
    g_IsDarkMode = dark;
    if (dark) {
        g_bgColor = RGB(11, 15, 20);
        g_panelColor = RGB(22, 28, 36);
        g_headerColor = RGB(14, 19, 26);
        g_textColor = RGB(245, 247, 250);
        g_mutedTextColor = RGB(158, 170, 186);
        g_accentColor = RGB(74, 144, 226);
        g_errorColor = RGB(255, 107, 107);
        g_borderColor = RGB(40, 48, 60);
    } else {
        g_bgColor = RGB(247, 248, 250);
        g_panelColor = RGB(255, 255, 255);
        g_headerColor = RGB(255, 255, 255);
        g_textColor = RGB(17, 24, 39);
        g_mutedTextColor = RGB(102, 112, 133);
        g_accentColor = RGB(15, 98, 254);
        g_errorColor = RGB(196, 38, 54);
        g_borderColor = RGB(220, 224, 230);
    }
}

SelectionKind DetectSelectionKind() {
    if (g_FilesToConvert.empty()) {
        return SelectionKind::Mixed;
    }

    auto classify = [](std::wstring path) {
        std::transform(path.begin(), path.end(), path.begin(), ::towlower);
        if (path.size() > 4 && path.substr(path.size() - 4) == L".pdf") return SelectionKind::Pdf;
        if (path.size() > 4 && path.substr(path.size() - 4) == L".mp4") return SelectionKind::Mp4;
        if (path.size() > 4 && path.substr(path.size() - 4) == L".mp3") return SelectionKind::Mp3;
        if (path.size() > 5) {
            std::wstring ext = path.substr(path.size() - 5);
            if (_wcsicmp(ext.c_str(), L".docx") == 0 || _wcsicmp(ext.c_str(), L".doc") == 0) return SelectionKind::Doc;
            if (_wcsicmp(ext.c_str(), L".xlsx") == 0 || _wcsicmp(ext.c_str(), L".xls") == 0) return SelectionKind::Excel;
            if (_wcsicmp(ext.c_str(), L".pptx") == 0 || _wcsicmp(ext.substr(1).c_str(), L".ppt") == 0) return SelectionKind::Ppt;
        }
        return SelectionKind::Image;
    };


    SelectionKind firstKind = classify(g_FilesToConvert[0]);
    for (size_t i = 1; i < g_FilesToConvert.size(); ++i) {
        if (classify(g_FilesToConvert[i]) != firstKind) {
            return SelectionKind::Mixed;
        }
    }

    return firstKind;
}

std::wstring BuildSubtitleText() {
    if (g_FilesToConvert.empty()) {
        return L"Preparing conversion";
    }

    switch (DetectSelectionKind()) {
    case SelectionKind::Pdf:
        return L"PDF files";
    case SelectionKind::Mp4:
        return L"MP4 files";
    case SelectionKind::Mp3:
        return L"MP3 files";
    case SelectionKind::Ppt:
        return L"PowerPoint Presentation";
    case SelectionKind::Doc:
        return L"Word Document";
    case SelectionKind::Excel:
        return L"Excel Spreadsheet";
    case SelectionKind::Image:
        return L"image files";
    default:
        return L"selected files";
    }
}

void CenterWindowOnCursorMonitor(HWND hWnd, int width, int height) {
    POINT cursor = {};
    GetCursorPos(&cursor);

    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    if (!GetMonitorInfoW(monitor, &mi)) {
        RECT rc = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
        mi.rcWork = rc;
    }

    int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - width) / 2;
    int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - height) / 2;
    SetWindowPos(hWnd, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
}

void ApplyWindowChrome(HWND hWnd) {
    BOOL immersiveDark = g_IsDarkMode ? TRUE : FALSE;
    DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &immersiveDark, sizeof(immersiveDark));
}

void PostStatusUpdate(int percent, const std::wstring& text) {
    std::wstring* copy = new std::wstring(text);
    PostMessageW(g_hWnd, WM_UPDATE_PROGRESS, (WPARAM)percent, (LPARAM)copy);
}

void UpdateStatusText(const std::wstring& text) {
    SetWindowTextW(g_hStatus, text.c_str());
}

void ApplyFonts() {
    SendMessageW(g_hTitle, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);
    SendMessageW(g_hSubtitle, WM_SETFONT, (WPARAM)g_hFontSubtitle, TRUE);
    SendMessageW(g_hStatus, WM_SETFONT, (WPARAM)g_hFontStatus, TRUE);
    SendMessageW(g_hErrorTitle, WM_SETFONT, (WPARAM)g_hFontErrorTitle, TRUE);
    SendMessageW(g_hErrorBody, WM_SETFONT, (WPARAM)g_hFontStatus, TRUE);
    SendMessageW(g_hPrimaryButton, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
}

void SetUiState(UiState state) {
    g_UiState = state;

    if (state == UiState::Working) {
        SetWindowTextW(g_hPrimaryButton, L"Cancel");
        ShowWindow(g_hProgress, SW_SHOW);
        ShowWindow(g_hErrorTitle, SW_HIDE);
        ShowWindow(g_hErrorBody, SW_HIDE);
        ShowWindow(g_hStatus, SW_SHOW);
        SendMessageW(g_hProgress, PBM_SETSTATE, PBST_NORMAL, 0);
    } else if (state == UiState::Success) {
        SetWindowTextW(g_hPrimaryButton, L"Close");
        ShowWindow(g_hProgress, SW_SHOW);
        ShowWindow(g_hErrorTitle, SW_HIDE);
        ShowWindow(g_hErrorBody, SW_HIDE);
        ShowWindow(g_hStatus, SW_SHOW);
        SendMessageW(g_hProgress, PBM_SETSTATE, PBST_NORMAL, 0);
    } else {
        SetWindowTextW(g_hPrimaryButton, L"Close");
        ShowWindow(g_hProgress, SW_HIDE);
        ShowWindow(g_hErrorTitle, SW_HIDE);
        ShowWindow(g_hErrorBody, SW_SHOW);
        ShowWindow(g_hStatus, SW_SHOW);
        SendMessageW(g_hProgress, PBM_SETSTATE, PBST_ERROR, 0);
    }

    InvalidateRect(g_hWnd, NULL, TRUE);
}

void FinalizeUiState(CompletionState state) {
    KillTimer(g_hWnd, IDT_AUTOCLOSE);

    if (state == CompletionState::Success) {
        SetUiState(UiState::Success);
        UpdateStatusText(L"Conversion complete");
        SetWindowTextW(g_hSubtitle, L"Your converted file is ready.");
        SetTimer(g_hWnd, IDT_AUTOCLOSE, 900, NULL);
        return;
    }

    if (state == CompletionState::Cancelled) {
        PostQuitMessage(0);
        return;
    }

    SetUiState(UiState::Error);
    UpdateStatusText(L"Conversion issue");
    SetWindowTextW(g_hSubtitle, L"Please review the message below.");
    SetWindowTextW(g_hErrorBody, g_ErrorMessage.empty() ? L"An unexpected error occurred." : g_ErrorMessage.c_str());
}

void CleanupResources() {
    if (g_hFontTitle) DeleteObject(g_hFontTitle);
    if (g_hFontSubtitle) DeleteObject(g_hFontSubtitle);
    if (g_hFontStatus) DeleteObject(g_hFontStatus);
    if (g_hFontErrorTitle) DeleteObject(g_hFontErrorTitle);
    if (g_hFontButton) DeleteObject(g_hFontButton);
    if (g_hbrBkgnd) DeleteObject(g_hbrBkgnd);
    g_hFontTitle = NULL;
    g_hFontSubtitle = NULL;
    g_hFontStatus = NULL;
    g_hFontErrorTitle = NULL;
    g_hFontButton = NULL;
    g_hbrBkgnd = NULL;
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        SetBkMode(hdcStatic, TRANSPARENT);

        HWND hCtrl = (HWND)lParam;
        COLORREF textColor = g_textColor;
        if (hCtrl == g_hSubtitle) {
            textColor = g_mutedTextColor;
        } else if (hCtrl == g_hErrorBody) {
            textColor = g_UiState == UiState::Error ? g_textColor : g_mutedTextColor;
        } else if (hCtrl == g_hErrorTitle || (g_UiState == UiState::Error && hCtrl == g_hStatus)) {
            textColor = g_errorColor;
        }

        SetTextColor(hdcStatic, textColor);
        return (INT_PTR)GetStockObject(NULL_BRUSH);
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT client;
        GetClientRect(hWnd, &client);

        HBRUSH bgBrush = CreateSolidBrush(g_bgColor);
        FillRect(hdc, &client, bgBrush);
        DeleteObject(bgBrush);

        RECT header = { 0, 0, client.right, 74 };
        HBRUSH headerBrush = CreateSolidBrush(g_headerColor);
        FillRect(hdc, &header, headerBrush);
        DeleteObject(headerBrush);

        RECT accent = { 0, 0, client.right, 5 };
        HBRUSH accentBrush = CreateSolidBrush(g_accentColor);
        FillRect(hdc, &accent, accentBrush);
        DeleteObject(accentBrush);

        RECT card = { 20, 86, client.right - 20, client.bottom - 74 };
        HBRUSH cardBrush = CreateSolidBrush(g_panelColor);
        FillRect(hdc, &card, cardBrush);
        DeleteObject(cardBrush);

        HPEN borderPen = CreatePen(PS_SOLID, 1, g_borderColor);
        HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, card.left, card.top, card.right, card.bottom);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(borderPen);

        RECT rail = { card.left, card.top, card.left + 6, card.bottom };
        HBRUSH railBrush = CreateSolidBrush(g_UiState == UiState::Error ? g_errorColor : g_accentColor);
        FillRect(hdc, &rail, railBrush);
        DeleteObject(railBrush);

        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_UPDATE_PROGRESS: {
        std::wstring* text = reinterpret_cast<std::wstring*>(lParam);
        SendMessageW(g_hProgress, PBM_SETPOS, (int)wParam, 0);
        if (text) {
            UpdateStatusText(*text);
            delete text;
        }
        return 0;
    }
    case WM_CONVERSION_DONE:
        FinalizeUiState((CompletionState)wParam);
        return 0;
    case WM_TIMER:
        if (wParam == IDT_AUTOCLOSE) {
            KillTimer(hWnd, IDT_AUTOCLOSE);
            PostQuitMessage(0);
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_PRIMARY_BUTTON) {
            if (g_UiState == UiState::Working) {
                g_Cancelled = true;
                UpdateStatusText(L"Cancelling...");
                SetWindowTextW(g_hSubtitle, L"Waiting for the current operation to stop.");
                EnableWindow(g_hPrimaryButton, FALSE);
            } else {
                PostQuitMessage(0);
            }
        }
        return 0;
    case WM_CLOSE:
        if (g_UiState == UiState::Working) {
            g_Cancelled = true;
        }
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        CleanupResources();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

void ConversionThread(ThreadParams* params) {
    try { winrt::init_apartment(winrt::apartment_type::multi_threaded); } catch (...) {}

    int total = (int)params->files.size();
    bool allSuccess = true;
    g_ErrorMessage.clear();

    for (int i = 0; i < total; ++i) {
        if (g_Cancelled) {
            PostMessageW(g_hWnd, WM_CONVERSION_DONE, (WPARAM)CompletionState::Cancelled, 0);
            delete params;
            return;
        }

        std::wstring fileName = params->files[i].substr(params->files[i].find_last_of(L"\\") + 1);
        int percent = total > 0 ? (int)((float)i / total * 100) : 0;
        PostStatusUpdate(percent, L"Processing " + fileName);

        HRESULT hr = Converter::ConvertImage(params->files[i], params->format);
        if (FAILED(hr)) {
            allSuccess = false;
            if (g_ErrorMessage.empty()) {
                g_ErrorMessage = DescribeConversionError(hr, params->format);
            }
            break;
        }
    }

    if (g_Cancelled) {
        PostMessageW(g_hWnd, WM_CONVERSION_DONE, (WPARAM)CompletionState::Cancelled, 0);
    } else if (allSuccess) {
        PostStatusUpdate(100, L"Wrapping up");
        PostMessageW(g_hWnd, WM_CONVERSION_DONE, (WPARAM)CompletionState::Success, 0);
    } else {
        PostMessageW(g_hWnd, WM_CONVERSION_DONE, (WPARAM)CompletionState::Error, 0);
    }

    delete params;
}

void ShowFloatingMenu(HWND hWnd) {
    POINT pt;
    GetCursorPos(&pt);

    SelectionKind selectionKind = DetectSelectionKind();

    HMENU hMenu = CreatePopupMenu();
    if (selectionKind == SelectionKind::Pdf) {
        AppendMenuW(hMenu, MF_STRING, 5, L"PDF to PNG (ZIP)");
        AppendMenuW(hMenu, MF_STRING, 6, L"PDF to JPG (ZIP)");
    } else if (selectionKind == SelectionKind::Mp4) {
        AppendMenuW(hMenu, MF_STRING, 7, L"To MP3");
        AppendMenuW(hMenu, MF_STRING, 8, L"To MOV");
    } else if (selectionKind == SelectionKind::Mp3) {
        AppendMenuW(hMenu, MF_STRING, 9, L"To OGG");
    } else if (selectionKind == SelectionKind::Ppt) {
        AppendMenuW(hMenu, MF_STRING, 11, L"To PDF");
    } else {
        AppendMenuW(hMenu, MF_STRING, 1, L"Convert to PNG");

        AppendMenuW(hMenu, MF_STRING, 2, L"Convert to JPG");
        AppendMenuW(hMenu, MF_STRING, 3, L"Convert to PDF");
        AppendMenuW(hMenu, MF_STRING, 4, L"Convert to WebP");
        AppendMenuW(hMenu, MF_STRING, 10, L"Convert to ICO");
    }

    SetForegroundWindow(hWnd);
    int sel = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);

    if (sel > 0) {
        TargetFormat fmt = TargetFormat::Png;
        if (sel == 2) fmt = TargetFormat::Jpg;
        else if (sel == 3) fmt = TargetFormat::Pdf;
        else if (sel == 4) fmt = TargetFormat::Webp;
        else if (sel == 5) fmt = TargetFormat::PdfToPngZip;
        else if (sel == 6) fmt = TargetFormat::PdfToJpgZip;
        else if (sel == 7) fmt = TargetFormat::Mp4ToMp3;
        else if (sel == 8) fmt = TargetFormat::Mp4ToMov;
        else if (sel == 9) fmt = TargetFormat::Mp3ToOgg;
        else if (sel == 10) fmt = TargetFormat::Ico;
        else if (sel == 11) fmt = TargetFormat::PptToPdf;


        g_Cancelled = false;
        g_ErrorMessage.clear();
        g_WindowSubtitle = BuildSubtitleText();
        SetWindowTextW(g_hSubtitle, g_WindowSubtitle.c_str());
        UpdateStatusText(L"Preparing conversion");
        SetWindowTextW(g_hErrorTitle, L"");
        SetWindowTextW(g_hErrorBody, L"");
        EnableWindow(g_hPrimaryButton, TRUE);
        SetUiState(UiState::Working);
        ShowWindow(hWnd, SW_SHOW);
        SetForegroundWindow(hWnd);

        ThreadParams* params = new ThreadParams{ g_FilesToConvert, fmt };
        std::thread(ConversionThread, params).detach();
    } else {
        PostQuitMessage(0);
    }
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc < 2) return 0;

    TargetFormat g_TargetFormat = TargetFormat::Png;
    bool formatSet = false;
    for (int i = 1; i < argc; i++) {
        std::wstring arg = argv[i];
        if (arg == L"png") { g_TargetFormat = TargetFormat::Png; formatSet = true; }
        else if (arg == L"jpg") { g_TargetFormat = TargetFormat::Jpg; formatSet = true; }
        else if (arg == L"pdf") { g_TargetFormat = TargetFormat::Pdf; formatSet = true; }
        else if (arg == L"webp") { g_TargetFormat = TargetFormat::Webp; formatSet = true; }
        else if (arg == L"ico") { g_TargetFormat = TargetFormat::Ico; formatSet = true; }
        else if (arg == L"pdfzip") { g_TargetFormat = TargetFormat::PdfToPngZip; formatSet = true; }
        else if (arg == L"pdfjpgzip") { g_TargetFormat = TargetFormat::PdfToJpgZip; formatSet = true; }
        else if (arg == L"mp4mp3") { g_TargetFormat = TargetFormat::Mp4ToMp3; formatSet = true; }
        else if (arg == L"mp4mov") { g_TargetFormat = TargetFormat::Mp4ToMov; formatSet = true; }
        else if (arg == L"mp3ogg") { g_TargetFormat = TargetFormat::Mp3ToOgg; formatSet = true; }
        else if (arg == L"pptxpdf") { g_TargetFormat = TargetFormat::PptToPdf; formatSet = true; }
        else if (arg == L"docxpdf") { g_TargetFormat = TargetFormat::WordToPdf; formatSet = true; }
        else if (arg == L"xlsxpdf") { g_TargetFormat = TargetFormat::ExcelToPdf; formatSet = true; }
        else if (arg == L"pdfword") { g_TargetFormat = TargetFormat::PdfToWord; formatSet = true; }
        else {
            g_FilesToConvert.push_back(arg);
        }
    }

    g_WindowSubtitle = BuildSubtitleText();

    ApplyTheme(IsDarkMode());
    g_hbrBkgnd = CreateSolidBrush(g_bgColor);

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"QuickConvertModern";
    wc.hbrBackground = g_hbrBkgnd;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc);

    const int windowWidth = 560;
    const int windowHeight = 324;

    g_hWnd = CreateWindowExW(
        WS_EX_TOPMOST,
        wc.lpszClassName,
        L"Quick Convert",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT,
        windowWidth, windowHeight,
        NULL, NULL, hInst, NULL);

    ApplyWindowChrome(g_hWnd);
    CenterWindowOnCursorMonitor(g_hWnd, windowWidth, windowHeight);

    g_hTitle = CreateWindowExW(0, L"STATIC", L"Quick Convert", WS_CHILD | WS_VISIBLE, 28, 24, 300, 32, g_hWnd, NULL, hInst, NULL);
    g_hSubtitle = CreateWindowExW(0, L"STATIC", g_WindowSubtitle.c_str(), WS_CHILD | WS_VISIBLE, 28, 52, 500, 24, g_hWnd, NULL, hInst, NULL);
    g_hStatus = CreateWindowExW(0, L"STATIC", L"Preparing conversion", WS_CHILD | WS_VISIBLE | SS_LEFT, 44, 118, 470, 28, g_hWnd, NULL, hInst, NULL);
    g_hProgress = CreateWindowExW(0, PROGRESS_CLASSW, NULL, WS_CHILD | WS_VISIBLE, 44, 162, 470, 16, g_hWnd, NULL, hInst, NULL);
    g_hErrorTitle = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_LEFT, 44, 0, 0, 0, g_hWnd, NULL, hInst, NULL);
    g_hErrorBody = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_LEFT, 44, 162, 470, 54, g_hWnd, NULL, hInst, NULL);
    g_hPrimaryButton = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 232, 244, 96, 34, g_hWnd, (HMENU)IDC_PRIMARY_BUTTON, hInst, NULL);

    g_hFontTitle = CreateFontW(24, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
    g_hFontSubtitle = CreateFontW(16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
    g_hFontStatus = CreateFontW(18, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
    g_hFontErrorTitle = CreateFontW(18, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
    g_hFontButton = CreateFontW(16, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
    ApplyFonts();

    SendMessageW(g_hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SendMessageW(g_hProgress, PBM_SETBKCOLOR, 0, g_IsDarkMode ? RGB(39, 48, 61) : RGB(232, 236, 241));
    SendMessageW(g_hProgress, PBM_SETBARCOLOR, 0, g_accentColor);

    SetUiState(UiState::Working);

    if (!formatSet) {
        ShowFloatingMenu(g_hWnd);
    } else {
        g_Cancelled = false;
        EnableWindow(g_hPrimaryButton, TRUE);
        ShowWindow(g_hWnd, SW_SHOW);
        UpdateWindow(g_hWnd);

        ThreadParams* params = new ThreadParams{ g_FilesToConvert, g_TargetFormat };
        std::thread(ConversionThread, params).detach();
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}
