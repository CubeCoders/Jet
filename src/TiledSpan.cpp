#include "TiledSpan.hpp"
#include "JetConfig.hpp"
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#define IRAM_ATTR
#define DRAM_ATTR
#endif

namespace Renderer {
DRAM_ATTR static const float inverseSteps[]={0,0,1,.5f,1.f/3.f,.25f,.2f,1.f/6.f,1.f/7.f};
template<TileFilter Filter>
static void IRAM_ATTR drawTiledSpanImpl(const TileTexture& texture,uint16_t* out,int count,
                           float uq,float vq,float q,float du,float dv,float dq,
                           int feedbackLane,bool exact) {
    const float texelU=texture.width/1024.f,texelV=texture.height/1024.f;
    for(int first=0;first<count;) {
        int n=exact?1:std::min(8,count-first);
        const float inv=tileReciprocal(q);
        const float u=uq*inv,v=vq*inv;
        float endU=u,endV=v;
        while(n>1){
            const float steps=float(n-1),endQ=q+dq*steps;
            if(endQ<=0||std::max(q,endQ)>1.25f*std::min(q,endQ)){n=(n+1)/2;continue;}
            const float endInv=tileReciprocal(endQ);
            endU=(uq+du*steps)*endInv;endV=(vq+dv*steps)*endInv;
            // Exact interpolation-error upper bound for linear 1/z:
            // |u_exact-u_linear| <= |du*dq| / (4*min(q0,q1)).
            const float curvature=.25f*std::abs(endQ-q)*std::max(inv,endInv);
            const float errorU=std::abs(endU-u)*curvature*texelU;
            const float errorV=std::abs(endV-v)*curvature*texelV;
            if(std::max(errorU,errorV)<=.125f)break;
            n=(n+1)/2;
        }
        const float scale=inverseSteps[n];
        const float stepU=(endU-u)*scale,stepV=(endV-v)*scale;
        for(int i=0;i<n;++i){
            const int su=int(u+stepU*i),sv=int(v+stepV*i),lane=((first+i)&7)==0?feedbackLane:-1;
            if constexpr(Filter==TileFilter::Bilinear)out[first+i]=texture.sampleBilinear(su,sv,lane);
            else if constexpr(Filter==TileFilter::ThreePoint)out[first+i]=texture.sampleThreePoint(su,sv,lane);
            else if constexpr(Filter==TileFilter::CachedBilinear)out[first+i]=texture.sampleHot(su,sv,lane);
            else out[first+i]=texture.sample(su,sv,lane);
        }
        first+=n;uq+=du*n;vq+=dv*n;q+=dq*n;
    }
}
void IRAM_ATTR drawTiledSpan(const TileTexture& texture,uint16_t* out,int count,
                           float uq,float vq,float q,float du,float dv,float dq,
                           int feedbackLane,bool exact,TileFilter filter) {
#if BILINEAR_FILTER
    if(filter==TileFilter::CachedBilinear){drawTiledSpanImpl<TileFilter::CachedBilinear>(texture,out,count,uq,vq,q,du,dv,dq,feedbackLane,exact);return;}
    if(filter==TileFilter::Bilinear){drawTiledSpanImpl<TileFilter::Bilinear>(texture,out,count,uq,vq,q,du,dv,dq,feedbackLane,exact);return;}
    if(filter==TileFilter::ThreePoint){drawTiledSpanImpl<TileFilter::ThreePoint>(texture,out,count,uq,vq,q,du,dv,dq,feedbackLane,exact);return;}
#endif
    drawTiledSpanImpl<TileFilter::Nearest>(texture,out,count,uq,vq,q,du,dv,dq,feedbackLane,exact);
}

}
