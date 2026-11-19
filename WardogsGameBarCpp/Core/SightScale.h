#pragma once
#include "Calculator.h"
#include <vector>

namespace wardogs {
// Positions are pixels in the captured strip, not coordinates in the game world.
struct SightLabel { double distance{},y{},tickX{};std::optional<double> mil; };
enum class SightDirection { Visible,IncreaseRange,DecreaseRange };
struct SightSolution {
    double y{},tickX{},firstY{},lastY{};
    SightDirection direction=SightDirection::Visible;
    bool textAnchored=false;
    size_t inliers=0;
    double residual=0;
};
// Requires three mutually consistent, distinct labels. Never extrapolates a line.
std::optional<SightSolution> LocateSightMark(std::vector<SightLabel> const& labels,
    double targetMil,double pixelsPerMil,double tolerance,bool rangeIncreasesUp=true);
// Fallback: three consistent OCR labels, calibrated against at least one real
// tick in the same frame. No fabricated labels and no previous-frame positions.
std::optional<SightSolution> LocateSightWithText(std::vector<SightLabel> const& ticks,
    std::vector<SightLabel> const& text,double targetMil,double pixelsPerMil,double tolerance,bool rangeIncreasesUp=true);
}
