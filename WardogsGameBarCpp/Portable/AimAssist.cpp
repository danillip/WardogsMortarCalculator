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
void AimAssist::HideMark() noexcept {m_mark.reset();if(m_marker)ShowWindow(m_marker,SW_HIDE);}
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
    HideMark();Cancel();m_preview={};m_game=nullptr;m_region={};m_failures=0;
    m_ru=L"F5 — проверка  ·  F6 — сопровождение  ·  F7 — убрать";
    m_en=L"F5 — check  ·  F6 — track  ·  F7 — clear";
    if(m_owner)InvalidateRect(m_owner,nullptr,FALSE);
}
bool AimAssist::ContextValid() const {
    RECT rect{};
    return GetForegroundWindow()==m_game&&ClientOnScreen(m_game,rect)&&EqualRect(&rect,&m_gameRect);
}
void AimAssist::Request(bool tracking,double targetMil,double distance) {
    if(tracking&&m_tracking){Clear();return;}
    Clear();
    try {
        if(!std::isfinite(targetMil)||targetMil<150||targetMil>950||!std::isfinite(distance)||distance<80||distance>684) {
            SetStatus(L"Сначала задайте F1/F2: нужна дальность 80–684 м",L"Set F1/F2 first: range must be 80–684 m");return;
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
        RECT hud{},overlap{};GetWindowRect(m_owner,&hud);
        if(IntersectRect(&overlap,&hud,&m_region)) {
            Clear();SetStatus(L"Переместите HUD: он перекрывает левую шкалу",L"Move the HUD: it overlaps the range scale");return;
        }
        m_mil=targetMil;m_distance=distance;m_tracking=tracking;m_active=true;
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
    auto job=std::make_shared<Job>();job->generation=++m_generation;job->capturedAt=GetTickCount64();m_lastCapture=job->capturedAt;
    m_job=job;const HWND owner=m_owner;
    m_worker=std::jthread([job,owner,image=std::move(image)]()mutable {
        try {
            init_apartment(apartment_type::multi_threaded);struct Apartment{~Apartment(){uninit_apartment();}} apartment;
            auto output=std::make_shared<SightOutput>();auto operation=RecognizeSightAsync(std::move(image),output);
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
    if(m_job&&now-m_job->capturedAt>2500){Clear();SetStatus(L"OCR не успел обновить шкалу. Повторите F5.",L"OCR timed out. Press F5 to retry.");return;}
    if(m_mark&&now-m_lastCapture>(m_tracking?1000:8000)) {
        HideMark();
        if(!m_tracking){Clear();SetStatus(L"Снимок устарел. F5 — обновить риску",L"Snapshot expired. F5 refreshes the mark");return;}
        InvalidateRect(m_owner,nullptr,FALSE);
    }
    if(m_tracking&&!m_job&&now>=m_nextScan) {
        try{Capture();}catch(...){Clear();SetStatus(L"Захват остановлен. Повторите F5.",L"Capture stopped. Press F5 to retry.");}
    }
}
void AimAssist::DrawMark(SightSolution const& solution) {
    const int left=m_region.left+static_cast<int>(std::lround(solution.tickX));
    const int y=m_region.top+static_cast<int>(std::lround(solution.y));
    const int width=m_center.x-left+1,height=11;
    if(width<20||width>m_gameRect.right-m_gameRect.left||y<m_gameRect.top||y>=m_gameRect.bottom)throw hresult_error(E_INVALIDARG);
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
    const uint32_t color=std::abs(y-m_center.y)<=2*m_scale?0xff34d399:0xff38bdf8;
    for(int row=0;row<height;++row)for(int x=0;x<width;++x)
        if((row>=4&&row<=6)||x<2||x>=width-2)data[static_cast<size_t>(row)*width+x]=color;
    POINT position{left,y-height/2},source{};SIZE size{width,height};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    if(!UpdateLayeredWindow(m_marker,nullptr,&position,&size,canvas.dc,&source,0,&blend,ULW_ALPHA))throw_last_error();
    SetWindowPos(m_marker,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
    m_mark=solution.y/(m_region.bottom-m_region.top);
}
void AimAssist::Finish(uint64_t generation) {
    if(!m_active||!m_job||generation!=m_generation||generation!=m_job->generation)return;
    if(m_worker.joinable())m_worker.join();auto job=std::exchange(m_job,nullptr);
    if(!ContextValid()){Clear();return;}
    m_nextScan=GetTickCount64()+350;
    const auto maximumAge=m_tracking?1000ULL:2500ULL;
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
    auto solution=LocateSightWithText(job->output->labels,job->output->textLabels,m_mil,102.*m_scale/50.,std::max(2.,3.*m_scale));
    m_preview=std::move(job->output->preview);
    if(!solution) {
        HideMark();++m_failures;
        auto [ru,en]=SightFailure(*job->output);
        if(!m_tracking){m_active=false;KillTimer(m_owner,TimerId);}
        if(m_tracking&&m_failures>=3){
            auto preview=std::move(m_preview);Clear();m_preview=std::move(preview);
            SetStatus(L"Сопровождение остановлено. "+ru,L"Tracking stopped. "+en);
        }else SetStatus(std::move(ru),std::move(en));
        return;
    }
    m_failures=0;
    if(solution->direction!=SightDirection::Visible) {
        HideMark();const bool increase=solution->direction==SightDirection::IncreaseRange;
        if(!m_tracking){m_active=false;KillTimer(m_owner,TimerId);}
        SetStatus(std::format(L"{} дальность: нужно {:.0f} м",increase?L"Увеличьте":L"Уменьшите",m_distance),
            std::format(L"{} range: target {:.0f} m",increase?L"Increase":L"Decrease",m_distance));return;
    }
    try {
        DrawMark(*solution);
        std::wstring ru=m_tracking?L"Сопровождение включено. Совместите риску с центром.":L"Разовая риска. После прокрутки обновите F5.";
        std::wstring en=m_tracking?L"Tracking on. Align the mark with the sight centre.":L"Snapshot mark. Press F5 again after adjusting.";
        if(solution->textAnchored){ru+=L" Привязка по цифрам и штриху.";en+=L" Anchored by labels and a tick.";}
        SetStatus(std::move(ru),std::move(en));
    }catch(...){Clear();SetStatus(L"Не удалось вывести риску",L"Could not display the mark");}
}
}
