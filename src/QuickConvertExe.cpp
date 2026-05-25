#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <winrt/base.h>
#include <string>
#include <vector>
#include <thread>
#include <algorithm>
#include <cwchar>
#include <shlwapi.h>
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
    Gif,
    Pdf,
    Video,
    Audio,
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
        || format == TargetFormat::Mp3ToOgg
        || format == TargetFormat::MovToMp3
        || format == TargetFormat::MovToMp4
        || format == TargetFormat::VideoToGif
        || format == TargetFormat::AudioToWav;
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
        const wchar_t* extension = PathFindExtensionW(path.c_str());
        if (!extension) {
            return SelectionKind::Image;
        }
        std::wstring ext = extension;
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        
        if (ext == L".pdf") return SelectionKind::Pdf;
        if (ext == L".gif") return SelectionKind::Gif;

        if (ext == L".mp4" || ext == L".mov" || ext == L".mkv" || ext == L".avi" || ext == L".webm") {
            return SelectionKind::Video;
        }

        if (ext == L".mp3" || ext == L".wav" || ext == L".m4a" || ext == L".flac" || ext == L".aac" || ext == L".ogg") {
            return SelectionKind::Audio;
        }

        if (ext == L".docx" || ext == L".doc") return SelectionKind::Doc;
        if (ext == L".xlsx" || ext == L".xls") return SelectionKind::Excel;
        if (ext == L".pptx" || ext == L".ppt") return SelectionKind::Ppt;
        
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
    case SelectionKind::Gif:
        return L"GIF files";
    case SelectionKind::Video:
        return L"Video files";
    case SelectionKind::Audio:
        return L"Audio files";
    case SelectionKind::Ppt:
        return L"PowerPoint Presentation";
    case SelectionKind::Doc:
        return L"Word Document";
    case SelectionKind::Excel:
        return L"Excel Spreadsheet";
    case SelectionKind::Image:
        return L"Image files";
    default:
        return L"Selected files";
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

void SetStaticText(HWND hStatic, const std::wstring& text) {
    SetWindowTextW(hStatic, text.c_str());
    if (g_hWnd && hStatic) {
        RECT rc;
        GetWindowRect(hStatic, &rc);
        MapWindowPoints(HWND_DESKTOP, g_hWnd, (LPPOINT)&rc, 2);
        InvalidateRect(g_hWnd, &rc, TRUE);
    }
}

void UpdateStatusText(const std::wstring& text) {
    SetStaticText(g_hStatus, text);
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
        SetStaticText(g_hSubtitle, L"Your converted file is ready.");
        SetTimer(g_hWnd, IDT_AUTOCLOSE, 900, NULL);
        return;
    }

    if (state == CompletionState::Cancelled) {
        PostQuitMessage(0);
        return;
    }

    SetUiState(UiState::Error);
    UpdateStatusText(L"Conversion issue");
    SetStaticText(g_hSubtitle, L"Please review the message below.");
    SetStaticText(g_hErrorBody, g_ErrorMessage.empty() ? L"An unexpected error occurred." : g_ErrorMessage.c_str());
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
                SetStaticText(g_hSubtitle, L"Waiting for the current operation to stop.");
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

struct WatermarkDialogParams {
    std::wstring text;
    bool confirmed;
    bool isDarkMode;
};

