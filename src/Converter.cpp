#include "Converter.h"
#include <shellapi.h>
#include <shlwapi.h>
#include <wincodecsdk.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "windowscodecs.lib")

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "shell32.lib")

namespace {
std::wstring g_LastConverterError;

std::wstring QuoteForCommandLine(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

std::wstring TrimWhitespace(std::wstring value) {
    while (!value.empty() && (value.front() == L' ' || value.front() == L'\t' || value.front() == L'\r' || value.front() == L'\n')) {
        value.erase(value.begin());
    }

    while (!value.empty() && (value.back() == L' ' || value.back() == L'\t' || value.back() == L'\r' || value.back() == L'\n')) {
        value.pop_back();
    }

    return value;
}

std::wstring DecodeProcessOutput(const std::string& bytes) {
    if (bytes.empty()) {
        return L"";
    }

    auto decode = [&](UINT codePage) {
        int required = MultiByteToWideChar(codePage, 0, bytes.c_str(), (int)bytes.size(), nullptr, 0);
        if (required <= 0) {
            return std::wstring();
        }

        std::wstring text(required, L'\0');
        MultiByteToWideChar(codePage, 0, bytes.c_str(), (int)bytes.size(), text.data(), required);
        return text;
    };

    std::wstring utf8 = decode(CP_UTF8);
    if (!utf8.empty()) {
        return utf8;
    }

    return decode(CP_ACP);
}

std::wstring SummarizeFfmpegOutput(const std::wstring& output) {
    std::vector<std::wstring> lines;
    size_t start = 0;
    while (start <= output.size()) {
        size_t end = output.find_first_of(L"\r\n", start);
        std::wstring line = TrimWhitespace(output.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start));
        if (!line.empty()) {
            lines.push_back(line);
        }

        if (end == std::wstring::npos) {
            break;
        }

        start = output.find_first_not_of(L"\r\n", end);
        if (start == std::wstring::npos) {
            break;
        }
    }

    if (lines.empty()) {
        return L"";
    }

    std::wstring summary;
    size_t begin = lines.size() > 3 ? lines.size() - 3 : 0;
    for (size_t i = begin; i < lines.size(); ++i) {
        if (!summary.empty()) {
            summary += L" ";
        }
        summary += lines[i];
    }

    return summary;
}

void SetLastConverterError(const std::wstring& error) {
    g_LastConverterError = error;
}

std::wstring EscapePowerShellSingleQuoted(const std::wstring& value) {
    std::wstring escaped;
    escaped.reserve(value.size());
    for (wchar_t ch : value) {
        escaped += ch;
        if (ch == L'\'') {
            escaped += L'\'';
        }
    }
    return escaped;
}

void DeleteDirectoryBestEffort(const std::wstring& path) {
    if (path.empty() || !PathFileExistsW(path.c_str())) {
        return;
    }

    std::wstring from = path;
    from.push_back(L'\0');
    from.push_back(L'\0');

    SHFILEOPSTRUCTW op = {};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    SHFileOperationW(&op);
}

HRESULT ResolveFfmpegPath(std::wstring& ffmpegPath) {
    wchar_t modulePath[MAX_PATH] = {};
    DWORD moduleLength = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (moduleLength > 0 && moduleLength < MAX_PATH) {
        PathRemoveFileSpecW(modulePath);
        std::wstring localPath = std::wstring(modulePath) + L"\\ffmpeg.exe";
        if (PathFileExistsW(localPath.c_str())) {
            ffmpegPath = localPath;
            return S_OK;
        }
    }

    wchar_t resolvedPath[MAX_PATH] = {};
    DWORD resolvedLength = SearchPathW(nullptr, L"ffmpeg.exe", nullptr, MAX_PATH, resolvedPath, nullptr);
    if (resolvedLength > 0 && resolvedLength < MAX_PATH) {
        ffmpegPath = resolvedPath;
        return S_OK;
    }

    return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
}

HRESULT RunProcessAndWait(const std::wstring& commandLine, std::wstring* processOutput) {
    wchar_t tempPath[MAX_PATH] = {};
    wchar_t tempFile[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, tempPath) == 0 || GetTempFileNameW(tempPath, L"QCF", 0, tempFile) == 0) {
        SetLastConverterError(L"Failed to create temp file path.");
        return HRESULT_FROM_WIN32(GetLastError());
    }


    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE logFile = CreateFileW(
        tempFile,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY,
        nullptr);
    if (logFile == INVALID_HANDLE_VALUE) {

        SetLastConverterError(L"CreateFile failed for log: " + std::to_wstring(GetLastError()));
        DeleteFileW(tempFile);
        return HRESULT_FROM_WIN32(GetLastError());
    }


    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = logFile;
    si.hStdError = logFile;
    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        DWORD err = GetLastError();
        CloseHandle(logFile);
        DeleteFileW(tempFile);
        
