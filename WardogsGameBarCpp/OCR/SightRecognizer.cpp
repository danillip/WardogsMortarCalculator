#include "../Shared/WindowsIncludes.h"
#include "SightRecognizer.h"
#include <winrt/Windows.Globalization.h>
#include <cwctype>
#include <functional>
#include <regex>

using namespace winrt;
namespace wardogs {
namespace {
bool Dark(Image const& image,int x,int y) {
    const auto p=image.bgra.data()+(static_cast<size_t>(y)*image.width+x)*4;
    return std::max({p[0],p[1],p[2]})<100;
}
// Find the actual horizontal tick beside the text instead of using its baseline.
std::optional<std::pair<double,double>> FindTick(Image const& image,double textRight,double textY,double textHeight) {
    const double scale=std::max(.6,textHeight/14.);
    // OCR sometimes includes the tick as a trailing word: also search behind its box.
    const int start=std::clamp(static_cast<int>(textRight-30*scale),0,image.width-1);
    const int end=std::min(image.width-1,static_cast<int>(textRight+45*scale));
    const int top=std::max(0,static_cast<int>(textY-5*scale));
    const int bottom=std::min(image.height-1,static_cast<int>(textY+5*scale));
    double bestScore=1e9;std::optional<std::pair<double,double>> best;
    for(int y=top;y<=bottom;++y)for(int x=start;x<end;++x) {
        if(!Dark(image,x,y)||(x>0&&Dark(image,x-1,y)))continue;
        int right=x;while(right<image.width&&Dark(image,right,y))++right;
        const int length=right-x;
        if(length<14*scale||length>32*scale)continue;
        // Both endpoints must be close to the label, not a background edge.
        if(right>end)continue;
        const int reach=std::max(3,static_cast<int>(5*scale));
        if(y<reach||y+reach>=image.height)continue;
        bool stem=false;
        for(int stemX=x;stemX<=std::min(x+2,right-1);++stemX)
            if(Dark(image,stemX,y-reach)&&Dark(image,stemX,y+reach))stem=true;
        if(!stem)continue;
        const double score=std::abs(y-textY)+std::abs(length-20*scale)*.1;
        if(score<bestScore){bestScore=score;best=std::pair<double,double>{x,static_cast<double>(y)};}
        x=right;
    }
    return best;
}
void ReadLabels(winrt::Windows::Media::Ocr::OcrResult const& result,Image const& image,int factor,SightOutput& output) {
    static const std::wregex meters(L"^([0-9]{2,3})[mMмМ][|Il:;.,+\\-]*$");
    for(auto const& line:result.Lines()) {
        std::wstring text;double left=1e9,right=0,top=1e9,bottom=0;
        for(auto const& word:line.Words()) {
            for(auto c:word.Text())if(!iswspace(c))text+=c;
            auto box=word.BoundingRect();left=std::min(left,static_cast<double>(box.X/factor));right=std::max(right,static_cast<double>((box.X+box.Width)/factor));
            top=std::min(top,static_cast<double>(box.Y/factor));bottom=std::max(bottom,static_cast<double>((box.Y+box.Height)/factor));
        }
        std::wsmatch match;if(!std::regex_match(text,match,meters))continue;
        const double distance=std::stod(match[1]);
        if(std::none_of(MortarTable.begin(),MortarTable.end(),[&](auto node){return node.first==distance;}))continue;
        if(right-left>image.width*.7||bottom-top<4)continue;
        auto tick=FindTick(image,right,(top+bottom)*.5,bottom-top);if(!tick)continue;
        bool duplicate=false;
        for(auto const& label:output.labels)if(label.distance==distance&&std::abs(label.y-tick->second)<5)duplicate=true;
        if(!duplicate)output.labels.push_back({distance,tick->second,tick->first});
    }
}
Image Prepare(Image const& source,int factor,bool threshold,std::function<bool()> const& cancelled) {
    Image result{source.width*factor,source.height*factor,std::vector<uint8_t>(static_cast<size_t>(source.width)*source.height*factor*factor*4)};
    for(int y=0;y<result.height;++y) {
        if(cancelled())throw hresult_canceled();
        for(int x=0;x<result.width;++x) {
            auto from=source.bgra.data()+(static_cast<size_t>(y/factor)*source.width+x/factor)*4;
            auto to=result.bgra.data()+(static_cast<size_t>(y)*result.width+x)*4;
            for(int c=0;c<3;++c)to[c]=threshold?(std::max({from[0],from[1],from[2]})<100?0:255):from[c];
            to[3]=255;
        }
    }
    return result;
}
}
winrt::Windows::Foundation::IAsyncAction RecognizeSightAsync(Image image,std::shared_ptr<SightOutput> output) {
    auto cancel=co_await get_cancellation_token();cancel.enable_propagation();co_await resume_background();
    using winrt::Windows::Media::Ocr::OcrEngine;
    if(image.width<=0||image.height<=0||image.bgra.size()!=static_cast<size_t>(image.width)*image.height*4)throw hresult_error(E_INVALIDARG);
    const int factor=std::min(3,static_cast<int>(OcrEngine::MaxImageDimension())/std::max(image.width,image.height));
    if(factor<1)throw hresult_error(E_INVALIDARG,L"Sight capture is too large for Windows OCR");
    auto engine=OcrEngine::TryCreateFromLanguage(winrt::Windows::Globalization::Language(L"en-US"));
    if(!engine)throw hresult_error(E_FAIL,L"English Windows OCR is unavailable");
    output->preview=image;
    for(bool threshold:{false,true}) {
        if(cancel())throw hresult_canceled();
        auto prepared=Prepare(image,factor,threshold,[cancel]{return cancel();});
        auto recognized=co_await engine.RecognizeAsync(ToBitmap(prepared));
        if(cancel())throw hresult_canceled();ReadLabels(recognized,image,factor,*output);
    }
}
}
