#pragma once
#include <cstdint>
#include <algorithm>
#include <array>

namespace Renderer {
enum class TileFilter : uint8_t { Nearest, Bilinear, ThreePoint, CachedBilinear };
// Each raster worker owns its table until the application joins both workers.
struct TileFeedback {
    static constexpr unsigned capacity=1024;
    struct Entry {uint16_t key,count;};
    std::array<Entry,capacity> entries;
    unsigned sampled=0,hits=0,dropped=0;
    void clear(){entries.fill({65535,0});sampled=hits=dropped=0;}
#if defined(__GNUC__)
    __attribute__((always_inline))
#endif
    inline void record(uint16_t key,bool hit){
        ++sampled;hits+=hit;
        const unsigned start=(uint32_t(key)*2654435761u)>>22;
        for(unsigned probe=0;probe<16;++probe){
            auto& entry=entries[(start+probe)&(capacity-1)];
            if(entry.key==key){if(entry.count<65535)++entry.count;return;}
            if(entry.key==65535){entry={key,1};return;}
        }
        ++dropped;
    }
};
// Experimental, immutable view for one render. The application resolves and
// loads pages before starting either raster worker. Tiles contain 32x32 indices.
struct TileTexture {
    const uint8_t* backing = nullptr;
    const uint8_t* cache = nullptr;
    const uint8_t* slots = nullptr;
    const uint8_t* coarse = nullptr;
    const uint16_t* palette = nullptr;
    // Optional exact RGB565 lookup for all 1024x1024 integer UV pairs of this
    // material/mip. Experimental app-owned immutable cache; 2 MiB per entry.
    const uint16_t* filteredUV = nullptr;
    const uint16_t* hotPool = nullptr;
    const uint8_t* hotSlots = nullptr;
    TileFeedback* hotFeedback[2] = {nullptr,nullptr};
    uint16_t hotKeyBase = 0;
    // A worker owns one disjoint screen band and its feedback array. Only the
    // application reads/resets these counts, after all raster workers join.
    uint8_t* feedback[2] = {nullptr,nullptr};
    unsigned width = 0, height = 0, pagesX = 0;
    unsigned coarseWidth = 0, coarseHeight = 0;
    bool direct = false;
    bool coarseOnly = false;
    bool exactPerspective = false;

#if defined(__GNUC__)
    __attribute__((always_inline))
#endif
    inline uint16_t sample(int u, int v, int feedbackBand=-1) const {
        const unsigned cu = unsigned(std::clamp(u, 0, 1023));
        const unsigned cv = unsigned(std::clamp(v, 0, 1023));
        if (coarseOnly)
            return palette[coarse[(cv*coarseHeight/1024)*coarseWidth+cu*coarseWidth/1024]];
        const unsigned x = cu*width/1024, y = cv*height/1024;
        const unsigned page = (y>>5)*pagesX+(x>>5);
        const unsigned local = ((y&31)<<5)+(x&31);
        if(feedbackBand>=0&&feedback[feedbackBand]) {
            auto& count=feedback[feedbackBand][page];if(count<255)++count;
        }
        if (direct) return palette[backing[(page<<10)+local]];
        const unsigned slot = slots[page];
        if (slot != 255) return palette[cache[(slot<<10)+local]];
        // Both PSRAM and flash are memory mapped. A cache miss retains the
        // requested detail by reading the immutable backing store directly.
        return palette[backing[(page<<10)+local]];
    }

