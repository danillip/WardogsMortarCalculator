#pragma once
#include "MainWidget.g.h"
#include "../Settings/SettingsManager.h"
#include "CompanionBridge.h"
#include <winrt/Microsoft.Gaming.XboxGameBar.h>
#include <winrt/Microsoft.Gaming.XboxGameBar.Input.h>
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.UI.Xaml.Navigation.h>
namespace winrt::WardogsGameBar::implementation {
struct MainWidget : MainWidgetT<MainWidget> {
    MainWidget();
    void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& args);
    void Stop() noexcept;
private:
    void Localize();
    void Recalculate();
    void ApplyManual();
    void RegisterHotkeys();
    void ClearHotkeys() noexcept;
    void SaveKeys();
    void StartScan(bool target);
    Windows::Foundation::IAsyncAction ScanAsync(bool target);
    fire_and_forget ToggleSettings();
    fire_and_forget ToggleHidden();
    void Opacity();
    void Message(std::wstring_view ru,std::wstring_view en,bool error=false);
    Microsoft::Gaming::XboxGameBar::XboxGameBarWidget m_widget{nullptr};
    std::array<Microsoft::Gaming::XboxGameBar::Input::XboxGameBarHotkeyWatcher,4> m_watchers{{nullptr,nullptr,nullptr,nullptr}};
    std::array<event_token,4> m_hotkeyTokens{};
    event_token m_opacityToken{};
    event_token m_visibleToken{};
    wardogs::Settings m_settings;
    std::shared_ptr<wardogs::CompanionBridge> m_bridge;
    Windows::Foundation::IAsyncAction m_scan{nullptr};
    std::optional<wardogs::Coordinates> m_gun,m_target;
    bool m_edit=false,m_hidden=false,m_busy=false,m_stopped=false,m_initializing=true;
    unsigned m_generation=0;
};
}
namespace winrt::WardogsGameBar::factory_implementation {struct MainWidget : MainWidgetT<MainWidget,implementation::MainWidget>{};}
