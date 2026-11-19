#include "../Core/Calculator.h"
#include "../Core/SightScale.h"
#include "../Core/WeaponProfile.h"
#include "../Core/HeadingScale.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
using namespace wardogs;
void Require(bool condition,char const* name){if(!condition)throw std::runtime_error(name);}
void Near(double a,double b,char const* name){Require(std::abs(a-b)<1e-8,name);}
int main(int argc,char** argv) {
 try{
    // Expectations transcribed from upstream tests/test_ballistics.py.
    for(auto [distance,mil]:MortarTable){auto [actual,status]=InterpolateMil(distance);Require(status==RangeStatus::Ok&&actual&&*actual==mil,"table node");}
    Near(*InterpolateMil(95).first,925,"95m midpoint");Near(*InterpolateMil(450).first,525,"450m midpoint");
    Require(InterpolateMil(79.9).second==RangeStatus::TooClose,"low bound");Require(InterpolateMil(684.1).second==RangeStatus::TooFar,"high bound");
    struct Case{Coordinates gun,target;double azimuth,distance,mil;};
    for(auto c:{Case{{100,100},{100,103.4},0,340,650},Case{{100,100},{104.7,100},90,470,500},Case{{100,100},{100,97.6},180,240,750},Case{{100,100},{98.9,100},270,110,900}}){auto r=CalculateBallistics(c.gun,c.target);Near(r.azimuth_deg,c.azimuth,"cardinal azimuth");Near(r.distance_m,c.distance,"cardinal distance");Require(r.elevation_mil.has_value(),"cardinal elevation present");Near(*r.elevation_mil,c.mil,"cardinal elevation");}
    auto diagonal=CalculateBallistics({50,50},{52,52});Near(diagonal.azimuth_deg,45,"diagonal azimuth");Near(diagonal.distance_m,283,"diagonal distance");
    auto zero=CalculateBallistics({98.9,109.79},{98.9,109.79});Near(zero.distance_m,0,"zero distance");Near(zero.azimuth_deg,0,"zero azimuth");Require(zero.range_status==RangeStatus::TooClose&&!zero.elevation_mil,"zero range");
    Near(RoundEven(2.5),2,"ties even down");Near(RoundEven(3.5),4,"ties even up");Near(RoundEven(-2.5),-2,"negative tie");
    Near(RoundPythonOneDecimal(2.55),2.5,"Python binary rounding 2.55");Near(RoundPythonOneDecimal(2.65),2.6,"Python binary rounding 2.65");
    Require(!ParseManualCoordinate(L"nan")&&!ParseManualCoordinate(L"inf")&&!ParseManualCoordinate(L"-1")&&!ParseManualCoordinate(L"1000")&&!ParseManualCoordinate(L"abc"),"invalid input");
    Near(*ParseManualCoordinate(L"X:98,90"),98.9,"manual comma");
    bool rejected=false;try{CalculateBallistics({std::numeric_limits<double>::infinity(),0},{1,1});}catch(std::invalid_argument const&){rejected=true;}Require(rejected,"finite validation");
    // Independent screen positions transcribed from the supplied 1920x1080 sight.
    std::vector<SightLabel> sight{{578,408,690},{545,510,690},{510,612,690},{470,714,690}};
    auto mark=LocateSightMark(sight,355,2.04,3);Require(mark&&mark->direction==SightDirection::Visible,"sight interpolation present");Near(mark->y,418.2,"sight interpolated pixel");Near(mark->tickX,690,"sight tick column");
    Near(LocateSightMark(sight,400,2.04,3)->y,510,"sight table node");
    Require(LocateSightMark(sight,200,2.04,3)->direction==SightDirection::IncreaseRange,"sight outside upper edge");
    Require(LocateSightMark(sight,900,2.04,3)->direction==SightDirection::DecreaseRange,"sight outside lower edge");
    Require(!LocateSightMark({sight[0],sight[1]},355,2.04,3),"sight needs three labels");
    Require(!LocateSightMark({sight[0],sight[0],sight[0]},355,2.04,3),"sight rejects duplicate labels");
    Require(!LocateSightMark({{578,408,690},{545,510,730},{510,612,770}},355,2.04,3),"sight rejects unrelated tick columns");
    Require(!LocateSightMark({{470,408,690},{510,510,690},{545,612,690}},355,2.04,3),"sight rejects inverted scale");
    Require(!LocateSightMark(sight,100,2.04,3),"sight rejects unavailable elevation");
    sight.push_back({80,600,690});Near(LocateSightMark(sight,355,2.04,3)->y,418.2,"sight ignores OCR outlier");
    for(auto& label:sight){label.y*=1.5;label.tickX*=1.5;}
    Near(LocateSightMark(sight,355,3.06,4.5)->y,627.3,"sight resolution scaling");
    // Three/four number labels remain readable when scenery hides most strokes.
    std::vector<SightLabel> words{{578,410,690},{545,512,690},{510,614,690},{470,716,690}};
    std::vector<SightLabel> oneTick{{578,408,690}};
    auto anchored=LocateSightWithText(oneTick,words,355,2.04,3);
    Require(anchored&&anchored->textAnchored,"sight uses numbers with one real stroke");Near(anchored->y,418.2,"sight compensates word box offset");
    auto direct=LocateSightWithText({{578,408,690},{545,510,690},{510,612,690}},words,355,2.04,3);
    Require(direct&&!direct->textAnchored,"sight prefers three real strokes");
    Require(!LocateSightWithText({},words,355,2.04,3),"sight needs a real anchor stroke");
    Require(!LocateSightWithText(oneTick,{words[0],words[1]},355,2.04,3),"sight still needs three numbers");
    Require(!LocateSightWithText({{578,430,690}},words,355,2.04,3),"sight rejects distant anchor stroke");
    Require(!LocateSightWithText({{578,408,720}},words,355,2.04,3),"sight rejects wrong anchor column");
    Require(!LocateSightWithText({{578,408,690},{545,515,690}},words,355,2.04,3),"sight rejects conflicting anchors");
    Require(!LocateSightWithText(oneTick,{words[0],words[0],words[0]},355,2.04,3),"sight rejects duplicate OCR numbers");
    auto outside=LocateSightWithText(oneTick,words,200,2.04,3);
    Require(outside&&outside->direction==SightDirection::IncreaseRange,"anchored sight still checks visible range");
    Near(*WeaponMil(734,WeaponMode::Artillery,Trajectory::Upper),1400,"SPH observed minimum");
    Near(*WeaponMil(2629,WeaponMode::Artillery,Trajectory::Upper),620,"SPH upper plateau boundary");
    Near(*WeaponMil(2629,WeaponMode::Artillery,Trajectory::Lower),600,"SPH lower plateau boundary");
    Require(!LabelMil(2629,WeaponMode::Artillery,Trajectory::Lower),"SPH repeated peak range cannot identify an OCR tick");
    Near(*WeaponMil(1574,WeaponMode::Artillery,Trajectory::Lower),100,"SPH observed lower branch");
    Require(!WeaponMil(2000,WeaponMode::Artillery,Trajectory::Lower),"SPH cannot interpolate through missing calibration");
    Require(!WeaponMil(733,WeaponMode::Artillery,Trajectory::Upper)&&!WeaponMil(2630,WeaponMode::Artillery,Trajectory::Upper),"SPH refuses extrapolation");
    auto art=CalculateForWeapon({100,100},{110,100},WeaponMode::Artillery,Trajectory::Upper);
    Near(art.distance_m,1000,"SPH retains map scale");Near(art.azimuth_deg,90,"SPH retains map north");Require(art.elevation_mil.has_value(),"SPH calibrated range");
    auto compass=LocateHeading({{345,100},{0,183},{15,266},{30,349}},359,183,83./15.,3);
    Require(compass&&compass->visible,"heading wraps across north");Near(HeadingDelta(0,compass->current),0,"heading current north");Near(compass->delta,-1,"heading shortest left turn");
    Near(HeadingDelta(1,359),2,"heading right turn across north");
    Require(!LocateHeading({{0,100},{0,100},{0,100}},10,183,83./15.,3),"heading needs distinct labels");
    Require(!LocateHeading({{0,300},{15,200},{30,100}},10,183,83./15.,3),"heading rejects inverted labels");
    auto farBearing=LocateHeading({{345,100},{0,183},{15,266},{30,349}},180,183,83./15.,3);
    Require(farBearing&&!farBearing->visible,"heading outside ruler uses arrow only");
    std::vector<SightLabel> artTicks{{780,410,145,1390},{824,278,145,1380},{868,146,145,1370}};
    auto artMark=LocateSightMark(artTicks,1385,13.2,3,true);Require(artMark.has_value(),"SPH 10-mil ruler");Near(artMark->y,344,"SPH ruler interpolation");
    // Optional CSV generated by the original Python implementation after the user's build.
    if(argc>1){std::ifstream file(argv[1]);Require(file.good(),"golden fixture file");std::string line;size_t count=0;while(std::getline(file,line)){if(line.empty()||line[0]=='#')continue;for(auto& ch:line)if(ch==',')ch=' ';std::istringstream row(line);Coordinates g,t;double distance,azimuth,mil,dx,dy;int status;Require(static_cast<bool>(row>>g.x>>g.y>>t.x>>t.y>>distance>>azimuth>>mil>>status>>dx>>dy),"fixture row");auto r=CalculateBallistics(g,t);Near(r.distance_m,distance,"golden distance");Near(r.azimuth_deg,azimuth,"golden azimuth");Require(static_cast<int>(r.range_status)==status,"golden range");if(status==0){Require(r.elevation_mil.has_value(),"golden elevation presence");Near(*r.elevation_mil,mil,"golden elevation");}else Require(!r.elevation_mil,"golden no elevation");Near(r.dx_m,dx,"golden dx");Near(r.dy_m,dy,"golden dy");++count;}Require(count>100,"fixture coverage");}
    std::cout<<"Core checks passed\n";return 0;
 }catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}
}