        if (err == 0x2FF || err == 767) { // ERROR_AUDITING_DISABLED or similar
             // Fallback to ShellExecute without capturing output
             SHELLEXECUTEINFOW sei = { sizeof(sei) };
             sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NO_CONSOLE;
             sei.lpVerb = L"open";
             sei.lpFile = L"powershell.exe";
             // Extract arguments from commandLine
             size_t space = commandLine.find(L" ");
             std::wstring args = (space != std::wstring::npos) ? commandLine.substr(space + 1) : L"";
             sei.lpParameters = args.c_str();
             sei.nShow = SW_HIDE;
             if (ShellExecuteExW(&sei)) {
                 WaitForSingleObject(sei.hProcess, INFINITE);
                 CloseHandle(sei.hProcess);
                 return S_OK;
             }
             err = GetLastError();
        }

        SetLastConverterError(L"CreateProcess failed: " + std::to_wstring(err));
        return HRESULT_FROM_WIN32(err);
    }


    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 0;
    BOOL gotExitCode = GetExitCodeProcess(pi.hProcess, &exitCode);

    SetFilePointer(logFile, 0, nullptr, FILE_BEGIN);
    std::string outputBytes;
    char buffer[4096];
    DWORD bytesRead = 0;
    while (ReadFile(logFile, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
        outputBytes.append(buffer, bytesRead);
    }

    if (processOutput) {
        *processOutput = DecodeProcessOutput(outputBytes);
    }

    CloseHandle(logFile);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    DeleteFileW(tempFile);

    if (!gotExitCode) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    if (exitCode != 0) {
        return E_FAIL;
    }

    return S_OK;
}

HRESULT ConvertWithFfmpeg(const std::wstring& sourcePath,
                          const std::wstring& targetPath,
                          const std::wstring& arguments) {
    SetLastConverterError(L"");

    std::wstring ffmpegPath;
    HRESULT ffmpegHr = ResolveFfmpegPath(ffmpegPath);
    if (FAILED(ffmpegHr)) {
        return ffmpegHr;
    }

    std::wstring commandLine = QuoteForCommandLine(ffmpegPath) + L" -y -i "
                             + QuoteForCommandLine(sourcePath) + L" "
                             + arguments + L" "
                             + QuoteForCommandLine(targetPath);

    std::wstring output;
    HRESULT hr = RunProcessAndWait(commandLine, &output);
    if (FAILED(hr)) {
        std::wstring summary = SummarizeFfmpegOutput(output);
        if (summary.find(L"Output file #0 does not contain any stream") != std::wstring::npos) {
            SetLastConverterError(L"The selected MP4 file does not contain an audio stream.");
        } else if (summary.find(L"Invalid argument") != std::wstring::npos) {
            SetLastConverterError(L"FFmpeg rejected the conversion arguments. " + summary);
        } else if (!summary.empty()) {
            SetLastConverterError(summary);
        }
    }

    return hr;
}

HRESULT CreateTempStorageFolder(winrt::Windows::Storage::StorageFolder& folder, std::wstring& path) {
    wchar_t tempRoot[MAX_PATH] = {};
    DWORD length = GetTempPathW(MAX_PATH, tempRoot);
    if (length == 0 || length >= MAX_PATH) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    std::wstring root = tempRoot;
    std::wstring candidate;
    for (int attempt = 0; attempt < 32; ++attempt) {
        candidate = root + L"QC_" + std::to_wstring(GetTickCount64()) + L"_" + std::to_wstring(attempt);
        if (CreateDirectoryW(candidate.c_str(), nullptr)) {
            try {
                folder = winrt::Windows::Storage::StorageFolder::GetFolderFromPathAsync(candidate).get();
                path = candidate;
                return S_OK;
            } catch (const winrt::hresult_error& e) {
                DeleteDirectoryBestEffort(candidate);
                return e.code();
            }
        }

        DWORD createError = GetLastError();
        if (createError != ERROR_ALREADY_EXISTS) {
            return HRESULT_FROM_WIN32(createError);
        }
    }

    return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
}

HRESULT ConvertPdfToImageZip(const std::wstring& sourcePath,
                             const std::wstring& targetPath,
                             const std::wstring& imageExtension,
                             winrt::guid encoderId);

