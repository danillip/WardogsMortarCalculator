#include "pch.h"
#include "App.h"
#include "MainWidget.h"
#include <winrt/Windows.UI.Xaml.Controls.h>
using namespace winrt;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Microsoft::Gaming::XboxGameBar;
namespace winrt::WardogsGameBar::implementation {
App::App(){InitializeComponent();Suspending([this](auto const&,auto const&){Cleanup();});}
void App::OnActivated(IActivatedEventArgs const& args) {
    if(args.Kind()!=ActivationKind::Protocol)return;
    auto protocol=args.try_as<IProtocolActivatedEventArgs>();
    if(!protocol||protocol.Uri().SchemeName()!=L"ms-gamebarwidget")return;
    auto activation=args.try_as<XboxGameBarWidgetActivatedEventArgs>();
    if(!activation||!activation.IsLaunchActivation())return;
    Cleanup();m_window=Window::Current();m_frame=Frame();m_window.Content(m_frame);
    m_widget=XboxGameBarWidget(activation,m_window.CoreWindow(),m_frame);
    m_frame.Navigate(xaml_typename<WardogsGameBar::MainWidget>(),m_widget);
    m_closed=m_window.Closed([this](auto const&,auto const&){Cleanup();});m_window.Activate();
}
void App::OnBackgroundActivated(BackgroundActivatedEventArgs const& args){m_bridge->Accept(args);}
void App::OnLaunched(LaunchActivatedEventArgs const& args) {
    if(args.PrelaunchActivated())return;
    // F5 launches the packaged app. Actual overlay activation belongs to Game Bar.
    auto text=TextBlock();text.Text(L"WARDOGS Mortar Calculator\nWin + G → Widgets → WARDOGS Mortar Calculator");text.TextWrapping(TextWrapping::Wrap);text.Margin(Thickness{24});
    Window::Current().Content(text);Window::Current().Activate();
}
void App::Cleanup() noexcept {
    try{if(m_frame){auto page=m_frame.Content().try_as<WardogsGameBar::MainWidget>();if(page)get_self<implementation::MainWidget>(page)->Stop();}}catch(...){}
    m_bridge->Stop();
    try{if(m_window)m_window.Closed(m_closed);}catch(...){}
    m_widget=nullptr;m_frame=nullptr;m_window=nullptr;
}
}
