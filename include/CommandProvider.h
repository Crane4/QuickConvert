#pragma once
#include <windows.h>
#include <shobjidl.h>

// GUID: {BF5E6C7D-8B9A-4E3D-BC1F-2E4D3C2B1A0F}
extern const GUID CLSID_QuickConvert;

HRESULT CreateCommandProvider(REFIID riid, void** ppv);
