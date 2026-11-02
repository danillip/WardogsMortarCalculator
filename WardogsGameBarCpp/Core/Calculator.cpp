// Port of ballistics.py. Copyright (c) 2026 ProDexortie. MIT License.
#include "Calculator.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

namespace wardogs {
double RoundEven(double value) {
    if (!std::isfinite(value)) throw std::invalid_argument("Non-finite value");
    const auto base = std::floor(value);
    const auto fraction = value - base;
    if (fraction < .5) return base;
    if (fraction > .5) return base + 1;
    return std::fmod(base, 2.) == 0 ? base : base + 1;
}
double RoundPythonOneDecimal(double value) {
    // to_chars rounds the original binary double to one decimal, avoiding the
    // extra multiplication rounding that differs from Python for e.g. 2.55.
    char buffer[64];
    auto [end, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::fixed, 1);
    if (ec != std::errc{}) throw std::invalid_argument("Invalid azimuth");
    double result{};
    std::from_chars(buffer, end, result, std::chars_format::fixed);
    return result;
}
std::pair<std::optional<double>, RangeStatus> InterpolateMil(double distance) {
    if (!std::isfinite(distance)) throw std::invalid_argument("Invalid distance");
    if (distance < 80.) return {std::nullopt, RangeStatus::TooClose};
    if (distance > 684.) return {std::nullopt, RangeStatus::TooFar};
    for (size_t i = 0; i + 1 < MortarTable.size(); ++i) {
        const auto [d0,m0] = MortarTable[i]; const auto [d1,m1] = MortarTable[i+1];
        if (d0 <= distance && distance <= d1)
            return {RoundEven(m0 + (distance-d0)/(d1-d0)*(m1-m0)), RangeStatus::Ok};
    }
    return {150., RangeStatus::Ok};
}
BallisticResult CalculateBallistics(Coordinates gun, Coordinates target) {
    if (!std::isfinite(gun.x) || !std::isfinite(gun.y) ||
        !std::isfinite(target.x) || !std::isfinite(target.y))
        throw std::invalid_argument("Invalid coordinates");
    const double dx = (target.x-gun.x)*100., dy = (target.y-gun.y)*100.;
    const double distance = std::hypot(dx,dy);
    const double azimuth = distance < 1e-6 ? 0. : std::fmod(std::atan2(dx,dy)*180./std::numbers::pi+360.,360.);
    auto [mil,status] = InterpolateMil(distance);
    return {RoundEven(distance),RoundPythonOneDecimal(azimuth),mil,status,dx,dy};
}
std::optional<double> ParseManualCoordinate(std::wstring_view input) {
    std::wstring text(input);
    const auto first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    text = text.substr(first);
    if (text.size() >= 2 && (text[0]==L'X'||text[0]==L'x'||text[0]==L'Y'||text[0]==L'y') && text[1]==L':') text.erase(0,2);
    std::string narrow;
    for (wchar_t ch : text) {
        if (ch==L' '||ch==L'\t'||ch==L'\r'||ch==L'\n') continue;
        if (ch > 127) return {};
        narrow.push_back(ch==L',' ? '.' : static_cast<char>(ch));
    }
    double value{};
    auto [end, ec] = std::from_chars(narrow.data(),narrow.data()+narrow.size(),value);
    if (ec != std::errc{} || end != narrow.data()+narrow.size() || !std::isfinite(value) || value < 0 || value > 999.99) return {};
    return value;
}
}
