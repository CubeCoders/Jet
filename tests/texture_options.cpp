#include "Scene.hpp"
#include <array>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstdio>
using namespace Renderer;
int main() {
    static_assert(TEXTURE_MAPPING);
    std::array<uint16_t,4> corners{0xf800,0x07e0,0x001f,0xffff};
    Texture tiny(2,2,corners.data(),false,0,false,CLAMP);
    tiny.bilinear=true;
    assert(tiny.getPixel(512,512)==(BILINEAR_FILTER?0x7bef:0xffff));
    tiny.bilinear=false;
    assert(tiny.getPixel(512,512)==0xffff);
    assert(tiny.getPixel(-1024,-1024)==0xf800);
    uint8_t indices[]={0,1,2,3};
    Texture paletted(2,2,reinterpret_cast<uint16_t*>(indices),false,0,false,CLAMP,corners.data());
    paletted.bilinear=true; // Index buffers retain safe nearest sampling.
    assert(paletted.getPixel(512,512)==0xffff);
    constexpr int w=64,h=64,stride=ZBUFFER_STRIDE(w);
    std::vector<uint16_t> color(stride*(FIELD_BUFFERS?h/2:h)+16,0xbeef),depth(stride*h,0xffff);
    Camera camera; camera.nearPlane=1; camera.farPlane=2000;
    Rasterizer raster(color.data(),w,h,depth.data(),&camera);
    raster.interlacedMode=FIELD_BUFFERS;
    std::array<uint16_t,32*32> ramp;
    for(int y=0;y<32;++y) for(int x=0;x<32;++x) ramp[y*32+x]=uint16_t((x<<11)|((y*2)<<5));
    Texture texture(32,32,ramp.data(),false,0,false,CLAMP);
    Material material(0xffff,&texture); material.shadingMode=ShadingMode::UNLIT;
    RenderVertex a,b,c;
    a.position={4,4,200}; a.uv={0,0};
    b.position={60,4,800}; b.uv={1024,0};
    c.position={4,60,200}; c.uv={0,1024};
    std::array<uint16_t,4> samples;
    for(int variant=0;variant<4;++variant) {
        material.perspectiveCorrect=variant&1; texture.bilinear=variant>=2;
        std::fill(depth.begin(),depth.end(),0xffff);
        assert(raster.drawTriangle(a,b,c,&material,nullptr,nullptr,false,false,false,0));
        samples[variant]=color[(FIELD_BUFFERS?10:20)*stride+(HALF_WIDTH_BUFFERS?10:20)];
    }
    // At (20,20), affine UV=(2/7,2/7); perspective UV=(1/11,4/11).
    // Nearest lookup in the coordinate ramp makes the selected texels explicit.
    assert(samples[0]==uint16_t((9<<11)|(18<<5)));
    assert(samples[1]==(PERSPECTIVE_CORRECT_TEXTURES?uint16_t((2<<11)|(22<<5)):samples[0]));
    assert(samples[2]==(BILINEAR_FILTER?uint16_t((8<<11)|(17<<5)):samples[0]));
    assert(samples[3]==(PERSPECTIVE_CORRECT_TEXTURES?uint16_t((2<<11)|(22<<5)):samples[2]));
    assert(std::all_of(color.end()-16,color.end(),[](auto p){return p==0xbeef;}));
    std::puts("Texture options: independent mapping/filter choices, analytic UV references, palette safety and guards pass");
}
