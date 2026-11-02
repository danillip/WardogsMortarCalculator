// Port of ocr_scanner_lite.py. Copyright (c) 2026 ProDexortie. MIT License.
#include "../Shared/WindowsIncludes.h"
#include "CoordinateRecognizer.h"
#include "Glyphs.h"
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Storage.Streams.h>
#include <algorithm>
#include <cmath>
#include <regex>
#include <set>
#include <tuple>
#include <functional>

using namespace winrt;
using namespace Windows::Graphics::Imaging;
using namespace Windows::Media::Ocr;
namespace wardogs {
Image MakePreview(Image const& source) {
    if(source.width<=0||source.height<=0||source.bgra.size()!=static_cast<size_t>(source.width)*source.height*4)return {};
    Image result{64,48,std::vector<uint8_t>(64*48*4)};
    for(int y=0;y<48;++y)for(int x=0;x<64;++x){auto from=(static_cast<size_t>(y*source.height/48)*source.width+x*source.width/64)*4;std::copy_n(source.bgra.data()+from,4,result.bgra.data()+(y*64+x)*4);}
    return result;
}
Image FromBitmap(SoftwareBitmap const& source) {
    auto bitmap = SoftwareBitmap::Convert(source,BitmapPixelFormat::Bgra8,BitmapAlphaMode::Premultiplied);
    Image image{bitmap.PixelWidth(),bitmap.PixelHeight(),{}};
    const auto length = static_cast<uint32_t>(image.width)*image.height*4;
    Windows::Storage::Streams::Buffer buffer(length); buffer.Length(length);
    bitmap.CopyToBuffer(buffer);
    image.bgra.assign(buffer.data(),buffer.data()+length);
    return image;
}
SoftwareBitmap ToBitmap(Image const& image) {
    Windows::Storage::Streams::Buffer buffer(static_cast<uint32_t>(image.bgra.size()));
    buffer.Length(static_cast<uint32_t>(image.bgra.size()));
    std::copy(image.bgra.begin(),image.bgra.end(),buffer.data());
    return SoftwareBitmap::CreateCopyFromBuffer(buffer,BitmapPixelFormat::Bgra8,image.width,image.height,BitmapAlphaMode::Premultiplied);
}
Image Crop(Image const& src,double anchorX,double anchorY,int size) {
    const int width=std::min(size,src.width),height=std::min(size,src.height);
    const int left=std::clamp(static_cast<int>(anchorX*src.width-size/2*.85),0,src.width-width);
    const int top=std::clamp(static_cast<int>(anchorY*src.height-size/2*1.15),0,src.height-height);
    Image out{width,height,std::vector<uint8_t>(static_cast<size_t>(width)*height*4)};
    for(int y=0;y<height;++y) std::copy_n(src.bgra.data()+(static_cast<size_t>(top+y)*src.width+left)*4,width*4,out.bgra.data()+static_cast<size_t>(y)*width*4);
    return out;
}
namespace {
struct Box {int x0,y0,x1,y1; int width()const{return x1-x0+1;} int height()const{return y1-y0+1;}};
struct Candidate {double y,value;Box box;};
struct ParseState {std::optional<double> x,y;std::vector<Candidate> candidates;std::set<std::wstring> seen;Image* preview;};
void Mark(Image& im,Box box,bool x) {
    for(int y=std::max(0,box.y0);y<=std::min(im.height-1,box.y1);++y)
        for(int col=std::max(0,box.x0);col<=std::min(im.width-1,box.x1);++col)
            if(y<=box.y0+1||y>=box.y1-1||col<=box.x0+1||col>=box.x1-1) {
                auto i=(static_cast<size_t>(y)*im.width+col)*4;
                im.bgra[i]=x?180:248;im.bgra[i+1]=x?255:189;im.bgra[i+2]=x?0:56;im.bgra[i+3]=255;
            }
}
void Parse(OcrResult const& result,int scale,ParseState& s) {
    static const std::wregex spaces(L"([xXyYvV0-9.])\\s+(?=[0-9.])");
    static const std::wregex rx(L"[xX]([0-9]{1,3}\\.[0-9]{2}|[0-9]{4,5})\\b");
    static const std::wregex ry(L"[yYvV]([0-9]{1,3}\\.[0-9]{2}|[0-9]{4,5})\\b");
    static const std::wregex dec(L"\\b([0-9]{1,3}\\.[0-9]{2})\\b");
    for(auto const& line:result.Lines()) {
        std::wstring text(line.Text());
        for(auto& c:text) {if(c==L',' )c=L'.';if(c==L'х'||c==L'Х'||c==L'×')c=L'x';if(c==L'у'||c==L'У')c=L'y';}
        // Repeated replacement also removes spaces between successive digits.
        for(;;){auto next=std::regex_replace(text,spaces,L"$1");if(next==text)break;text=std::move(next);}
        Box box{100000,100000,0,0};
        for(auto const& word:line.Words()){auto r=word.BoundingRect();box.x0=std::min(box.x0,static_cast<int>(r.X/scale));box.y0=std::min(box.y0,static_cast<int>(r.Y/scale));box.x1=std::max(box.x1,static_cast<int>((r.X+r.Width)/scale));box.y1=std::max(box.y1,static_cast<int>((r.Y+r.Height)/scale));}
        std::wsmatch m;
        auto value=[](std::wstring str){double v=std::stod(str);return str.find(L'.')==std::wstring::npos&&v>=1000?v/100:v;};
        if(!s.x&&std::regex_search(text,m,rx)){s.x=value(m[1]);Mark(*s.preview,box,true);continue;}
        if(!s.y&&std::regex_search(text,m,ry)){s.y=value(m[1]);Mark(*s.preview,box,false);continue;}
        if(std::regex_search(text,m,dec)&&s.seen.insert(std::to_wstring(box.y0/4)+L":"+m[1].str()).second)
            s.candidates.push_back({(box.y0+box.y1)*.5,value(m[1]),box});
    }
}
std::vector<uint8_t> Achromatic(Image const& image) {
    std::vector<uint8_t> out(static_cast<size_t>(image.width)*image.height);
    for(size_t i=0;i<out.size();++i) {
        auto p=image.bgra.data()+i*4;int lo=std::min({p[0],p[1],p[2]}),hi=std::max({p[0],p[1],p[2]});
        out[i]=static_cast<uint8_t>(std::max(0,lo-static_cast<int>((hi-lo)*.15)));
    }
    return out;
}
// Separable bicubic (a=-0.5) and Lanczos3 kernels, matching the original filters.
double Kernel(double x,bool lanczos) {
    x=std::abs(x);
    if(lanczos){if(x<1e-10)return 1;if(x>=3)return 0;const double p=3.141592653589793*x;return std::sin(p)*std::sin(p/3)/(p*p/3);}
    if(x<1)return ((1.5*x-2.5)*x)*x+1;
    if(x<2)return ((-.5*x+2.5)*x-4)*x+2;
    return 0;
}
Image Resize(Image const& src,int scale,bool lanczos,std::function<bool()> const& cancelled) {
    Image out{src.width*scale,src.height*scale,std::vector<uint8_t>(static_cast<size_t>(src.width)*src.height*scale*scale*4)};
    const int radius=lanczos?3:2;
    for(int y=0;y<out.height;++y)for(int x=0;x<out.width;++x) {
        if(x==0&&cancelled())throw hresult_canceled();
        double sx=(x+.5)/scale-.5,sy=(y+.5)/scale-.5;double sum[3]{},weight=0;
        for(int j=static_cast<int>(std::floor(sy))-radius+1;j<=static_cast<int>(std::floor(sy))+radius;++j)
            for(int i=static_cast<int>(std::floor(sx))-radius+1;i<=static_cast<int>(std::floor(sx))+radius;++i){
                double w=Kernel(sx-i,lanczos)*Kernel(sy-j,lanczos);weight+=w;
                auto p=src.bgra.data()+(static_cast<size_t>(std::clamp(j,0,src.height-1))*src.width+std::clamp(i,0,src.width-1))*4;
                for(int c=0;c<3;++c)sum[c]+=w*p[c];
            }
        auto p=out.bgra.data()+(static_cast<size_t>(y)*out.width+x)*4;
        for(int c=0;c<3;++c)p[c]=static_cast<uint8_t>(std::clamp(std::lround(sum[c]/weight),0L,255L));p[3]=255;
    }
    return out;
}
Image GrayImage(std::vector<uint8_t> const& gray,int w,int h) {
    Image out{w,h,std::vector<uint8_t>(gray.size()*4)};
    for(size_t i=0;i<gray.size();++i){out.bgra[i*4]=out.bgra[i*4+1]=out.bgra[i*4+2]=gray[i];out.bgra[i*4+3]=255;}return out;
}
// Templates and geometric fallback live in a separate file to keep OCR passes readable.
#include "Geometric.inl"
}
Windows::Foundation::IAsyncAction RecognizeAsync(Image image,std::shared_ptr<ScanOutput> output) {
    auto cancel=co_await get_cancellation_token();cancel.enable_propagation();
    co_await resume_background();
    if(cancel())throw hresult_canceled();
    auto engine=OcrEngine::TryCreateFromLanguage(Windows::Globalization::Language(L"en-US"));
    if(!engine)throw hresult_error(E_FAIL,L"OCR English language pack is missing");
    output->preview=image;ParseState state{{},{},{},{},&output->preview};
    auto gray=Achromatic(image);
    if(image.width*3>static_cast<int>(OcrEngine::MaxImageDimension())||image.height*3>static_cast<int>(OcrEngine::MaxImageDimension()))throw hresult_error(E_INVALIDARG,L"OCR region is too large");
    for(int pass=0;pass<4&&(!state.x||!state.y);++pass){
        if(cancel())throw hresult_canceled();
        int scale=pass==3?2:3; Image prepared;
        if(pass<2){prepared=Resize(GrayImage(gray,image.width,image.height),scale,false,[cancel]{return cancel();});int threshold=pass==0?125:145;for(size_t i=0;i<prepared.bgra.size();i+=4){uint8_t v=prepared.bgra[i]>=threshold?0:255;prepared.bgra[i]=prepared.bgra[i+1]=prepared.bgra[i+2]=v;}}
        else if(pass==2){
            std::array<int,256> histogram{};for(auto v:gray)++histogram[v];int cutoff=static_cast<int>(gray.size()/100),lo=0,hi=255,count=0;
            while(lo<255&&count+histogram[lo]<=cutoff)count+=histogram[lo++];count=0;while(hi>lo&&count+histogram[hi]<=cutoff)count+=histogram[hi--];
            auto smooth=gray;for(auto& v:smooth)v=static_cast<uint8_t>(255-std::clamp((v-lo)*255/std::max(1,hi-lo),0,255));
            prepared=Resize(GrayImage(smooth,image.width,image.height),scale,false,[cancel]{return cancel();});
        }else prepared=Resize(image,scale,true,[cancel]{return cancel();});
        auto result=co_await engine.RecognizeAsync(ToBitmap(prepared));
        if(cancel())throw hresult_canceled();Parse(result,scale,state);
    }
    for(int threshold:{145,120})if(!state.x||!state.y){if(cancel())throw hresult_canceled();Geometric(gray,image.width,image.height,threshold,state);}
    if((!state.x||!state.y)&&!state.candidates.empty()){
        std::stable_sort(state.candidates.begin(),state.candidates.end(),[](auto a,auto b){return a.y<b.y;});
        if(!state.x&&!state.y&&state.candidates.size()>=2){state.y=state.candidates[0].value;state.x=state.candidates[1].value;Mark(output->preview,state.candidates[0].box,false);Mark(output->preview,state.candidates[1].box,true);}
        else if(!state.y&&state.x){state.y=state.candidates.front().value;Mark(output->preview,state.candidates.front().box,false);}
        else if(!state.x&&state.y){state.x=state.candidates.back().value;Mark(output->preview,state.candidates.back().box,true);}
    }
    if(cancel())throw hresult_canceled();
    if(state.x&&state.y&&*state.x>=0&&*state.y>=0&&*state.x<=999.99&&*state.y<=999.99)output->coordinates=Coordinates{*state.x,*state.y};
}
}
