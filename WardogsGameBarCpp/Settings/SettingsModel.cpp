#include "SettingsManager.h"
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <set>
#include <stdexcept>
using namespace winrt::Windows::System;
namespace wardogs {
std::vector<VirtualKey> ParseHotkey(std::wstring const& input) {
    std::wstring text;for(auto c:input)if(!iswspace(c))text+=static_cast<wchar_t>(towupper(c));
    std::vector<VirtualKey> keys;size_t start=0;int mainKeys=0;
    while(start<text.size()){
        auto end=text.find(L'+',start);auto part=text.substr(start,end==std::wstring::npos?end:end-start);int key=0;
        if(part==L"CTRL")key=17;else if(part==L"SHIFT")key=16;else if(part==L"ALT")key=18;
        else if(part.size()==1&&((part[0]>=L'A'&&part[0]<=L'Z')||(part[0]>=L'0'&&part[0]<=L'9'))){key=part[0];++mainKeys;}
        else if(part.size()>=2&&part[0]==L'F'){auto number=part.substr(1);if(number.find_first_not_of(L"0123456789")==std::wstring::npos){int n=std::stoi(number);if(n>=1&&n<=12){key=111+n;++mainKeys;}}}
        else if(part.size()==4&&part.substr(0,3)==L"NUM"&&part[3]>=L'0'&&part[3]<=L'9'){key=96+part[3]-L'0';++mainKeys;}
        if(!key||std::find(keys.begin(),keys.end(),static_cast<VirtualKey>(key))!=keys.end())throw std::invalid_argument("Unsupported hotkey");
        keys.push_back(static_cast<VirtualKey>(key));if(end==std::wstring::npos)break;start=end+1;if(start==text.size())throw std::invalid_argument("Empty key");
    }
    if(keys.empty()||mainKeys!=1||keys.size()>4)throw std::invalid_argument("Unsupported hotkey");
    std::sort(keys.begin(),keys.end(),[](auto a,auto b){return static_cast<int>(a)<static_cast<int>(b);});return keys;
}
void NormalizeSettings(Settings& settings) {
    if(!std::isfinite(settings.opacity))settings.opacity=.88;
    settings.opacity=std::clamp(settings.opacity,.2,1.);
    settings.captureSize=std::clamp(settings.captureSize,160,640);
    try {
        std::set<std::vector<VirtualKey>> unique;
        for(auto const& key:settings.hotkeys)if(!unique.insert(ParseHotkey(key)).second)throw std::invalid_argument("Duplicate hotkey");
    } catch(...) { settings.hotkeys={L"F1",L"F2",L"F3",L"F4"}; }
}
}