HRESULT EncodeBitmapSourceToPngBuffer(IWICImagingFactory* pFactory,
                                      IWICBitmapSource* pBitmapSource,
                                      std::vector<BYTE>& buffer) {
    if (!pFactory || !pBitmapSource) {
        return E_INVALIDARG;
    }

    IStream* pMemStream = SHCreateMemStream(NULL, 0);
    if (!pMemStream) {
        return E_OUTOFMEMORY;
    }

    IWICBitmapEncoder* pEncoder = NULL;
    HRESULT hr = pFactory->CreateEncoder(GUID_ContainerFormatPng, NULL, &pEncoder);
    if (SUCCEEDED(hr)) hr = pEncoder->Initialize(pMemStream, WICBitmapEncoderNoCache);

    IWICBitmapFrameEncode* pFrameEncode = NULL;
    if (SUCCEEDED(hr)) hr = pEncoder->CreateNewFrame(&pFrameEncode, NULL);
    if (SUCCEEDED(hr)) hr = pFrameEncode->Initialize(NULL);

    if (SUCCEEDED(hr)) {
        UINT width = 0;
        UINT height = 0;
        pBitmapSource->GetSize(&width, &height);
        hr = pFrameEncode->SetSize(width, height);
    }

    if (SUCCEEDED(hr)) {
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        hr = pFrameEncode->SetPixelFormat(&format);
    }

    if (SUCCEEDED(hr)) hr = pFrameEncode->WriteSource(pBitmapSource, NULL);
    if (SUCCEEDED(hr)) hr = pFrameEncode->Commit();
    if (SUCCEEDED(hr)) hr = pEncoder->Commit();

    if (SUCCEEDED(hr)) {
        STATSTG stat = {};
        hr = pMemStream->Stat(&stat, STATFLAG_NONAME);
        if (SUCCEEDED(hr)) {
            ULONG pngSize = (ULONG)stat.cbSize.LowPart;
            buffer.resize(pngSize);
            LARGE_INTEGER zero = {};
            pMemStream->Seek(zero, STREAM_SEEK_SET, NULL);
            ULONG bytesRead = 0;
            hr = pMemStream->Read(buffer.data(), pngSize, &bytesRead);
            if (SUCCEEDED(hr) && bytesRead != pngSize) {
                hr = E_FAIL;
            }
        }
    }

    if (pFrameEncode) pFrameEncode->Release();
    if (pEncoder) pEncoder->Release();
    pMemStream->Release();
    return hr;
}
HRESULT ConvertPptToPdf(const std::wstring& sourcePath, const std::wstring& targetPath) {
    CLSID clsid;
    HRESULT hr = CLSIDFromProgID(L"PowerPoint.Application", &clsid);
    if (FAILED(hr)) {
        SetLastConverterError(L"Microsoft PowerPoint is not installed. This conversion requires PowerPoint.");
        return hr;
    }

    std::wstring escapedSource = EscapePowerShellSingleQuoted(sourcePath);
    std::wstring escapedTarget = EscapePowerShellSingleQuoted(targetPath);
    
    std::wstring cmd = L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -Command \""
                       L"$ErrorActionPreference='Stop'; "
                       L"try { "
                       L"  $app = New-Object -ComObject PowerPoint.Application; "
                       L"  $pres = $app.Presentations.Open('" + escapedSource + L"', -1, 0, 0); "
                       L"  $pres.ExportAsFixedFormat('" + escapedTarget + L"', 2); "
                       L"  $pres.Close(); "
                       L"  $app.Quit(); "
                       L"} catch { "
                       L"  Write-Error $_; "
                       L"  exit 1; "
                       L"}\"";


    std::wstring output;
    hr = RunProcessAndWait(cmd, &output);
    if (FAILED(hr) && Converter::GetLastErrorDetails().empty()) {
        if (!output.empty()) {
            SetLastConverterError(output);
        } else {
            SetLastConverterError(L"PowerPoint failed to convert the file. Make sure the file is not open elsewhere.");
        }
    }
    return hr;
}


HRESULT ConvertWordToPdf(const std::wstring& sourcePath, const std::wstring& targetPath) {
    CLSID clsid;
    if (FAILED(CLSIDFromProgID(L"Word.Application", &clsid))) {
        SetLastConverterError(L"Microsoft Word is not installed.");
        return E_FAIL;
    }
    std::wstring escapedSource = EscapePowerShellSingleQuoted(sourcePath);
    std::wstring escapedTarget = EscapePowerShellSingleQuoted(targetPath);
    std::wstring cmd = L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -Command \""
                       L"$ErrorActionPreference='Stop'; "
                       L"try { "
                       L"  $app = New-Object -ComObject Word.Application; "
                       L"  $doc = $app.Documents.Open('" + escapedSource + L"', $false, $true); "
                       L"  $doc.ExportAsFixedFormat('" + escapedTarget + L"', 17); "
                       L"  $doc.Close($false); "
                       L"  $app.Quit(); "
                       L"} catch { Write-Error $_; exit 1; }\"";
    std::wstring output;
    HRESULT hr = RunProcessAndWait(cmd, &output);

    if (FAILED(hr) && Converter::GetLastErrorDetails().empty() && !output.empty()) {
        SetLastConverterError(output);
    }
    return hr;
}


