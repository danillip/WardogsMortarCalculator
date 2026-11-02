void Geometric(std::vector<uint8_t> const& gray,int w,int h,int threshold,ParseState& s) {
    std::vector<bool> visited(static_cast<size_t>(w)*h);std::vector<Box> blobs;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        auto index=static_cast<size_t>(y)*w+x;if(visited[index]||gray[index]<threshold)continue;
        std::vector<std::pair<int,int>> q{{x,y}};visited[index]=true;Box b{x,y,x,y};int count=0;
        while(!q.empty()){
            auto [cx,cy]=q.back();q.pop_back();++count;b.x0=std::min(b.x0,cx);b.y0=std::min(b.y0,cy);b.x1=std::max(b.x1,cx);b.y1=std::max(b.y1,cy);
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){int nx=cx+dx,ny=cy+dy;if(nx<0||nx>=w||ny<0||ny>=h)continue;auto n=static_cast<size_t>(ny)*w+nx;if(!visited[n]&&gray[n]>=threshold){visited[n]=true;q.emplace_back(nx,ny);}}
        }
        if((b.width()>=4&&b.width()<=14&&b.height()>=9&&b.height()<=24)||(b.width()>=1&&b.width()<=4&&b.height()>=1&&b.height()<=4&&count>=2))blobs.push_back(b);
    }
    std::vector<Box> tall,dots;for(auto b:blobs){if(b.height()>=10&&b.height()<=22&&b.width()>=4&&b.width()<=13)tall.push_back(b);if(b.width()<=4&&b.height()<=4)dots.push_back(b);}
    std::stable_sort(tall.begin(),tall.end(),[](auto a,auto b){return std::pair{a.y0/12,a.x0}<std::pair{b.y0/12,b.x0};});
    std::vector<std::vector<Box>> lines;
    for(auto b:tall){bool placed=false;for(auto& line:lines){auto last=line.back();if(std::abs(b.y0-last.y0)<=6&&b.x0-last.x1>=0&&b.x0-last.x1<=16){line.push_back(b);placed=true;break;}}if(!placed)lines.push_back({b});}
    for(auto const& line:lines){
        if(line.size()<4)continue;int baseline=0;for(size_t i=1;i<line.size();++i)baseline=std::max(baseline,line[i].y1);
        auto dot=std::find_if(dots.begin(),dots.end(),[&](Box b){return line.front().x0<b.x0&&b.x0<line.back().x1&&std::abs(b.y1-baseline)<=4;});if(dot==dots.end())continue;
        const auto first=line.front();bool axisX=first.y1>=baseline+2?false:first.y0>=line[1].y0+2?true:first.height()<line[1].height();
        std::string integer,fraction;
        for(size_t n=1;n<line.size();++n){
            auto b=line[n];char ch='1';
            if(static_cast<double>(b.width())/b.height()>.44){
                std::array<int,135> values{};int lo=255,hi=0;
                for(int r=0;r<15;++r)for(int c=0;c<9;++c){
                    double px=std::clamp((c+.5)*b.width()/9-.5,0.,static_cast<double>(b.width()-1)),py=std::clamp((r+.5)*b.height()/15-.5,0.,static_cast<double>(b.height()-1));
                    int x0=static_cast<int>(px),y0=static_cast<int>(py),x1=std::min(x0+1,b.width()-1),y1=std::min(y0+1,b.height()-1);double fx=px-x0,fy=py-y0;
                    auto at=[&](int x,int y){return gray[static_cast<size_t>(b.y0+y)*w+b.x0+x];};
                    int v=static_cast<int>(std::lround((1-fy)*((1-fx)*at(x0,y0)+fx*at(x1,y0))+fy*((1-fx)*at(x0,y1)+fx*at(x1,y1))));values[r*9+c]=v;lo=std::min(lo,v);hi=std::max(hi,v);
                }
                int local=std::max(100,static_cast<int>((lo+hi)*.52)),best=-1;
                for(int digit=0;digit<10;++digit){int score=0;for(int r=0;r<15;++r)for(int c=0;c<9;++c)score+=((values[r*9+c]>=local)==(Glyphs[digit][r][c]=='#'));if(score>best){best=score;ch=static_cast<char>('0'+digit);}}
            }
            if(b.x1<dot->x0)integer+=ch;else if(b.x0>dot->x1)fraction+=ch;
        }
        if(integer.empty()||fraction.size()<2)continue;double value=std::stod(integer+"."+fraction.substr(0,2));
        Box bounds=line.front();for(auto b:line){bounds.x0=std::min(bounds.x0,b.x0);bounds.y0=std::min(bounds.y0,b.y0);bounds.x1=std::max(bounds.x1,b.x1);bounds.y1=std::max(bounds.y1,b.y1);}
        if(axisX&&!s.x){s.x=value;Mark(*s.preview,bounds,true);}else if(!axisX&&!s.y){s.y=value;Mark(*s.preview,bounds,false);}
    }
}
