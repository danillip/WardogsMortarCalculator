#pragma once
#include "CoordinateRecognizer.h"
#include "../Core/SightScale.h"

namespace wardogs {
struct SightOutput {
    Image preview;
    std::vector<SightLabel> labels;
    // RAM-only diagnostics distinguish unreadable numbers from rejected ticks.
    std::vector<SightLabel> textLabels;
};
winrt::Windows::Foundation::IAsyncAction RecognizeSightAsync(Image image,std::shared_ptr<SightOutput> output);
}
