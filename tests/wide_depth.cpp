#include "Scene.hpp"
#include <vector>
#include <cassert>
#include <algorithm>
#include <cstdlib>
using namespace Renderer;
int main() {
    static_assert(Z_BUFFERING && !FAST_Z);
    constexpr int w=64,h=64,stride=ZBUFFER_STRIDE(w),count=stride*(FIELD_BUFFERS?h/2:h);
    std::vector<uint16_t> color(count+16,0xbeef),depth(stride*h+16,0xffff);
    Camera camera;camera.nearPlane=1;camera.farPlane=10000;
    Rasterizer raster(color.data(),w,h,depth.data(),&camera);raster.interlacedMode=FIELD_BUFFERS;
    Material material(0xf800);material.shadingMode=ShadingMode::UNLIT;
    for(int extent:{100,10000,100000}) {
        RenderVertex a,b,c;
        a.position={-extent,-extent,1000};b.position={extent*3,-extent,8000};c.position={-extent,extent*3,3000};
        const int64_t area=int64_t(extent*4)*(extent*4);
        for(bool parity:{false,true}) {
            std::fill(color.begin(),color.begin()+count,0);
            std::fill(depth.begin(),depth.end(),0xffff);
            assert(raster.drawTriangle(a,b,c,&material,nullptr,nullptr,parity,false,false,0));
            for(int y=(FIELD_BUFFERS?int(parity):0);y<h;y+=(FIELD_BUFFERS?2:1))
                for(int x=0;x<w;x+=(HALF_WIDTH_BUFFERS?2:1)) {
                    const int64_t wb=int64_t(x+extent)*(extent*4),wc=int64_t(y+extent)*(extent*4),wa=area-wb-wc;
                    int expected=int((1000*wa+8000*wb+3000*wc)/area);
                    int actual=depth[y*stride+(HALF_WIDTH_BUFFERS?x/2:x)];
                    assert(std::abs(actual-expected)<=1);
                }
            assert(std::all_of(color.begin()+count,color.end(),[](auto p){return p==0xbeef;}));
            assert(std::all_of(depth.begin()+stride*h,depth.end(),[](auto p){return p==0xffff;}));
        }
    }
}
