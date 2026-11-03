#pragma once
#include "CoordinateRecognizer.h"
#include "../Core/SightScale.h"

namespace wardogs {
struct SightOutput {Image preview;std::vector<SightLabel> labels;};
winrt::Windows::Foundation::IAsyncAction RecognizeSightAsync(Image image,std::shared_ptr<SightOutput> output);
}