INT_PTR CALLBACK WatermarkDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    WatermarkDialogParams* params = (WatermarkDialogParams*)GetWindowLongPtrW(hDlg, DWLP_USER);
    switch (message) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(hDlg, DWLP_USER, lParam);
        params = (WatermarkDialogParams*)lParam;

        BOOL immersiveDark = params->isDarkMode ? TRUE : FALSE;
        DwmSetWindowAttribute(hDlg, DWMWA_USE_IMMERSIVE_DARK_MODE, &immersiveDark, sizeof(immersiveDark));

        RECT rcParent, rcDlg;
        GetWindowRect(GetParent(hDlg), &rcParent);
        GetWindowRect(hDlg, &rcDlg);
        int x = rcParent.left + ((rcParent.right - rcParent.left) - (rcDlg.right - rcDlg.left)) / 2;
        int y = rcParent.top + ((rcParent.bottom - rcParent.top) - (rcDlg.bottom - rcDlg.top)) / 2;
        SetWindowPos(hDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        SetDlgItemTextW(hDlg, 101, L"QUICK CONVERT");
        
        HWND hOk = GetDlgItem(hDlg, IDOK);
        HWND hCancel = GetDlgItem(hDlg, IDCANCEL);
        HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
        SendMessageW(GetDlgItem(hDlg, 100), WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(GetDlgItem(hDlg, 101), WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hOk, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hCancel, WM_SETFONT, (WPARAM)hFont, TRUE);
        return (INT_PTR)TRUE;
    }
    case WM_CTLCOLORDLG: {
        HDC hdc = (HDC)wParam;
        COLORREF bg = params->isDarkMode ? RGB(22, 28, 36) : RGB(255, 255, 255);
        SetBkColor(hdc, bg);
        static HBRUSH hbr = NULL;
        if (hbr) DeleteObject(hbr);
        hbr = CreateSolidBrush(bg);
        return (INT_PTR)hbr;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        COLORREF fg = params->isDarkMode ? RGB(245, 247, 250) : RGB(17, 24, 39);
        SetTextColor(hdc, fg);
        static HBRUSH hbr = NULL;
        if (hbr) DeleteObject(hbr);
        hbr = CreateSolidBrush(params->isDarkMode ? RGB(22, 28, 36) : RGB(255, 255, 255));
        return (INT_PTR)hbr;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        COLORREF bg = params->isDarkMode ? RGB(11, 15, 20) : RGB(247, 248, 250);
        COLORREF fg = params->isDarkMode ? RGB(245, 247, 250) : RGB(17, 24, 39);
        SetBkColor(hdc, bg);
        SetTextColor(hdc, fg);
        static HBRUSH hbr = NULL;
        if (hbr) DeleteObject(hbr);
        hbr = CreateSolidBrush(bg);
        return (INT_PTR)hbr;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            wchar_t buf[256] = {};
            GetDlgItemTextW(hDlg, 101, buf, 256);
            params->text = buf;
            params->confirmed = true;
            EndDialog(hDlg, IDOK);
            return (INT_PTR)TRUE;
        } else if (LOWORD(wParam) == IDCANCEL) {
            params->confirmed = false;
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

bool PromptForWatermarkText(HWND hWndParent, std::wstring& outText, bool isDarkMode) {
#pragma pack(push, 2)
    struct DLGTEMPLATEEX {
        WORD dlgVer;
        WORD signature;
        DWORD helpID;
        DWORD exStyle;
        DWORD style;
        WORD cDlgItems;
        short x;
        short y;
        short cx;
        short cy;
    };
#pragma pack(pop)

    DLGTEMPLATEEX lpdg = {0};
    lpdg.dlgVer = 1;
    lpdg.signature = 0xFFFF;
    lpdg.style = WS_POPUP | WS_BORDER | WS_SYSMENU | WS_CAPTION | DS_MODALFRAME | DS_SETFONT;
    lpdg.cDlgItems = 4;
    lpdg.cx = 240;
    lpdg.cy = 90;

    std::vector<BYTE> memory(sizeof(DLGTEMPLATEEX) + 500, 0);
    DLGTEMPLATEEX* pTemplate = (DLGTEMPLATEEX*)memory.data();
    *pTemplate = lpdg;

    BYTE* p = memory.data() + sizeof(DLGTEMPLATEEX);
    
    *(WORD*)p = 0; p += 2;
    *(WORD*)p = 0; p += 2;
    
    wcscpy_s((wchar_t*)p, 50, L"Watermark Settings");
    p += (wcslen(L"Watermark Settings") + 1) * 2;
    
    *(WORD*)p = 9; p += 2;
    *(WORD*)p = FW_NORMAL; p += 2;
    *p = 0; p++;
    *p = DEFAULT_CHARSET; p++;
    wcscpy_s((wchar_t*)p, 50, L"Segoe UI");
    p += (wcslen(L"Segoe UI") + 1) * 2;

    auto alignToDword = [](BYTE* ptr, const BYTE* base) -> BYTE* {
        size_t offset = ptr - base;
        size_t aligned = (offset + 3) & ~3;
        return (BYTE*)base + aligned;
    };

    auto addItem = [&](WORD id, DWORD style, short x, short y, short cx, short cy, const wchar_t* className, const wchar_t* title) {
        p = alignToDword(p, memory.data());
        
#pragma pack(push, 2)
        struct DLGITEMTEMPLATEEX {
            DWORD helpID;
            DWORD exStyle;
            DWORD style;
            short x;
            short y;
            short cx;
            short cy;
            DWORD id;
        };
#pragma pack(pop)
        
        DLGITEMTEMPLATEEX item = {0};
        item.style = style | WS_CHILD | WS_VISIBLE;
        item.x = x;
        item.y = y;
        item.cx = cx;
        item.cy = cy;
        item.id = id;
        
        *(DLGITEMTEMPLATEEX*)p = item;
        p += sizeof(DLGITEMTEMPLATEEX);
        
        if (className[0] == 0xFFFF) {
            *(WORD*)p = 0xFFFF;
            *(WORD*)(p + 2) = className[1];
            p += 4;
        } else {
            wcscpy_s((wchar_t*)p, 50, className);
            p += (wcslen(className) + 1) * 2;
        }
        
        wcscpy_s((wchar_t*)p, 100, title);
        p += (wcslen(title) + 1) * 2;
        
        *(WORD*)p = 0;
        p += 2;
    };

    addItem(100, SS_LEFT, 15, 12, 210, 12, L"STATIC", L"Enter watermark text to overlay on the image:");
    addItem(101, ES_LEFT | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 15, 28, 210, 15, L"EDIT", L"");
    addItem(IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP, 60, 56, 50, 16, L"BUTTON", L"OK");
    addItem(IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP, 130, 56, 50, 16, L"BUTTON", L"Cancel");

    WatermarkDialogParams params = { L"QUICK CONVERT", false, isDarkMode };
    DialogBoxIndirectParamW(GetModuleHandleW(NULL), (LPCDLGTEMPLATEW)memory.data(), hWndParent, WatermarkDlgProc, (LPARAM)&params);
    
    if (params.confirmed) {
        outText = params.text;
        return true;
    }
    return false;
}

void ShowFloatingMenu(HWND hWnd) {
    POINT pt;
    GetCursorPos(&pt);

    SelectionKind selectionKind = DetectSelectionKind();

    HMENU hMenu = CreatePopupMenu();
    
    if (selectionKind == SelectionKind::Pdf) {
        AppendMenuW(hMenu, MF_STRING, 8, L"PDF to PNG (ZIP)");
        AppendMenuW(hMenu, MF_STRING, 9, L"PDF to JPG (ZIP)");
        AppendMenuW(hMenu, MF_STRING, 10, L"Convert to Word");
    } else if (selectionKind == SelectionKind::Video) {
        std::wstring firstExt = PathFindExtensionW(g_FilesToConvert[0].c_str());
        std::transform(firstExt.begin(), firstExt.end(), firstExt.begin(), ::towlower);
        if (firstExt != L".mp4") AppendMenuW(hMenu, MF_STRING, 14, L"To MP4");
        if (firstExt != L".mov") AppendMenuW(hMenu, MF_STRING, 15, L"To MOV");
        AppendMenuW(hMenu, MF_STRING, 17, L"To MP3");
        AppendMenuW(hMenu, MF_STRING, 16, L"To GIF");
    } else if (selectionKind == SelectionKind::Audio) {
        std::wstring firstExt = PathFindExtensionW(g_FilesToConvert[0].c_str());
        std::transform(firstExt.begin(), firstExt.end(), firstExt.begin(), ::towlower);
        if (firstExt != L".mp3") AppendMenuW(hMenu, MF_STRING, 17, L"To MP3");
        if (firstExt != L".ogg") AppendMenuW(hMenu, MF_STRING, 18, L"To OGG");
        if (firstExt != L".wav") AppendMenuW(hMenu, MF_STRING, 19, L"To WAV");
    } else if (selectionKind == SelectionKind::Gif) {
        AppendMenuW(hMenu, MF_STRING, 1, L"Convert to PNG");
        AppendMenuW(hMenu, MF_STRING, 2, L"Convert to JPG");
        AppendMenuW(hMenu, MF_STRING, 3, L"Convert to PDF");
        AppendMenuW(hMenu, MF_STRING, 4, L"Convert to WebP");
        AppendMenuW(hMenu, MF_STRING, 6, L"Convert to ICO");
        AppendMenuW(hMenu, MF_STRING, 5, L"Convert to BMP");
        AppendMenuW(hMenu, MF_STRING, 14, L"Convert to MP4");
    } else if (selectionKind == SelectionKind::Ppt) {
        AppendMenuW(hMenu, MF_STRING, 11, L"To PDF");
    } else if (selectionKind == SelectionKind::Doc) {
        AppendMenuW(hMenu, MF_STRING, 12, L"To PDF");
    } else if (selectionKind == SelectionKind::Excel) {
        AppendMenuW(hMenu, MF_STRING, 13, L"To PDF");
    } else {
        std::wstring firstExt = PathFindExtensionW(g_FilesToConvert[0].c_str());
        std::transform(firstExt.begin(), firstExt.end(), firstExt.begin(), ::towlower);
        if (firstExt != L".png") AppendMenuW(hMenu, MF_STRING, 1, L"Convert to PNG");
        if (firstExt != L".jpg" && firstExt != L".jpeg") AppendMenuW(hMenu, MF_STRING, 2, L"Convert to JPG");
        AppendMenuW(hMenu, MF_STRING, 3, L"Convert to PDF");
        if (firstExt != L".webp") AppendMenuW(hMenu, MF_STRING, 4, L"Convert to WebP");
        if (firstExt != L".ico") AppendMenuW(hMenu, MF_STRING, 6, L"Convert to ICO");
        if (firstExt != L".bmp") AppendMenuW(hMenu, MF_STRING, 5, L"Convert to BMP");
        AppendMenuW(hMenu, MF_STRING, 7, L"Add Watermark");
    }

    SetForegroundWindow(hWnd);
    int sel = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);

    if (sel > 0) {
        TargetFormat fmt = TargetFormat::Png;
        if (sel == 1) fmt = TargetFormat::Png;
        else if (sel == 2) fmt = TargetFormat::Jpg;
        else if (sel == 3) fmt = TargetFormat::Pdf;
        else if (sel == 4) fmt = TargetFormat::Webp;
        else if (sel == 5) fmt = TargetFormat::Bmp;
        else if (sel == 6) fmt = TargetFormat::Ico;
        else if (sel == 7) fmt = TargetFormat::AddWatermark;
        else if (sel == 8) fmt = TargetFormat::PdfToPngZip;
        else if (sel == 9) fmt = TargetFormat::PdfToJpgZip;
        else if (sel == 10) fmt = TargetFormat::PdfToWord;
        else if (sel == 11) fmt = TargetFormat::PptToPdf;
        else if (sel == 12) fmt = TargetFormat::WordToPdf;
        else if (sel == 13) fmt = TargetFormat::ExcelToPdf;
        else if (sel == 14) fmt = TargetFormat::MovToMp4;
        else if (sel == 15) fmt = TargetFormat::Mp4ToMov;
        else if (sel == 16) fmt = TargetFormat::VideoToGif;
        else if (sel == 17) fmt = TargetFormat::Mp4ToMp3;
        else if (sel == 18) fmt = TargetFormat::Mp3ToOgg;
        else if (sel == 19) fmt = TargetFormat::AudioToWav;


        g_Cancelled = false;
        g_ErrorMessage.clear();
        g_WindowSubtitle = BuildSubtitleText();
        SetStaticText(g_hSubtitle, g_WindowSubtitle.c_str());
        UpdateStatusText(L"Preparing conversion");
        SetStaticText(g_hErrorTitle, L"");
        SetStaticText(g_hErrorBody, L"");
        EnableWindow(g_hPrimaryButton, TRUE);
        SetUiState(UiState::Working);
        ShowWindow(hWnd, SW_SHOW);
        SetForegroundWindow(hWnd);

        if (fmt == TargetFormat::AddWatermark) {
            std::wstring watermarkText;
            if (PromptForWatermarkText(hWnd, watermarkText, g_IsDarkMode)) {
                Converter::SetWatermarkText(watermarkText);
            } else {
                PostQuitMessage(0);
                return;
            }
        }

        ThreadParams* params = new ThreadParams{ g_FilesToConvert, fmt };
        std::thread(ConversionThread, params).detach();
    } else {
        PostQuitMessage(0);
    }
}

bool g_ConsoleAttached = false;

void InitializeConsole() {
    DWORD dwType = GetFileType(GetStdHandle(STD_OUTPUT_HANDLE));
    if (dwType == FILE_TYPE_DISK || dwType == FILE_TYPE_PIPE || dwType == FILE_TYPE_CHAR) {
        g_ConsoleAttached = true;
        return;
    }

    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        g_ConsoleAttached = true;
    }
}

