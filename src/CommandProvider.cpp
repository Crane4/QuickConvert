#include "CommandProvider.h"
#include <shlwapi.h>
#include <string>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")

const GUID CLSID_QuickConvert = { 0xBF5E6C7D, 0x8B9A, 0x4E3D, { 0xBC, 0x1F, 0x2E, 0x4D, 0x3C, 0x2B, 0x1A, 0x0F } };

namespace {
enum SelectionKind {
    Kind_Image = 1 << 0,
    Kind_Gif   = 1 << 1,
    Kind_Video = 1 << 2,
    Kind_Audio = 1 << 3,
    Kind_Pdf   = 1 << 4,
    Kind_Ppt   = 1 << 5,
    Kind_Doc   = 1 << 6,
    Kind_Excel = 1 << 7
};

SelectionKind GetPathSelectionKind(const wchar_t* path) {
    if (!path) {
        return Kind_Image;
    }

    const wchar_t* extension = PathFindExtensionW(path);
    if (!extension) {
        return Kind_Image;
    }

    if (_wcsicmp(extension, L".pdf") == 0) return Kind_Pdf;
    if (_wcsicmp(extension, L".gif") == 0) return Kind_Gif;

    if (_wcsicmp(extension, L".mp4") == 0 || _wcsicmp(extension, L".mov") == 0 || 
        _wcsicmp(extension, L".mkv") == 0 || _wcsicmp(extension, L".avi") == 0 || 
        _wcsicmp(extension, L".webm") == 0) {
        return Kind_Video;
    }

    if (_wcsicmp(extension, L".mp3") == 0 || _wcsicmp(extension, L".wav") == 0 || 
        _wcsicmp(extension, L".m4a") == 0 || _wcsicmp(extension, L".flac") == 0 || 
        _wcsicmp(extension, L".aac") == 0 || _wcsicmp(extension, L".ogg") == 0) {
        return Kind_Audio;
    }

    if (_wcsicmp(extension, L".pptx") == 0 || _wcsicmp(extension, L".ppt") == 0) return Kind_Ppt;
    if (_wcsicmp(extension, L".docx") == 0 || _wcsicmp(extension, L".doc") == 0) return Kind_Doc;
    if (_wcsicmp(extension, L".xlsx") == 0 || _wcsicmp(extension, L".xls") == 0) return Kind_Excel;

    return Kind_Image;
}

bool MatchesSelectionKind(IShellItemArray* psiItemArray, int kindMask) {
    if (!psiItemArray) {
        return true;
    }

    DWORD count = 0;
    if (FAILED(psiItemArray->GetCount(&count)) || count == 0) {
        return false;
    }

    for (DWORD i = 0; i < count; ++i) {
        IShellItem* psi = nullptr;
        if (FAILED(psiItemArray->GetItemAt(i, &psi)) || !psi) {
            return false;
        }

        LPWSTR path = nullptr;
        HRESULT hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &path);
        psi->Release();
        if (FAILED(hr) || !path) {
            return false;
        }

        SelectionKind pathKind = GetPathSelectionKind(path);
        CoTaskMemFree(path);

        if ((pathKind & kindMask) == 0) {
            return false;
        }
    }

    return true;
}