    // Palette indices are decoded BEFORE interpolation. Tile/cache boundaries
    // are resolved separately for each tap, including the row-major coarse mip.
#if defined(__GNUC__)
    __attribute__((always_inline))
#endif
    inline uint16_t texel(unsigned x,unsigned y,int feedbackBand=-1) const {
        if(coarseOnly)return palette[coarse[y*coarseWidth+x]];
        const unsigned page=(y>>5)*pagesX+(x>>5),local=((y&31)<<5)+(x&31);
        if(feedbackBand>=0&&feedback[feedbackBand]){
            auto& count=feedback[feedbackBand][page];if(count<255)++count;
        }
        if(!direct){const unsigned slot=slots[page];
            if(slot!=255)return palette[cache[(slot<<10)+local]];}
        return palette[backing[(page<<10)+local]];
    }
#if defined(__GNUC__)
    __attribute__((always_inline))
#endif
    inline uint16_t sampleBilinear(int u,int v,int feedbackBand=-1) const {
        if(filteredUV)return filteredUV[(unsigned(std::clamp(v,0,1023))<<10)+unsigned(std::clamp(u,0,1023))];
        const unsigned w=coarseOnly?coarseWidth:width,h=coarseOnly?coarseHeight:height;
        // Match Jet's clamp-filter convention and 10-bit fractional weights.
        const unsigned su=unsigned(std::clamp(u,0,1023))*(w-1);
        const unsigned sv=unsigned(std::clamp(v,0,1023))*(h-1);
        const unsigned x=su>>10,y=sv>>10,x1=std::min(x+1,w-1),y1=std::min(y+1,h-1);
        const unsigned fx=su&1023,fy=sv&1023;
        const unsigned a=(1024-fx)*(1024-fy),b=fx*(1024-fy),c=(1024-fx)*fy,d=fx*fy;
        const unsigned p00=texel(x,y,feedbackBand),p10=texel(x1,y,feedbackBand);
        const unsigned p01=texel(x,y1,feedbackBand),p11=texel(x1,y1,feedbackBand);
        const unsigned r=((p00>>11)*a+(p10>>11)*b+(p01>>11)*c+(p11>>11)*d)>>20;
        const unsigned g=(((p00>>5)&63)*a+((p10>>5)&63)*b+((p01>>5)&63)*c+((p11>>5)&63)*d)>>20;
        const unsigned blue=((p00&31)*a+(p10&31)*b+(p01&31)*c+(p11&31)*d)>>20;
        return uint16_t((r<<11)|(g<<5)|blue);
    }


    // N64-style triangular interpolation (not bit-exact RDP emulation).
    // Use the same UV convention and 10-bit weights as our bilinear sampler.
    // Exactly three decoded colours; interpolate within one half of the cell.
#if defined(__GNUC__)
    __attribute__((always_inline))
#endif
    inline uint16_t sampleThreePoint(int u,int v,int feedbackBand=-1) const {
        const unsigned w=coarseOnly?coarseWidth:width,h=coarseOnly?coarseHeight:height;
        const unsigned su=unsigned(std::clamp(u,0,1023))*(w-1);
        const unsigned sv=unsigned(std::clamp(v,0,1023))*(h-1);
        const unsigned x=su>>10,y=sv>>10,x1=std::min(x+1,w-1),y1=std::min(y+1,h-1);
        int fx=int(su&1023),fy=int(sv&1023);
        unsigned base,a,b;
        if(fx+fy<=1024){
            base=texel(x,y,feedbackBand);a=texel(x1,y,feedbackBand);b=texel(x,y1,feedbackBand);
        }else{
            base=texel(x1,y1,feedbackBand);a=texel(x,y1,feedbackBand);b=texel(x1,y,feedbackBand);
            fx=1024-fx;fy=1024-fy;
        }
        // Difference form takes two multiplies per channel. The complete
        // numerator is nonnegative because the three weights are nonnegative.
        const int r0=int(base>>11),g0=int((base>>5)&63),b0=int(base&31);
        const unsigned r=unsigned(r0*1024+(int(a>>11)-r0)*fx+(int(b>>11)-r0)*fy)>>10;
        const unsigned g=unsigned(g0*1024+(int((a>>5)&63)-g0)*fx+(int((b>>5)&63)-g0)*fy)>>10;
        const unsigned blue=unsigned(b0*1024+(int(a&31)-b0)*fx+(int(b&31)-b0)*fy)>>10;
        return uint16_t((r<<11)|(g<<5)|blue);
    }

#if defined(__GNUC__)
    __attribute__((always_inline))
#endif
    inline uint16_t sampleHot(int u,int v,int feedbackBand=-1) const {
        if(!hotSlots)return sample(u,v);
        const unsigned cu=unsigned(std::clamp(u,0,1023)),cv=unsigned(std::clamp(v,0,1023));
        const unsigned page=((cv>>6)<<4)+(cu>>6),slot=hotSlots[page];
        if(feedbackBand>=0&&hotFeedback[feedbackBand])hotFeedback[feedbackBand]->record(uint16_t(hotKeyBase+page),slot!=255);
        if(slot!=255)return hotPool[(slot<<12)+((cv&63)<<6)+(cu&63)];
        return sample(u,v);
    }

};
}