HRESULT ConvertExcelToPdf(const std::wstring& sourcePath, const std::wstring& targetPath) {
    CLSID clsid;
    if (FAILED(CLSIDFromProgID(L"Excel.Application", &clsid))) {
        SetLastConverterError(L"Microsoft Excel is not installed.");
        return E_FAIL;
    }
    std::wstring escapedSource = EscapePowerShellSingleQuoted(sourcePath);
    std::wstring escapedTarget = EscapePowerShellSingleQuoted(targetPath);
    std::wstring cmd = L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -Command \""
                       L"$ErrorActionPreference='Stop'; "
                       L"try { "
                       L"  $app = New-Object -ComObject Excel.Application; "
                       L"  $wb = $app.Workbooks.Open('" + escapedSource + L"', $true, $true); "
                       L"  $wb.ExportAsFixedFormat(0, '" + escapedTarget + L"'); "
                       L"  $wb.Close($false); "
                       L"  $app.Quit(); "
                       L"} catch { Write-Error $_; exit 1; }\"";
    std::wstring output;
    HRESULT hr = RunProcessAndWait(cmd, &output);

    if (FAILED(hr) && Converter::GetLastErrorDetails().empty() && !output.empty()) {
        SetLastConverterError(output);
    }
    return hr;
}


HRESULT ConvertPdfToWord(const std::wstring& sourcePath, const std::wstring& targetPath) {
    CLSID clsid;
    if (FAILED(CLSIDFromProgID(L"Word.Application", &clsid))) {
        SetLastConverterError(L"Microsoft Word is not installed.");
        return E_FAIL;
    }
    std::wstring escapedSource = EscapePowerShellSingleQuoted(sourcePath);
    std::wstring escapedTarget = EscapePowerShellSingleQuoted(targetPath);
    std::wstring cmd = L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -Command \""
                       L"$ErrorActionPreference='Stop'; "
                       L"try { "
                       L"  $app = New-Object -ComObject Word.Application; "
                       L"  $doc = $app.Documents.Open('" + escapedSource + L"'); "
                       L"  $doc.SaveAs2('" + escapedTarget + L"', 16); "
                       L"  $doc.Close(); "
                       L"  $app.Quit(); "
                       L"} catch { Write-Error $_; exit 1; }\"";
    std::wstring output;
    HRESULT hr = RunProcessAndWait(cmd, &output);

    if (FAILED(hr) && Converter::GetLastErrorDetails().empty() && !output.empty()) {
        SetLastConverterError(output);
    }
    return hr;
}

}


