#pragma once
#include <cstddef>
#include <optional>
#include <vector>
namespace wardogs {
struct HeadingLabel {double degrees,x;};
struct HeadingSolution {double current{},delta{},x{};bool visible=false;size_t inliers{};double residual{};};
double HeadingDelta(double target,double current);
std::optional<HeadingSolution> LocateHeading(std::vector<HeadingLabel> const& labels,
    double target,double centerX,double expectedPixelsPerDegree,double tolerance);
}
