#include "Scene.hpp"
#include "Primitives.hpp"
#include <cassert>
#include <vector>
#include <array>
#include <algorithm>
#include <cstdio>
using namespace Renderer;
int main() {
    static_assert(Z_BUFFERING && JET_DEPTH_SORT_OPAQUE_FRONT_TO_BACK);
    constexpr int w=64,h=64,stride=ZBUFFER_STRIDE(w);
    std::vector<uint16_t> color(stride*(FIELD_BUFFERS?h/2:h)),depth(stride*h);
    Camera camera; camera.nearPlane=1; camera.farPlane=2000; camera.setFOV(60,w);
    Material blue(0x001f),red(0xf800),green(0x07e0),white(0xffff);
    for (auto m : {&blue,&red,&green,&white}) m->shadingMode=ShadingMode::UNLIT;
    red.alpha=green.alpha=128;
    auto make=[&](Material* m,int z) {
        auto* o=Primitives::createCube(1200,1200,10,m); o->setPosition(0,0,z); return o;
    };
    // Fully occluded blended green must not overwrite opaque blue. Blended
    // red in front must then composite over blue, independent of submission.
    std::array<int,4> order{0,1,2,3};
    unsigned checks=0;
    do {
        Scene scene(color.data(),depth.data(),w,h); scene.setCamera(&camera);
        scene.getRenderer()->interlacedMode=FIELD_BUFFERS;
        std::array<Object*,4> objects{make(&blue,400),make(&red,200),make(&green,500),make(&white,100)};
        objects[3]->noWriteZBuffer=true; // Background band despite its near Z.
        for(int i:order) scene.addObject(objects[i]);
        scene.render();
        assert(scene.lastFrameRasterizedTriangles>0);
        // First interlaced field uses odd rows. Avoid a shared triangle edge.
        const int y=FIELD_BUFFERS?33:32;
        const auto pixel=color[(FIELD_BUFFERS?y/2:y)*stride+(HALF_WIDTH_BUFFERS?28/2:28)];
        assert(pixel==0x780f);
        auto* overlay=make(&white,900); overlay->ignoreZBuffer=true; scene.addObject(overlay);
        scene.render();
        const int y2=32;
        assert(color[(FIELD_BUFFERS?y2/2:y2)*stride+(HALF_WIDTH_BUFFERS?28/2:28)]==0xffff);
        for (auto* o:objects) delete o;
        delete overlay;
        ++checks;
    } while(std::next_permutation(order.begin(),order.end()));
    std::printf("Depth sort: %u submission orders preserve opaque occlusion, blended faces, background and overlays\n",checks);
}
