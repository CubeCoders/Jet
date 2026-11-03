// Compare this fingerprint against the previous engine with the SAME config.
// Covers all built-in shading modes, custom shaders, mixed-material meshes,
// emissive/specular combinations, both windings and near/far clipping.
#include "Scene.hpp"
#include <cassert>
#include <cstdio>
#ifdef NDEBUG
#error "Compile this regression test with assertions enabled."
#endif
static uint64_t hash(const std::vector<uint16_t>& v) {
    uint64_t h=14695981039346656037ull;
    for(auto x:v){h^=x;h*=1099511628211ull;}return h;
}
struct TestShader : Shader {
    TestShader(){screenSpace=false;hasAlpha=false;alphaColor=0;}
    uint16_t getPixel(uint32_t u,uint32_t v,ShaderParameters*) override {return uint16_t((u*31)^(v*211));}
};
static void lightingCases(){
    using namespace Renderer;
    constexpr int pixels=(HALF_WIDTH_BUFFERS?240:480)*(FIELD_BUFFERS?160:320);
    std::vector<uint16_t> fb(pixels+128,0xa55a), depth(ZBUFFER_STRIDE(480)*320);
    Scene s(fb.data()+64,depth.data(),480,320);s.getRenderer()->interlacedMode=true;
    Camera camera;camera.nearPlane=128;camera.farPlane=4000;s.setCamera(&camera);
    DirectionalLight light({40,160,0},{255,255,255},220);AmbientLight ambient({20,30,40});
    s.setDirectionalLight(&light);s.setAmbientLight(&ambient);
    TestShader shader;
    Material mat(0x8ee9),other(0xfad3);other.shadingMode=ShadingMode::GOURAUD;
    Object o;o.cullingMode=CullingMode::NO_CULLING;
    o.vertices={{{-100,-80,240},{0,0},{0,0,-1024}},{{130,-50,750},{1024,0},{300,0,-980}},{{10,150,400},{0,1024},{0,200,-1000}}};
    o.triangles={{0,1,2,&mat},{2,1,0,&other}};o.calculateBoundingBox();s.addObject(&o);
    for(int mode=0;mode<7;++mode)for(int variant=0;variant<32;++variant){
        mat.shadingMode=ShadingMode(mode);mat.emissive=(variant&1);mat.specular=(variant&2)?40:0;
        mat.specularExponent=variant&4?32:0;mat.shader=(variant&8)?&shader:nullptr;
        // Mix normal-consuming and unlit faces in the same object and vary clipping.
        o.triangles.resize(variant&16?2:1);o.triangles[0]={0,1,2,&mat};
        if(o.triangles.size()==2)o.triangles[1]={2,1,0,&other};
        for(int z : {-170,0,3600}){
            o.position.z=z;s.render();
            assert(std::all_of(fb.begin(),fb.begin()+64,[](auto x){return x==0xa55a;}));
            assert(std::all_of(fb.end()-64,fb.end(),[](auto x){return x==0xa55a;}));
            std::printf("lighting %d %d %d %016llx %d\n",mode,variant,z,(unsigned long long)hash(fb),s.lastFrameDrawnTriangles);
        }
    }
}
int main(){ Renderer::initializeTrigTables(); lightingCases(); }
