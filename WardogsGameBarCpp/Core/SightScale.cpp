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
}