HRESULT Converter::ConvertImage(const std::wstring& sourcePath, TargetFormat format) {
    SetLastConverterError(L"");
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return hr;

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);


    if (format == TargetFormat::PdfToPngZip) {
        std::wstring targetPath = GetTargetPath(sourcePath, L".zip");
        return ConvertPdfToPngZip(sourcePath, targetPath);
    }

    if (format == TargetFormat::PdfToJpgZip) {
        std::wstring targetPath = GetTargetPath(sourcePath, L".zip");
        return ConvertPdfToImageZip(
            sourcePath,
            targetPath,
            L".jpg",
            winrt::Windows::Graphics::Imaging::BitmapEncoder::JpegEncoderId());
    }

    if (format == TargetFormat::Mp4ToMp3) {
        std::wstring targetPath = GetTargetPath(sourcePath, L".mp3");
        return ConvertWithFfmpeg(sourcePath, targetPath, L"-vn -c:a libmp3lame -q:a 2");
    }

    if (format == TargetFormat::Mp4ToMov) {
        std::wstring targetPath = GetTargetPath(sourcePath, L".mov");
        return ConvertWithFfmpeg(sourcePath, targetPath, L"-c copy");
    }

    if (format == TargetFormat::Mp3ToOgg) {
        std::wstring targetPath = GetTargetPath(sourcePath, L".ogg");
        return ConvertWithFfmpeg(sourcePath, targetPath, L"-codec:a libvorbis");
    }

    if (format == TargetFormat::PptToPdf) {
        std::wstring targetPath = GetTargetPath(sourcePath, L".pdf");
        return ConvertPptToPdf(sourcePath, targetPath);
    }

    if (format == TargetFormat::PdfToWord) {
        std::wstring targetPath = GetTargetPath(sourcePath, L".docx");
        return ConvertPdfToWord(sourcePath, targetPath);
    }

    if (format == TargetFormat::WordToPdf) {
        std::wstring targetPath = GetTargetPath(sourcePath, L".pdf");
        return ConvertWordToPdf(sourcePath, targetPath);
    }

    if (format == TargetFormat::ExcelToPdf) {
        std::wstring targetPath = GetTargetPath(sourcePath, L".pdf");
        hr = ConvertExcelToPdf(sourcePath, targetPath);
        Gdiplus::GdiplusShutdown(gdiplusToken);
        return hr;
    }

    IWICImagingFactory* pFactory = NULL;



    hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
    if (FAILED(hr)) return hr;

    IWICBitmapDecoder* pDecoder = NULL;
    hr = pFactory->CreateDecoderFromFilename(sourcePath.c_str(), NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDecoder);
    if (FAILED(hr)) {
        pFactory->Release();
        return hr;
    }

    IWICBitmapFrameDecode* pFrame = NULL;
    hr = pDecoder->GetFrame(0, &pFrame);
    if (FAILED(hr)) {
        pDecoder->Release();
        pFactory->Release();
        return hr;
    }

    std::wstring targetPath;
    if (format == TargetFormat::Png) {
        targetPath = GetTargetPath(sourcePath, L".png");
        hr = SaveToImage(pFrame, targetPath, GUID_ContainerFormatPng);
    } else if (format == TargetFormat::Jpg) {
        targetPath = GetTargetPath(sourcePath, L".jpg");
        hr = SaveToImage(pFrame, targetPath, GUID_ContainerFormatJpeg);
    } else if (format == TargetFormat::Webp) {
        targetPath = GetTargetPath(sourcePath, L".webp");
        // GUID_ContainerFormatWebp: {e094e098-644a-4712-881b-a5d6c8b919d3}
        GUID GUID_Webp = { 0xe094e098, 0x644a, 0x4712, { 0x88, 0x1b, 0xa5, 0xd6, 0xc8, 0xb9, 0x19, 0xd3 } };
        hr = SaveToImage(pFrame, targetPath, GUID_Webp);
    } else if (format == TargetFormat::Bmp) {
        targetPath = GetTargetPath(sourcePath, L".bmp");
        hr = SaveToImage(pFrame, targetPath, GUID_ContainerFormatBmp);
    } else if (format == TargetFormat::Ico) {
        targetPath = GetTargetPath(sourcePath, L".ico");
        hr = SaveToIco(pFrame, targetPath);
    } else if (format == TargetFormat::Pdf) {
        targetPath = GetTargetPath(sourcePath, L".pdf");
        hr = SaveToPdf(pFrame, targetPath);
    } else if (format == TargetFormat::AddWatermark) {
        targetPath = GetTargetPath(sourcePath, L"_watermarked.jpg");
        hr = SaveWithWatermark(pFactory, pFrame, targetPath, L"QUICK CONVERT");
    }



    pFrame->Release();
    pDecoder->Release();
    pFactory->Release();

    Gdiplus::GdiplusShutdown(gdiplusToken);
    return hr;
}


HRESULT Converter::SaveToImage(IWICBitmapSource* pBitmapSource, const std::wstring& targetPath, REFGUID containerFormat) {
    IWICImagingFactory* pFactory = NULL;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
    if (FAILED(hr)) return hr;

    IWICStream* pStream = NULL;
    hr = pFactory->CreateStream(&pStream);
    if (SUCCEEDED(hr)) {
        hr = pStream->InitializeFromFilename(targetPath.c_str(), GENERIC_WRITE);
    }

    IWICBitmapEncoder* pEncoder = NULL;
    if (SUCCEEDED(hr)) {
        hr = pFactory->CreateEncoder(containerFormat, NULL, &pEncoder);
    }

    if (SUCCEEDED(hr)) {
        hr = pEncoder->Initialize(pStream, WICBitmapEncoderNoCache);
    }

    IWICBitmapFrameEncode* pFrameEncode = NULL;
    if (SUCCEEDED(hr)) {
        hr = pEncoder->CreateNewFrame(&pFrameEncode, NULL);
    }

    if (SUCCEEDED(hr)) {
        hr = pFrameEncode->Initialize(NULL);
    }

    if (SUCCEEDED(hr)) {
        UINT width, height;
        pBitmapSource->GetSize(&width, &height);
        hr = pFrameEncode->SetSize(width, height);
        
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        if (SUCCEEDED(hr)) hr = pFrameEncode->SetPixelFormat(&format);
        if (SUCCEEDED(hr)) hr = pFrameEncode->WriteSource(pBitmapSource, NULL);
    }

    if (SUCCEEDED(hr)) hr = pFrameEncode->Commit();
    if (SUCCEEDED(hr)) hr = pEncoder->Commit();

    if (pFrameEncode) pFrameEncode->Release();
    if (pEncoder) pEncoder->Release();
    if (pStream) pStream->Release();
    if (pFactory) pFactory->Release();

    return hr;
}


