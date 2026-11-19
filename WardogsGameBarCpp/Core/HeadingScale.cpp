#include "HeadingScale.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
namespace wardogs {
double HeadingDelta(double target,double current){return std::remainder(target-current,360.);}
std::optional<HeadingSolution> LocateHeading(std::vector<HeadingLabel> const& labels,
    double target,double centerX,double expectedPixelsPerDegree,double tolerance) {
    if(!std::isfinite(target)||!std::isfinite(centerX)||!std::isfinite(expectedPixelsPerDegree)||expectedPixelsPerDegree<=0||!std::isfinite(tolerance)||tolerance<=0)return {};
    struct Point {double angle,x;};std::vector<Point> points,best;
    double origin=0;bool haveOrigin=false;
    for(auto label:labels) {
        if(!std::isfinite(label.degrees)||label.degrees<0||label.degrees>=360||!std::isfinite(label.x))continue;
        if(!haveOrigin){origin=label.degrees;haveOrigin=true;}
        points.push_back({origin+HeadingDelta(label.degrees,origin),label.x});
    }
    double bestError=std::numeric_limits<double>::infinity();
    for(size_t i=0;i<points.size();++i)for(size_t j=i+1;j<points.size();++j) {
        auto a=points[i],b=points[j];if(a.angle==b.angle)continue;
        const double slope=(b.x-a.x)/(b.angle-a.angle),intercept=a.x-slope*a.angle;
        if(slope<expectedPixelsPerDegree*.75||slope>expectedPixelsPerDegree*1.25)continue;
        std::set<double> seen;std::vector<Point> group;double error=0;
        for(auto point:points) {
            const double residual=std::abs(point.x-intercept-slope*point.angle);
            if(residual<=tolerance&&seen.insert(point.angle).second){group.push_back(point);error+=residual;}
        }
        if(group.size()>best.size()||(group.size()==best.size()&&error<bestError)){best=std::move(group);bestError=error;}
    }
    if(best.size()<3)return {};
    double meanA=0,meanX=0;for(auto point:best){meanA+=point.angle;meanX+=point.x;}meanA/=best.size();meanX/=best.size();
    double numerator=0,denominator=0,first=1e9,last=-1e9;
    for(auto point:best){numerator+=(point.angle-meanA)*(point.x-meanX);denominator+=(point.angle-meanA)*(point.angle-meanA);first=std::min(first,point.x);last=std::max(last,point.x);}
    if(denominator<=0)return {};
    const double slope=numerator/denominator,intercept=meanX-slope*meanA;
    const double current=std::fmod((centerX-intercept)/slope+720.,360.),delta=HeadingDelta(target,current),x=centerX+slope*delta;
    double residual=0;for(auto point:best)residual=std::max(residual,std::abs(point.x-intercept-slope*point.angle));
    if(residual>tolerance)return {};
    return HeadingSolution{current,delta,x,x>=first&&x<=last,best.size(),residual};
}
}
