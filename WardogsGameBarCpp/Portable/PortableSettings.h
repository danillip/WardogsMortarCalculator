#pragma once
#include "../Settings/SettingsManager.h"
#include <filesystem>
namespace wardogs {
struct OverlaySettings {
    Settings common;
    int x=40,y=40;
    bool topmost=true,clickThrough=true;
};
class PortableSettingsStore {
public:
    PortableSettingsStore();
    OverlaySettings Load() const;
    void Save(OverlaySettings const& settings) const;
    std::filesystem::path const& Path() const {return m_path;}
private:
    std::filesystem::path m_path,m_adjacent;
};
}
