#include "../Shared/WindowsIncludes.h"
#include "SightRecognizer.h"
#include <winrt/Windows.Globalization.h>
#include <cwctype>
#include <functional>

using namespace winrt;
namespace wardogs {
namespace {
using Cancelled=std::function<bool()>;

// Local contrast retains black lettering on dark walls. A fixed threshold of
// 100 merged the lettering and much of the scenery into one black component.
Image InkMask(Image const& image,Cancelled const& cancelled,bool light=false) {
    const int width=image.width,height=image.height,stride=width+1;
    std::vector<int> gray(static_cast<size_t>(width)*height);
    std::vector<int64_t> integral(static_cast<size_t>(stride)*(height+1));
    for(int y=0;y<height;++y) {
        if(cancelled())throw hresult_canceled();
        int64_t row=0;
        for(int x=0;x<width;++x) {
            auto pixel=image.bgra.data()+(static_cast<size_t>(y)*width+x)*4;
            const int value=(pixel[0]*29+pixel[1]*150+pixel[2]*77)/256;
            gray[static_cast<size_t>(y)*width+x]=value;row+=value;
            integral[static_cast<size_t>(y+1)*stride+x+1]=integral[static_cast<size_t>(y)*stride+x+1]+row;
        }
    }
    const int radius=std::max(4,static_cast<int>(std::lround(8.*height/520.)));
    Image mask{width,height,std::vector<uint8_t>(image.bgra.size())};
    for(int y=0;y<height;++y) {
        if(cancelled())throw hresult_canceled();
        const int top=std::max(0,y-radius),bottom=std::min(height,y+radius+1);
        for(int x=0;x<width;++x) {
            const int left=std::max(0,x-radius),right=std::min(width,x+radius+1);
            const int64_t sum=integral[static_cast<size_t>(bottom)*stride+right]-integral[static_cast<size_t>(top)*stride+right]
                -integral[static_cast<size_t>(bottom)*stride+left]+integral[static_cast<size_t>(top)*stride+left];
            const double mean=static_cast<double>(sum)/((right-left)*(bottom-top));
            const int value=gray[static_cast<size_t>(y)*width+x];
            const bool ink=light?(value>140&&value-mean>=std::max(6.,(255-mean)*.13)):
                (value<100&&((mean-value)>=std::max(6.,mean*.13)||(value<=20&&mean-value>=3)));
            auto pixel=mask.bgra.data()+(static_cast<size_t>(y)*width+x)*4;
            pixel[0]=pixel[1]=pixel[2]=ink?0:255;pixel[3]=255;
        }
    }
    return mask;
}
bool Dark(Image const& mask,int x,int y) {
    return mask.bgra[(static_cast<size_t>(y)*mask.width+x)*4]==0;
}
std::optional<std::pair<double,double>> FindTick(Image const& mask,double expectedX,double textY,double scale) {
    const int start=std::clamp(static_cast<int>(expectedX-12*scale),0,mask.width-1);
    const int end=std::min(mask.width-1,static_cast<int>(expectedX+35*scale));
    const int top=std::max(0,static_cast<int>(textY-8*scale));
    const int bottom=std::min(mask.height-1,static_cast<int>(textY+8*scale));
    double bestScore=1e9;std::optional<std::pair<double,double>> best;
    for(int y=top;y<=bottom;++y)for(int x=start;x<end;++x) {
        if(!Dark(mask,x,y)||(x>0&&Dark(mask,x-1,y)))continue;
        int right=x;while(right<mask.width&&Dark(mask,right,y))++right;
        const int length=right-x;
        if(length<13*scale||length>32*scale||right>end)continue;
        const int reach=std::max(3,static_cast<int>(5*scale));
        if(y<reach||y+reach>=mask.height)continue;
        bool stem=false;
        for(int stemX=x;stemX<=std::min(x+2,right-1);++stemX) {
            int vertical=0;
            for(int dy=-reach;dy<=reach;++dy)if(Dark(mask,stemX,y+dy))++vertical;
            if(vertical>=(2*reach+1)*.75)stem=true;
        }
        if(!stem)continue;
        const double score=std::abs(y-textY)+std::abs(x-expectedX)*.25+std::abs(length-20*scale)*.1;
        if(score<bestScore){bestScore=score;best=std::pair<double,double>{x,static_cast<double>(y)};}
        x=right;
    }
    if(best)return best;
    // A tick touching a wall/roof edge is not an isolated horizontal run.
    // Score its T shape instead, allowing a few antialiasing gaps. Keep the
    // vertical stem and clear flanks mandatory to reject solid scenery patches.
    const int arm=std::max(8,static_cast<int>(std::lround(18*scale)));
    const int reach=std::max(3,static_cast<int>(std::lround(6*scale)));
    const int x0=std::max(0,static_cast<int>(expectedX-5*scale));
    const int x1=std::min(mask.width-arm-2,static_cast<int>(expectedX+5*scale));
    for(int y=std::max(top,reach);y<=std::min(bottom,mask.height-reach-1);++y)for(int x=x0;x<=x1;++x) {
        int horizontal=0,vertical=0,clear=0;
        for(int k=1;k<=arm;++k)if(Dark(mask,x+k,y)||Dark(mask,x+k,y-1)||Dark(mask,x+k,y+1))++horizontal;
        for(int column=x;column<=x+2;++column) {
            int hits=0;for(int dy=-reach;dy<=reach;++dy)if(Dark(mask,column,y+dy))++hits;
            vertical=std::max(vertical,hits);
        }
        for(int k=3;k<=arm;++k){if(!Dark(mask,x+k,y-reach))++clear;if(!Dark(mask,x+k,y+reach))++clear;}
        const double h=static_cast<double>(horizontal)/arm,v=static_cast<double>(vertical)/(reach*2+1);
        const double flank=static_cast<double>(clear)/((arm-2)*2);
        if(h<.8||v<.65||flank<.4)continue;
        const double score=(1-h)*5+(1-v)*3+(1-flank)+std::abs(y-textY)*.2+std::abs(x-expectedX)*.1;
        if(score<bestScore){bestScore=score;best=std::pair<double,double>{x,static_cast<double>(y)};}
    }
    return best;
}
std::optional<int> ReadNumber(std::wstring text) {
    text.erase(std::remove_if(text.begin(),text.end(),[](wchar_t c){return iswspace(c)!=0;}),text.end());
    // Range readers accept an omitted unit only for exact calibration entries.
    while(!text.empty()&&(text.back()==L'|'||text.back()==L'.'||text.back()==L':'||text.back()==L'-'||text.back()==L'+'))text.pop_back();
    if(!text.empty()&&(text.back()==L'M'||text.back()==L'm'||text.back()==L'М'||text.back()==L'м'))text.pop_back();
    if(text.empty()||text.size()>4)return {};
    int value=0;
    for(auto c:text) {
        if(c==L'O'||c==L'o')c=L'0';else if(c==L'I'||c==L'l'||c==L'|')c=L'1';
        else if(c==L'S'||c==L's')c=L'5';else if(c==L'B')c=L'8';
        if(c<L'0'||c>L'9')return {};value=value*10+c-L'0';
    }
    return value;
}
void AddUnique(std::vector<SightLabel>& labels,SightLabel label,double tolerance) {
    for(auto const& prior:labels)if(prior.distance==label.distance&&std::abs(prior.y-label.y)<=tolerance)return;
    labels.push_back(label);
}
void ReadLabels(winrt::Windows::Media::Ocr::OcrResult const& result,Image const& mask,int factor,
    double expectedTickX,double scale,SightRequest const& request,SightOutput& output) {
    for(auto const& line:result.Lines()) {
        std::wstring text;double top=1e9,bottom=0;
        for(auto const& word:line.Words()) {
            text+=word.Text();
            auto box=word.BoundingRect();top=std::min(top,static_cast<double>(box.Y/factor));
            bottom=std::max(bottom,static_cast<double>((box.Y+box.Height)/factor));
        }
        auto distance=ReadNumber(text);if(!distance||bottom-top<4*scale||bottom-top>25*scale)continue;
        auto mil=LabelMil(*distance,request.weapon,request.trajectory);if(!mil)continue;
        const double y=(top+bottom)*.5;
        AddUnique(output.textLabels,{static_cast<double>(*distance),y,expectedTickX,mil},8*scale);
        auto tick=FindTick(mask,expectedTickX,y,scale);if(!tick)continue;
        AddUnique(output.labels,{static_cast<double>(*distance),tick->second,tick->first,mil},5*scale);
    }
}
Image Prepare(Image const& source,int left,int right,int factor,Cancelled const& cancelled) {
    Image result{(right-left)*factor,source.height*factor,std::vector<uint8_t>(static_cast<size_t>(right-left)*source.height*factor*factor*4)};
    // Bilinear enlargement preserves antialiased digit edges.
    for(int y=0;y<result.height;++y) {
        if(cancelled())throw hresult_canceled();
        const double sy=(y+.5)/factor-.5;
        const int y0=std::clamp(static_cast<int>(std::floor(sy)),0,source.height-1),y1=std::min(source.height-1,y0+1);
        const double fy=std::clamp(sy-y0,0.,1.);
        for(int x=0;x<result.width;++x) {
            const double sx=left+(x+.5)/factor-.5;
            const int x0=std::clamp(static_cast<int>(std::floor(sx)),left,right-1),x1=std::min(right-1,x0+1);
            const double fx=std::clamp(sx-x0,0.,1.);
            auto target=result.bgra.data()+(static_cast<size_t>(y)*result.width+x)*4;
            for(int c=0;c<3;++c) {
                auto at=[&](int px,int py){return source.bgra[(static_cast<size_t>(py)*source.width+px)*4+c];};
                target[c]=static_cast<uint8_t>(std::lround((at(x0,y0)*(1-fx)+at(x1,y0)*fx)*(1-fy)+(at(x0,y1)*(1-fx)+at(x1,y1)*fx)*fy));
            }
            target[3]=255;
        }
    }
    return result;
}
Image Spectral(Image const& image,int mode,bool light,Cancelled const& cancelled) {
    if(mode==0)return image;
    if(mode==1)return InkMask(image,cancelled,light);
    if(mode==7)return InkMask(image,cancelled,!light);
    Image channel=image;
    for(int y=0;y<image.height;++y) {
        if(cancelled())throw hresult_canceled();
        for(int x=0;x<image.width;++x) {
            auto p=channel.bgra.data()+(static_cast<size_t>(y)*image.width+x)*4;
            uint8_t v=mode==2?p[2]:mode==3?p[1]:mode==4?p[0]:mode==5?std::min({p[0],p[1],p[2]}):std::max({p[0],p[1],p[2]});
            p[0]=p[1]=p[2]=v;
        }
    }
    return InkMask(channel,cancelled,light);
}
// Heading digits and strokes have separate polarity: SPH-2 has dark digits
// above a light ruler. Never OR the two masks: scenery would connect the ink.
std::optional<double> HeadingTick(Image const& mask,double textX,double scale) {
    const int x0=std::max(0,static_cast<int>(std::lround(textX-5*scale)));
    const int x1=std::min(mask.width-1,static_cast<int>(std::lround(textX+5*scale)));
    const int y0=std::max(0,static_cast<int>(std::lround(68*scale)));
    const int y1=std::min(mask.height-1,static_cast<int>(std::lround(78*scale)));
    const int rise=std::max(6,static_cast<int>(std::lround(18*scale)));
    const int arm=std::max(4,static_cast<int>(std::lround(7*scale)));
    double score=1e9;std::optional<double> best;
    for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
        if(y<rise||x<arm||x+arm>=mask.width)continue;
        int stem=0,base=0,clear=0;
        for(int k=2;k<=rise;++k) {
            bool hit=false;for(int dx=-1;dx<=1;++dx)if(x+dx>=0&&x+dx<mask.width&&Dark(mask,x+dx,y-k))hit=true;
            if(hit)++stem;
            if(k>=rise/2){if(!Dark(mask,x-arm,y-k))++clear;if(!Dark(mask,x+arm,y-k))++clear;}
        }
        for(int dx=-arm;dx<=arm;++dx) {
            bool hit=false;for(int dy=-1;dy<=1;++dy)if(y+dy>=0&&y+dy<mask.height&&Dark(mask,x+dx,y+dy))hit=true;
            if(hit)++base;
        }
        if(stem<(rise-1)*.75||base<(arm*2+1)*.65||clear<(rise-rise/2+1)*.7)continue;
        const double candidate=std::abs(x-textX)+std::abs(y-72*scale)*.25+(rise-1-stem)*.5;
        if(candidate<score){score=candidate;best=x;}
    }
    return best;
}
Image HeadingDigits(Image image,double scale) {
    // Keep the digit row only. A white horizontal ruler is not part of a glyph.
    const int bottom=std::clamp(static_cast<int>(std::lround(52*scale)),1,image.height);
    image.height=bottom;image.bgra.resize(static_cast<size_t>(image.width)*bottom*4);
    return image;
}
Image HeadingPreview(Image const& digits,Image const& strokes,double scale) {
    Image result=strokes;
    const int rows=std::min(result.height,static_cast<int>(std::lround(48*scale)));
    std::copy_n(digits.bgra.begin(),static_cast<size_t>(rows)*result.width*4,result.bgra.begin());
    return result;
}
std::vector<HeadingLabel> ReadHeading(winrt::Windows::Media::Ocr::OcrResult const& result,int factor,double scale) {
    std::vector<HeadingLabel> labels;
    for(auto const& line:result.Lines())for(auto const& word:line.Words()) {
        auto value=ReadNumber(std::wstring(word.Text()));auto box=word.BoundingRect();
        if(!value||*value>=360||*value%15!=0||box.Y/factor>55*scale||box.Height/factor<4*scale||box.Height/factor>22*scale)continue;
        labels.push_back({static_cast<double>(*value),(box.X+box.Width*.5)/factor});
    }
    return labels;
}
}
winrt::Windows::Foundation::IAsyncAction RecognizeSightAsync(Image image,Image heading,
    SightRequest request,std::shared_ptr<SightOutput> output) {
    auto cancel=co_await get_cancellation_token();cancel.enable_propagation();co_await resume_background();
    using winrt::Windows::Media::Ocr::OcrEngine;
    if(image.width<=0||image.height<=0||image.bgra.size()!=static_cast<size_t>(image.width)*image.height*4)throw hresult_error(E_INVALIDARG);
    const int factor=std::min(4,static_cast<int>(OcrEngine::MaxImageDimension())/std::max(image.width,image.height));
    if(factor<1)throw hresult_error(E_INVALIDARG,L"Sight capture is too large for Windows OCR");
    auto engine=OcrEngine::TryCreateFromLanguage(winrt::Windows::Globalization::Language(L"en-US"));
    if(!engine)throw hresult_error(E_FAIL,L"English Windows OCR is unavailable");
    output->preview=image;
    const double scale=image.height/520.;
    // Capture begins 410 px left of centre; ticks are 270 px left of centre.
    const double expectedTickX=(request.weapon==WeaponMode::Artillery?145:140)*scale;
    const int left=std::clamp(static_cast<int>(std::lround(expectedTickX-70*scale)),0,image.width-1);
    const int right=std::clamp(static_cast<int>(std::lround(expectedTickX-5*scale)),left+1,image.width);
    const bool light=request.weapon==WeaponMode::Artillery;
    auto normalMask=InkMask(image,[cancel]{return cancel();},light);
    constexpr wchar_t const* names[]{L"RGB",L"Local",L"Red",L"Green",L"Blue",L"Min RGB",L"Max RGB",L"Inverse"};
    // Min/max RGB suppress coloured scenery while retaining neutral HUD ink.
    constexpr int order[]{0,1,5,6,2,3,4,7};
    std::vector<SightOutput> candidates;
    size_t variants=0;
    const ULONGLONG start=GetTickCount64();
    for(int pass=0;pass<(request.tracking?4:8);++pass) {
        if(cancel())throw hresult_canceled();
        if(pass>=2&&GetTickCount64()-start>(request.tracking?1100:2800))break;
        const int mode=order[pass];
        auto source=Spectral(image,mode,light,[cancel]{return cancel();});
        auto const& mask=mode==0||mode==7?normalMask:source;
        auto prepared=Prepare(source,left,right,factor,[cancel]{return cancel();});
        auto recognized=co_await engine.RecognizeAsync(ToBitmap(prepared));
        if(cancel())throw hresult_canceled();++variants;
        SightOutput candidate;
        ReadLabels(recognized,mask,factor,expectedTickX,scale,request,candidate);
        candidate.solution=LocateSightWithText(candidate.labels,candidate.textLabels,request.targetMil,
            SightPixelsPerMil(request.weapon)*scale,std::max(2.,3*scale),RangeIncreasesUp(request.weapon,request.trajectory));
        candidate.processed=mode==0?normalMask:std::move(source);candidate.filter=names[mode];
        if(candidate.solution)candidates.push_back(std::move(candidate));
        else if(candidate.textLabels.size()>output->textLabels.size()) {
            output->textLabels=std::move(candidate.textLabels);output->labels=std::move(candidate.labels);
            output->processed=std::move(candidate.processed);output->filter=candidate.filter;
        }
    }
    size_t best=0,bestVotes=0;double bestScore=-1e9;
    for(size_t i=0;i<candidates.size();++i) {
        const auto& mark=*candidates[i].solution;size_t votes=0;
        for(auto const& other:candidates)if(std::abs(mark.y-other.solution->y)<=std::max(2.,3*scale)&&std::abs(mark.tickX-other.solution->tickX)<=4*scale)++votes;
        const double score=votes*100+mark.inliers*20+std::min(candidates[i].labels.size(),mark.inliers)*4+(mark.textAnchored?0:8)-mark.residual*4;
        if(score>bestScore){best=i;bestScore=score;bestVotes=votes;}
    }
    if(!candidates.empty()) {
        auto chosen=std::move(candidates[best]);
        output->labels=std::move(chosen.labels);output->textLabels=std::move(chosen.textLabels);
        output->processed=std::move(chosen.processed);output->filter=std::move(chosen.filter);
        if(bestVotes>=2)output->solution=chosen.solution;else output->conflict=true;
    }
    output->variants=variants;
    // OCR dark heading digits separately from the weapon's dark/light strokes.
    // Each candidate must anchor three labels to actual major ticks, then agree
    // with another image treatment. Raw OCR alone cannot establish the ruler.
    if(heading.width>0&&heading.height>0&&heading.bgra.size()==static_cast<size_t>(heading.width)*heading.height*4) {
        const int headingFactor=std::min(3,static_cast<int>(OcrEngine::MaxImageDimension())/std::max(heading.width,heading.height));
        struct HeadingCandidate {HeadingSolution solution;Image preview;std::wstring filter;};
        std::vector<HeadingCandidate> headings;
        auto headingMask=InkMask(heading,[cancel]{return cancel();},light);
        const auto headingStart=GetTickCount64();
        if(headingFactor>0)for(int pass=0;pass<(request.tracking?4:8);++pass) {
            if(cancel())throw hresult_canceled();
            if(pass>=2&&GetTickCount64()-headingStart>(request.tracking?650:1500))break;
            const int mode=order[pass];
            auto source=Spectral(heading,mode,false,[cancel]{return cancel();});
            // The digit filter may change polarity; tick detection must not.
            auto strokes=mode==0||mode==7?headingMask:Spectral(heading,mode,light,[cancel]{return cancel();});
            auto digits=HeadingDigits(source,scale);
            auto prepared=Prepare(digits,0,digits.width,headingFactor,[cancel]{return cancel();});
            auto recognized=co_await engine.RecognizeAsync(ToBitmap(prepared));
            if(cancel())throw hresult_canceled();
            std::vector<HeadingLabel> anchored;
            for(auto label:ReadHeading(recognized,headingFactor,scale)) {
                auto tick=HeadingTick(strokes,label.x,scale);if(!tick)continue;
                label.x=*tick;anchored.push_back(label);
            }
            auto solution=LocateHeading(anchored,request.targetAzimuth,heading.width*.5,83.*scale/15.,std::max(2.,3*scale));
            auto preview=HeadingPreview(mode==0?InkMask(heading,[cancel]{return cancel();},false):source,strokes,scale);
            if(output->headingPreview.bgra.empty()){output->headingPreview=preview;output->headingFilter=names[mode];}
            if(!solution)continue;
            headings.push_back({*solution,std::move(preview),names[mode]});
        }
        size_t headingVotes=0;double headingScore=-1e9;
        for(auto const& candidate:headings) {
            size_t votes=0;
            for(auto const& other:headings)if(std::abs(HeadingDelta(candidate.solution.current,other.solution.current))<=.7&&
                std::abs(candidate.solution.x-other.solution.x)<=std::max(2.,3*scale))++votes;
            const double score=votes*100+candidate.solution.inliers*20-candidate.solution.residual*4;
            if(votes>=2&&score>headingScore) {
                headingVotes=votes;headingScore=score;output->heading=candidate.solution;
                output->headingPreview=candidate.preview;output->headingFilter=candidate.filter;
            }
        }
        output->headingConflict=!headings.empty()&&headingVotes<2;
    }
}
}
