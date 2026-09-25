#include "TileTexture.hpp"
#include "TiledSpan.hpp"
#include "Texture.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace Renderer;

int main(){
    std::vector<uint16_t> palette(256);
    std::vector<uint8_t> tiles(4096),coarse(16*8),slots(4,255),cache(1024);
    for(unsigned i=0;i<256;++i)palette[i]=uint16_t((i%32)<<11|((i*3)%64)<<5|((i*7)%32));
    auto index=[](unsigned x,unsigned y){return uint8_t(x*13+y*7);};
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)
        tiles[((y/32)*2+x/32)*1024+(y%32)*32+x%32]=index(x,y);
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<16;++x)coarse[y*16+x]=index(x,y);
    TileTexture t;t.backing=tiles.data();t.palette=palette.data();t.width=t.height=64;
    t.pagesX=2;t.direct=true;t.coarse=coarse.data();t.coarseWidth=16;t.coarseHeight=8;
    auto bilinear=[&](int u,int v,unsigned w,unsigned h){
        const double x=std::clamp(u,0,1023)*(w-1)/1024.,y=std::clamp(v,0,1023)*(h-1)/1024.;
        const unsigned ix=unsigned(x),iy=unsigned(y),ex=std::min(ix+1,w-1),ey=std::min(iy+1,h-1);
        const double fx=x-ix,fy=y-iy;
        const uint16_t p[]={palette[index(ix,iy)],palette[index(ex,iy)],palette[index(ix,ey)],palette[index(ex,ey)]};
        unsigned result=0;
        for(unsigned channel=0;channel<3;++channel){const unsigned shift=channel==0?11:channel==1?5:0,mask=channel==1?63:31;
            const double a=(p[0]>>shift)&mask,b=(p[1]>>shift)&mask,c=(p[2]>>shift)&mask,d=(p[3]>>shift)&mask;
            result|=unsigned((a*(1-fx)+b*fx)*(1-fy)+(c*(1-fx)+d*fx)*fy)<<shift;
        }return uint16_t(result);
    };
    for(unsigned variant=0;variant<3;++variant){
        t.direct=variant!=1;t.coarseOnly=variant==2;
        t.slots=slots.data();t.cache=cache.data();slots[0]=0;std::copy_n(tiles.data(),1024,cache.data());
        const unsigned w=t.coarseOnly?16:64,h=t.coarseOnly?8:64;
        for(int v=-80;v<1100;v+=7)for(int u=-80;u<1100;u+=11){
            assert(t.sample(u,v)==palette[index(std::clamp(u,0,1023)*w/1024,std::clamp(v,0,1023)*h/1024)]);
            assert(t.sampleBilinear(u,v)==bilinear(u,v,w,h));
        }
    }
    t.coarseOnly=false;t.direct=true;
    std::vector<uint8_t> hotSlots(256,255);std::vector<uint16_t> pool(4096);
    for(unsigned v=0;v<64;++v)for(unsigned u=0;u<64;++u)pool[v*64+u]=bilinear(u,v,64,64);
    hotSlots[0]=0;t.hotSlots=hotSlots.data();t.hotPool=pool.data();
    TileFeedback feedback;feedback.clear();t.hotFeedback[0]=&feedback;
    for(int v=-12;v<1100;v+=5)for(int u=-12;u<1100;u+=5){
        const bool hit=std::clamp(u,0,1023)<64&&std::clamp(v,0,1023)<64;
        assert(t.sampleHot(u,v,0)==(hit?bilinear(u,v,64,64):t.sample(u,v)));
    }
    assert(feedback.sampled&&feedback.hits);
    Texture texture(64,64,reinterpret_cast<uint16_t*>(tiles.data()),false,0,false,CLAMP,palette.data());
    texture.tiled=&t;
    for(auto mode:{TileFilter::Nearest,TileFilter::Bilinear,TileFilter::ThreePoint,TileFilter::CachedBilinear}){
        texture.bilinear=true;texture.tiledFilter=mode;
        std::vector<uint16_t> out(102+2,0xa55a);
        drawTiledSpan(t,out.data()+1,102,0,0,1,9,3,0,0,false,mode);
        for(unsigned i=0;i<102;++i)assert(out[i+1]==texture.getPixel(i*9,i*3));
        assert(out.front()==0xa55a&&out.back()==0xa55a);
    }
    texture.bilinear=false;texture.tiledFilter=TileFilter::CachedBilinear;
    assert(texture.getPixel(19,33)==t.sample(19,33));
    for(int exponent=-60;exponent<=60;++exponent){const float v=std::ldexp(1.37f,exponent);assert(std::abs(double(tileReciprocal(v))*v-1)<.000001);}
    std::puts("PASS: tiled clamp sampling, independent bilinear oracle, coarse/cache paths, hot hits/misses, span dispatch and guards");
}
