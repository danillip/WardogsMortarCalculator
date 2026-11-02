#pragma once
#include <array>
#include <optional>
#include <string_view>
#include <utility>

namespace wardogs {
struct Coordinates { double x{}, y{}; };
enum class RangeStatus { Ok, TooClose, TooFar };
struct BallisticResult {
    double distance_m{}, azimuth_deg{};
    std::optional<double> elevation_mil;
    RangeStatus range_status{};
    double dx_m{}, dy_m{};
};
inline constexpr std::array<std::pair<double, double>, 17> MortarTable{{
    {80,950},{110,900},{132,850},{187,800},{240,750},{290,700},
    {340,650},{385,600},{430,550},{470,500},{510,450},{545,400},
    {578,350},{609,300},{637,250},{661,200},{684,150}
}};
// Python round(): ties to even, independent of the current floating-point mode.
double RoundEven(double value);
double RoundPythonOneDecimal(double value);
std::pair<std::optional<double>, RangeStatus> InterpolateMil(double distance);
BallisticResult CalculateBallistics(Coordinates gun, Coordinates target);
std::optional<double> ParseManualCoordinate(std::wstring_view text);
}
