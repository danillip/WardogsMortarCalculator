#pragma once
#include "../Core/Calculator.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace wardogs {
struct Image {
    int width{}, height{};
    std::vector<uint8_t> bgra;
};
struct ScanOutput {
    std::optional<Coordinates> coordinates;
    Image preview;
};
Image FromBitmap(winrt::Windows::Graphics::Imaging::SoftwareBitmap const& bitmap);
winrt::Windows::Graphics::Imaging::SoftwareBitmap ToBitmap(Image const& image);
Image Crop(Image const& source, double anchorX, double anchorY, int size);
Image MakePreview(Image const& source);
// Each call owns its buffers. No text, coordinates, or images are written to disk.
winrt::Windows::Foundation::IAsyncAction RecognizeAsync(Image image, std::shared_ptr<ScanOutput> output);
}