HRESULT Converter::SaveWithWatermark(IWICImagingFactory* pFactory, IWICBitmapSource* pBitmapSource, const std::wstring& targetPath, const std::wstring& watermarkText) {
    UINT width, height;
    pBitmapSource->GetSize(&width, &height);

    IWICBitmap* pBitmap = NULL;
    HRESULT hr = pFactory->CreateBitmapFromSource(pBitmapSource, WICBitmapCacheOnDemand, &pBitmap);
    if (FAILED(hr)) return hr;

    {
        Gdiplus::Bitmap gdiBitmap(width, height, PixelFormat32bppARGB);
        Gdiplus::Graphics graphics(&gdiBitmap);

        WICRect rect = { 0, 0, (INT)width, (INT)height };
        IWICBitmapLock* pLock = NULL;
        if (SUCCEEDED(pBitmap->Lock(&rect, WICBitmapLockRead, &pLock))) {
            UINT bufferSize = 0;
            BYTE* pBuffer = NULL;
            pLock->GetDataPointer(&bufferSize, &pBuffer);
            
            Gdiplus::BitmapData data;
            Gdiplus::Rect gdiRect(0, 0, width, height);
            gdiBitmap.LockBits(&gdiRect, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &data);
            memcpy(data.Scan0, pBuffer, bufferSize);
            gdiBitmap.UnlockBits(&data);
            pLock->Release();
        }

        Gdiplus::Font font(L"Arial", (float)(height / 15), Gdiplus::FontStyleBold);
        Gdiplus::SolidBrush brush(Gdiplus::Color(128, 255, 255, 255));
        Gdiplus::StringFormat format;
        format.SetAlignment(Gdiplus::StringAlignmentCenter);
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

        Gdiplus::RectF layoutRect(0, 0, (float)width, (float)height);
        graphics.DrawString(watermarkText.c_str(), -1, &font, layoutRect, &format, &brush);

        CLSID encoderClsid;
        CLSIDFromString(L"{557cf401-1a04-11d3-9a73-0000f81ef32e}", &encoderClsid);
        gdiBitmap.Save(targetPath.c_str(), &encoderClsid, NULL);
    }

    pBitmap->Release();
    return S_OK;
}



HRESULT Converter::SaveToIco(IWICBitmapSource* pBitmapSource, const std::wstring& targetPath) {
    IWICImagingFactory* pFactory = NULL;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
    if (FAILED(hr)) return hr;

    UINT sourceWidth = 0;
    UINT sourceHeight = 0;
    pBitmapSource->GetSize(&sourceWidth, &sourceHeight);
    if (sourceWidth == 0 || sourceHeight == 0) {
        pFactory->Release();
        return E_FAIL;
    }

    IWICBitmapScaler* pScaler = NULL;
    hr = pFactory->CreateBitmapScaler(&pScaler);
    if (SUCCEEDED(hr)) {
        hr = pScaler->Initialize(pBitmapSource, 256, 256, WICBitmapInterpolationModeFant);
    }

    std::vector<BYTE> pngBuffer;
    if (SUCCEEDED(hr)) {
        hr = EncodeBitmapSourceToPngBuffer(pFactory, pScaler, pngBuffer);
    }

    if (SUCCEEDED(hr)) {
        std::ofstream ico(targetPath, std::ios::binary);
        if (!ico.is_open()) {
            hr = E_FAIL;
        } else {
            const WORD reserved = 0;
            const WORD type = 1;
            const WORD count = 1;
            const BYTE width = 0;
            const BYTE height = 0;
            const BYTE colorCount = 0;
            const BYTE entryReserved = 0;
            const WORD planes = 1;
            const WORD bitCount = 32;
            const DWORD bytesInRes = (DWORD)pngBuffer.size();
            const DWORD imageOffset = 6 + 16;

            ico.write((const char*)&reserved, sizeof(reserved));
            ico.write((const char*)&type, sizeof(type));
            ico.write((const char*)&count, sizeof(count));
            ico.write((const char*)&width, sizeof(width));
            ico.write((const char*)&height, sizeof(height));
            ico.write((const char*)&colorCount, sizeof(colorCount));
            ico.write((const char*)&entryReserved, sizeof(entryReserved));
            ico.write((const char*)&planes, sizeof(planes));
            ico.write((const char*)&bitCount, sizeof(bitCount));
            ico.write((const char*)&bytesInRes, sizeof(bytesInRes));
            ico.write((const char*)&imageOffset, sizeof(imageOffset));
            ico.write((const char*)pngBuffer.data(), pngBuffer.size());

            if (!ico.good()) {
                hr = E_FAIL;
            }
        }
    }

    if (pScaler) pScaler->Release();
    if (pFactory) pFactory->Release();
    return hr;
}

