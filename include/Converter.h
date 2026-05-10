#pragma once

#include <windows.h>
#include <wincodec.h>
#include <string>
#include <vector>

enum class TargetFormat {
    Png,
    Jpg,
    Pdf,
    Webp,
    Bmp,
    Ico,
    PdfToPngZip,
    PdfToJpgZip,
    Mp4ToMp3,
    Mp4ToMov,
    Mp3ToOgg,
    PptToPdf,
    AddWatermark,
    CompressPdf,
    PdfToWord,
    WordToPdf,
    ExcelToPdf
};




class Converter {
public:
    static HRESULT ConvertImage(const std::wstring& sourcePath, TargetFormat format);
    static HRESULT ConvertPdfToPngZip(const std::wstring& sourcePath, const std::wstring& targetPath);
    static const std::wstring& GetLastErrorDetails();

private:
    static HRESULT SaveToImage(IWICBitmapSource* pBitmapSource, const std::wstring& targetPath, REFGUID containerFormat);
    static HRESULT SaveWithWatermark(IWICImagingFactory* pFactory, IWICBitmapSource* pBitmapSource, const std::wstring& targetPath, const std::wstring& watermarkText);
    static HRESULT SaveToPdf(IWICBitmapSource* pBitmapSource, const std::wstring& targetPath);

    static HRESULT SaveToIco(IWICBitmapSource* pBitmapSource, const std::wstring& targetPath);
    
    static std::wstring GetTargetPath(const std::wstring& sourcePath, const std::wstring& extension);

};