bool AllItemsHaveExtension(IShellItemArray* psiItemArray, const wchar_t* targetExt) {
    if (!psiItemArray) return false;
    DWORD count = 0;
    if (FAILED(psiItemArray->GetCount(&count)) || count == 0) return false;

    for (DWORD i = 0; i < count; ++i) {
        IShellItem* psi = nullptr;
        if (SUCCEEDED(psiItemArray->GetItemAt(i, &psi)) && psi) {
            LPWSTR path = nullptr;
            if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                const wchar_t* ext = PathFindExtensionW(path);
                bool match = (ext && _wcsicmp(ext, targetExt) == 0);
                CoTaskMemFree(path);
                psi->Release();
                if (!match) return false;
            } else {
                psi->Release();
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

HRESULT GetQuickConvertExePath(wchar_t* exePath, size_t cchExePath) {
    if (!exePath || cchExePath == 0) {
        return E_INVALIDARG;
    }

    if (GetModuleFileNameW(g_hInst, exePath, (DWORD)cchExePath) == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    PathRemoveFileSpecW(exePath);
    PathAppendW(exePath, L"QuickConvert.exe");
    return S_OK;
}

HRESULT GetQuickConvertIconPath(LPWSTR* ppszIcon) {
    if (!ppszIcon) {
        return E_POINTER;
    }

    wchar_t exePath[MAX_PATH] = {};
    HRESULT hr = GetQuickConvertExePath(exePath, MAX_PATH);
    if (FAILED(hr)) {
        return hr;
    }

    std::wstring iconPath = exePath;
    iconPath += L",0";
    return SHStrDupW(iconPath.c_str(), ppszIcon);
}
}

class QuickConvertSubCommand : public IExplorerCommand {
public:
    QuickConvertSubCommand(const wchar_t* title, const wchar_t* format, int selectionKindMask)
        : m_cRef(1), m_title(title), m_format(format), m_selectionKindMask(selectionKindMask) {}

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) {
        if (riid == IID_IUnknown || riid == IID_IExplorerCommand) { *ppv = static_cast<IExplorerCommand*>(this); AddRef(); return S_OK; }
        *ppv = NULL; return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_cRef); }
    IFACEMETHODIMP_(ULONG) Release() {
        long cRef = InterlockedDecrement(&m_cRef);
        if (cRef == 0) delete this;
        return cRef;
    }

    IFACEMETHODIMP GetTitle(IShellItemArray*, LPWSTR* ppszName) { return SHStrDupW(m_title, ppszName); }
    IFACEMETHODIMP GetIcon(IShellItemArray*, LPWSTR* ppszIcon) { return GetQuickConvertIconPath(ppszIcon); }
    IFACEMETHODIMP GetToolTip(IShellItemArray*, LPWSTR* ppszTip) { *ppszTip = NULL; return S_OK; }
    IFACEMETHODIMP GetCanonicalName(GUID* pguid) { *pguid = GUID_NULL; return S_OK; }
    IFACEMETHODIMP GetState(IShellItemArray* psiItemArray, BOOL, EXPCMDSTATE* pState) {
        if (!MatchesSelectionKind(psiItemArray, m_selectionKindMask)) {
            *pState = ECS_HIDDEN;
            return S_OK;
        }

        // Hide redundant conversions
        if (wcscmp(m_format, L"movmp4") == 0) {
            if (AllItemsHaveExtension(psiItemArray, L".mp4")) {
                *pState = ECS_HIDDEN;
                return S_OK;
            }
        }
        if (wcscmp(m_format, L"mp4mov") == 0) {
            if (AllItemsHaveExtension(psiItemArray, L".mov")) {
                *pState = ECS_HIDDEN;
                return S_OK;
            }
        }
        if (wcscmp(m_format, L"mp3") == 0) {
            if (AllItemsHaveExtension(psiItemArray, L".mp3")) {
                *pState = ECS_HIDDEN;
                return S_OK;
            }
        }
        if (wcscmp(m_format, L"ogg") == 0) {
            if (AllItemsHaveExtension(psiItemArray, L".ogg")) {
                *pState = ECS_HIDDEN;
                return S_OK;
            }
        }
        if (wcscmp(m_format, L"wav") == 0) {
            if (AllItemsHaveExtension(psiItemArray, L".wav")) {
                *pState = ECS_HIDDEN;
                return S_OK;
            }
        }
        if (wcscmp(m_format, L"png") == 0) {
            if (AllItemsHaveExtension(psiItemArray, L".png")) {
                *pState = ECS_HIDDEN;
                return S_OK;
            }
        }
        if (wcscmp(m_format, L"jpg") == 0) {
            if (AllItemsHaveExtension(psiItemArray, L".jpg") || AllItemsHaveExtension(psiItemArray, L".jpeg")) {
                *pState = ECS_HIDDEN;
                return S_OK;
            }
        }
        if (wcscmp(m_format, L"webp") == 0) {
            if (AllItemsHaveExtension(psiItemArray, L".webp")) {
                *pState = ECS_HIDDEN;
                return S_OK;
            }
        }
        if (wcscmp(m_format, L"ico") == 0) {
            if (AllItemsHaveExtension(psiItemArray, L".ico")) {
                *pState = ECS_HIDDEN;
                return S_OK;
            }
        }
        if (wcscmp(m_format, L"bmp") == 0) {
            if (AllItemsHaveExtension(psiItemArray, L".bmp")) {
                *pState = ECS_HIDDEN;
                return S_OK;
            }
        }

        *pState = ECS_ENABLED;
        return S_OK;
    }
    IFACEMETHODIMP GetFlags(EXPCMDFLAGS* pFlags) { *pFlags = ECF_DEFAULT; return S_OK; }
    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** ppEnum) { *ppEnum = NULL; return E_NOTIMPL; }

    IFACEMETHODIMP Invoke(IShellItemArray* psiItemArray, IBindCtx*) {
        if (!psiItemArray) return S_OK;
        wchar_t exePath[MAX_PATH];
        if (FAILED(GetQuickConvertExePath(exePath, MAX_PATH))) {
            return E_FAIL;
        }

        std::wstring args = L"";
        DWORD count;
        psiItemArray->GetCount(&count);
        for (DWORD i = 0; i < count; ++i) {
            IShellItem* psi;
            if (SUCCEEDED(psiItemArray->GetItemAt(i, &psi))) {
                LPWSTR path;
                if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    args += L"\""; args += path; args += L"\" ";
                    CoTaskMemFree(path);
                }
                psi->Release();
            }
        }
        args += m_format;
        
        std::wstring quotedExe = L"\"";
        quotedExe += exePath;
        quotedExe += L"\"";

        HINSTANCE hResult = ShellExecuteW(NULL, L"open", quotedExe.c_str(), args.c_str(), NULL, SW_SHOWNORMAL);
        if ((INT_PTR)hResult <= 32) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        return S_OK;
    }

