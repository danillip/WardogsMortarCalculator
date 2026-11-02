#pragma once
#include "../OCR/CoordinateRecognizer.h"
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.AppService.h>
#include <winrt/Windows.ApplicationModel.Background.h>
#include <mutex>
namespace wardogs {
class CompanionBridge : public std::enable_shared_from_this<CompanionBridge> {
public:
    winrt::Windows::Foundation::IAsyncAction ScanAsync(int size,std::shared_ptr<ScanOutput> output);
    void Accept(winrt::Windows::ApplicationModel::Activation::BackgroundActivatedEventArgs const& args);
    void Stop(uint64_t generation=0) noexcept;
private:
    winrt::fire_and_forget Request(winrt::Windows::ApplicationModel::AppService::AppServiceRequestReceivedEventArgs args,uint64_t generation);
    std::mutex m_mutex;
    bool m_active=false;
    uint64_t m_generation=0;
    int m_size=320;
    std::wstring m_error;
    std::shared_ptr<ScanOutput> m_output;
    winrt::handle m_ready{CreateEventExW(nullptr,nullptr,CREATE_EVENT_MANUAL_RESET,EVENT_ALL_ACCESS)};
    winrt::Windows::ApplicationModel::AppService::AppServiceConnection m_connection{nullptr};
    winrt::Windows::ApplicationModel::Background::BackgroundTaskDeferral m_deferral{nullptr};
};
}
