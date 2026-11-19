#include "AimAssist.h"
#include "../Capture/CaptureService.h"
#include <dwmapi.h>
#include <format>

using namespace winrt;
namespace wardogs {
namespace {
bool ClientOnScreen(HWND window,RECT& rect) {
    if(!window||!IsWindow(window)||IsIconic(window)||!GetClientRect(window,&rect))return false;
    POINT origin{};if(!ClientToScreen(window,&origin))return false;
    OffsetRect(&rect,origin.x,origin.y);return rect.right-rect.left>=640&&rect.bottom-rect.top>=480;
}
std::pair<std::wstring,std::wstring> SightFailure(SightOutput const& output) {
    if(output.conflict)return {L"Фильтры не подтвердили одну риску. F5 — повторить.",L"Filters did not confirm the same mark. F5 retries."};
    if(output.textLabels.empty())return {L"OCR не прочитал цифры шкалы. F5 — повторить.",L"OCR could not read the range labels. Press F5 to retry."};
    auto labels=output.textLabels;
    std::sort(labels.begin(),labels.end(),[](auto a,auto b){return a.y<b.y;});
    std::wstring values;std::vector<double> seen;
    for(auto label:labels) {
        if(std::find(seen.begin(),seen.end(),label.distance)!=seen.end())continue;
        if(!values.empty())values+=L", ";values+=std::format(L"{:.0f}",label.distance);seen.push_back(label.distance);
        if(seen.size()==5)break;
    }
    if(seen.size()<3)return {
        std::format(L"OCR: {}. Подписи: {}/3, штрихи: {}. F5 — повторить.",values,seen.size(),output.labels.size()),
        std::format(L"OCR: {}. Labels: {}/3, strokes: {}. F5 retries.",values,seen.size(),output.labels.size())};
    if(output.labels.empty())return {L"OCR: "+values+L". Не найден штрих для привязки. F5 — повторить.",
        L"OCR: "+values+L". No tick to anchor the mark. F5 retries."};
    return {L"OCR: "+values+L". Привязка шкалы не совпала. F5 — повторить.",
        L"OCR: "+values+L". Scale alignment does not match. F5 retries."};
}
}
AimAssist::~AimAssist(){Shutdown();}
void AimAssist::Shutdown() noexcept {Clear();if(m_marker)DestroyWindow(m_marker);m_marker=nullptr;m_owner=nullptr;}
void AimAssist::Initialize(HWND owner) {
    m_owner=owner;
    m_marker=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,
        L"STATIC",L"WARDOGS range mark",WS_POPUP,0,0,1,1,owner,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!m_marker)throw_last_error();
    // Best effort on our own window. Capture() also hides it before every capture.
    SetWindowDisplayAffinity(m_marker,WDA_EXCLUDEFROMCAPTURE);
}
void AimAssist::SetStatus(std::wstring ru,std::wstring en) {
    m_ru=std::move(ru);m_en=std::move(en);if(m_owner)InvalidateRect(m_owner,nullptr,FALSE);
}
void AimAssist::HideMark() noexcept {m_mark.reset();m_guidesVisible=false;if(m_marker)ShowWindow(m_marker,SW_HIDE);}
void AimAssist::Cancel() noexcept {
    ++m_generation;
    if(m_job) {
        m_job->cancelled=true;winrt::Windows::Foundation::IAsyncAction operation{nullptr};
        {std::scoped_lock lock(m_job->mutex);operation=m_job->operation;}
        try{if(operation)operation.Cancel();}catch(...){}
    }
    if(m_worker.joinable()) {
        // Only our own worker is waited on; no game process handles are opened.
        if(WaitForSingleObject(m_worker.native_handle(),3000)!=WAIT_OBJECT_0)ExitProcess(ERROR_CANCELLED);
        m_worker.join();
    }
    m_job.reset();
}
void AimAssist::Clear() noexcept {
    m_active=false;m_tracking=false;if(m_owner)KillTimer(m_owner,TimerId);
    HideMark();Cancel();m_preview={};m_headingPreview={};m_game=nullptr;m_region={};m_failures=0;
    m_ru=L"F5 — проверка  ·  F6 — сопровождение  ·  F7 — убрать";
    m_en=L"F5 — check  ·  F6 — track  ·  F7 — clear";
    if(m_owner)InvalidateRect(m_owner,nullptr,FALSE);
}
bool AimAssist::ContextValid() const {
    RECT rect{};
    return GetForegroundWindow()==m_game&&ClientOnScreen(m_game,rect)&&EqualRect(&rect,&m_gameRect);
}
void AimAssist::Request(bool tracking,double targetMil,double distance,double azimuth,WeaponMode weapon,Trajectory trajectory) {
    if(tracking&&m_tracking){Clear();return;}
    Clear();
    try {
        if(!std::isfinite(targetMil)||targetMil<0||targetMil>1600||!std::isfinite(distance)||!std::isfinite(azimuth)||
           !WeaponMil(distance,weapon,trajectory)) {
            SetStatus(L"Для этой дальности нет данных выбранного оружия/ветви",L"No calibration for this range and weapon/branch");return;
        }
        m_game=GetForegroundWindow();DWORD process{};GetWindowThreadProcessId(m_game,&process);
        if(process==GetCurrentProcessId()||!ClientOnScreen(m_game,m_gameRect)) {
            Clear();SetStatus(L"Вернитесь в окно игры и нажмите F5",L"Return to the game window and press F5");return;
        }
        const int width=m_gameRect.right-m_gameRect.left,height=m_gameRect.bottom-m_gameRect.top;
        m_scale=height/1080.;m_center={m_gameRect.left+width/2,m_gameRect.top+height/2};
        // The game locks the cursor to the sight centre. Locate the left ruler
        // from the client centre instead; offsets scale with the viewport height.
        // Reference L81 at 1920x1080: labels x=634..680, ticks x=690..710.
        // Capture x=550..770, y=280..800. No cursor movement or calibration needed.
        m_region={
            std::max(m_gameRect.left,m_center.x-static_cast<int>(std::lround(410*m_scale))),
            std::max(m_gameRect.top,m_center.y-static_cast<int>(std::lround(260*m_scale))),
            std::min(m_gameRect.right,m_center.x-static_cast<int>(std::lround(190*m_scale))),
            std::min(m_gameRect.bottom,m_center.y+static_cast<int>(std::lround(260*m_scale)))};
        if(m_region.right<=m_region.left||m_region.bottom<=m_region.top) {
            Clear();SetStatus(L"Не удалось определить область левой шкалы в этом окне",L"Could not locate the left scale region in this window");return;
        }
        m_headingRegion={std::max(m_gameRect.left,m_center.x-static_cast<int>(300*m_scale)),
            std::max(m_gameRect.top,m_center.y-static_cast<int>(290*m_scale)),
            std::min(m_gameRect.right,m_center.x+static_cast<int>(300*m_scale)),
            std::min(m_gameRect.bottom,m_center.y-static_cast<int>(205*m_scale))};
        RECT hud{},overlap{};GetWindowRect(m_owner,&hud);
        if(IntersectRect(&overlap,&hud,&m_region)||IntersectRect(&overlap,&hud,&m_headingRegion)) {
            Clear();SetStatus(L"Переместите HUD: он перекрывает шкалу прицела",L"Move the HUD: it overlaps a sight scale");return;
        }
        m_mil=targetMil;m_distance=distance;m_tracking=tracking;m_active=true;
        m_request={weapon,trajectory,targetMil,azimuth,tracking};
        if(!SetTimer(m_owner,TimerId,100,nullptr))throw_last_error();
        SetStatus(tracking?L"Сопровождение: распознаю шкалу…":L"Разовая проверка шкалы…",tracking?L"Tracking: reading the scale…":L"Reading the scale once…");
        Capture();
    }catch(...) {Clear();SetStatus(L"Захват прицела недоступен. Проверьте оконный режим.",L"Sight capture unavailable. Check the window mode.");}
}
void AimAssist::Capture() {
    if(!m_active||m_job)return;
    if(!ContextValid()){Clear();return;}
    HideMark();DwmFlush(); // Never feed our own mark back into OCR.
    auto image=CaptureRegion(m_region.left,m_region.top,m_region.right-m_region.left,m_region.bottom-m_region.top);
    auto heading=CaptureRegion(m_headingRegion.left,m_headingRegion.top,m_headingRegion.right-m_headingRegion.left,m_headingRegion.bottom-m_headingRegion.top);
    auto job=std::make_shared<Job>();job->generation=++m_generation;job->capturedAt=GetTickCount64();m_lastCapture=job->capturedAt;
    m_job=job;const HWND owner=m_owner;auto request=m_request;
    m_worker=std::jthread([job,owner,request,image=std::move(image),heading=std::move(heading)]()mutable {
        try {
            init_apartment(apartment_type::multi_threaded);struct Apartment{~Apartment(){uninit_apartment();}} apartment;
            auto output=std::make_shared<SightOutput>();auto operation=RecognizeSightAsync(std::move(image),std::move(heading),request,output);
            {std::scoped_lock lock(job->mutex);job->operation=operation;if(job->cancelled)operation.Cancel();}
            operation.get();
            {std::scoped_lock lock(job->mutex);job->operation=nullptr;if(!job->cancelled)job->output=std::move(output);}
        }catch(hresult_canceled const&){}
        catch(hresult_error const& error){std::scoped_lock lock(job->mutex);job->error=error.message().c_str();}
        catch(...){std::scoped_lock lock(job->mutex);job->error=L"Sight OCR failed";}
        if(!job->cancelled)PostMessageW(owner,ResultMessage,static_cast<WPARAM>(job->generation),0);
    });
}
void AimAssist::Tick() {
    if(!m_active)return;
    if(!ContextValid()){Clear();SetStatus(L"Подсказка остановлена: окно игры изменилось",L"Hint stopped: the game window changed");return;}
    const auto now=GetTickCount64();
    if(m_job&&now-m_job->capturedAt>(m_tracking?3000:6000)){Clear();SetStatus(L"OCR не успел обновить шкалу. Повторите F5.",L"OCR timed out. Press F5 to retry.");return;}
    if(m_guidesVisible&&now-m_lastCapture>(m_tracking?2000:8000)) {
        HideMark();
        if(!m_tracking){Clear();SetStatus(L"Снимок устарел. F5 — обновить риску",L"Snapshot expired. F5 refreshes the mark");return;}
        InvalidateRect(m_owner,nullptr,FALSE);
    }
    if(m_tracking&&!m_job&&now>=m_nextScan) {
        try{Capture();}catch(...){Clear();SetStatus(L"Захват остановлен. Повторите F5.",L"Capture stopped. Press F5 to retry.");}
    }
}
void AimAssist::DrawMark(std::optional<SightSolution> const& solution,std::optional<HeadingSolution> const& heading) {
    m_mark.reset();
    const int left=std::max(m_gameRect.left,m_center.x-static_cast<int>(440*m_scale));
    const int top=std::max(m_gameRect.top,m_center.y-static_cast<int>(350*m_scale));
    const int right=std::min(m_gameRect.right,m_center.x+static_cast<int>(440*m_scale));
    const int bottom=std::min(m_gameRect.bottom,m_center.y+static_cast<int>(270*m_scale));
    const int width=right-left,height=bottom-top;
    if(width<=0||height<=0)throw hresult_error(E_INVALIDARG);
    struct Canvas {
        HDC dc=CreateCompatibleDC(nullptr);HBITMAP bitmap{};HGDIOBJ old{};
        ~Canvas(){if(old&&old!=HGDI_ERROR)SelectObject(dc,old);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);}
    } canvas;
    if(!canvas.dc)throw_last_error();
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* pixels{};canvas.bitmap=CreateDIBSection(canvas.dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!canvas.bitmap||!pixels)throw_last_error();canvas.old=SelectObject(canvas.dc,canvas.bitmap);
    if(!canvas.old||canvas.old==HGDI_ERROR)throw_last_error();
    auto data=static_cast<uint32_t*>(pixels);std::fill_n(data,static_cast<size_t>(width)*height,0U);
    auto dot=[&](int x,int y,int radius,uint32_t color){x-=left;y-=top;for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx)
        if(x+dx>=0&&x+dx<width&&y+dy>=0&&y+dy<height)data[static_cast<size_t>(y+dy)*width+x+dx]=color;};
    auto line=[&](int x0,int y0,int x1,int y1,uint32_t color){
        const int steps=std::max(std::abs(x1-x0),std::abs(y1-y0));
        const int radius=std::max(1,static_cast<int>(std::lround(m_scale)));
        for(int outline=1;outline>=0;--outline)for(int i=0;i<=steps;++i){const double t=steps?static_cast<double>(i)/steps:0;
            dot(static_cast<int>(std::lround(x0+(x1-x0)*t)),static_cast<int>(std::lround(y0+(y1-y0)*t)),radius+outline,outline?0xff101827:color);}
    };
    auto arrow=[&](int x,int y,int dx,int dy,uint32_t color){
        const int length=std::max(8,static_cast<int>(24*m_scale)),wing=std::max(4,static_cast<int>(8*m_scale));
        line(x-dx*length,y-dy*length,x+dx*length,y+dy*length,color);
        line(x+dx*length,y+dy*length,x+dx*(length-wing)-dy*wing,y+dy*(length-wing)+dx*wing,color);
        line(x+dx*length,y+dy*length,x+dx*(length-wing)+dy*wing,y+dy*(length-wing)-dx*wing,color);
    };
    if(solution) {
        const int y=m_region.top+static_cast<int>(std::lround(solution->y));
        const bool aligned=std::abs(y-m_center.y)<=2*m_scale;const uint32_t color=aligned?0xff34d399:0xff38bdf8;
        if(solution->direction==SightDirection::Visible) {
            line(m_region.left+static_cast<int>(std::lround(solution->tickX)),y,m_center.x,y,color);
            m_mark=solution->y/(m_region.bottom-m_region.top);
        }
        if(!aligned)arrow(m_center.x-static_cast<int>(385*m_scale),m_center.y-static_cast<int>(170*m_scale),0,y<m_center.y?-1:1,color);
    }
    if(heading) {
        const uint32_t color=std::abs(heading->delta)<=.3?0xff34d399:0xfffbbf24;
        if(heading->visible) {
            const int x=m_headingRegion.left+static_cast<int>(std::lround(heading->x));
            line(x,m_center.y-static_cast<int>(218*m_scale),x,m_center.y,color);
        }
        if(std::abs(heading->delta)>.3)arrow(m_center.x,m_center.y-static_cast<int>(320*m_scale),heading->delta<0?-1:1,0,color);
    }
    POINT position{left,top},source{};SIZE size{width,height};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    if(!UpdateLayeredWindow(m_marker,nullptr,&position,&size,canvas.dc,&source,0,&blend,ULW_ALPHA))throw_last_error();
    SetWindowPos(m_marker,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
    m_guidesVisible=true;
}
void AimAssist::Finish(uint64_t generation) {
    if(!m_active||!m_job||generation!=m_generation||generation!=m_job->generation)return;
    if(m_worker.joinable())m_worker.join();auto job=std::exchange(m_job,nullptr);
    if(!ContextValid()){Clear();return;}
    m_nextScan=GetTickCount64()+350;
    const auto maximumAge=m_tracking?2000ULL:5500ULL;
    if(GetTickCount64()-job->capturedAt>maximumAge){
        HideMark();++m_failures;
        if(m_tracking&&m_failures>=3)Clear();
        else if(!m_tracking){m_active=false;KillTimer(m_owner,TimerId);}
        SetStatus(L"Шкала обновляется слишком медленно. F5 — повторить",L"Scale updates too slowly. Press F5 to retry");return;
    }
    std::scoped_lock lock(job->mutex);
    if(!job->error.empty()||!job->output) {
        Clear();SetStatus(L"Windows OCR недоступен для прицела",L"Windows OCR is unavailable for the sight");return;
    }
    auto solution=job->output->solution;
    m_preview=job->output->processed.bgra.empty()?std::move(job->output->preview):std::move(job->output->processed);
    m_headingPreview=std::move(job->output->headingPreview);
    if(!solution) {
        HideMark();++m_failures;
        auto [ru,en]=SightFailure(*job->output);
        if(job->output->heading){ru+=L" Азимут: "+job->output->headingFilter+L".";en+=L" Heading: "+job->output->headingFilter+L".";}
        else if(job->output->headingConflict){ru+=L" Фильтры азимута расходятся.";en+=L" Heading filters disagree.";}
        if(!m_tracking&&!job->output->heading){m_active=false;KillTimer(m_owner,TimerId);}
        if(m_tracking&&m_failures>=3){
            auto preview=std::move(m_preview),headingPreview=std::move(m_headingPreview);Clear();m_preview=std::move(preview);m_headingPreview=std::move(headingPreview);
            SetStatus(L"Сопровождение остановлено. "+ru,L"Tracking stopped. "+en);
        }else {
            if(job->output->heading)try{DrawMark({},job->output->heading);}catch(...){}
            SetStatus(std::move(ru),std::move(en));
        }
        return;
    }
    m_failures=0;
    if(solution->direction!=SightDirection::Visible) {
        HideMark();const bool increase=solution->direction==SightDirection::IncreaseRange;
        try{DrawMark(solution,job->output->heading);}catch(...){}
        SetStatus(std::format(L"{} дальность: нужно {:.0f} м",increase?L"Увеличьте":L"Уменьшите",m_distance),
            std::format(L"{} range: target {:.0f} m",increase?L"Increase":L"Decrease",m_distance));return;
    }
    try {
        DrawMark(solution,job->output->heading);
        std::wstring ru=std::format(L"{} · {} ({}). ",m_tracking?L"F6":L"F5",job->output->filter,job->output->variants);
        std::wstring en=ru;
        if(solution->textAnchored){ru+=L"По цифрам + штриху. ";en+=L"Labels + tick. ";}
        if(job->output->heading){ru+=std::format(L"Азимут {} {:+.1f}°. ",job->output->headingFilter,job->output->heading->delta);en+=std::format(L"Heading {} {:+.1f}°. ",job->output->headingFilter,job->output->heading->delta);}
        else if(job->output->headingConflict){ru+=L" Фильтры азимута расходятся.";en+=L" Heading filters disagree.";}
        else{ru+=L" Азимут не прочитан.";en+=L" Heading unreadable.";}
        ru+=L" Совместите линии с центром.";en+=L" Align the lines with the centre.";
        SetStatus(std::move(ru),std::move(en));
    }catch(...){Clear();SetStatus(L"Не удалось вывести риску",L"Could not display the mark");}
}
}