void PrintToConsole(const std::wstring& msg) {
    if (g_ConsoleAttached) {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut != INVALID_HANDLE_VALUE && hOut != NULL) {
            DWORD dwMode;
            if (GetConsoleMode(hOut, &dwMode)) {
                DWORD written = 0;
                WriteConsoleW(hOut, msg.c_str(), (DWORD)msg.size(), &written, NULL);
            } else {
                int size_needed = WideCharToMultiByte(CP_UTF8, 0, msg.c_str(), (int)msg.size(), NULL, 0, NULL, NULL);
                if (size_needed > 0) {
                    std::vector<char> utf8(size_needed);
                    WideCharToMultiByte(CP_UTF8, 0, msg.c_str(), (int)msg.size(), utf8.data(), size_needed, NULL, NULL);
                    DWORD written = 0;
                    WriteFile(hOut, utf8.data(), (DWORD)utf8.size(), &written, NULL);
                }
            }
        }
    }
}

TargetFormat ResolveTargetFormat(const std::wstring& inputExt, const std::wstring& targetFormatStr) {
    std::wstring ext = inputExt;
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    std::wstring fmt = targetFormatStr;
    std::transform(fmt.begin(), fmt.end(), fmt.begin(), ::towlower);

    if (fmt == L"png") {
        if (ext == L".pdf") return TargetFormat::PdfToPngZip;
        return TargetFormat::Png;
    }
    if (fmt == L"jpg" || fmt == L"jpeg") {
        if (ext == L".pdf") return TargetFormat::PdfToJpgZip;
        return TargetFormat::Jpg;
    }
    if (fmt == L"webp") return TargetFormat::Webp;
    if (fmt == L"bmp") return TargetFormat::Bmp;
    if (fmt == L"ico") return TargetFormat::Ico;
    if (fmt == L"watermark" || fmt == L"addwatermark") return TargetFormat::AddWatermark;

    if (fmt == L"pdf") {
        if (ext == L".pptx" || ext == L".ppt") return TargetFormat::PptToPdf;
        if (ext == L".docx" || ext == L".doc") return TargetFormat::WordToPdf;
        if (ext == L".xlsx" || ext == L".xls") return TargetFormat::ExcelToPdf;
        return TargetFormat::Pdf;
    }
    if (fmt == L"mp3" || fmt == L"mp4mp3" || fmt == L"movmp3") {
        return TargetFormat::Mp4ToMp3;
    }
    if (fmt == L"mov" || fmt == L"mp4mov") {
        return TargetFormat::Mp4ToMov;
    }
    if (fmt == L"mp4" || fmt == L"movmp4") {
        return TargetFormat::MovToMp4;
    }
    if (fmt == L"ogg" || fmt == L"mp3ogg") {
        return TargetFormat::Mp3ToOgg;
    }
    if (fmt == L"gif" || fmt == L"videogif") {
        return TargetFormat::VideoToGif;
    }
    if (fmt == L"wav") {
        return TargetFormat::AudioToWav;
    }
    if (fmt == L"docx" || fmt == L"doc" || fmt == L"word" || fmt == L"pdfword" || fmt == L"pdfdocx") {
        if (ext == L".pdf") return TargetFormat::PdfToWord;
    }

    if (fmt == L"pdfzip") return TargetFormat::PdfToPngZip;
    if (fmt == L"pdfjpgzip") return TargetFormat::PdfToJpgZip;
    if (fmt == L"pptxpdf") return TargetFormat::PptToPdf;
    if (fmt == L"docxpdf") return TargetFormat::WordToPdf;
    if (fmt == L"xlsxpdf") return TargetFormat::ExcelToPdf;

    return TargetFormat::Png;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    INITCOMMONCONTROLSEX icce = { sizeof(icce) };
    icce.dwICC = ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icce);

    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    
    InitializeConsole();

    if (argc < 2) {
        if (g_ConsoleAttached) {
            PrintToConsole(L"\nQuickConvert CLI\n");
            PrintToConsole(L"Usage:\n");
            PrintToConsole(L"  quickconvert <file_path(s)> [to] <format>\n\n");
            PrintToConsole(L"Supported formats:\n");
            PrintToConsole(L"  png, jpg, webp, pdf, ico, mp3, mov, ogg, word/docx\n\n");
            PrintToConsole(L"Examples:\n");
            PrintToConsole(L"  quickconvert image.jpg to png\n");
            PrintToConsole(L"  quickconvert document.docx to pdf\n");
            PrintToConsole(L"  quickconvert video.mp4 to mp3\n\n");
        }
        if (argv) LocalFree(argv);
        return 0;
    }

    std::vector<std::wstring> files;
    std::wstring targetFormatStr = L"";
    bool formatSet = false;

    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        std::wstring argLower = arg;
        std::transform(argLower.begin(), argLower.end(), argLower.begin(), ::towlower);

        if (argLower == L"to") {
            if (i + 1 < argc) {
                targetFormatStr = argv[i + 1];
                formatSet = true;
                i++;
                std::wstring fmtLower = targetFormatStr;
                std::transform(fmtLower.begin(), fmtLower.end(), fmtLower.begin(), ::towlower);
                if ((fmtLower == L"watermark" || fmtLower == L"addwatermark") && i + 1 < argc) {
                    Converter::SetWatermarkText(argv[i + 1]);
                    i++;
                }
            }
        } else if (argLower == L"watermark" || argLower == L"addwatermark") {
            targetFormatStr = arg;
            formatSet = true;
            if (i + 1 < argc) {
                Converter::SetWatermarkText(argv[i + 1]);
                i++;
            }
        } else if (i == argc - 1) {
            if (argLower == L"png" || argLower == L"jpg" || argLower == L"jpeg" || 
                argLower == L"pdf" || argLower == L"webp" || argLower == L"bmp" || 
                argLower == L"ico" || argLower == L"mp3" || argLower == L"mov" || 
                argLower == L"ogg" || argLower == L"docx" || argLower == L"doc" || 
                argLower == L"word" || argLower == L"pptx" || argLower == L"ppt" || 
                argLower == L"xlsx" || argLower == L"xls" || argLower == L"pdfzip" || 
                argLower == L"pdfjpgzip" || argLower == L"mp4mp3" || argLower == L"mp4mov" || 
                argLower == L"mp3ogg" || argLower == L"pptxpdf" || argLower == L"docxpdf" || 
                argLower == L"xlsxpdf" || argLower == L"pdfword" || argLower == L"pdfdocx" ||
                argLower == L"movmp3" || argLower == L"movmp4" || argLower == L"mp4" ||
                argLower == L"gif" || argLower == L"videogif" || argLower == L"wav" ||
                argLower == L"watermark" || argLower == L"addwatermark") {
                targetFormatStr = arg;
                formatSet = true;
            } else {
                files.push_back(arg);
            }
        } else {
            files.push_back(arg);
        }
    }

    if (g_ConsoleAttached && formatSet && !files.empty()) {
        int successCount = 0;
        int failCount = 0;

        PrintToConsole(L"\n[QuickConvert] Starting conversion of " + std::to_wstring(files.size()) + L" file(s)...\n");

        for (const auto& file : files) {
            wchar_t fullPath[MAX_PATH];
            std::wstring resolvedPath = file;
            if (_wfullpath(fullPath, file.c_str(), MAX_PATH) != nullptr) {
                resolvedPath = fullPath;
            }

            std::wstring ext = PathFindExtensionW(resolvedPath.c_str());
            TargetFormat resolvedFormat = ResolveTargetFormat(ext, targetFormatStr);

            std::wstring fileName = resolvedPath.substr(resolvedPath.find_last_of(L"\\") + 1);
            PrintToConsole(L"Converting " + fileName + L"... ");

            HRESULT hr = Converter::ConvertImage(resolvedPath, resolvedFormat);
            if (SUCCEEDED(hr)) {
                PrintToConsole(L"SUCCESS\n");
                successCount++;
            } else {
                std::wstring errMsg = DescribeConversionError(hr, resolvedFormat);
                PrintToConsole(L"FAILED: " + errMsg + L"\n");
                failCount++;
            }
        }

        PrintToConsole(L"[QuickConvert] Finished. Success: " + std::to_wstring(successCount) + L", Failed: " + std::to_wstring(failCount) + L"\n\n");
        LocalFree(argv);
        return failCount > 0 ? 1 : 0;
    }

    g_FilesToConvert = files;
    TargetFormat g_TargetFormat = TargetFormat::Png;
    if (formatSet && !files.empty()) {
        std::wstring ext = PathFindExtensionW(files[0].c_str());
        g_TargetFormat = ResolveTargetFormat(ext, targetFormatStr);
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

        if (g_TargetFormat == TargetFormat::AddWatermark && !g_ConsoleAttached) {
            std::wstring watermarkText;
            if (PromptForWatermarkText(g_hWnd, watermarkText, g_IsDarkMode)) {
                Converter::SetWatermarkText(watermarkText);
            } else {
                DestroyWindow(g_hWnd);
                CleanupResources();
                if (argv) LocalFree(argv);
                return 0;
            }
        }

        ThreadParams* params = new ThreadParams{ g_FilesToConvert, g_TargetFormat };
        std::thread(ConversionThread, params).detach();
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (argv) LocalFree(argv);
    return 0;
}
