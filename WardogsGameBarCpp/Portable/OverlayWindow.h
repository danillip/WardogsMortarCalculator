#pragma once
#include "../Shared/WindowsIncludes.h"
#include "PortableSettings.h"
#include "AimAssist.h"
#include "../OCR/CoordinateRecognizer.h"
#include <d2d1.h>
#include <dwrite.h>
#include <atomic>
#include <mutex>
#include <thread>

namespace wardogs {
class OverlayWindow {
public:
    OverlayWindow();
    ~OverlayWindow();
    int Run(HINSTANCE instance,int show);
private:
    struct Surface;
    struct ScanJob {
        std::mutex mutex;
        winrt::Windows::Foundation::IAsyncAction operation{nullptr};
        std::shared_ptr<ScanOutput> result;
        std::wstring error;
        std::atomic_bool cancelled=false;
        bool target=false;
    };
    static LRESULT CALLBACK WindowProc(HWND,UINT,WPARAM,LPARAM);
    LRESULT HandleMessage(UINT,WPARAM,LPARAM);
    void InitializeGraphics();
    void LoadLogo();
    void Paint();
    void Text(std::wstring_view text,D2D1_RECT_F rect,uint32_t color,IDWriteTextFormat* format=nullptr);
    void Fill(D2D1_RECT_F rect,uint32_t color);
    void Outline(D2D1_RECT_F rect,uint32_t color);
    void Button(std::wstring_view text,D2D1_RECT_F rect,bool selected=false);
    void DrawImage(Image const& image,D2D1_RECT_F rect);
    void Click(float x,float y);
    void Slider(float x);
    void KeyDown(WPARAM key);
    void InsertText(std::wstring_view text);
    void BindKey(WPARAM key);
    bool RegisterKeys();
    void UnregisterKeys() noexcept;
    void ToggleEdit();
    void ToggleHidden();
    void ApplyWindowStyle();
    void Resize();
    void ClampPosition();
    void ApplyManual();
    void StartScan(bool target);
    void FinishScan();
    void StopScan() noexcept;
    void Save();
    void SetStatus(std::wstring ru,std::wstring en,bool error=false);
    void Recalculate();
    void RequestAim(bool tracking);
    std::wstring Tr(wchar_t const* ru,wchar_t const* en) const;
    HWND m_window{};
    float m_scale=1;
    bool m_edit=false,m_hidden=false,m_closing=false,m_statusError=false;
    int m_activeField=-1,m_rebinding=-1,m_slider=0;
    size_t m_caret=0;
    bool m_selectAll=false;
    std::array<bool,7> m_registered{};
    std::array<std::wstring,4> m_fields;
    std::optional<Coordinates> m_gun,m_target;
    std::wstring m_statusRu,m_statusEn,m_azimuth=L"---.-°",m_elevation=L"--- mil",m_distance=L"---";
    bool m_rangeError=false;
    PortableSettingsStore m_store;
    OverlaySettings m_settings;
    Image m_preview,m_logo;
    AimAssist m_aim;
    std::shared_ptr<ScanJob> m_job;
    std::jthread m_worker;
    winrt::com_ptr<ID2D1Factory> m_factory;
    winrt::com_ptr<IDWriteFactory> m_writeFactory;
    winrt::com_ptr<ID2D1DCRenderTarget> m_renderTarget;
    winrt::com_ptr<IDWriteTextFormat> m_labelFormat,m_smallFormat,m_valueFormat,m_fieldFormat;
    std::unique_ptr<Surface> m_surface;
};
}
