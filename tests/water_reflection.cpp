#include "Scene.hpp"
#include <vector>
#include <array>
#include <cassert>
#include <algorithm>
using namespace Renderer;
int main() {
    constexpr int w=64,h=64,stride=ZBUFFER_STRIDE(w),count=stride*(FIELD_BUFFERS?h/2:h);
    std::vector<uint16_t> color(count+16,0xbeef),depth(stride*h,0xffff),source(count,0x07e0);
    std::array<uint16_t,h> sky;sky.fill(0x001f);
    Camera camera;camera.nearPlane=1;camera.farPlane=2000;
    Rasterizer raster(color.data(),w,h,depth.data(),&camera);
    raster.interlacedMode=FIELD_BUFFERS;raster.reflectBuffer=source.data();
    raster.gradientColors=sky.data();raster.gradientSize=h;raster.waterlineY=20;
    Material water(0x0488);water.shadingMode=ShadingMode::WATER_REFLECT;
    water.alpha=200;water.specular=0;water.waterYBias=20;water.waterReflectionMaxY=30;
    RenderVertex a,b,c;a.position={-100,-100,200};b.position={300,-100,200};c.position={-100,300,200};
    auto render=[&] {
        std::fill(color.begin(),color.begin()+count,0);
        std::fill(depth.begin(),depth.end(),0xffff);
        assert(raster.drawTriangle(a,b,c,&water,nullptr,nullptr,false,false,false,0));
        assert(std::all_of(color.begin()+count,color.end(),[](auto x){return x==0xbeef;}));
        return color;
    };
    const auto reference=render();
    for(int row=(FIELD_BUFFERS?15:30);row<(FIELD_BUFFERS?h/2:h);++row)
        std::fill_n(source.data()+row*stride,stride,0xf81f);
    assert(render()==reference); // Excluded source rows cannot feed back into the water.
    water.waterReflectionMaxY=-1;
    assert(render()!=reference); // Default retains unrestricted legacy sampling.
    water.waterReflectionMaxY=30;
    std::fill_n(source.data(),stride*(FIELD_BUFFERS?15:30),0xf800);
    assert(render()!=reference); // Valid above-cutoff geometry still reflects.
}
