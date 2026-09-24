#include "../src/TrigLUT.hpp"
#include "../src/TextureSpans.hpp"
#include <cstdio>
#include <algorithm>
#include <array>

using namespace Renderer;
namespace {
uint16_t blendReference(uint16_t dst, uint16_t src, unsigned alpha, bool add) {
    unsigned result=0;
    for(int shift:{0,5,11}) {
        unsigned mask=shift==5?63:31;
        unsigned s=(src>>shift)&mask,d=(dst>>shift)&mask;
        unsigned value=add?d+s*(alpha+1)/256:(s*alpha+d*(256-alpha))/256;
        result|=std::min(value,mask)<<shift;
    }
    return uint16_t(result);
}
unsigned referenceIndex(unsigned w,unsigned h,int64_t u,int64_t v) {
    int64_t ui=u/65536,vi=v/65536;
    ui=(ui%1024+1024)%1024;vi=(vi%1024+1024)%1024;
    return unsigned((vi*h/1024)*w+ui*w/1024);
}
}
int main() {
    alignas(16) static uint8_t storage[32768+16];
    alignas(16) static uint16_t palettes[256+8],actual[512],expected[512],rgb[32768];
    const unsigned widths[]={1,2,4,8,16,32,64,128,256,1024,1,64,32};
    const unsigned heights[]={1,16,8,4,32,64,128,64,128,1,1024,64,32};
    uint32_t seed=4477;
    auto random=[&]() {seed=seed*1664525u+1013904223u;return seed;};
    uint64_t checked=0;
    for(int shape=0;shape<13;++shape) {
        unsigned w=widths[shape],h=heights[shape];
        for(int phase=0;phase<16;++phase) {
            const uint8_t* indices=storage+phase;
            uint16_t* palette=palettes+(phase%8);
            for(unsigned i=0;i<256;++i)palette[i]=uint16_t(random());
            for(unsigned i=0;i<w*h;++i){storage[phase+i]=uint8_t(random()>>13);rgb[i]=palette[indices[i]];}
            // Every signed integer UV around two wrap periods, with fractions
            // on both sides of zero and each boundary. The oracle uses modulo.
            for(int uv=-2049;uv<=2049;++uv) for(int frac:{-65535,-1,0,1,65535}) {
                int32_t u=uv*65536+frac,v=(1949-uv)*65536-frac;
                auto expectedPixel=rgb[referenceIndex(w,h,u,v)];
                auto pixel=TextureSpans::sampleIndexed8<false>(indices,palette,w,h,u,v);
                if(pixel!=expectedPixel){printf("INDEXED UV FAIL shape=%d phase=%d uv=%d frac=%d\n",shape,phase,uv,frac);return 1;}
                ++checked;
                if(w==32&&h==64) {
                    if(TextureSpans::sampleIndexed8<true>(indices,palette,w,h,u,v)!=expectedPixel)return 2;
                    ++checked;
                }
            }
            // Every output tail and halfword alignment, randomized signed UVs.
            for(int n=0;n<=480;++n) {
                int phaseDst=n%8;
                for(int i=0;i<512;++i)actual[i]=expected[i]=uint16_t(random());
                int32_t u=int32_t(random()%268435456)-134217728,v=int32_t(random()%268435456)-134217728;
                int32_t du=int32_t(random()%524288)-262144,dv=int32_t(random()%524288)-262144;
                int32_t actualU=u,actualV=v;
                TextureSpans::sampleIndexed8Row<false>(actual+8+phaseDst,n,indices,palette,w,h,actualU,actualV,du,dv);
                for(int i=0;i<n;++i)expected[8+phaseDst+i]=rgb[referenceIndex(w,h,int64_t(u)+i*du,int64_t(v)+i*dv)];
                if(actualU!=u+n*du||actualV!=v+n*dv)return 3;
                for(int i=0;i<512;++i){++checked;if(actual[i]!=expected[i]){printf("INDEXED ROW FAIL shape=%d phase=%d n=%d i=%d\n",shape,phase,n,i);return 4;}}
            }
        }
        // Independent component equations exercise both staged operations and
        // their separate rounding, including block tails and all alignments.
        const uint8_t* indices=storage+15;const uint16_t* palette=palettes+7;
        for(int phase=0;phase<8;++phase)
        for(int n:{0,1,2,7,8,15,16,17,31,32,33,127,128,129,239,240,257,479})
        for(int alpha:{0,1,63,128,254,255})
        for(int fadeAlpha:{-1,0,1,128,254,255})
        for(bool add:{false,true}) {
            for(int i=0;i<512;++i)actual[i]=expected[i]=uint16_t(random());
            int32_t u=int32_t(random()%134217728)-67108864,v=int32_t(random()%134217728)-67108864;
            int32_t du=int32_t(random()%524288)-262144,dv=int32_t(random()%524288)-262144;
            uint16_t flat=uint16_t(random());RGB565ConstantBlend fade;
            if(fadeAlpha>=0)fade.prepare(flat,uint8_t(fadeAlpha),true);
            TextureSpans::drawIndexed8(actual+8+phase,n,indices,palette,w,h,u,v,du,dv,uint8_t(alpha),add,fadeAlpha<0?nullptr:&fade);
            for(int i=0;i<n;++i) {
                uint16_t pixel=rgb[referenceIndex(w,h,int64_t(u)+i*du,int64_t(v)+i*dv)];
                if(fadeAlpha>=0)pixel=blendReference(flat,pixel,fadeAlpha,false);
                int at=8+phase+i;
                expected[at]=alpha==255&&!add?pixel:blendReference(expected[at],pixel,alpha,add);
            }
            for(int i=0;i<512;++i){++checked;if(actual[i]!=expected[i]){printf("INDEXED BLEND FAIL shape=%d phase=%d n=%d alpha=%d fade=%d add=%d i=%d\n",shape,phase,n,alpha,fadeAlpha,add,i);return 5;}}
        }
    }
    printf("INDEXED8 SPANS PASS: %llu independent UV/pixel/guard checks\n",(unsigned long long)checked);
}
