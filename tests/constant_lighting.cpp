#include "Renderer.hpp"
#include "Specular.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <set>
#include <vector>
using namespace Renderer;

int main(int argc, char** argv) {
    static_assert(LIGHTING && !Z_BRIGHTNESS);
    constexpr int w=64,h=64,stride=ZBUFFER_STRIDE(w);
    constexpr int count=stride*(FIELD_BUFFERS?h/2:h);
    constexpr uint16_t background=0x1020;
    std::vector<uint16_t> pixels(count+16,0xbeef),depth(stride*h,0xffff);
    Camera camera;camera.nearPlane=1;camera.farPlane=4000;
    Rasterizer raster(pixels.data(),w,h,depth.data(),&camera);
    raster.interlacedMode=FIELD_BUFFERS;
    DirectionalLight light({0,0,0},{240,180,110},220);
    light.lightDir={0,0,-1024};
    AmbientLight ambient({20,35,50});
    RenderVertex a,b,c;
    a.position={4,4,200};b.position={60,4,800};c.position={4,60,1700};
    a.uv={0,0};b.uv={1024,0};c.uv={0,1024};
    uint16_t texels[]={0x6210,0x2505,0x3a94,0x7bef};
    Texture texture(2,2,texels,false,0,false,CLAMP);texture.bilinear=true;
    Material material(0x6210);material.diffuse=140;material.specular=100;
    auto draw=[&](int parity=0,bool cached=false) {
        std::fill(pixels.begin(),pixels.begin()+count,background);
        std::fill(depth.begin(),depth.end(),0xffff);
        assert(raster.drawTriangle(a,b,c,&material,&light,&ambient,parity,false,false,0,255,cached));
        assert(std::all_of(pixels.begin()+count,pixels.end(),[](auto p){return p==0xbeef;}));
        return std::vector<uint16_t>(pixels.begin(),pixels.begin()+count);
    };
    // Optional dump for byte-for-byte comparisons with the old library. All
    // these cases must retain the interpolating path (one unequal component,
    // or equal normals but unequal cached Gouraud lighting).
    if(argc>1) {
        FILE* f=std::fopen(argv[1],"wb");assert(f);
        for(auto mode:{ShadingMode::PHONG,ShadingMode::GOURAUD})
        for(int component=0;component<4;++component)for(int parity=0;parity<2;++parity) {
            a.normal=b.normal=c.normal={210,120,-980};
            if(component==0)++b.normal.x;
            if(component==1)++b.normal.y;
            if(component==2)++b.normal.z;
            if(component==3 && mode==ShadingMode::PHONG)continue;
            a.lambertBrightness=20;b.lambertBrightness=180;c.lambertBrightness=90;
            material.shadingMode=mode;material.specularExponent=32;material.diffuseMap=&texture;
            const auto result=draw(parity,component==3);
            std::fwrite(result.data(),sizeof(uint16_t),result.size(),f);
        }
        std::fclose(f);return 0;
    }
    unsigned cases=0;
    for(Vector3 input:{Vector3{0,0,-1024},Vector3{200,100,-980},Vector3{400,200,-1960},Vector3{0,0,0},Vector3{0,0,1024}})
    for(int textured=0;textured<2;++textured)for(int exponent:{0,32})for(int parity=0;parity<2;++parity) {
        a.normal=b.normal=c.normal=input;
        material.shadingMode=ShadingMode::PHONG;material.specular=100;
        material.specularExponent=exponent;material.diffuseMap=textured?&texture:nullptr;
        const auto phong=draw(parity);
        // Independent reference: one normalized normal, ordinary FLAT diffuse
        // lighting, then the separately tested additive Phong specular lobe.
        Vector3 n=input;const auto length=n.length();
        if(length>0)n=(n*1024)/length;
        a.normal=b.normal=c.normal=n;
        material.shadingMode=ShadingMode::FLAT;material.specular=exponent?0:100;
        const auto flat=draw(parity);
        const auto gloss=(exponent && Vector3::dotProduct(n,light.lightDir)>0)
            ? detail::phongSpecular(n,{0,0,-1024},32,100,light.intensity):0;
        unsigned shaded=0;
        for(int i=0;i<count;++i) {
            if(flat[i]==background){assert(phong[i]==background);continue;}
            ++shaded;
            assert(phong[i]==detail::addSpecular(flat[i],gloss,light.color));
        }
        assert(shaded>100);++cases;
    }
    // Gouraud shares constant lighting with FLAT, but must respect supplied
    // per-vertex light values instead of recomputing from the normals.
    for(bool cached:{false,true})for(int textured=0;textured<2;++textured)for(int parity=0;parity<2;++parity) {
        a.normal=b.normal=c.normal={200,100,-980};
        a.lambertBrightness=b.lambertBrightness=c.lambertBrightness=137;
        material.diffuseMap=textured?&texture:nullptr;material.specular=100;
        material.shadingMode=ShadingMode::GOURAUD;
        const auto gouraud=draw(parity,cached);
        material.shadingMode=ShadingMode::FLAT;
        assert(gouraud==draw(parity,cached));++cases;
    }
    material.shadingMode=ShadingMode::GOURAUD;material.diffuseMap=nullptr;
    a.lambertBrightness=10;b.lambertBrightness=180;c.lambertBrightness=90;
    auto gradient=draw(0,true);
    assert(std::set<uint16_t>(gradient.begin(),gradient.end()).size()>8);
    std::printf("Constant normal lighting: %u analytic Phong/Gouraud cases, cached-light fallback and guards pass\n",cases);
}
