#include "OverlayWindow.h"
#include "Resources.h"
#include "../Capture/CaptureService.h"
#include <windowsx.h>
#include <wincodec.h>
#include <format>
#include <set>
#include <cwchar>
#include <stdexcept>

using namespace winrt;
namespace wardogs {
namespace {
constexpr UINT ScanDone=WM_APP+1;
constexpr int Width=400,AimHeight=165,CombatHeight=220+AimHeight,SettingsHeight=515+AimHeight;
constexpr uint32_t Background=0x0b0f19,Panel=0x111827,Muted=0x64748b,Gray=0x94a3b8,White=0xf8fafc,Blue=0x38bdf8,Green=0x34d399,Yellow=0xfbbf24,Red=0xf87171;
D2D1_RECT_F Rect(float x,float y,float width,float height){return D2D1::RectF(x,y,x+width,y+height);}
bool Contains(D2D1_RECT_F r,float x,float y){return x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom;}
D2D1_RECT_F FieldRect(int index){return Rect(index%2?202.f:108.f,index<2?111.f:142.f,86,25);}
D2D1_RECT_F KeyRect(int index){return Rect(index%2?265.f:78.f,index<2?401.f:435.f,110,25);}
struct Hotkey {UINT modifiers=MOD_NOREPEAT,key=0;};
Hotkey NativeHotkey(std::wstring const& text) {
    Hotkey hotkey;
    for(auto key:ParseHotkey(text)) {
        auto value=static_cast<UINT>(key);
        if(value==VK_CONTROL)hotkey.modifiers|=MOD_CONTROL;
        else if(value==VK_SHIFT)hotkey.modifiers|=MOD_SHIFT;
        else if(value==VK_MENU)hotkey.modifiers|=MOD_ALT;
        else {if(hotkey.key||value==VK_F12)throw std::invalid_argument("Unsupported or reserved hotkey");hotkey.key=value;}
    }
    if(!hotkey.key)throw std::invalid_argument("Missing main key");return hotkey;
}
}
struct OverlayWindow::Surface {
    HDC dc{};HBITMAP bitmap{};HGDIOBJ old{};int width{},height{};
    Surface(int w,int h):width(w),height(h) {
        dc=CreateCompatibleDC(nullptr);if(!dc)throw_last_error();
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        void* pixels{};bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
        if(!bitmap){DeleteDC(dc);dc=nullptr;throw_last_error();}
        old=SelectObject(dc,bitmap);
        if(!old||old==HGDI_ERROR){DeleteObject(bitmap);DeleteDC(dc);dc=nullptr;bitmap=nullptr;throw_last_error();}
    }
    ~Surface(){if(old&&old!=HGDI_ERROR)SelectObject(dc,old);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);}
};
OverlayWindow::OverlayWindow():m_settings(m_store.Load()) {
    SetStatus(L"Наведите курсор на X/Y: F1 — орудие, F2 — цель",L"Hover over X/Y: F1 — mortar, F2 — target");
}
OverlayWindow::~OverlayWindow(){m_closing=true;m_aim.Shutdown();StopScan();UnregisterKeys();if(m_window&&IsWindow(m_window)){SetWindowLongPtrW(m_window,GWLP_USERDATA,0);DestroyWindow(m_window);}m_window=nullptr;m_renderTarget=nullptr;m_surface.reset();}
std::wstring OverlayWindow::Tr(wchar_t const* ru,wchar_t const* en)const{return m_settings.common.english?en:ru;}
void OverlayWindow::SetStatus(std::wstring ru,std::wstring en,bool error) {
    m_statusRu=std::move(ru);m_statusEn=std::move(en);m_statusError=error;
    if(m_window)InvalidateRect(m_window,nullptr,FALSE);
}
int OverlayWindow::Run(HINSTANCE instance,int) {
    WNDCLASSEXW klass{sizeof(klass)};klass.lpfnWndProc=WindowProc;klass.hInstance=instance;klass.hCursor=LoadCursorW(nullptr,IDC_ARROW);klass.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(IDI_WARDOGS));klass.hIconSm=klass.hIcon;klass.lpszClassName=L"WardogsPortableOverlay";
    if(!RegisterClassExW(&klass))throw_last_error();
    m_window=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW,klass.lpszClassName,L"WARDOGS Mortar Calculator",WS_POPUP,m_settings.x,m_settings.y,Width,CombatHeight,nullptr,nullptr,instance,this);
    if(!m_window)throw_last_error();
    m_scale=GetDpiForWindow(m_window)/96.f;InitializeGraphics();LoadLogo();m_aim.Initialize(m_window);
    if(!RegisterKeys())m_edit=true; // Keep settings reachable if F3 cannot register.
    Resize();ClampPosition();ApplyWindowStyle();ShowWindow(m_window,m_edit?SW_SHOW:SW_SHOWNOACTIVATE);Paint();
    MSG message{};BOOL status;
    while((status=GetMessageW(&message,nullptr,0,0))>0){TranslateMessage(&message);DispatchMessageW(&message);}
    if(status<0)throw_last_error();return static_cast<int>(message.wParam);
}
void OverlayWindow::InitializeGraphics() {
    check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,m_factory.put()));
    check_hresult(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(m_writeFactory.put())));
    auto format=[&](wchar_t const* family,float size,DWRITE_FONT_WEIGHT weight,com_ptr<IDWriteTextFormat>& output){check_hresult(m_writeFactory->CreateTextFormat(family,nullptr,weight,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"ru-RU",output.put()));output->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);};
    format(L"Segoe UI",11.5f,DWRITE_FONT_WEIGHT_NORMAL,m_labelFormat);format(L"Segoe UI",10.f,DWRITE_FONT_WEIGHT_NORMAL,m_smallFormat);
    format(L"Consolas",23.f,DWRITE_FONT_WEIGHT_BOLD,m_valueFormat);format(L"Consolas",12.f,DWRITE_FONT_WEIGHT_BOLD,m_fieldFormat);
}
void OverlayWindow::LoadLogo() {
    auto module=GetModuleHandleW(nullptr);auto resource=FindResourceW(module,MAKEINTRESOURCEW(IDR_LOGO),RT_RCDATA);
    if(!resource)return;auto data=LoadResource(module,resource);auto bytes=static_cast<BYTE*>(LockResource(data));auto size=SizeofResource(module,resource);
    if(!bytes||!size)return;
    com_ptr<IWICImagingFactory> factory;check_hresult(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.put())));
    com_ptr<IWICStream> stream;check_hresult(factory->CreateStream(stream.put()));check_hresult(stream->InitializeFromMemory(bytes,size));
    com_ptr<IWICBitmapDecoder> decoder;check_hresult(factory->CreateDecoderFromStream(stream.get(),nullptr,WICDecodeMetadataCacheOnLoad,decoder.put()));
    com_ptr<IWICBitmapFrameDecode> frame;check_hresult(decoder->GetFrame(0,frame.put()));
    com_ptr<IWICFormatConverter> converter;check_hresult(factory->CreateFormatConverter(converter.put()));check_hresult(converter->Initialize(frame.get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
    UINT width{},height{};check_hresult(converter->GetSize(&width,&height));if(width>4096||height>4096)throw hresult_error(E_INVALIDARG);
    m_logo={static_cast<int>(width),static_cast<int>(height),std::vector<uint8_t>(static_cast<size_t>(width)*height*4)};
    check_hresult(converter->CopyPixels(nullptr,width*4,static_cast<UINT>(m_logo.bgra.size()),m_logo.bgra.data()));
}
void OverlayWindow::Text(std::wstring_view text,D2D1_RECT_F rect,uint32_t color,IDWriteTextFormat* format) {
    if(text.empty())return;com_ptr<IDWriteTextLayout> layout;com_ptr<ID2D1SolidColorBrush> brush;
    check_hresult(m_writeFactory->CreateTextLayout(text.data(),static_cast<UINT32>(text.size()),format?format:m_labelFormat.get(),rect.right-rect.left,rect.bottom-rect.top,layout.put()));
    check_hresult(m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(color),brush.put()));
    m_renderTarget->PushAxisAlignedClip(rect,D2D1_ANTIALIAS_MODE_ALIASED);m_renderTarget->DrawTextLayout(D2D1::Point2F(rect.left,rect.top),layout.get(),brush.get());m_renderTarget->PopAxisAlignedClip();
}
void OverlayWindow::Fill(D2D1_RECT_F rect,uint32_t color){com_ptr<ID2D1SolidColorBrush> brush;check_hresult(m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(color),brush.put()));m_renderTarget->FillRectangle(rect,brush.get());}
void OverlayWindow::Outline(D2D1_RECT_F rect,uint32_t color){com_ptr<ID2D1SolidColorBrush> brush;check_hresult(m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(color),brush.put()));m_renderTarget->DrawRectangle(rect,brush.get());}
void OverlayWindow::Button(std::wstring_view text,D2D1_RECT_F rect,bool selected){Fill(rect,selected?0x334155:0x1e293b);Text(text,Rect(rect.left+5,rect.top+4,rect.right-rect.left-10,rect.bottom-rect.top-4),selected?Yellow:Blue);}
void OverlayWindow::DrawImage(Image const& image,D2D1_RECT_F rect) {
    if(image.bgra.empty())return;com_ptr<ID2D1Bitmap> bitmap;
    auto properties=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
    check_hresult(m_renderTarget->CreateBitmap(D2D1::SizeU(image.width,image.height),image.bgra.data(),image.width*4,properties,bitmap.put()));
    m_renderTarget->DrawBitmap(bitmap.get(),rect);
}
void OverlayWindow::Paint() {
    if(!m_window||m_hidden||m_closing)return;
    RECT client{};GetClientRect(m_window,&client);int width=client.right,height=client.bottom;if(width<=0||height<=0)return;
    if(!m_surface||m_surface->width!=width||m_surface->height!=height){m_renderTarget=nullptr;m_surface=std::make_unique<Surface>(width,height);}
    if(!m_renderTarget){auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));check_hresult(m_factory->CreateDCRenderTarget(&properties,m_renderTarget.put()));}
    check_hresult(m_renderTarget->BindDC(m_surface->dc,&client));m_renderTarget->BeginDraw();m_renderTarget->SetDpi(96,96);m_renderTarget->SetTransform(D2D1::Matrix3x2F::Scale(m_scale,m_scale));m_renderTarget->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);m_renderTarget->Clear(D2D1::ColorF(Background));
    Outline(Rect(.5f,.5f,399,static_cast<float>(m_edit?SettingsHeight:CombatHeight)-1),m_edit?Blue:0x334155);
    DrawImage(m_logo,Rect(10,12,16,16));Text(m_edit?Tr(L"⚙ НАСТРОЙКА",L"⚙ SETTINGS"):Tr(L"● БОЕВОЙ",L"● COMBAT"),Rect(34,12,190,20),m_edit?Blue:Green);
    Text(L"["+m_settings.common.hotkeys[2]+L"] "+Tr(L"Настр.",L"Setup"),Rect(238,12,64,20),Gray,m_smallFormat.get());Text(L"["+m_settings.common.hotkeys[3]+L"] "+Tr(L"Скрыть",L"Hide"),Rect(305,12,60,20),Gray,m_smallFormat.get());
    if(m_edit)Button(L"×",Rect(370,8,20,25));
    Fill(Rect(10,40,380,60),Panel);Outline(Rect(10,40,380,60),0x1e293b);
    Text(Tr(L"АЗИМУТ",L"AZIMUTH"),Rect(20,47,110,15),Muted,m_smallFormat.get());Text(Tr(L"ПРИЦЕЛ",L"ELEVATION"),Rect(147,47,110,15),Muted,m_smallFormat.get());Text(Tr(L"ДИСТАНЦИЯ",L"DISTANCE"),Rect(273,47,110,15),Muted,m_smallFormat.get());
    Text(m_azimuth,Rect(20,66,119,30),Blue,m_valueFormat.get());Text(m_elevation,Rect(147,66,122,30),m_rangeError?Red:Yellow,m_valueFormat.get());Text(m_distance,Rect(273,66,114,30),White,m_valueFormat.get());
    Text(Tr(L"ОРУДИЕ",L"MORTAR")+L" ["+m_settings.common.hotkeys[0]+L"]",Rect(12,117,94,20),Gray,m_smallFormat.get());Text(Tr(L"ЦЕЛЬ",L"TARGET")+L" ["+m_settings.common.hotkeys[1]+L"]",Rect(12,148,94,20),Gray,m_smallFormat.get());
    for(int i=0;i<4;++i){
        auto rect=FieldRect(i);Fill(rect,Panel);Outline(rect,m_activeField==i?Blue:0x1e293b);auto value=m_fields[i].empty()?L"---":m_fields[i];Text(std::wstring(i%2?L"Y: ":L"X: ")+value,Rect(rect.left+4,rect.top+5,78,19),m_activeField==i&&m_selectAll?Yellow:White,m_fieldFormat.get());
        if(m_activeField==i&&!m_selectAll){
            std::wstring prefix=std::wstring(i%2?L"Y: ":L"X: ")+m_fields[i].substr(0,m_caret);com_ptr<IDWriteTextLayout> layout;
            check_hresult(m_writeFactory->CreateTextLayout(prefix.c_str(),static_cast<UINT32>(prefix.size()),m_fieldFormat.get(),300,25,layout.put()));DWRITE_TEXT_METRICS metrics{};check_hresult(layout->GetMetrics(&metrics));
            Fill(Rect(std::min(rect.right-2,rect.left+4+metrics.widthIncludingTrailingWhitespace),rect.top+5,1,15),Blue);
        }
    }
    if(m_settings.common.preview){Fill(Rect(310,111,64,48),Panel);Text(L"OCR",Rect(326,125,40,18),Muted,m_smallFormat.get());DrawImage(m_preview,Rect(310,111,64,48));}
    Text(m_settings.common.english?m_statusEn:m_statusRu,Rect(12,177,376,34),m_statusError?Red:Muted,m_smallFormat.get());
    Fill(Rect(10,215,380,162),Panel);Outline(Rect(10,215,380,162),0x1e293b);
    Text(Tr(L"ПОДСКАЗКА ПРИЦЕЛА",L"SIGHT RANGE HINT"),Rect(20,222,355,18),m_aim.Tracking()?Green:Blue,m_smallFormat.get());
    Fill(Rect(20,245,76,122),Background);
    if(m_settings.common.preview){
        DrawImage(m_aim.Preview(),Rect(20,245,76,122));
        if(auto mark=m_aim.PreviewMark())Fill(Rect(20,245+static_cast<float>(*mark)*122,76,2),Blue);
    }
    Text(m_aim.Status(m_settings.common.english),Rect(108,245,267,66),Gray);
    Text(L"F5",Rect(108,320,76,25),Blue);Text(L"F6",Rect(190,320,76,25),m_aim.Tracking()?Green:Blue);Text(L"F7",Rect(272,320,76,25),Blue);
    Text(Tr(L"Проверка / сопровождение / убрать",L"Check / track / clear"),Rect(108,351,270,23),Muted,m_smallFormat.get());
    if(m_edit){
        m_renderTarget->SetTransform(D2D1::Matrix3x2F::Translation(0,AimHeight)*D2D1::Matrix3x2F::Scale(m_scale,m_scale));
        Fill(Rect(10,223,380,280),Panel);
        Button(L"RU",Rect(20,232,40,25),!m_settings.common.english);Button(L"EN",Rect(65,232,40,25),m_settings.common.english);Button(Tr(L"Применить X/Y",L"Apply X/Y"),Rect(235,232,145,25));
        Text(Tr(L"Непрозрачность",L"Opacity"),Rect(20,267,115,20),Gray);Fill(Rect(143,274,180,4),0x0b0f19);Fill(Rect(143+static_cast<float>((m_settings.common.opacity-.2)/.8)*180-3,266,6,20),Blue);Text(std::format(L"{:.0f}%",m_settings.common.opacity*100),Rect(333,267,48,20),Blue);
        Text(Tr(L"Область захвата",L"Capture area"),Rect(20,299,115,20),Gray);Fill(Rect(143,306,180,4),0x0b0f19);Fill(Rect(143+(m_settings.common.captureSize-160)/480.f*180-3,298,6,20),Blue);Text(std::to_wstring(m_settings.common.captureSize)+L"px",Rect(333,299,50,20),Blue);
        Text(std::wstring(m_settings.common.preview?L"☑ ":L"☐ ")+Tr(L"Мини-превью OCR",L"OCR mini-preview"),Rect(20,330,350,21),Gray);
        Text(std::wstring(m_settings.topmost?L"☑ ":L"☐ ")+Tr(L"Поверх окон",L"Always on top"),Rect(20,355,165,21),Gray);Text(std::wstring(m_settings.clickThrough?L"☑ ":L"☐ ")+Tr(L"Сквозной режим",L"Click-through"),Rect(205,355,180,21),Gray);
        Text(Tr(L"Клавиши: нажмите ячейку, затем сочетание",L"Hotkeys: click a cell, then press the combination"),Rect(20,381,360,18),Muted,m_smallFormat.get());
        for(int i=0;i<4;++i){auto rect=KeyRect(i);std::wstring label=i==0?Tr(L"Орудие",L"Mortar"):i==1?Tr(L"Цель",L"Target"):i==2?Tr(L"Настр.",L"Setup"):Tr(L"Скрыть",L"Hide");Text(label,Rect(rect.left-58,rect.top+5,56,20),Gray,m_smallFormat.get());Button(m_rebinding==i?L"… Esc":m_settings.common.hotkeys[i],rect,m_rebinding==i);}
        Text(Tr(L"Перетаскивайте заголовок. F3 — вернуться в HUD.",L"Drag the header. F3 returns to the HUD."),Rect(20,474,360,24),Muted,m_smallFormat.get());
        m_renderTarget->SetTransform(D2D1::Matrix3x2F::Scale(m_scale,m_scale));
    }
    auto result=m_renderTarget->EndDraw();if(result==D2DERR_RECREATE_TARGET){m_renderTarget=nullptr;InvalidateRect(m_window,nullptr,FALSE);return;}check_hresult(result);
    RECT window{};GetWindowRect(m_window,&window);POINT destination{window.left,window.top},source{};SIZE size{width,height};BLENDFUNCTION blend{AC_SRC_OVER,0,static_cast<BYTE>(std::lround(m_settings.common.opacity*255)),AC_SRC_ALPHA};
    if(!UpdateLayeredWindow(m_window,nullptr,&destination,&size,m_surface->dc,&source,0,&blend,ULW_ALPHA))throw_last_error();
}
void OverlayWindow::Save() {
    try{RECT window{};GetWindowRect(m_window,&window);m_settings.x=window.left;m_settings.y=window.top;m_store.Save(m_settings);}
    catch(...){SetStatus(L"Не удалось сохранить настройки",L"Could not save settings",true);}
}
void OverlayWindow::Resize(){
    m_scale=GetDpiForWindow(m_window)/96.f;
    MONITORINFO monitor{sizeof(monitor)};
    if(GetMonitorInfoW(MonitorFromWindow(m_window,MONITOR_DEFAULTTONEAREST),&monitor))
        m_scale=std::min(m_scale,std::max(.5f,(monitor.rcWork.bottom-monitor.rcWork.top-20.f)/SettingsHeight));
    SetWindowPos(m_window,nullptr,0,0,static_cast<int>(Width*m_scale),static_cast<int>((m_edit?SettingsHeight:CombatHeight)*m_scale),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);InvalidateRect(m_window,nullptr,FALSE);
}
void OverlayWindow::ClampPosition() {
    RECT window{};GetWindowRect(m_window,&window);MONITORINFO info{sizeof(info)};
    if(GetMonitorInfoW(MonitorFromRect(&window,MONITOR_DEFAULTTONEAREST),&info)){int x=std::clamp(window.left,info.rcWork.left,std::max(info.rcWork.left,info.rcWork.right-(window.right-window.left)));int y=std::clamp(window.top,info.rcWork.top,std::max(info.rcWork.top,info.rcWork.bottom-(window.bottom-window.top)));SetWindowPos(m_window,nullptr,x,y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);}
}
void OverlayWindow::ApplyWindowStyle() {
    auto style=GetWindowLongPtrW(m_window,GWL_EXSTYLE);style&=~static_cast<LONG_PTR>(WS_EX_TRANSPARENT|WS_EX_NOACTIVATE);
    if(!m_edit){style|=WS_EX_NOACTIVATE;if(m_settings.clickThrough)style|=WS_EX_TRANSPARENT;}
    SetWindowLongPtrW(m_window,GWL_EXSTYLE,style);
    SetWindowPos(m_window,m_settings.topmost?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_FRAMECHANGED);
}
void OverlayWindow::ToggleEdit() {
    m_aim.Clear();
    if(m_rebinding>=0){m_rebinding=-1;RegisterKeys();}
    if(m_hidden){m_hidden=false;ShowWindow(m_window,SW_SHOWNOACTIVATE);}
    if(m_edit)ApplyManual();m_edit=!m_edit;m_activeField=-1;m_selectAll=false;Resize();ClampPosition();ApplyWindowStyle();
    if(m_edit){SetForegroundWindow(m_window);SetFocus(m_window);}Save();InvalidateRect(m_window,nullptr,FALSE);
}
void OverlayWindow::ToggleHidden() {
    if(!m_registered[3]){SetStatus(L"Назначьте свободную клавишу возврата HUD",L"Bind an available HUD restore hotkey first",true);return;}
    m_aim.Clear();m_hidden=!m_hidden;ShowWindow(m_window,m_hidden?SW_HIDE:SW_SHOWNOACTIVATE);if(!m_hidden)InvalidateRect(m_window,nullptr,FALSE);
}
void OverlayWindow::UnregisterKeys() noexcept {if(!m_window)return;for(int i=0;i<7;++i){UnregisterHotKey(m_window,1+i);m_registered[i]=false;}}
bool OverlayWindow::RegisterKeys() {
    UnregisterKeys();bool success=true;
    // Reserve sight actions before registering the configurable coordinate keys.
    for(int i=4;i<7;++i){m_registered[i]=RegisterHotKey(m_window,i+1,MOD_NOREPEAT,VK_F5+i-4)!=FALSE;if(!m_registered[i])success=false;}
    for(int i=0;i<4;++i){try{auto key=NativeHotkey(m_settings.common.hotkeys[i]);m_registered[i]=RegisterHotKey(m_window,i+1,key.modifiers,key.key)!=FALSE;}catch(...){m_registered[i]=false;}if(!m_registered[i])success=false;}
    if(!success)SetStatus(L"Конфликт клавиш. F5–F7 нужны прицелу, F12 — Windows. Освободите клавиши или измените сочетания.",L"Hotkey conflict. F5–F7 are for sight hints; F12 is reserved. Free these keys or rebind shortcuts.",true);
    return success;
}
void OverlayWindow::ApplyManual() {
    if(m_job){SetStatus(L"Дождитесь завершения OCR",L"Wait for OCR to finish",true);return;}
    std::array<std::optional<double>,4> values;for(int i=0;i<4;++i)values[i]=ParseManualCoordinate(m_fields[i]);
    bool gun=!m_fields[0].empty()||!m_fields[1].empty(),target=!m_fields[2].empty()||!m_fields[3].empty();
    if((gun&&(!values[0]||!values[1]))||(target&&(!values[2]||!values[3]))){SetStatus(L"Введите обе координаты X/Y от 0 до 999.99",L"Enter both X/Y coordinates from 0 to 999.99",true);return;}
    m_aim.Clear();m_gun.reset();m_target.reset();if(gun)m_gun=Coordinates{*values[0],*values[1]};if(target)m_target=Coordinates{*values[2],*values[3]};Recalculate();
}
void OverlayWindow::Recalculate() {
    if(!m_gun||!m_target){m_azimuth=L"---.-°";m_elevation=L"--- mil";m_distance=L"---";m_rangeError=false;InvalidateRect(m_window,nullptr,FALSE);return;}auto result=CalculateBallistics(*m_gun,*m_target);
    m_azimuth=std::format(L"{:05.1f}°",result.azimuth_deg);m_distance=std::format(L"{:.0f} {}",result.distance_m,m_settings.common.english?L"m":L"м");
    m_rangeError=!result.elevation_mil;m_elevation=result.elevation_mil?std::format(L"{:.0f} mil",*result.elevation_mil):(result.range_status==RangeStatus::TooClose?L"<80!":L">684!");InvalidateRect(m_window,nullptr,FALSE);
}
void OverlayWindow::RequestAim(bool tracking) {
    if(m_closing||m_hidden||m_edit)return;
    if(m_job){SetStatus(L"Дождитесь распознавания координат",L"Wait for coordinate OCR to finish",true);return;}
    if(!m_gun||!m_target){SetStatus(L"Сначала считайте орудие и цель: F1/F2",L"Read mortar and target first: F1/F2",true);return;}
    auto result=CalculateBallistics(*m_gun,*m_target);
    m_aim.Request(tracking,result.elevation_mil.value_or(0),result.distance_m);
}
void OverlayWindow::StartScan(bool target) {
    if(m_job||m_closing)return;
    m_aim.Clear();
    m_activeField=-1;m_selectAll=false;
    SetStatus(target?L"Сканирование ЦЕЛИ…":L"Сканирование ОРУДИЯ…",target?L"Scanning TARGET…":L"Scanning MORTAR…");
    // Capture at the command, before scheduling OCR. No repeating capture thread.
    Image image;
    try{image=CaptureAroundCursor(m_settings.common.captureSize);}catch(...){SetStatus(L"Захват экрана недоступен. Проверьте режим отображения.",L"Screen capture is unavailable. Check the display mode.",true);return;}
    auto job=std::make_shared<ScanJob>();job->target=target;m_job=job;HWND window=m_window;
    m_worker=std::jthread([job,window,image=std::move(image)]()mutable{
        try{
            init_apartment(apartment_type::multi_threaded);
            struct Apartment {~Apartment(){uninit_apartment();}} apartment;
            auto output=std::make_shared<ScanOutput>();
            auto operation=RecognizeAsync(std::move(image),output);
            {std::scoped_lock lock(job->mutex);job->operation=operation;if(job->cancelled)operation.Cancel();}
            operation.get();
            if(!job->cancelled){std::scoped_lock lock(job->mutex);job->result=std::move(output);}
            {std::scoped_lock lock(job->mutex);job->operation=nullptr;}
        }catch(hresult_canceled const&){}
        catch(hresult_error const& error){std::scoped_lock lock(job->mutex);job->error=error.message().c_str();}
        catch(...){std::scoped_lock lock(job->mutex);job->error=L"Capture/OCR failed";}
        if(!job->cancelled)PostMessageW(window,ScanDone,0,0);
    });
}
void OverlayWindow::FinishScan() {
    if(!m_job)return;if(m_worker.joinable())m_worker.join();auto job=std::exchange(m_job,nullptr);
    std::scoped_lock lock(job->mutex);
    if(!job->error.empty()){SetStatus(L"Windows OCR недоступен: нужен English OCR, а официальная поддержка desktop требует пакет. "+job->error,L"Windows OCR unavailable: install English OCR; official desktop support requires package identity. "+job->error,true);return;}
    if(!job->result)return;auto const& output=*job->result;
    if(m_settings.common.preview)m_preview=MakePreview(output.preview);
    if(!output.coordinates){SetStatus(L"Не распознано X/Y. Наведите курсор точнее.",L"Could not read X/Y. Align the cursor more clearly.",true);return;}
    auto point=*output.coordinates;int offset=job->target?2:0;if(job->target)m_target=point;else m_gun=point;
    m_fields[offset]=std::format(L"{:.2f}",point.x);m_fields[offset+1]=std::format(L"{:.2f}",point.y);Recalculate();SetStatus(L"Координаты считаны",L"Coordinates captured");
}
void OverlayWindow::StopScan() noexcept {
    if(m_job){m_job->cancelled=true;winrt::Windows::Foundation::IAsyncAction operation{nullptr};{std::scoped_lock lock(m_job->mutex);operation=m_job->operation;}try{if(operation)operation.Cancel();}catch(...){} }
    if(m_worker.joinable()){
        // Do not leave a process behind if an OS OCR operation ignores cancellation.
        // This handle belongs to our own worker; no game process is ever opened.
        if(WaitForSingleObject(m_worker.native_handle(),3000)!=WAIT_OBJECT_0)ExitProcess(ERROR_CANCELLED);
        m_worker.join();
    }
    m_job.reset();m_preview={};m_gun.reset();m_target.reset();for(auto& field:m_fields)field.clear();
}
void OverlayWindow::Slider(float x) {
    float t=std::clamp((x-143.f)/180.f,0.f,1.f);
    if(m_slider==1)m_settings.common.opacity=std::round((.2+t*.8)*100)/100.;
    if(m_slider==2)m_settings.common.captureSize=std::clamp(static_cast<int>(std::lround((160+t*480)/10))*10,160,640);
    InvalidateRect(m_window,nullptr,FALSE);
}
void OverlayWindow::Click(float x,float y) {
    if(Contains(Rect(238,8,64,26),x,y)){ToggleEdit();return;}
    if(Contains(Rect(305,8,60,26),x,y)){ToggleHidden();return;}
    if(!m_edit)return;
    if(Contains(Rect(370,8,20,25),x,y)){SendMessageW(m_window,WM_CLOSE,0,0);return;}
    if(y<35){ReleaseCapture();SendMessageW(m_window,WM_NCLBUTTONDOWN,HTCAPTION,0);return;}
    // Sight buttons are labels in setup; capture must be requested with the game focused.
    if(y>=215&&y<215+AimHeight)return;
    if(y>=215+AimHeight)y-=AimHeight;
    if(m_statusError&&Contains(Rect(12,177,376,34),x,y)){MessageBoxW(m_window,(m_settings.common.english?m_statusEn:m_statusRu).c_str(),L"WARDOGS Portable",MB_OK|MB_ICONINFORMATION);return;}
    for(int i=0;i<4;++i)if(Contains(FieldRect(i),x,y)){if(m_rebinding>=0){m_rebinding=-1;RegisterKeys();}m_activeField=i;m_caret=m_fields[i].size();m_selectAll=true;SetFocus(m_window);InvalidateRect(m_window,nullptr,FALSE);return;}
    m_activeField=-1;m_selectAll=false;
    if(Contains(Rect(20,232,40,25),x,y)){m_settings.common.english=false;Recalculate();}
    else if(Contains(Rect(65,232,40,25),x,y)){m_settings.common.english=true;Recalculate();}
    else if(Contains(Rect(235,232,145,25),x,y))ApplyManual();
    else if(Contains(Rect(135,261,195,26),x,y)){m_slider=1;SetCapture(m_window);Slider(x);return;}
    else if(Contains(Rect(135,293,195,26),x,y)){m_slider=2;SetCapture(m_window);Slider(x);return;}
    else if(Contains(Rect(20,327,350,22),x,y)){m_settings.common.preview=!m_settings.common.preview;if(!m_settings.common.preview)m_preview={};}
    else if(Contains(Rect(20,352,165,22),x,y)){m_settings.topmost=!m_settings.topmost;ApplyWindowStyle();}
    else if(Contains(Rect(205,352,180,22),x,y)){m_settings.clickThrough=!m_settings.clickThrough;ApplyWindowStyle();}
    else for(int i=0;i<4;++i)if(Contains(KeyRect(i),x,y)){m_rebinding=i;UnregisterKeys();SetFocus(m_window);SetStatus(L"Нажмите сочетание. Esc — отмена.",L"Press a combination. Esc cancels.");break;}
    Save();InvalidateRect(m_window,nullptr,FALSE);
}
void OverlayWindow::BindKey(WPARAM key) {
    if(key==VK_ESCAPE){m_rebinding=-1;RegisterKeys();InvalidateRect(m_window,nullptr,FALSE);return;}
    if(key==VK_CONTROL||key==VK_SHIFT||key==VK_MENU||key==VK_LWIN||key==VK_RWIN)return;
    if(key>=VK_F5&&key<=VK_F7&&!(GetKeyState(VK_CONTROL)&0x8000)&&!(GetKeyState(VK_SHIFT)&0x8000)&&!(GetKeyState(VK_MENU)&0x8000)){
        SetStatus(L"F5–F7 заняты подсказкой прицела. Выберите другое сочетание.",L"F5–F7 are used by sight hints. Choose another shortcut.",true);return;
    }
    if((GetKeyState(VK_LWIN)&0x8000)||(GetKeyState(VK_RWIN)&0x8000)){SetStatus(L"Сочетания с Win зарезервированы системой",L"Windows-key combinations are reserved by the system",true);return;}
    std::wstring name;
    if(GetKeyState(VK_CONTROL)&0x8000)name+=L"CTRL+";if(GetKeyState(VK_SHIFT)&0x8000)name+=L"SHIFT+";if(GetKeyState(VK_MENU)&0x8000)name+=L"ALT+";
    if(key>=VK_F1&&key<=VK_F11)name+=L"F"+std::to_wstring(key-VK_F1+1);
    else if((key>=L'A'&&key<=L'Z')||(key>=L'0'&&key<=L'9'))name+=static_cast<wchar_t>(key);
    else if(key>=VK_NUMPAD0&&key<=VK_NUMPAD9)name+=L"NUM"+std::to_wstring(key-VK_NUMPAD0);
    else {SetStatus(L"Эта клавиша не поддерживается. F12 зарезервирована.",L"This key is unsupported. F12 is reserved.",true);return;}
    auto old=m_settings.common.hotkeys;
    try{
        m_settings.common.hotkeys[m_rebinding]=name;std::set<std::vector<winrt::Windows::System::VirtualKey>> unique;
        for(auto const& text:m_settings.common.hotkeys){NativeHotkey(text);if(!unique.insert(ParseHotkey(text)).second)throw std::invalid_argument("Duplicate shortcut");}
        m_rebinding=-1;if(!RegisterKeys()){m_settings.common.hotkeys=old;RegisterKeys();SetStatus(L"Сочетание занято. Предыдущие клавиши восстановлены.",L"Shortcut is in use. Previous bindings restored.",true);}else SetStatus(L"Клавиша назначена",L"Shortcut assigned");Save();
    }catch(...){m_settings.common.hotkeys=old;SetStatus(L"Сочетание уже назначено другому действию",L"Shortcut is already assigned to another action",true);}
    InvalidateRect(m_window,nullptr,FALSE);
}
void OverlayWindow::InsertText(std::wstring_view text) {
    if(m_activeField<0||!m_edit)return;auto& value=m_fields[m_activeField];
    for(wchar_t ch:text)if((ch>=L'0'&&ch<=L'9')||ch==L'.'||ch==L','||ch==L'X'||ch==L'x'||ch==L'Y'||ch==L'y'||ch==L':'||ch==L' '||ch==L'-'){
        if(m_selectAll){value.clear();m_caret=0;m_selectAll=false;}if(value.size()<24)value.insert(m_caret++,1,ch);
    }
    InvalidateRect(m_window,nullptr,FALSE);
}
void OverlayWindow::KeyDown(WPARAM key) {
    if(m_rebinding>=0){BindKey(key);return;}if(!m_edit||m_activeField<0)return;auto& value=m_fields[m_activeField];
    if(key==VK_TAB){m_activeField=(m_activeField+((GetKeyState(VK_SHIFT)&0x8000)?3:1))%4;m_caret=m_fields[m_activeField].size();m_selectAll=true;}
    else if(key==VK_ESCAPE){m_activeField=-1;m_selectAll=false;}
    else if(key==VK_RETURN){ApplyManual();m_activeField=-1;}
    else if(key==L'A'&&(GetKeyState(VK_CONTROL)&0x8000))m_selectAll=true;
    else if(key==L'V'&&(GetKeyState(VK_CONTROL)&0x8000)){
        if(OpenClipboard(m_window)){auto data=GetClipboardData(CF_UNICODETEXT);if(data){auto text=static_cast<wchar_t const*>(GlobalLock(data));if(text){InsertText(std::wstring_view(text,wcsnlen(text,4096)));GlobalUnlock(data);}}CloseClipboard();}
    }
    else if(key==VK_BACK||key==VK_DELETE){if(m_selectAll){value.clear();m_caret=0;}else if(key==VK_BACK&&m_caret)value.erase(--m_caret,1);else if(key==VK_DELETE&&m_caret<value.size())value.erase(m_caret,1);m_selectAll=false;}
    else if(key==VK_LEFT){if(m_caret)--m_caret;m_selectAll=false;}else if(key==VK_RIGHT){if(m_caret<value.size())++m_caret;m_selectAll=false;}else if(key==VK_HOME){m_caret=0;m_selectAll=false;}else if(key==VK_END){m_caret=value.size();m_selectAll=false;}
    InvalidateRect(m_window,nullptr,FALSE);
}
LRESULT CALLBACK OverlayWindow::WindowProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    auto self=reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<OverlayWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);self->m_window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(window,message,wParam,lParam);
    try{return self->HandleMessage(message,wParam,lParam);}catch(...){
        // Never unwind through the OS window procedure. Stop workers before shutdown.
        if(!self->m_closing)PostMessageW(window,WM_CLOSE,0,0);return 0;
    }
}
LRESULT OverlayWindow::HandleMessage(UINT message,WPARAM wParam,LPARAM lParam) {
    switch(message){
    case WM_PAINT:{PAINTSTRUCT paint{};BeginPaint(m_window,&paint);EndPaint(m_window,&paint);Paint();return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_HOTKEY:if(m_closing)return 0;if(wParam==1||wParam==2)StartScan(wParam==2);else if(wParam==3)ToggleEdit();else if(wParam==4)ToggleHidden();else if(wParam==5)RequestAim(false);else if(wParam==6)RequestAim(true);else if(wParam==7)m_aim.Clear();return 0;
    case WM_TIMER:if(wParam==AimAssist::TimerId){m_aim.Tick();return 0;}break;
    case AimAssist::ResultMessage:m_aim.Finish(static_cast<uint64_t>(wParam));return 0;
    case WM_LBUTTONDOWN:Click(GET_X_LPARAM(lParam)/m_scale,GET_Y_LPARAM(lParam)/m_scale);return 0;
    case WM_MOUSEMOVE:if(m_slider)Slider(GET_X_LPARAM(lParam)/m_scale);return 0;
    case WM_LBUTTONUP:if(m_slider){m_slider=0;ReleaseCapture();Save();}return 0;
    case WM_CAPTURECHANGED:if(m_slider){m_slider=0;Save();}return 0;
    case WM_KEYDOWN:case WM_SYSKEYDOWN:if(wParam==VK_F4&&(GetKeyState(VK_MENU)&0x8000)&&m_rebinding<0){SendMessageW(m_window,WM_CLOSE,0,0);return 0;}KeyDown(wParam);return 0;
    case WM_CHAR:if(wParam>=32&&m_rebinding<0&&!(GetKeyState(VK_CONTROL)&0x8000))InsertText(std::wstring(1,static_cast<wchar_t>(wParam)));return 0;
    case WM_KILLFOCUS:if(m_rebinding>=0){m_rebinding=-1;RegisterKeys();InvalidateRect(m_window,nullptr,FALSE);}return 0;
    case WM_NCHITTEST:if(!m_edit&&m_settings.clickThrough)return HTTRANSPARENT;break;
    case WM_MOUSEACTIVATE:if(!m_edit)return MA_NOACTIVATE;break;
    case WM_EXITSIZEMOVE:Save();return 0;
    case WM_DISPLAYCHANGE:m_aim.Clear();ClampPosition();InvalidateRect(m_window,nullptr,FALSE);return 0;
    case WM_DPICHANGED:{m_aim.Clear();m_scale=HIWORD(wParam)/96.f;auto suggested=reinterpret_cast<RECT*>(lParam);SetWindowPos(m_window,nullptr,suggested->left,suggested->top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);Resize();ClampPosition();return 0;}
    case ScanDone:FinishScan();return 0;
    case WM_CLOSE:if(m_closing)return 0;m_closing=true;m_aim.Shutdown();UnregisterKeys();StopScan();Save();DestroyWindow(m_window);return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    case WM_NCDESTROY:{auto window=m_window;SetWindowLongPtrW(window,GWLP_USERDATA,0);m_window=nullptr;return DefWindowProcW(window,message,wParam,lParam);}
    }
    return DefWindowProcW(m_window,message,wParam,lParam);
}
}
