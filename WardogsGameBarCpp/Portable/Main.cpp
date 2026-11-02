#include "OverlayWindow.h"
#include "Resources.h"
#include "StartupCheck.h"
#include <exception>
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    int result=1;
    try {
        wardogs::PortableSettingsStore settings;
        if(wardogs::ConfirmPortableStartup(instance,settings.Load().common.english)) {
            wardogs::OverlayWindow window;
            result=window.Run(instance,show);
        } else result=0;
    } catch(winrt::hresult_error const& error) {
        MessageBoxW(nullptr,error.message().c_str(),L"WARDOGS Portable",MB_OK|MB_ICONERROR);
    } catch(std::exception const& error) {
        auto text=winrt::to_hstring(error.what());
        MessageBoxW(nullptr,text.c_str(),L"WARDOGS Portable",MB_OK|MB_ICONERROR);
    }
    winrt::uninit_apartment();
    return result;
}
