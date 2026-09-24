// Actual rasterizer versus an independent double-precision barycentric oracle.
// Build with TEXTURE_MAPPING=1, LIGHTING=0, FAST_Z=1, no perspective or AA.
#include "Renderer.hpp"
#include <cstdio>
#include <vector>
#include <cmath>
#include <algorithm>
using namespace Renderer;

int main() {
    constexpr int W=480,H=320;
    initializeTrigTables();
    Camera camera; camera.nearPlane=1; camera.farPlane=100000;
    std::vector<uint16_t> fb(W*H);
    Rasterizer raster(fb.data(),W,H,nullptr,&camera);
    uint32_t seed=4391;
    auto rnd=[&]() {seed=seed*1664525u+1013904223u;return seed;};
    auto blend=[](uint16_t dst,uint16_t src,int alpha) {
        unsigned result=0;
        for(int shift:{0,5,11}) {
            const unsigned mask=shift==5?63:31;
            result|=(((((src>>shift)&mask)*alpha+((dst>>shift)&mask)*(256-alpha))>>8)<<shift);
        }
        return (uint16_t)result;
    };
    uint64_t checked=0,different=0; int bad=0;
    for(int test=0;test<240;++test) {
        const int tw=test%4==0?31:32, th=test%4==0?63:64;
        std::vector<uint16_t> data(tw*th);
        for(int i=0;i<tw*th;++i) data[i]=(uint16_t)(i+1);
        const auto address=(TextureAddressMode)(test%3);
        Texture texture(tw,th,data.data(),false,0,false,address);
        std::vector<uint16_t> palette(256); std::vector<uint8_t> indices(tw*th);
        if(test%9==2) {
            for(int i=0;i<256;++i) palette[i]=(uint16_t)(i+1);
            for(int i=0;i<tw*th;++i) indices[i]=(uint8_t)i;
            texture.data=(uint16_t*)indices.data(); texture.palette=palette.data();
        }
        Material mat(0x7bef,&texture); mat.shadingMode=ShadingMode::UNLIT;
        if(test%9==3) mat.alpha=128;
        raster.textureLodEnabled=test%9==4 || test%9==5;
        raster.textureLodNear=test%9==5?300:400;
        raster.textureLodFar=test%9==5?400:600;
        auto shade=[&](int u,int v) {
            uint16_t pixel=texture.getPixel(u,v);
            if(test%9==4) pixel=blend(mat.color,pixel,128);
            if(test%9==5) pixel=mat.color;
            if(mat.alpha!=255) pixel=blend(0xffff,pixel,mat.alpha);
            return pixel;
        };
        RenderVertex a,b,c;
        a.position={-40-(int)(rnd()%250),-30-(int)(rnd()%200),500};
        b.position={300+(int)(rnd()%500),20+(int)(rnd()%80),500};
        c.position={30+(int)(rnd()%140),380+(int)(rnd()%500),500};
        if(test%7==0) {a.position={-90000,-60000,500};b.position={95000,-20000,500};c.position={400,90000,500};}
        const int range=test%5==0?32000:6000;
        auto uv=[&](){return (int)(rnd()%(range*2))-range;};
        a.uv={uv(),uv()};b.uv={uv(),uv()};c.uv={uv(),uv()};
        if(test%11==0) {a.uv={0,0};b.uv={1024,0};c.uv={0,1024};}
        std::fill(fb.begin(),fb.end(),0xffff);
        raster.interlacedMode=FIELD_BUFFERS || test%2==0;
        raster.checkerboardMode=!raster.interlacedMode && test%3==0;
        raster.yBandMin=test%4==0?61:0; raster.yBandMax=test%4==0?253:H;
        const bool even=test%2==0;
        raster.drawTriangle(a,b,c,&mat,nullptr,nullptr,even,false,false,0);
        const double area=(double)(b.position.x-a.position.x)*(c.position.y-a.position.y)
                         -(double)(b.position.y-a.position.y)*(c.position.x-a.position.x);
        for(int y=0;y<H;++y) for(int x=0;x<W;++x) {
            int index=y*W+x;
#if HALF_WIDTH_BUFFERS
            if(x&1) continue;
            index=y*(W/2)+x/2;
#endif
#if FIELD_BUFFERS
            index=(y/2)*(W/(HALF_WIDTH_BUFFERS?2:1))+x/(HALF_WIDTH_BUFFERS?2:1);
#endif
            // Only inspect written pixels; coverage is independently tested
            // in triangle_spans.cpp. This also handles field/band layouts.
            const auto actual=fb[index]; if(actual==0xffff) continue;
            if(y<raster.yBandMin || y>=raster.yBandMax) continue;
            // Field parity is global, including bands that start on an odd row.
            if(raster.interlacedMode && ((y&1)!=(int)even)) continue;
            if(raster.checkerboardMode && (((x^y)&1)!=(even?0:1))) continue;
            const double w0=(double)(b.position.y-c.position.y)*(x-c.position.x)+(double)(c.position.x-b.position.x)*(y-c.position.y);
            const double w1=(double)(c.position.y-a.position.y)*(x-c.position.x)+(double)(a.position.x-c.position.x)*(y-c.position.y);
            const double w2=area-w0-w1;
            if(std::min({w0,w1,w2})<0) continue;
            const double u=(a.uv.x*w0+b.uv.x*w1+c.uv.x*w2)/area;
            const double v=(a.uv.y*w0+b.uv.y*w1+c.uv.y*w2)/area;
            const auto expected=shade((int)u,(int)v);
            ++checked; if(actual==expected) continue;
            ++different;
            // Q16/float rounding may select an adjacent texel at a boundary.
            // Accept only results reachable within one UV unit of the oracle.
            bool adjacent=false;
            for(int du=-1;du<=1;++du) for(int dv=-1;dv<=1;++dv)
                adjacent |= actual==shade((int)u+du,(int)v+dv);
            if(!adjacent && bad++<8) std::printf("bad test %d xy %d,%d uv %.4f,%.4f actual %u expected %u\n",test,x,y,u,v,actual,expected);
        }
    }
    std::printf("%llu pixels checked; %llu adjacent-boundary differences; %d errors\n",
        (unsigned long long)checked,(unsigned long long)different,bad);
    return bad || checked<100000 || different>checked/100 ? 1:0;
}
