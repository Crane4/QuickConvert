#include "Converter.h"
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: test_conversion.exe <source> <format: png|jpg|pdf|webp|ico|pdfzip|pdfjpgzip|mp4mp3|mp4mov|mp3ogg|movmp3|movmp4>" << std::endl;
        return 1;
    }

    std::wstring source = std::wstring(argv[1], argv[1] + strlen(argv[1]));
    std::string formatStr = argv[2];
    TargetFormat format;

    if (formatStr == "png") format = TargetFormat::Png;
    else if (formatStr == "jpg") format = TargetFormat::Jpg;
    else if (formatStr == "pdf") format = TargetFormat::Pdf;
    else if (formatStr == "webp") format = TargetFormat::Webp;
    else if (formatStr == "ico") format = TargetFormat::Ico;
    else if (formatStr == "pdfzip") format = TargetFormat::PdfToPngZip;
    else if (formatStr == "pdfjpgzip") format = TargetFormat::PdfToJpgZip;
    else if (formatStr == "mp4mp3") format = TargetFormat::Mp4ToMp3;
    else if (formatStr == "mp4mov") format = TargetFormat::Mp4ToMov;
    else if (formatStr == "movmp3") format = TargetFormat::MovToMp3;
    else if (formatStr == "movmp4") format = TargetFormat::MovToMp4;
    else if (formatStr == "mp3ogg") format = TargetFormat::Mp3ToOgg;
    else {
        std::cout << "Invalid format" << std::endl;
        return 1;
    }

    HRESULT hr = Converter::ConvertImage(source, format);
    if (SUCCEEDED(hr)) {
        std::cout << "Success!" << std::endl;
    } else {
        std::cout << "Failed with HRESULT: 0x" << std::hex << hr << std::endl;
        std::wcout << L"Details: " << Converter::GetLastErrorDetails() << std::endl;
    }

    return 0;
}
