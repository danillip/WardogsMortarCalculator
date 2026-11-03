#include "../Shared/WindowsIncludes.h"
#include "CaptureService.h"
#include <algorithm>
#include <stdexcept>
namespace wardogs {
Image CaptureRegion(int left,int top,int width,int height) {
    const int vx=GetSystemMetrics(SM_XVIRTUALSCREEN),vy=GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw=GetSystemMetrics(SM_CXVIRTUALSCREEN),vh=GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if(width<=0||height<=0||width>4096||height>4096||left<vx||top<vy||
        static_cast<int64_t>(left)+width>static_cast<int64_t>(vx)+vw||
        static_cast<int64_t>(top)+height>static_cast<int64_t>(vy)+vh)
        throw std::runtime_error("Capture rectangle is outside the desktop");
    struct ScreenDC {HDC value=GetDC(nullptr);~ScreenDC(){if(value)ReleaseDC(nullptr,value);}} screen;
    if(!screen.value)winrt::throw_last_error();
    struct MemoryDC {HDC value{};~MemoryDC(){if(value)DeleteDC(value);}} memory{CreateCompatibleDC(screen.value)};
    if(!memory.value)winrt::throw_last_error();
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* pixels{};
    struct Bitmap {HBITMAP value{};~Bitmap(){if(value)DeleteObject(value);}} bitmap{CreateDIBSection(screen.value,&info,DIB_RGB_COLORS,&pixels,nullptr,0)};
    if(!bitmap.value||!pixels)winrt::throw_last_error();
    struct Selected {HDC dc;HGDIOBJ old;~Selected(){if(old&&old!=HGDI_ERROR)SelectObject(dc,old);}} selected{memory.value,SelectObject(memory.value,bitmap.value)};
    if(!selected.old||selected.old==HGDI_ERROR)winrt::throw_last_error();
    if(!BitBlt(memory.value,0,0,width,height,screen.value,left,top,SRCCOPY|CAPTUREBLT))winrt::throw_last_error();
    GdiFlush();Image image{width,height,{}};auto begin=static_cast<uint8_t*>(pixels);image.bgra.assign(begin,begin+static_cast<size_t>(width)*height*4);
    for(size_t i=3;i<image.bgra.size();i+=4)image.bgra[i]=255;
    return image;
}
Image CaptureAroundCursor(int size) {
    size=std::clamp(size,160,640);
    POINT cursor{};if(!GetCursorPos(&cursor))winrt::throw_last_error();
    const int vx=GetSystemMetrics(SM_XVIRTUALSCREEN),vy=GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width=std::min(size,GetSystemMetrics(SM_CXVIRTUALSCREEN));
    const int height=std::min(size,GetSystemMetrics(SM_CYVIRTUALSCREEN));
    if(width<=0||height<=0)throw std::runtime_error("No desktop to capture");
    const int left=std::clamp(static_cast<int>(cursor.x)-static_cast<int>(size/2*.85),vx,vx+GetSystemMetrics(SM_CXVIRTUALSCREEN)-width);
    const int top=std::clamp(static_cast<int>(cursor.y)-static_cast<int>(size/2*1.15),vy,vy+GetSystemMetrics(SM_CYVIRTUALSCREEN)-height);
    return CaptureRegion(left,top,width,height);
}
}
