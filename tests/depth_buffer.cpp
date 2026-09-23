#include "Scene.hpp"
#include <cassert>
#include <algorithm>
#include <vector>
#include <cstdio>

using namespace Renderer;
int main() {
    static_assert(Z_BUFFERING && !FAST_Z, "Use per-pixel depth testing");
    constexpr int w=64,h=64,stride=ZBUFFER_STRIDE(w);
    constexpr int colorHeight=FIELD_BUFFERS?h/2:h;
    std::vector<uint16_t> color(stride*colorHeight+16,0xbeef), depth(stride*h+16,0x1234);
    Scene scene(color.data(),depth.data(),w,h);
    Camera camera; camera.nearPlane=1; camera.farPlane=2000;
    scene.setCamera(&camera);
    scene.getRenderer()->interlacedMode=true;
    for(int parity=1;parity>=0;--parity) {
        std::fill(depth.begin(),depth.end(),0x1234);
        scene.render();
        for(int y=0;y<h;++y) for(int x=0;x<stride;++x)
            assert(depth[y*stride+x]==((y&1)==parity?0xffff:0x1234));
        assert(std::all_of(depth.begin()+stride*h,depth.end(),[](auto p){return p==0x1234;}));
    }
    Material red(0xf800),blue(0x001f);
    red.shadingMode=blue.shadingMode=ShadingMode::UNLIT;
    Rasterizer raster(color.data(),w,h,depth.data(),&camera);
    raster.interlacedMode=true;
    auto quad=[&](int x0,int x1,int z0,int z1,Material* m,int parity) {
        RenderVertex a,b,c,d;
        a.position={x0,2,z0}; b.position={x1,2,z1};
        c.position={x1,62,z1}; d.position={x0,62,z0};
        raster.drawTriangle(a,b,c,m,nullptr,nullptr,parity,false,false,0);
        raster.drawTriangle(a,c,d,m,nullptr,nullptr,parity,false,false,0);
    };
    for(int parity=0;parity<2;++parity) for(int reverse=0;reverse<2;++reverse) {
        std::fill(depth.begin(),depth.end(),0xffff);
        std::fill(color.begin(),color.end(),0xbeef);
        if(reverse) quad(2,62,500,500,&blue,parity);
        quad(16,48,300,700,&red,parity);
        if(!reverse) quad(2,62,500,500,&blue,parity);
        for(int y=12+parity;y<50;y+=2) for(int x: {8,20,26,40,44,54}) {
            const int column=HALF_WIDTH_BUFFERS?x/2:x;
            const int row=FIELD_BUFFERS?y/2:y;
            assert(color[row*stride+column]==(x==20||x==26?0xf800:0x001f));
            assert(depth[y*stride+column] == (x==20?350:x==26?425:500));
        }
        assert(std::all_of(color.begin()+stride*colorHeight,color.end(),[](auto p){return p==0xbeef;}));
        assert(std::all_of(depth.begin()+stride*h,depth.end(),[](auto p){return p==0xffff;}));
    }
    std::puts("Depth: field clears, horizontal addressing, crossing surfaces and draw-order independence pass");
}
