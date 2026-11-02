#include "../GameBarWidget/pch.h"
#include "../Capture/CaptureService.h"
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.ApplicationModel.AppService.h>
#include <thread>
using namespace winrt;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::AppService;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    init_apartment(apartment_type::multi_threaded);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    handle finished(CreateEventExW(nullptr,nullptr,CREATE_EVENT_MANUAL_RESET,EVENT_ALL_ACCESS));
    handle disconnected(CreateEventExW(nullptr,nullptr,CREATE_EVENT_MANUAL_RESET,EVENT_ALL_ACCESS));
    if(!finished||!disconnected)return 1;
    // No idle companion. Also exit if the widget/IPC disappears or exceeds 15s.
    std::thread watchdog([&]{HANDLE handles[]{finished.get(),disconnected.get()};DWORD result=WaitForMultipleObjects(2,handles,FALSE,15000);if(result!=WAIT_OBJECT_0)ExitProcess(1);});
    int exitCode=0;
    try{
        AppServiceConnection connection;connection.AppServiceName(L"Wardogs.CursorOcr");connection.PackageFamilyName(Package::Current().Id().FamilyName());
        auto token=connection.ServiceClosed([&](auto const&,auto const&){SetEvent(disconnected.get());});
        if(connection.OpenAsync().get()!=AppServiceConnectionStatus::Success)throw hresult_error(E_FAIL,L"Cannot connect to widget");
        ValueSet ready;ready.Insert(L"command",box_value(L"ready"));
        auto permission=connection.SendMessageAsync(ready).get();
        if(permission.Status()!=AppServiceResponseStatus::Success||!permission.Message().HasKey(L"size"))throw hresult_error(E_ACCESSDENIED,L"Widget did not request capture");
        int size=unbox_value<int>(permission.Message().Lookup(L"size"));
        ValueSet message;message.Insert(L"command",box_value(L"result"));
        try{
            auto image=wardogs::CaptureAroundCursor(size);auto output=std::make_shared<wardogs::ScanOutput>();
            wardogs::RecognizeAsync(std::move(image),output).get();
            message.Insert(L"ok",box_value(output->coordinates.has_value()));
            if(output->coordinates){message.Insert(L"x",box_value(output->coordinates->x));message.Insert(L"y",box_value(output->coordinates->y));}
            if(!output->preview.bgra.empty()){
                auto thumbnail=wardogs::MakePreview(output->preview);
                message.Insert(L"preview",PropertyValue::CreateUInt8Array(thumbnail.bgra));
            }
        }catch(hresult_error const& error){message.Insert(L"ok",box_value(false));message.Insert(L"error",box_value(error.message()));}
        catch(...){message.Insert(L"ok",box_value(false));message.Insert(L"error",box_value(L"Capture or OCR failed"));}
        connection.SendMessageAsync(message).get();connection.ServiceClosed(token);
        SetEvent(finished.get());connection.Close();
    }catch(...){exitCode=1;SetEvent(finished.get());}
    watchdog.join();return exitCode;
}
