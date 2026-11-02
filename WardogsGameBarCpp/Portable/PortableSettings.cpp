#include "../Shared/WindowsIncludes.h"
#include "PortableSettings.h"
#include <winrt/Windows.Data.Json.h>
#include <shlobj.h>
#include <fstream>
#include <iterator>
#include <stdexcept>
using namespace winrt;
using namespace Windows::Data::Json;
namespace wardogs {
namespace {
bool Writable(std::filesystem::path const& path) {
    auto raw=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(raw==INVALID_HANDLE_VALUE)return false;
    handle file(raw);return true;
}
}
PortableSettingsStore::PortableSettingsStore() {
    std::wstring exe(32768,L'\0');auto length=GetModuleFileNameW(nullptr,exe.data(),static_cast<DWORD>(exe.size()));
    if(!length||length>=exe.size())throw std::runtime_error("Cannot locate executable");exe.resize(length);
    m_adjacent=std::filesystem::path(exe).parent_path()/L"WardogsPortable.settings.json";
    if(Writable(m_adjacent)){m_path=m_adjacent;return;}
    PWSTR folder{};check_hresult(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_CREATE,nullptr,&folder));
    std::filesystem::path base(folder);CoTaskMemFree(folder);base/=L"WardogsMortarCalculator";
    std::filesystem::create_directories(base);m_path=base/L"WardogsPortable.settings.json";
}
OverlaySettings PortableSettingsStore::Load() const {
    OverlaySettings settings;
    try {
        auto path=std::filesystem::exists(m_path)?m_path:m_adjacent;
        std::ifstream stream(path,std::ios::binary);if(!stream)return settings;
        // Only a small settings document is accepted, never arbitrary image data.
        stream.seekg(0,std::ios::end);if(stream.tellg()>65536)return settings;stream.seekg(0);
        std::string text((std::istreambuf_iterator<char>(stream)),{});if(text.empty())return settings;
        auto object=JsonObject::Parse(to_hstring(text));
        settings.common.english=object.GetNamedBoolean(L"english",false);
        settings.common.preview=object.GetNamedBoolean(L"preview",true);
        settings.common.opacity=object.GetNamedNumber(L"opacity",.88);
        auto integer=[&](wchar_t const* name,int fallback,int minimum,int maximum){double n=object.GetNamedNumber(name,fallback);return std::isfinite(n)&&n>=minimum&&n<=maximum?static_cast<int>(n):fallback;};
        settings.common.captureSize=integer(L"captureSize",320,160,640);
        settings.x=integer(L"x",40,-100000,100000);settings.y=integer(L"y",40,-100000,100000);
        settings.topmost=object.GetNamedBoolean(L"topmost",true);settings.clickThrough=object.GetNamedBoolean(L"clickThrough",true);
        for(int i=0;i<4;++i)settings.common.hotkeys[i]=object.GetNamedString(L"hotkey"+std::to_wstring(i),settings.common.hotkeys[i]);
        NormalizeSettings(settings.common);
    }catch(...){return OverlaySettings{};}
    return settings;
}
void PortableSettingsStore::Save(OverlaySettings const& settings) const {
    JsonObject object;
    object.SetNamedValue(L"english",JsonValue::CreateBooleanValue(settings.common.english));
    object.SetNamedValue(L"preview",JsonValue::CreateBooleanValue(settings.common.preview));
    object.SetNamedValue(L"opacity",JsonValue::CreateNumberValue(settings.common.opacity));
    object.SetNamedValue(L"captureSize",JsonValue::CreateNumberValue(settings.common.captureSize));
    object.SetNamedValue(L"x",JsonValue::CreateNumberValue(settings.x));object.SetNamedValue(L"y",JsonValue::CreateNumberValue(settings.y));
    object.SetNamedValue(L"topmost",JsonValue::CreateBooleanValue(settings.topmost));object.SetNamedValue(L"clickThrough",JsonValue::CreateBooleanValue(settings.clickThrough));
    for(int i=0;i<4;++i)object.SetNamedValue(L"hotkey"+std::to_wstring(i),JsonValue::CreateStringValue(settings.common.hotkeys[i]));
    const auto utf8=to_string(object.Stringify());auto temporary=m_path;temporary+=L".tmp";
    try {
        {std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);if(!stream)throw std::runtime_error("Cannot write settings");stream.write(utf8.data(),static_cast<std::streamsize>(utf8.size()));stream.flush();if(!stream)throw std::runtime_error("Cannot flush settings");}
        if(!MoveFileExW(temporary.c_str(),m_path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw_last_error();
    }catch(...){std::error_code ignored;std::filesystem::remove(temporary,ignored);throw;}
}
}
