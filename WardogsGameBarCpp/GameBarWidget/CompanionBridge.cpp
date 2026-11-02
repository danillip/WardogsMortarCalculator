#include "pch.h"
#include "CompanionBridge.h"
#include <winrt/Windows.ApplicationModel.h>
#include <chrono>
using namespace winrt;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::AppService;
using namespace Windows::Foundation::Collections;
namespace wardogs {
Windows::Foundation::IAsyncAction CompanionBridge::ScanAsync(int size,std::shared_ptr<ScanOutput> output) {
    auto lifetime=shared_from_this();auto cancel=co_await get_cancellation_token();cancel.enable_propagation();
    if(!m_ready)throw_last_error();
    uint64_t generation;
    {std::scoped_lock lock(m_mutex);if(m_active)throw hresult_error(E_PENDING,L"OCR is already running");generation=++m_generation;m_active=true;m_size=size;m_output=output;m_error.clear();ResetEvent(m_ready.get());}
    struct Guard{CompanionBridge* bridge;uint64_t generation;~Guard(){bridge->Stop(generation);}}guard{this,generation};
    co_await FullTrustProcessLauncher::LaunchFullTrustProcessForCurrentAppAsync();
    bool signaled=co_await resume_on_signal(m_ready.get(),std::chrono::seconds(15));
    if(cancel())throw hresult_canceled();
    std::wstring error;{std::scoped_lock lock(m_mutex);if(generation!=m_generation)throw hresult_canceled();error=m_error;}
    if(!signaled)throw hresult_error(E_FAIL,L"Companion did not respond in time");
    if(!error.empty())throw hresult_error(E_FAIL,error);
}
void CompanionBridge::Accept(Activation::BackgroundActivatedEventArgs const& args) {
    auto details=args.TaskInstance().TriggerDetails().try_as<AppServiceTriggerDetails>();
    if(!details||details.Name()!=L"Wardogs.CursorOcr"||details.CallerPackageFamilyName()!=Package::Current().Id().FamilyName())return;
    std::scoped_lock lock(m_mutex);
    if(!m_active||m_connection){details.AppServiceConnection().Close();return;}
    m_deferral=args.TaskInstance().GetDeferral();m_connection=details.AppServiceConnection();
    auto weak=weak_from_this();auto generation=m_generation;
    m_connection.RequestReceived([weak,generation](auto const&,auto const& e){if(auto self=weak.lock())self->Request(e,generation);});
    args.TaskInstance().Canceled([weak,generation](auto const&,auto const&){if(auto self=weak.lock())self->Stop(generation);});
}
fire_and_forget CompanionBridge::Request(AppServiceRequestReceivedEventArgs args,uint64_t generation) {
    auto lifetime=shared_from_this();auto deferral=args.GetDeferral();bool done=false;
    ValueSet response;
    try{
        auto message=args.Request().Message();auto command=unbox_value<hstring>(message.Lookup(L"command"));
        {std::scoped_lock lock(m_mutex);
            if(!m_active||generation!=m_generation)throw hresult_canceled();
            if(command==L"ready"){response.Insert(L"size",box_value(m_size));}
            else if(command==L"result"){
                if(unbox_value<bool>(message.Lookup(L"ok"))){
                    Coordinates point{unbox_value<double>(message.Lookup(L"x")),unbox_value<double>(message.Lookup(L"y"))};
                    if(!std::isfinite(point.x)||!std::isfinite(point.y)||point.x<0||point.y<0||point.x>999.99||point.y>999.99)throw hresult_error(E_INVALIDARG,L"Invalid coordinates from companion");
                    m_output->coordinates=point;
                }
                if(message.HasKey(L"preview")){
                    com_array<uint8_t> bytes;message.Lookup(L"preview").as<Windows::Foundation::IPropertyValue>().GetUInt8Array(bytes);
                    if(bytes.size()!=64*48*4)throw hresult_error(E_INVALIDARG,L"Invalid preview");
                    m_output->preview={64,48,std::vector<uint8_t>(bytes.begin(),bytes.end())};
                }
                if(message.HasKey(L"error"))m_error=unbox_value<hstring>(message.Lookup(L"error"));
                done=true;response.Insert(L"received",box_value(true));
            }else throw hresult_error(E_INVALIDARG,L"Unknown command");
        }
        co_await args.Request().SendResponseAsync(response);
    }catch(...){std::scoped_lock lock(m_mutex);if(generation==m_generation){m_error=L"Companion communication failed";done=true;}}
    try{deferral.Complete();}catch(...){}
    if(done){std::scoped_lock lock(m_mutex);if(generation==m_generation)SetEvent(m_ready.get());}
}
void CompanionBridge::Stop(uint64_t generation) noexcept {
    AppServiceConnection connection{nullptr};Background::BackgroundTaskDeferral deferral{nullptr};
    {std::scoped_lock lock(m_mutex);if(generation&&generation!=m_generation)return;++m_generation;m_active=false;m_output.reset();connection=std::exchange(m_connection,nullptr);deferral=std::exchange(m_deferral,nullptr);SetEvent(m_ready.get());}
    try{if(connection)connection.Close();}catch(...){}try{if(deferral)deferral.Complete();}catch(...){}
}
}