std::wstring Converter::GetTargetPath(const std::wstring& sourcePath, const std::wstring& extension) {
    wchar_t path[MAX_PATH];
    wcscpy_s(path, sourcePath.c_str());
    PathRemoveExtensionW(path);
    
    std::wstring base = path;
    std::wstring target = base + extension;
    
    // Simple deduplication if file exists
    int i = 1;
    while (PathFileExistsW(target.c_str())) {
        target = base + L" (" + std::to_wstring(i++) + L")" + extension;
    }
    
    return target;
}

HRESULT Converter::SaveToPdf(IWICBitmapSource* pBitmapSource, const std::wstring& targetPath) {
    // For PDF, we first encode the image as a JPEG to a memory stream, then wrap it.
    IWICImagingFactory* pFactory = NULL;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
    if (FAILED(hr)) return hr;

    IStream* pMemStream = SHCreateMemStream(NULL, 0);
    if (!pMemStream) {
        pFactory->Release();
        return E_OUTOFMEMORY;
    }

    IWICBitmapEncoder* pEncoder = NULL;
    hr = pFactory->CreateEncoder(GUID_ContainerFormatJpeg, NULL, &pEncoder);
    if (SUCCEEDED(hr)) hr = pEncoder->Initialize(pMemStream, WICBitmapEncoderNoCache);

    IWICBitmapFrameEncode* pFrameEncode = NULL;
    UINT width, height;
    pBitmapSource->GetSize(&width, &height);

    if (SUCCEEDED(hr)) hr = pEncoder->CreateNewFrame(&pFrameEncode, NULL);
    if (SUCCEEDED(hr)) hr = pFrameEncode->Initialize(NULL);
    if (SUCCEEDED(hr)) hr = pFrameEncode->SetSize(width, height);
    if (SUCCEEDED(hr)) hr = pFrameEncode->WriteSource(pBitmapSource, NULL);
    if (SUCCEEDED(hr)) hr = pFrameEncode->Commit();
    if (SUCCEEDED(hr)) hr = pEncoder->Commit();

    if (SUCCEEDED(hr)) {
        STATSTG stat;
        pMemStream->Stat(&stat, STATFLAG_NONAME);
        ULONG jpgSize = (ULONG)stat.cbSize.LowPart;

        std::vector<BYTE> buffer(jpgSize);
        LARGE_INTEGER liZero = {0};
        pMemStream->Seek(liZero, STREAM_SEEK_SET, NULL);
        pMemStream->Read(buffer.data(), jpgSize, NULL);

        // Build minimal PDF
        std::ofstream pdf(targetPath, std::ios::binary);
        if (pdf.is_open()) {
            char header[] = "%PDF-1.4\n";
            pdf.write(header, strlen(header));

            long offsets[10];
            int objCount = 0;

            // 1: Catalog
            offsets[++objCount] = (long)pdf.tellp();
            pdf << objCount << " 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";

            // 2: Pages root
            offsets[++objCount] = (long)pdf.tellp();
            pdf << objCount << " 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n";

            // 3: Page
            offsets[++objCount] = (long)pdf.tellp();
            pdf << objCount << " 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " << width << " " << height << "] /Resources << /XObject << /I1 4 0 R >> >> /Contents 5 0 R >>\nendobj\n";

            // 4: Image XObject
            offsets[++objCount] = (long)pdf.tellp();
            pdf << objCount << " 0 obj\n<< /Type /XObject /Subtype /Image /Width " << width << " /Height " << height << " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length " << jpgSize << " >>\nstream\n";
            pdf.write((char*)buffer.data(), jpgSize);
            pdf << "\nendstream\nendobj\n";

            // 5: Contents (draw the image)
            std::string content = "q " + std::to_string(width) + " 0 0 " + std::to_string(height) + " 0 0 cm /I1 Do Q";
            offsets[++objCount] = (long)pdf.tellp();
            pdf << objCount << " 0 obj\n<< /Length " << content.length() << " >>\nstream\n" << content << "\nendstream\nendobj\n";

            // XRef
            long xrefPos = (long)pdf.tellp();
            pdf << "xref\n0 " << (objCount + 1) << "\n0000000000 65535 f \n";
            for (int i = 1; i <= objCount; i++) {
                char buf[25];
                sprintf_s(buf, "%010ld 00000 n \n", offsets[i]);
                pdf << buf;
            }

            // Trailer
            pdf << "trailer\n<< /Size " << (objCount + 1) << " /Root 1 0 R >>\nstartxref\n" << xrefPos << "\n%%EOF";
            pdf.close();
        } else {
            hr = E_FAIL;
        }
    }

    if (pFrameEncode) pFrameEncode->Release();
    if (pEncoder) pEncoder->Release();
    if (pMemStream) pMemStream->Release();
    if (pFactory) pFactory->Release();

    return hr;
}

