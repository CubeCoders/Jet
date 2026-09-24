#include "Scene.hpp"
#include <cassert>
#include <vector>
using namespace Renderer;

int main() {
    static_assert(!Z_BUFFERING && HALF_WIDTH_BUFFERS && FIELD_BUFFERS);
    static_assert(JET_SORT_DEPTH_BUCKETS == 128);
    constexpr int width=64,height=64;
    std::vector<uint16_t> pixels(width*height/4);
    Camera camera;camera.nearPlane=40;camera.farPlane=9000;camera.setFOV(60,width);
    Scene scene(pixels.data(),nullptr,width,height);
    scene.setCamera(&camera);scene.setClearBuffer(true);scene.getRenderer()->interlacedMode=true;
    Material green(0x07e0),red(0xf800),blue(0x001f);
    for(auto* m:{&green,&red,&blue}){m->emissive=true;m->shadingMode=ShadingMode::UNLIT;}
    Object nearObject,farObject,overlay,background,equalDepth;
    auto quad=[](Object& o,int z,Material* m){
        const int a=z/3;o.cullingMode=CullingMode::NO_CULLING;
        o.addVertex({{-a,-a,z}});o.addVertex({{a,-a,z}});
        o.addVertex({{a,a,z}});o.addVertex({{-a,a,z}});
        o.addFace(0,1,2,3,m);o.calculateBoundingBox();o.cachePositions();
    };
    quad(nearObject,480,&green);quad(farObject,550,&red);
    quad(overlay,3000,&blue);overlay.ignoreZBuffer=true;
    quad(background,150,&red);background.noWriteZBuffer=true;background.ignoreZBuffer=true;
    auto expect=[&](uint16_t color){
        for(int parity=0;parity<2;++parity){
            scene.render();assert(pixels[(height/4)*(width/2)+width/4]==color);
        }
    };
    // At farPlane=9000 these layers shared a 64-way bucket: adding the
    // farther layer last incorrectly painted over the nearer surface.
    scene.addObject(&nearObject);scene.addObject(&farObject);expect(green.color);
    scene.getObjects().clear();scene.addObject(&farObject);scene.addObject(&nearObject);expect(green.color);
    // Draw bands retain their priority; background wins over ignoreZBuffer.
    scene.addObject(&background);expect(green.color);
    scene.addObject(&overlay);expect(blue.color);
    // Equal-depth surfaces keep insertion order within the bucket.
    scene.getObjects().clear();quad(equalDepth,480,&red);
    scene.addObject(&nearObject);scene.addObject(&equalDepth);expect(red.color);
    // Close surfaces share even a 128-way bucket. Refinement must be
    // opt-in, independent of insertion order, and reset when disabled.
    Object closeObject;quad(closeObject,510,&red);
    scene.getObjects().clear();scene.addObject(&nearObject);scene.addObject(&closeObject);expect(red.color);
    nearObject.preciseDepthSort=true;expect(green.color);
    nearObject.preciseDepthSort=false;expect(red.color);
    closeObject.preciseDepthSort=true;expect(green.color);
    closeObject.zBias=1;expect(red.color);closeObject.zBias=0;
    scene.addObject(&background);expect(green.color);
    scene.addObject(&overlay);expect(blue.color);
    scene.getObjects().clear();nearObject.preciseDepthSort=true;
    scene.addObject(&nearObject);scene.addObject(&equalDepth);expect(red.color);
    scene.getObjects().clear();
}
