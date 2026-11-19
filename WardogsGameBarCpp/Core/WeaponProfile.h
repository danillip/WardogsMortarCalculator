#pragma once
#include "Calculator.h"
#include <span>
#include <vector>

namespace wardogs {
enum class WeaponMode { Mortar,Artillery };
enum class Trajectory { Upper,Lower };
struct RangeNode {double distance,mil;};
std::span<RangeNode const> WeaponTable(WeaponMode weapon,Trajectory trajectory);
std::optional<double> WeaponMil(double distance,WeaponMode weapon,Trajectory trajectory);
std::optional<double> LabelMil(double distance,WeaponMode weapon,Trajectory trajectory);
double SightPixelsPerMil(WeaponMode weapon);
bool RangeIncreasesUp(WeaponMode weapon,Trajectory trajectory);
BallisticResult CalculateForWeapon(Coordinates gun,Coordinates target,WeaponMode weapon,Trajectory trajectory);
}
