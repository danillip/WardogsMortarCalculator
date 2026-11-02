#pragma once
#include "App.xaml.g.h"
#include "CompanionBridge.h"
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Microsoft.Gaming.XboxGameBar.h>
namespace winrt::WardogsGameBar::implementation {
struct App : AppT<App> {
    App();
    void OnActivated(Windows::ApplicationModel::Activation::IActivatedEventArgs const& args);
    void OnLaunched(Windows::ApplicationModel::Activation::LaunchActivatedEventArgs const& args);
    void OnBackgroundActivated(Windows::ApplicationModel::Activation::BackgroundActivatedEventArgs const& args);
    std::shared_ptr<wardogs::CompanionBridge> Bridge() const {return m_bridge;}
    void Cleanup() noexcept;
private:
    Microsoft::Gaming::XboxGameBar::XboxGameBarWidget m_widget{nullptr};
    Windows::UI::Xaml::Controls::Frame m_frame{nullptr};
    Windows::UI::Xaml::Window m_window{nullptr};
    event_token m_closed{};
    std::shared_ptr<wardogs::CompanionBridge> m_bridge=std::make_shared<wardogs::CompanionBridge>();
};
}
