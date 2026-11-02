#pragma once
#include <array>
#include <string>
#include <vector>
#include <winrt/Windows.System.h>
namespace wardogs {
struct Settings {
    bool english=false,preview=true;
    double opacity=.88;
    int captureSize=320;
    std::array<std::wstring,4> hotkeys{L"F1",L"F2",L"F3",L"F4"};
};
Settings LoadSettings();
void NormalizeSettings(Settings& settings);
void SaveSettings(Settings const& settings);
std::vector<winrt::Windows::System::VirtualKey> ParseHotkey(std::wstring const& text);
}
