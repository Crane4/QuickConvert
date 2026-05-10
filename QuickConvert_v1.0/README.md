# 🚀 Quick Convert - Windows Shell Extension

**Quick Convert** is a high-performance Windows Shell Extension that allows you to convert images and documents instantly via the right-click context menu. It is fully optimized for Windows 11 with native Modern Context Menu support.

![Hero Image](docs/assets/hero.png)

## ✨ Features

- **Images**: Convert to `PNG`, `JPG`, `WebP`, `BMP`, `ICO`.
- **PDF Integration**:
  - Image to **PDF**.
  - **PDF to PNG/JPG (ZIP)**: Converts every page of a PDF into high-quality images and packs them into a single ZIP file.
  - **PDF to Word**: Convert PDFs back to editable `.docx` files (requires Word).
- **Office Support**:
  - **PowerPoint to PDF** (`.pptx`, `.ppt`).
  - **Word to PDF** (`.docx`, `.doc`).
  - **Excel to PDF** (`.xlsx`, `.xls`).
- **Advanced Tools**:
  - **Add Watermark**: Quick text overlays.
- **Media (FFmpeg)**:
  - **MP4 to MP3** (Audio extraction).
  - **MP4 to MOV**.
  - **MP3 to OGG**.
- **Windows 11 Ready**: Native integration in the primary context menu.
- **Modern UI**: Dark Mode support, real-time progress bars, and high-fidelity design.

## 🛠️ Installation (User)

1.  **Download**: Get the latest release from the [Releases](https://github.com/USER/QuickConvert/releases) page.
2.  **Extract**: Unzip the folder to a permanent location (e.g., `C:\Program Files\QuickConvert`).
3.  **Install**: Run `Install.exe` as Administrator.
4.  **Usage**: Right-click any file to see the **Quick Convert** menu.

## 📁 Project Structure

```text
.
├── assets/             # Icons, manifests, and resource files
├── docs/               # GitHub Pages documentation
├── include/            # C++ Header files
├── src/                # C++ Source code
│   └── tools/          # Installer/Uninstaller source code
├── release.bat         # One-click release script
└── LICENSE             # MIT License
```

## 💻 For Developers

- **Requirements**: Visual Studio 2022 with C++ Build Tools and Windows SDK.
- **Build All**: Run `release.bat` to build the app, the DLL, and all installer tools.
- **Dependencies**: Ensure `ffmpeg.exe` is in the root folder for media features.

## 📝 Technical Details

- **Core**: Written in pure C++17 using WIC (Windows Imaging Component) and WinRT APIs.
- **Stability**: The Shell Extension (DLL) is lightweight and stable, delegating heavy work to a background process (`QuickConvert.exe`).
- **Identity**: Uses a Sparse Package manifest to grant the shell extension identity on Windows 11.

---
License: **MIT** | Developed by **crane4**.

