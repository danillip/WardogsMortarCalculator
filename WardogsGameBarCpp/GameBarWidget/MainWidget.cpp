#include "pch.h"
#include "MainWidget.h"
#include "MainWidget.g.cpp"
#include "App.h"
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <format>
#include <set>
using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::UI;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Media::Imaging;
using namespace Microsoft::Gaming::XboxGameBar;
using namespace Microsoft::Gaming::XboxGameBar::Input;
namespace winrt::WardogsGameBar::implementation {
namespace {SolidColorBrush Brush(uint8_t r,uint8_t g,uint8_t b){return SolidColorBrush(Color{255,r,g,b});}}
MainWidget::MainWidget() {
    InitializeComponent();m_settings=wardogs::LoadSettings();
    OpacitySlider().Value(m_settings.opacity*100);CaptureSlider().Value(m_settings.captureSize);PreviewCheck().IsChecked(m_settings.preview);
    KeyGun().Text(m_settings.hotkeys[0]);KeyTarget().Text(m_settings.hotkeys[1]);KeySetup().Text(m_settings.hotkeys[2]);KeyHide().Text(m_settings.hotkeys[3]);
    SetupButton().Click([this](auto const&,auto const&){ToggleSettings();});HideButton().Click([this](auto const&,auto const&){ToggleHidden();});
    GunButton().Click([this](auto const&,auto const&){StartScan(false);});TargetButton().Click([this](auto const&,auto const&){StartScan(true);});
    ApplyButton().Click([this](auto const&,auto const&){ApplyManual();});SaveKeysButton().Click([this](auto const&,auto const&){SaveKeys();});
    CloseButton().Click([this](auto const&,auto const&){auto widget=m_widget;Stop();if(widget)widget.Close();});
    RuButton().Click([this](auto const&,auto const&){m_settings.english=false;Localize();try{wardogs::SaveSettings(m_settings);}catch(...){Message(L"Не удалось сохранить настройки",L"Could not save settings",true);}});
    EnButton().Click([this](auto const&,auto const&){m_settings.english=true;Localize();try{wardogs::SaveSettings(m_settings);}catch(...){Message(L"Не удалось сохранить настройки",L"Could not save settings",true);}});
    OpacitySlider().ValueChanged([this](auto const&,auto const&){if(m_initializing)return;m_settings.opacity=OpacitySlider().Value()/100.;Opacity();try{wardogs::SaveSettings(m_settings);}catch(...){Message(L"Не удалось сохранить настройки",L"Could not save settings",true);}});
    CaptureSlider().ValueChanged([this](auto const&,auto const&){if(m_initializing)return;m_settings.captureSize=static_cast<int>(CaptureSlider().Value());Localize();try{wardogs::SaveSettings(m_settings);}catch(...){Message(L"Не удалось сохранить настройки",L"Could not save settings",true);}});
    auto previewChanged=[this](auto const&,auto const&){if(m_initializing)return;m_settings.preview=PreviewCheck().IsChecked().Value();PreviewBorder().Visibility(m_settings.preview?Visibility::Visible:Visibility::Collapsed);if(!m_settings.preview)Preview().Source(nullptr);try{wardogs::SaveSettings(m_settings);}catch(...){Message(L"Не удалось сохранить настройки",L"Could not save settings",true);}};
    PreviewCheck().Checked(previewChanged);PreviewCheck().Unchecked(previewChanged);
    m_initializing=false;Localize();
}
void MainWidget::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& args) {
    m_widget=args.Parameter().as<XboxGameBarWidget>();
    auto app=get_self<implementation::App>(Application::Current());m_bridge=app->Bridge();
    auto weak=get_weak();
    m_opacityToken=m_widget.RequestedOpacityChanged([weak](auto const&,auto const&){if(auto self=weak.get())self->Dispatcher().RunAsync(CoreDispatcherPriority::Normal,[weak]{if(auto page=weak.get())page->Opacity();});});
    m_visibleToken=m_widget.VisibleChanged([weak](auto const& widget,auto const&){if(widget.Visible())return;if(auto self=weak.get())self->Dispatcher().RunAsync(CoreDispatcherPriority::Normal,[weak]{if(auto page=weak.get()){++page->m_generation;if(page->m_scan)page->m_scan.Cancel();if(page->m_bridge)page->m_bridge->Stop();page->m_busy=false;}});});
    Opacity();RegisterHotkeys();
}
void MainWidget::Message(std::wstring_view ru,std::wstring_view en,bool error) {
    Status().Text(hstring(m_settings.english?en:ru));Status().Foreground(error?Brush(248,113,113):Brush(100,116,139));
}
void MainWidget::Localize() {
    auto choose=[&](wchar_t const* ru,wchar_t const* en){return hstring(m_settings.english?en:ru);};
    Mode().Text(m_edit?choose(L"⚙ НАСТРОЙКА",L"⚙ SETTINGS"):choose(L"● БОЕВОЙ",L"● COMBAT"));
    Mode().Foreground(m_edit?Brush(56,189,248):Brush(52,211,153));
    SetupButton().Content(box_value(hstring(m_settings.hotkeys[2])+choose(L" Настр.",L" Setup")));HideButton().Content(box_value(hstring(m_settings.hotkeys[3])+choose(L" Скрыть",L" Hide")));
    AzimuthTitle().Text(choose(L"АЗИМУТ",L"AZIMUTH"));ElevationTitle().Text(choose(L"ПРИЦЕЛ",L"ELEVATION"));DistanceTitle().Text(choose(L"ДИСТАНЦИЯ",L"DISTANCE"));
    GunButton().Content(box_value(choose(L"ОРУДИЕ ",L"MORTAR ")+hstring(m_settings.hotkeys[0])));TargetButton().Content(box_value(choose(L"ЦЕЛЬ ",L"TARGET ")+hstring(m_settings.hotkeys[1])));
    LanguageLabel().Text(choose(L"Язык:",L"Language:"));ApplyButton().Content(box_value(choose(L"Применить X/Y",L"Apply X/Y")));
    OpacityLabel().Text(choose(L"Непрозрачность",L"Opacity"));CaptureLabel().Text(choose(L"Область захвата: ",L"Capture area: ")+to_hstring(m_settings.captureSize)+L" px");
    PreviewCheck().Content(box_value(choose(L"Показывать мини-превью OCR",L"Show OCR mini-preview")));
    HotkeyLabel().Text(choose(L"Клавиши: орудие / цель / настройки / скрыть. Пример: CTRL+F1. F1–F12, A–Z, 0–9, NUM0–NUM9.",L"Hotkeys: mortar / target / setup / hide. Example: CTRL+F1. F1–F12, A–Z, 0–9, NUM0–NUM9."));
    SaveKeysButton().Content(box_value(choose(L"Сохранить клавиши",L"Save hotkeys")));
    CaptureHint().Text(choose(L"Наведите курсор на X/Y в игре и нажмите F1/F2. Pin и сквозной режим включаются в Game Bar. Ручной ввод — резервный способ.",L"Hover over in-game X/Y and press F1/F2. Enable Pin and click-through in Game Bar. Manual input is a fallback."));
    CloseButton().Content(box_value(choose(L"Закрыть виджет",L"Close widget")));
    PreviewBorder().Visibility(m_settings.preview?Visibility::Visible:Visibility::Collapsed);
    Message(L"Наведите курсор на X/Y: орудие / цель",L"Hover over X/Y: mortar / target");Recalculate();
}
void MainWidget::Recalculate() {
    if(!m_gun||!m_target)return;
    auto r=wardogs::CalculateBallistics(*m_gun,*m_target);
    Azimuth().Text(std::format(L"{:05.1f}°",r.azimuth_deg));Distance().Text(std::format(L"{:.0f} {}",r.distance_m,m_settings.english?L"m":L"м"));
    Elevation().Text(r.elevation_mil?std::format(L"{:.0f} mil",*r.elevation_mil):(r.range_status==wardogs::RangeStatus::TooClose?L"<80!":L">684!"));
    Elevation().Foreground(r.elevation_mil?Brush(251,191,36):Brush(248,113,113));
}
void MainWidget::ApplyManual() {
    auto gx=wardogs::ParseManualCoordinate(GunX().Text().c_str()),gy=wardogs::ParseManualCoordinate(GunY().Text().c_str());
    auto tx=wardogs::ParseManualCoordinate(TargetX().Text().c_str()),ty=wardogs::ParseManualCoordinate(TargetY().Text().c_str());
    bool gunEntered=!GunX().Text().empty()||!GunY().Text().empty(),targetEntered=!TargetX().Text().empty()||!TargetY().Text().empty();
    if((gunEntered&&(!gx||!gy))||(targetEntered&&(!tx||!ty))){Message(L"Введите обе координаты X/Y от 0 до 999.99",L"Enter both X/Y coordinates from 0 to 999.99",true);return;}
    if(gx&&gy)m_gun=wardogs::Coordinates{*gx,*gy};if(tx&&ty)m_target=wardogs::Coordinates{*tx,*ty};Recalculate();
}
void MainWidget::Opacity() {if(!m_stopped)Surface().Opacity(m_settings.opacity*(m_widget?std::clamp(m_widget.RequestedOpacity()/100.,0.,1.):1.));}
fire_and_forget MainWidget::ToggleSettings() {
    auto lifetime=get_strong();if(m_stopped)return;
    try{if(m_hidden){co_await m_widget.RestoreAsync();m_hidden=false;}if(m_edit)ApplyManual();m_edit=!m_edit;
        SettingsPanel().Visibility(m_edit?Visibility::Visible:Visibility::Collapsed);
        for(auto box:{GunX(),GunY(),TargetX(),TargetY()})box.IsReadOnly(!m_edit);
        Localize();co_await m_widget.TryResizeWindowAsync(Size{400.f,m_edit?610.f:250.f});
    }catch(...){Message(L"Не удалось изменить размер. Используйте прокрутку",L"Could not resize. Use scrolling",true);}
}
fire_and_forget MainWidget::ToggleHidden() {
    auto lifetime=get_strong();if(m_stopped)return;
    try{if(m_hidden)co_await m_widget.RestoreAsync();else co_await m_widget.MinimizeAsync();m_hidden=!m_hidden;}
    catch(...){Message(L"Game Bar не изменил видимость",L"Game Bar could not change visibility",true);}
}
void MainWidget::StartScan(bool target) {
    if(m_busy||m_stopped)return;m_scan=ScanAsync(target);
}
IAsyncAction MainWidget::ScanAsync(bool target) {
    auto lifetime=get_strong();auto cancellation=co_await get_cancellation_token();cancellation.enable_propagation();
    m_busy=true;unsigned generation=m_generation;
    Message(target?L"Сканирование ЦЕЛИ…":L"Сканирование ОРУДИЯ…",target?L"Scanning TARGET…":L"Scanning MORTAR…");
    try{
        auto output=std::make_shared<wardogs::ScanOutput>();co_await m_bridge->ScanAsync(m_settings.captureSize,output);
        if(m_stopped||generation!=m_generation)co_return;
        if(m_settings.preview&&!output->preview.bgra.empty()){
            SoftwareBitmapSource source;co_await source.SetBitmapAsync(wardogs::ToBitmap(output->preview));
            if(m_stopped||generation!=m_generation)co_return;Preview().Source(source);
        }
        if(output->coordinates){auto p=*output->coordinates;
            if(target){m_target=p;TargetX().Text(std::format(L"{:.2f}",p.x));TargetY().Text(std::format(L"{:.2f}",p.y));}
            else{m_gun=p;GunX().Text(std::format(L"{:.2f}",p.x));GunY().Text(std::format(L"{:.2f}",p.y));}
            Recalculate();Message(L"Координаты считаны",L"Coordinates captured");
        }else Message(L"Не распознано X/Y. Наведите курсор точнее",L"Could not read X/Y. Align the cursor more clearly",true);
    }catch(hresult_canceled const&){}
    catch(hresult_error const& error){if(!m_stopped&&generation==m_generation){Message(L"Ошибка захвата/OCR. Доступен ручной ввод. ",L"Capture/OCR failed. Manual input is available. ",true);Status().Text(Status().Text()+error.message());}}
    catch(...){if(!m_stopped)Message(L"Ошибка OCR",L"OCR failed",true);}
    if(generation==m_generation)m_busy=false;
}
void MainWidget::ClearHotkeys() noexcept {
    for(size_t i=0;i<m_watchers.size();++i){try{if(m_watchers[i]){m_watchers[i].Stop();m_watchers[i].HotkeySetStateChanged(m_hotkeyTokens[i]);}}catch(...){}m_watchers[i]=nullptr;}
}
void MainWidget::RegisterHotkeys() {
    ClearHotkeys();auto weak=get_weak();
    for(int i=0;i<4;++i)try{
        auto keys=single_threaded_vector<Windows::System::VirtualKey>(wardogs::ParseHotkey(m_settings.hotkeys[i]));
        m_watchers[i]=XboxGameBarHotkeyWatcher::CreateWatcher(m_widget,keys.GetView());
        m_hotkeyTokens[i]=m_watchers[i].HotkeySetStateChanged([weak,i](auto const&,HotkeySetStateChangedArgs const& args){if(!args.HotkeySetDown)return;if(auto self=weak.get())self->Dispatcher().RunAsync(CoreDispatcherPriority::Normal,[weak,i]{if(auto page=weak.get()){if(page->m_stopped)return;if(i<2)page->StartScan(i==1);else if(i==2)page->ToggleSettings();else page->ToggleHidden();}});});
        m_watchers[i].Start();
    }catch(...){Message(L"Game Bar отклонил клавишу. Измените сочетание или используйте кнопки",L"Game Bar rejected a hotkey. Rebind it or use the buttons",true);}
}
void MainWidget::SaveKeys() {
    std::array<std::wstring,4> keys{KeyGun().Text().c_str(),KeyTarget().Text().c_str(),KeySetup().Text().c_str(),KeyHide().Text().c_str()};
    try{std::set<std::vector<Windows::System::VirtualKey>> used;for(auto const& key:keys)if(!used.insert(wardogs::ParseHotkey(key)).second)throw std::invalid_argument("Duplicate key");m_settings.hotkeys=keys;wardogs::SaveSettings(m_settings);Localize();RegisterHotkeys();}
    catch(...){Message(L"Неверные или повторяющиеся клавиши. Пример: CTRL+F1",L"Invalid or duplicate hotkeys. Example: CTRL+F1",true);}
}
void MainWidget::Stop() noexcept {
    if(m_stopped)return;m_stopped=true;++m_generation;ClearHotkeys();
    try{if(m_scan)m_scan.Cancel();}catch(...){}if(m_bridge)m_bridge->Stop();
    try{if(m_widget)m_widget.RequestedOpacityChanged(m_opacityToken);}catch(...){}
    try{if(m_widget)m_widget.VisibleChanged(m_visibleToken);}catch(...){}
    try{Preview().Source(nullptr);GunX().Text(L"");GunY().Text(L"");TargetX().Text(L"");TargetY().Text(L"");}catch(...){}
    m_gun.reset();m_target.reset();m_widget=nullptr;m_busy=false;
}
}
