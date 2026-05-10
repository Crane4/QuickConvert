#include "CommandProvider.h"
#include <shlwapi.h>
#include <string>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")

const GUID CLSID_QuickConvert = { 0xBF5E6C7D, 0x8B9A, 0x4E3D, { 0xBC, 0x1F, 0x2E, 0x4D, 0x3C, 0x2B, 0x1A, 0x0F } };

namespace {
enum class SelectionKind {
    ImageOnly,
    PdfOnly,
    Mp4Only,
    Mp3Only,
    PptOnly,
    DocOnly,
    ExcelOnly
};



SelectionKind GetPathSelectionKind(const wchar_t* path) {
    if (!path) {
        return SelectionKind::ImageOnly;
    }

    const wchar_t* extension = PathFindExtensionW(path);
    if (!extension) {
        return SelectionKind::ImageOnly;
    }

    if (_wcsicmp(extension, L".pdf") == 0) {
        return SelectionKind::PdfOnly;
    }

    if (_wcsicmp(extension, L".mp4") == 0) {
        return SelectionKind::Mp4Only;
    }

    if (_wcsicmp(extension, L".mp3") == 0) {
        return SelectionKind::Mp3Only;
    }

    if (_wcsicmp(extension, L".pptx") == 0 || _wcsicmp(extension, L".ppt") == 0) {
        return SelectionKind::PptOnly;
    }

    if (_wcsicmp(extension, L".docx") == 0 || _wcsicmp(extension, L".doc") == 0) {
        return SelectionKind::DocOnly;
    }

    if (_wcsicmp(extension, L".xlsx") == 0 || _wcsicmp(extension, L".xls") == 0) {
        return SelectionKind::ExcelOnly;
    }

    return SelectionKind::ImageOnly;

}


bool MatchesSelectionKind(IShellItemArray* psiItemArray, SelectionKind kind) {
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

        if (pathKind != kind) {
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
    QuickConvertSubCommand(const wchar_t* title, const wchar_t* format, SelectionKind selectionKind)
        : m_cRef(1), m_title(title), m_format(format), m_selectionKind(selectionKind) {}

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
        *pState = MatchesSelectionKind(psiItemArray, m_selectionKind) ? ECS_ENABLED : ECS_HIDDEN;
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
    SelectionKind m_selectionKind;
};

class EnumQuickConvertCommands : public IEnumExplorerCommand {
public:
    EnumQuickConvertCommands() : m_cRef(1), m_index(0) {
        m_cmds[0] = new QuickConvertSubCommand(L"To PNG", L"png", SelectionKind::ImageOnly);
        m_cmds[1] = new QuickConvertSubCommand(L"To JPG", L"jpg", SelectionKind::ImageOnly);
        m_cmds[2] = new QuickConvertSubCommand(L"To PDF", L"pdf", SelectionKind::ImageOnly);
        m_cmds[3] = new QuickConvertSubCommand(L"To WebP", L"webp", SelectionKind::ImageOnly);
        m_cmds[4] = new QuickConvertSubCommand(L"To ICO", L"ico", SelectionKind::ImageOnly);
        m_cmds[5] = new QuickConvertSubCommand(L"To PNG (ZIP)", L"pdfzip", SelectionKind::PdfOnly);
        m_cmds[6] = new QuickConvertSubCommand(L"To JPG (ZIP)", L"pdfjpgzip", SelectionKind::PdfOnly);
        m_cmds[7] = new QuickConvertSubCommand(L"To MP3", L"mp4mp3", SelectionKind::Mp4Only);
        m_cmds[8] = new QuickConvertSubCommand(L"To MOV", L"mp4mov", SelectionKind::Mp4Only);
        m_cmds[9] = new QuickConvertSubCommand(L"To OGG", L"mp3ogg", SelectionKind::Mp3Only);
        m_cmds[10] = new QuickConvertSubCommand(L"To PDF", L"pptxpdf", SelectionKind::PptOnly);
        m_cmds[11] = new QuickConvertSubCommand(L"To PDF", L"docxpdf", SelectionKind::DocOnly);
        m_cmds[12] = new QuickConvertSubCommand(L"To PDF", L"xlsxpdf", SelectionKind::ExcelOnly);
        m_cmds[13] = nullptr;
        m_cmds[14] = nullptr;
        m_cmds[15] = new QuickConvertSubCommand(L"Convert to Word", L"pdfword", SelectionKind::PdfOnly);
    }
    virtual ~EnumQuickConvertCommands() { for (int i = 0; i < 16; i++) if (m_cmds[i]) m_cmds[i]->Release(); }




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
        while (m_index < 16 && fetched < celt) {
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
    IExplorerCommand* m_cmds[16];
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
    IFACEMETHODIMP GetState(IShellItemArray*, BOOL, EXPCMDSTATE* pState) { *pState = ECS_ENABLED; return S_OK; }
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