using namespace winrt;
using namespace Windows::Data::Pdf;
using namespace Windows::Storage;
using namespace Windows::Storage::Streams;

namespace {
HRESULT ConvertPdfToImageZip(const std::wstring& sourcePath,
                             const std::wstring& targetPath,
                             const std::wstring& imageExtension,
                             winrt::guid encoderId) {
    std::wstring tempPathForCleanup;

    try {
        // init_apartment is now called once at the start of the thread in QuickConvertExe.cpp
        
        // 1. Load PDF
        StorageFile file = StorageFile::GetFileFromPathAsync(sourcePath).get();
        PdfDocument doc = PdfDocument::LoadFromFileAsync(file).get();
        uint32_t pageCount = doc.PageCount();

        // 2. Create Temp Folder
        StorageFolder tempDir{ nullptr };
        HRESULT tempHr = CreateTempStorageFolder(tempDir, tempPathForCleanup);
        if (FAILED(tempHr)) {
            return tempHr;
        }

        // 3. Render Pages
        for (uint32_t i = 0; i < pageCount; i++) {
            PdfPage page = doc.GetPage(i);
            std::wstring outName = L"page_" + std::to_wstring(i + 1) + imageExtension;
            StorageFile outFile = tempDir.CreateFileAsync(outName, CreationCollisionOption::ReplaceExisting).get();
            IRandomAccessStream stream = outFile.OpenAsync(FileAccessMode::ReadWrite).get();
            PdfPageRenderOptions options;
            options.BitmapEncoderId(encoderId);
            page.RenderToStreamAsync(stream, options).get();
            stream.FlushAsync().get();
        }

        // 4. Zip (Using PowerShell for JUST the zipping part as it's the most lightweight ZIP creator available)
        // Since we already have the files in a clean temp folder, this is very safe.
        std::wstring escapedTemp = EscapePowerShellSingleQuoted(tempPathForCleanup);
        std::wstring escapedTarget = EscapePowerShellSingleQuoted(targetPath);
        std::wstring cmd = L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -Command \""
                           L"$ErrorActionPreference='Stop'; "
                           L"Add-Type -AssemblyName 'System.IO.Compression.FileSystem'; "
                           L"[System.IO.Compression.ZipFile]::CreateFromDirectory('" + escapedTemp + L"', '" + escapedTarget + L"')\"";

        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        std::vector<wchar_t> cmdLine(cmd.begin(), cmd.end());
        cmdLine.push_back(L'\0');

        if (!CreateProcessW(NULL, cmdLine.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            DeleteDirectoryBestEffort(tempPathForCleanup);
            return HRESULT_FROM_WIN32(GetLastError());
        }

        WaitForSingleObject(pi.hProcess, INFINITE);

        DWORD exitCode = 0;
        BOOL gotExitCode = GetExitCodeProcess(pi.hProcess, &exitCode);

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        DeleteDirectoryBestEffort(tempPathForCleanup);

        if (!gotExitCode) {
            return HRESULT_FROM_WIN32(GetLastError());
        }

        if (exitCode != 0) {
            return HRESULT_FROM_WIN32(exitCode);
        }

        if (!PathFileExistsW(targetPath.c_str())) {
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        }

        return S_OK;
    } catch (const winrt::hresult_error& e) {
        DeleteDirectoryBestEffort(tempPathForCleanup);
        return e.code();
    } catch (...) {
        DeleteDirectoryBestEffort(tempPathForCleanup);
        return E_FAIL;
    }
}
}

HRESULT Converter::ConvertPdfToPngZip(const std::wstring& sourcePath, const std::wstring& targetPath) {
    return ConvertPdfToImageZip(
        sourcePath,
        targetPath,
        L".png",
        winrt::Windows::Graphics::Imaging::BitmapEncoder::PngEncoderId());
}

const std::wstring& Converter::GetLastErrorDetails() {
    return g_LastConverterError;
}
