#include "WeaponProfile.h"
#include <algorithm>
#include <cmath>

namespace wardogs {
namespace {
constexpr auto MortarNodes=[] {
    std::array<RangeNode,MortarTable.size()> nodes{};
    for(size_t i=0;i<nodes.size();++i)nodes[i]={MortarTable[i].first,MortarTable[i].second};
    return nodes;
}();
// Transcribed from the user's SPH-2 screenshots, same position/ammunition/zoom.
// This is an observed sight calibration, not a model of chassis tilt or terrain.
constexpr RangeNode ArtilleryUpper[]{
    {734,1400},{780,1390},{824,1380},{868,1370},{912,1360},{955,1350},
    {998,1340},{1041,1330},{1083,1320},{1124,1310},{1165,1300},{1205,1290},
    {1245,1280},{1285,1270},{1324,1260},{1362,1250},{1400,1240},{1438,1230},
    {1475,1220},{1511,1210},{1547,1200},{1582,1190},{1617,1180},{1651,1170},
    {1685,1160},{1718,1150},{1750,1140},{1782,1130},{1814,1120},{1845,1110},
    {1875,1100},{1905,1090},{1934,1080},{1962,1070},{1990,1060},{2018,1050},
    {2045,1040},{2071,1030},{2097,1020},{2122,1010},{2147,1000},{2170,990},
    {2194,980},{2217,970},{2239,960},{2260,950},{2281,940},{2302,930},
    {2322,920},{2341,910},{2359,900},{2378,890},{2395,880},{2412,870},
    {2428,860},{2444,850},{2459,840},{2473,830},{2487,820},{2500,810},
    {2513,800},{2524,790},{2536,780},{2546,770},{2556,760},{2566,750},
    {2575,740},{2583,730},{2591,720},{2597,710},{2603,700},{2609,690},
    {2614,680},{2618,670},{2622,660},{2624,650},{2626,640},{2628,630},{2629,620}
};
constexpr RangeNode ArtilleryLower[]{
    {1179,20},{1230,30},{1281,40},{1332,50},{1382,60},
    // Gaps are deliberately retained: WeaponMil refuses to bridge them.
    {1528,90},{1574,100},{1620,110},{1664,120},{1708,130},{1751,140},
    {1791,150},{1831,160},{1870,170},{1907,180},{1944,190},{1979,200},
    {2013,210},{2046,220},{2078,230},{2108,240},{2138,250},{2167,260},
    {2195,270},{2222,280},{2248,290},{2272,300},{2296,310},{2319,320},
    {2341,330},{2362,340},{2382,350},{2402,360},{2420,370},{2437,380},
    {2454,390},{2470,400},{2485,410},{2499,420},{2513,430},{2526,440},
    {2537,450},{2548,460},{2559,470},{2568,480},{2578,490},{2586,500},
    {2593,510},{2600,520},{2606,530},{2611,540},{2616,550},{2620,560},
    {2623,570},{2626,580},{2627,590},{2629,600},{2629,610}
};
}
std::span<RangeNode const> WeaponTable(WeaponMode weapon,Trajectory trajectory) {
    if(weapon==WeaponMode::Mortar)return MortarNodes;
    if(trajectory==Trajectory::Upper)return ArtilleryUpper;
    return ArtilleryLower;
}
std::optional<double> LabelMil(double distance,WeaponMode weapon,Trajectory trajectory) {
    std::optional<double> result;
    for(auto node:WeaponTable(weapon,trajectory))if(node.distance==distance) {
        // Rounded range labels repeat at the peak. They cannot identify a
        // unique tick without reading the corresponding right-hand mil label.
        if(result&&*result!=node.mil)return {};
        result=node.mil;
    }
    return result;
}
std::optional<double> WeaponMil(double distance,WeaponMode weapon,Trajectory trajectory) {
    if(weapon==WeaponMode::Mortar)return InterpolateMil(distance).first;
    if(!std::isfinite(distance))return {};
    const auto table=WeaponTable(weapon,trajectory);
    for(auto node:table)if(distance==node.distance)return node.mil;
    for(size_t i=1;i<table.size();++i) {
        const auto a=table[i-1],b=table[i];
        if(distance>a.distance&&distance<b.distance&&std::abs(a.mil-b.mil)<=10)
            return a.mil+(distance-a.distance)/(b.distance-a.distance)*(b.mil-a.mil);
    }
    return {};
}
double SightPixelsPerMil(WeaponMode weapon){return weapon==WeaponMode::Mortar?102./50.:132./10.;}
bool RangeIncreasesUp(WeaponMode weapon,Trajectory trajectory){return weapon==WeaponMode::Mortar||trajectory==Trajectory::Upper;}
BallisticResult CalculateForWeapon(Coordinates gun,Coordinates target,WeaponMode weapon,Trajectory trajectory) {
    auto result=CalculateBallistics(gun,target);
    if(weapon==WeaponMode::Artillery) {
        // Keep map units, azimuth and distance identical to the original.
        result.elevation_mil=WeaponMil(std::hypot(result.dx_m,result.dy_m),weapon,trajectory);
        result.range_status=result.elevation_mil?RangeStatus::Ok:RangeStatus::TooFar;
    }
    return result;
}
}
