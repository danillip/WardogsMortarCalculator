#include "SightScale.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace wardogs {
std::optional<SightSolution> LocateSightMark(std::vector<SightLabel> const& labels,
    double targetMil,double pixelsPerMil,double tolerance) {
    if(!std::isfinite(targetMil)||targetMil<150||targetMil>950||
       !std::isfinite(pixelsPerMil)||pixelsPerMil<=0||!std::isfinite(tolerance)||tolerance<=0)return {};
    struct Point {double mil,y,x;};
    std::vector<Point> points;
    for(auto label:labels) {
        if(!std::isfinite(label.distance)||!std::isfinite(label.y)||!std::isfinite(label.tickX))continue;
        for(auto [distance,mil]:MortarTable)if(label.distance==distance)points.push_back({mil,label.y,label.tickX});
    }
    std::vector<Point> best;
    double bestError=std::numeric_limits<double>::infinity();
    for(size_t a=0;a<points.size();++a)for(size_t b=a+1;b<points.size();++b) {
        auto p=points[a],q=points[b];if(p.mil==q.mil)continue;
        const double slope=(q.y-p.y)/(q.mil-p.mil),intercept=p.y-slope*p.mil;
        if(slope<pixelsPerMil*.7||slope>pixelsPerMil*1.3)continue;
        std::vector<Point> inliers;std::set<double> seen;double error=0;
        for(auto point:points) {
            const double residual=std::abs(point.y-(intercept+slope*point.mil));
            if(residual<=tolerance&&std::abs(point.x-p.x)<=tolerance*2&&seen.insert(point.mil).second) {
                inliers.push_back(point);error+=residual;
            }
        }
        if(inliers.size()>best.size()||(inliers.size()==best.size()&&error<bestError)) {best=std::move(inliers);bestError=error;}
    }
    if(best.size()<3)return {};
    double meanM=0,meanY=0,meanX=0;
    for(auto point:best){meanM+=point.mil;meanY+=point.y;meanX+=point.x;}
    meanM/=best.size();meanY/=best.size();meanX/=best.size();
    double numerator=0,denominator=0;
    double first=std::numeric_limits<double>::infinity(),last=-first;
    for(auto point:best){numerator+=(point.mil-meanM)*(point.y-meanY);denominator+=(point.mil-meanM)*(point.mil-meanM);first=std::min(first,point.y);last=std::max(last,point.y);}
    if(denominator<=0)return {};
    const double slope=numerator/denominator,intercept=meanY-slope*meanM;
    for(auto point:best)if(std::abs(point.y-(intercept+slope*point.mil))>tolerance)return {};
    const double y=intercept+slope*targetMil;
    return SightSolution{y,meanX,first,last,y<first?SightDirection::IncreaseRange:y>last?SightDirection::DecreaseRange:SightDirection::Visible};
}
std::optional<SightSolution> LocateSightWithText(std::vector<SightLabel> const& ticks,
    std::vector<SightLabel> const& text,double targetMil,double pixelsPerMil,double tolerance) {
    if(auto direct=LocateSightMark(ticks,targetMil,pixelsPerMil,tolerance))return direct;
    if(text.size()<3||ticks.empty()||!std::isfinite(tolerance)||tolerance<=0)return {};
    // Word boxes sit slightly above/below the actual horizontal stroke. Use a
    // detected tick to measure that offset; never assume the OCR centre is exact.
    for(auto tick:ticks)for(auto anchor:text) {
        if(tick.distance!=anchor.distance||!std::isfinite(tick.y)||!std::isfinite(tick.tickX)||
           !std::isfinite(anchor.y)||!std::isfinite(anchor.tickX)||
           std::abs(tick.y-anchor.y)>tolerance*2||std::abs(tick.tickX-anchor.tickX)>tolerance*4)continue;
        std::vector<SightLabel> aligned;
        for(auto label:text) {
            if(std::abs(label.tickX-anchor.tickX)>tolerance*2)continue;
            aligned.push_back({label.distance,label.y+(tick.y-anchor.y),tick.tickX});
        }
        auto mark=LocateSightMark(aligned,targetMil,pixelsPerMil,tolerance);
        if(!mark||tick.y<mark->firstY-tolerance||tick.y>mark->lastY+tolerance)continue;
        bool consistent=true;
        for(auto check:ticks) {
            auto node=std::find_if(MortarTable.begin(),MortarTable.end(),[&](auto pair){return pair.first==check.distance;});
            if(node==MortarTable.end()||!std::isfinite(check.y)||!std::isfinite(check.tickX)){consistent=false;break;}
            auto predicted=LocateSightMark(aligned,node->second,pixelsPerMil,tolerance);
            if(!predicted||std::abs(predicted->y-check.y)>tolerance||std::abs(check.tickX-tick.tickX)>tolerance*2){consistent=false;break;}
        }
        if(consistent){mark->textAnchored=true;return mark;}
    }
    return {};
}
}
