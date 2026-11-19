#pragma once
#include "CoordinateRecognizer.h"
#include "../Core/SightScale.h"
#include "../Core/HeadingScale.h"
#include "../Core/WeaponProfile.h"

namespace wardogs {
struct SightOutput {
    Image preview;
    std::vector<SightLabel> labels;
    // RAM-only diagnostics distinguish unreadable numbers from rejected ticks.
    std::vector<SightLabel> textLabels;
    Image processed;
    std::optional<SightSolution> solution;
    std::optional<HeadingSolution> heading;
    std::wstring filter;
    bool conflict=false;
    size_t variants=0;
};
struct SightRequest {
    WeaponMode weapon=WeaponMode::Mortar;
    Trajectory trajectory=Trajectory::Upper;
    double targetMil{},targetAzimuth{};
    bool tracking=false;
};
winrt::Windows::Foundation::IAsyncAction RecognizeSightAsync(Image image,Image heading,
    SightRequest request,std::shared_ptr<SightOutput> output);
}
