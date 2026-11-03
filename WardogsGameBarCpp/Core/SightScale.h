#pragma once
#include "Calculator.h"
#include <vector>

namespace wardogs {
// Positions are pixels in the captured strip, not coordinates in the game world.
struct SightLabel { double distance{},y{},tickX{}; };
enum class SightDirection { Visible,IncreaseRange,DecreaseRange };
struct SightSolution {
    double y{},tickX{},firstY{},lastY{};
    SightDirection direction=SightDirection::Visible;
};
// Requires three mutually consistent, distinct labels. Never extrapolates a line.
std::optional<SightSolution> LocateSightMark(std::vector<SightLabel> const& labels,
    double targetMil,double pixelsPerMil,double tolerance);
}
