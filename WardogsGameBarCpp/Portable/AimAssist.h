#pragma once
#include "../Shared/WindowsIncludes.h"
#include "../OCR/SightRecognizer.h"
#include <atomic>
#include <mutex>
#include <thread>

namespace wardogs {
// UI-thread controller. Workers own only RAM buffers and never touch HWND state.
class AimAssist {
public:
    static constexpr UINT ResultMessage=WM_APP+2;
    static constexpr UINT_PTR TimerId=0x5741;
    ~AimAssist();
    void Initialize(HWND owner);
    void Shutdown() noexcept;
    void Request(bool tracking,double targetMil,double distance);
    void Clear() noexcept;
    void Tick();
    void Finish(uint64_t generation);
    bool Tracking() const {return m_tracking;}
    bool Active() const {return m_active;}
    Image const& Preview() const {return m_preview;}
    std::optional<double> PreviewMark() const {return m_mark;}
    std::wstring const& Status(bool english) const {return english?m_en:m_ru;}
private:
    struct Job {
        std::mutex mutex;
        winrt::Windows::Foundation::IAsyncAction operation{nullptr};
        std::shared_ptr<SightOutput> output;
        std::wstring error;
        std::atomic_bool cancelled=false;
        uint64_t generation{};
        ULONGLONG capturedAt{};
    };
    void Cancel() noexcept;
    void Capture();
    void HideMark() noexcept;
    void DrawMark(SightSolution const& solution);
    bool ContextValid() const;
    void SetStatus(std::wstring ru,std::wstring en);
    HWND m_owner{},m_marker{},m_game{};
    RECT m_gameRect{},m_region{};
    POINT m_center{};
    double m_mil{},m_distance{},m_scale=1;
    bool m_active=false,m_tracking=false;
    unsigned m_failures=0;
    uint64_t m_generation=0;
    ULONGLONG m_nextScan=0,m_lastCapture=0;
    Image m_preview;
    std::optional<double> m_mark;
    std::wstring m_ru=L"F5: наведите курсор на левую шкалу прицела",m_en=L"F5: hover over the left sight range scale";
    std::shared_ptr<Job> m_job;
    std::jthread m_worker;
};
}
