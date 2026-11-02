#include "../GameBarWidget/pch.h"
#include "SettingsManager.h"
#include <winrt/Windows.Storage.h>
#include <algorithm>
#include <cwctype>
#include <set>
#include <stdexcept>
using namespace winrt;
using namespace Windows::Storage;
using namespace Windows::System;
namespace wardogs {
Settings LoadSettings() {
    Settings s;auto values=ApplicationData::Current().LocalSettings().Values();
    auto read=[&](wchar_t const* key,auto fallback){try{auto v=values.TryLookup(key);return v?unbox_value<decltype(fallback)>(v):fallback;}catch(...){return fallback;}};
    s.english=read(L"english",false);s.preview=read(L"preview",true);
    s.opacity=read(L"opacity",.88);s.captureSize=read(L"captureSize",320);
    for(int i=0;i<4;++i){auto key=L"hotkey"+std::to_wstring(i);s.hotkeys[i]=read(key.c_str(),hstring(s.hotkeys[i]));}
    NormalizeSettings(s);
    return s;
}
void SaveSettings(Settings const& s) {
    // Explicit allowlist: never persist coordinates, ROI anchors, source identity or OCR text.
    auto values=ApplicationData::Current().LocalSettings().Values();
    values.Insert(L"english",box_value(s.english));values.Insert(L"preview",box_value(s.preview));values.Insert(L"opacity",box_value(s.opacity));values.Insert(L"captureSize",box_value(s.captureSize));
    for(int i=0;i<4;++i)values.Insert(L"hotkey"+std::to_wstring(i),box_value(hstring(s.hotkeys[i])));
}
}
