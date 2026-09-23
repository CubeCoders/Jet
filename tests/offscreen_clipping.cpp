#include "Renderer.hpp"
#include <array>
#include <cassert>
#include <algorithm>
using namespace Renderer;
int main() {
    constexpr int w=64,h=64;
    constexpr int n=w*h/(HALF_WIDTH_BUFFERS?2:1)/(FIELD_BUFFERS?2:1);
    std::array<uint16_t,n+64> pixels;
    pixels.fill(0xbeef);
    Camera camera;camera.nearPlane=1;camera.farPlane=2000;
    Rasterizer raster(pixels.data()+32,w,h,nullptr,&camera);
    raster.interlacedMode=FIELD_BUFFERS;
    Material mat(0x1127);mat.shadingMode=ShadingMode::UNLIT;
    // x == width formerly produced slot zero from a negative clipped width:
    // (-1)/2 == 0. The final row then overwrote the allocation's suffix.
    for(int side=0;side<4;++side) for(int parity=0;parity<2;++parity) {
        RenderVertex a,b,c;
        if(side==0){a.position={w,0,100};b.position={w+100,0,100};c.position={w,h,100};}
        if(side==1){a.position={-100,0,100};b.position={-2,0,100};c.position={-2,h,100};}
        if(side==2){a.position={0,h,100};b.position={w,h,100};c.position={0,h+100,100};}
        if(side==3){a.position={0,-100,100};b.position={w,-2,100};c.position={0,-2,100};}
        assert(!raster.drawTriangle(a,b,c,&mat,nullptr,nullptr,parity,false,false,0));
        assert(std::all_of(pixels.begin(),pixels.end(),[](auto p){return p==0xbeef;}));
    }
}