private:
    long m_cRef;
    const wchar_t* m_title;
    const wchar_t* m_format;
    int m_selectionKindMask;
};

class EnumQuickConvertCommands : public IEnumExplorerCommand {
public:
    EnumQuickConvertCommands() : m_cRef(1), m_index(0) {
        int imagesAndGifs = Kind_Image | Kind_Gif;

        m_cmds[0]  = new QuickConvertSubCommand(L"To PNG", L"png", imagesAndGifs);
        m_cmds[1]  = new QuickConvertSubCommand(L"To JPG", L"jpg", imagesAndGifs);
        m_cmds[2]  = new QuickConvertSubCommand(L"To PDF", L"pdf", imagesAndGifs);
        m_cmds[3]  = new QuickConvertSubCommand(L"To WebP", L"webp", imagesAndGifs);
        m_cmds[4]  = new QuickConvertSubCommand(L"To ICO", L"ico", imagesAndGifs);
        m_cmds[5]  = new QuickConvertSubCommand(L"To BMP", L"bmp", imagesAndGifs);
        m_cmds[6]  = new QuickConvertSubCommand(L"Add Watermark", L"watermark", Kind_Image);
        
        m_cmds[7]  = new QuickConvertSubCommand(L"To PNG (ZIP)", L"pdfzip", Kind_Pdf);
        m_cmds[8]  = new QuickConvertSubCommand(L"To JPG (ZIP)", L"pdfjpgzip", Kind_Pdf);
        m_cmds[9]  = new QuickConvertSubCommand(L"Convert to Word", L"pdfword", Kind_Pdf);
        
        m_cmds[10] = new QuickConvertSubCommand(L"To PDF", L"pptxpdf", Kind_Ppt);
        m_cmds[11] = new QuickConvertSubCommand(L"To PDF", L"docxpdf", Kind_Doc);
        m_cmds[12] = new QuickConvertSubCommand(L"To PDF", L"xlsxpdf", Kind_Excel);
        
        m_cmds[13] = new QuickConvertSubCommand(L"To MP4", L"movmp4", Kind_Video | Kind_Gif);
        m_cmds[14] = new QuickConvertSubCommand(L"To MOV", L"mp4mov", Kind_Video);
        m_cmds[15] = new QuickConvertSubCommand(L"To GIF", L"videogif", Kind_Video);
        
        m_cmds[16] = new QuickConvertSubCommand(L"To MP3", L"mp3", Kind_Video | Kind_Audio);
        m_cmds[17] = new QuickConvertSubCommand(L"To OGG", L"ogg", Kind_Audio);
        m_cmds[18] = new QuickConvertSubCommand(L"To WAV", L"wav", Kind_Audio);
    }
    virtual ~EnumQuickConvertCommands() { for (int i = 0; i < 19; i++) if (m_cmds[i]) m_cmds[i]->Release(); }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) {
        if (riid == IID_IUnknown || riid == IID_IEnumExplorerCommand) { *ppv = static_cast<IEnumExplorerCommand*>(this); AddRef(); return S_OK; }
        *ppv = NULL; return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_cRef); }
    IFACEMETHODIMP_(ULONG) Release() {
        long cRef = InterlockedDecrement(&m_cRef);
        if (cRef == 0) delete this;
        return cRef;
    }

    IFACEMETHODIMP Next(ULONG celt, IExplorerCommand** pUICommand, ULONG* pceltFetched) {
        ULONG fetched = 0;
        while (m_index < 19 && fetched < celt) {
            if (m_cmds[m_index]) {
                pUICommand[fetched] = m_cmds[m_index];
                pUICommand[fetched]->AddRef();
                fetched++;
            }
            m_index++;
        }
        if (pceltFetched) *pceltFetched = fetched;
        return (fetched == celt) ? S_OK : S_FALSE;
    }

    IFACEMETHODIMP Skip(ULONG celt) { m_index += (int)celt; return S_OK; }
    IFACEMETHODIMP Reset() { m_index = 0; return S_OK; }
    IFACEMETHODIMP Clone(IEnumExplorerCommand** pp) { *pp = NULL; return E_NOTIMPL; }

