// Large ground/water faces must keep their visible portion when their
// average depth lies beyond the far plane. Run with full-width buffers.
#include "Scene.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace Renderer;

int main() {
    static_assert(!HALF_WIDTH_BUFFERS && !FIELD_BUFFERS);
    initializeTrigTables();
    constexpr int W = 160, H = 120;
    std::vector<uint16_t> fb(W*H), z(W*H, 65535);
    Material material(0xf800);
    material.shadingMode = ShadingMode::UNLIT;
    Camera camera;
    camera.nearPlane = 100;
    camera.farPlane = 2000;
    camera.setFOV(60, W);
    Object plane;
    plane.cullingMode = CullingMode::NO_CULLING;
    Scene scene(fb.data(), z.data(), W, H);
    scene.setCamera(&camera);
    scene.setBackcolor(0);
    scene.setClearBuffer(true);
    scene.addObject(&plane);

    // Cross just the far plane, then both near and far planes. Reverse
    // winding as well so the clipped polygon remains valid from either side.
    for (int nearZ : {150, 0}) {
        for (bool reverse : {false, true}) {
            plane.vertices.clear(); plane.triangles.clear();
            for (auto p : {Vector3{-2000,-100,nearZ}, Vector3{2000,-100,nearZ},
                           Vector3{2000,-100,10000}, Vector3{-2000,-100,10000}})
                plane.addVertex({p});
            if (reverse) plane.addFace(0,3,2,1,&material);
            else plane.addFace(0,1,2,3,&material);
            plane.calculateBoundingBox();
            scene.render();
            // This rectangle is well inside the visible part of the plane,
            // away from rounding differences on its clipped far boundary.
            for (int y=80; y<110; ++y)
                for (int x=20; x<140; ++x) assert(fb[y*W+x] != 0);
            assert(fb[20*W+80] == 0);
        }
    }
    for (auto& v:plane.vertices) v.position.z += 3000;
    plane.calculateBoundingBox();
    scene.render();
    for (auto p:fb) assert(p == 0);
    std::puts("Far-plane clipping: visible coverage, both planes, reversed winding and full rejection passed.");
}