private:
    long m_cRef;
    int m_index;
    IExplorerCommand* m_cmds[19];
};

class QuickConvertCommand : public IExplorerCommand {
public:
    QuickConvertCommand() : m_cRef(1) {}
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) {
        if (riid == IID_IUnknown || riid == IID_IExplorerCommand) { *ppv = static_cast<IExplorerCommand*>(this); AddRef(); return S_OK; }
        *ppv = NULL; return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_cRef); }
    IFACEMETHODIMP_(ULONG) Release() {
        long cRef = InterlockedDecrement(&m_cRef);
        if (cRef == 0) delete this;
        return cRef;
    }

    IFACEMETHODIMP GetTitle(IShellItemArray*, LPWSTR* ppszName) { return SHStrDupW(L"Quick Convert", ppszName); }
    IFACEMETHODIMP GetIcon(IShellItemArray*, LPWSTR* ppszIcon) { return GetQuickConvertIconPath(ppszIcon); }
    IFACEMETHODIMP GetToolTip(IShellItemArray*, LPWSTR* ppszTip) { *ppszTip = NULL; return S_OK; }
    IFACEMETHODIMP GetCanonicalName(GUID* pguid) { *pguid = GUID_NULL; return S_OK; }
    IFACEMETHODIMP GetState(IShellItemArray* psiItemArray, BOOL, EXPCMDSTATE* pState) {
        if (!psiItemArray) {
            *pState = ECS_ENABLED;
            return S_OK;
        }

        bool anyMatch = MatchesSelectionKind(psiItemArray, Kind_Image | Kind_Gif)
                     || MatchesSelectionKind(psiItemArray, Kind_Pdf)
                     || MatchesSelectionKind(psiItemArray, Kind_Ppt)
                     || MatchesSelectionKind(psiItemArray, Kind_Doc)
                     || MatchesSelectionKind(psiItemArray, Kind_Excel)
                     || MatchesSelectionKind(psiItemArray, Kind_Video)
                     || MatchesSelectionKind(psiItemArray, Kind_Audio);

        *pState = anyMatch ? ECS_ENABLED : ECS_HIDDEN;
        return S_OK;
    }
    IFACEMETHODIMP GetFlags(EXPCMDFLAGS* pFlags) { *pFlags = ECF_HASSUBCOMMANDS; return S_OK; }
    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** ppEnum) { *ppEnum = new EnumQuickConvertCommands(); return S_OK; }
    IFACEMETHODIMP Invoke(IShellItemArray*, IBindCtx*) { return S_OK; }

private:
    long m_cRef;
};

class QuickConvertClassFactory : public IClassFactory {
public:
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) {
        if (riid == IID_IUnknown || riid == IID_IClassFactory) { *ppv = this; return S_OK; }
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() { return 2; }
    IFACEMETHODIMP_(ULONG) Release() { return 1; }
    IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) {
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        QuickConvertCommand* pCmd = new QuickConvertCommand();
        HRESULT hr = pCmd->QueryInterface(riid, ppv);
        pCmd->Release();
        return hr;
    }
    IFACEMETHODIMP LockServer(BOOL) { return S_OK; }
};

HRESULT CreateCommandProvider(REFIID riid, void** ppv) {
    static QuickConvertClassFactory factory;
    return factory.QueryInterface(riid, ppv);
}
