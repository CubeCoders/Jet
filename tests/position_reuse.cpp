// Duplicate positions can carry different face normals and UVs. Projection
// reuse must match the uncached renderer through transforms and clipping.
#include "Scene.hpp"
#include <cstdio>
#include <vector>
#if defined(JET_POSITION_REUSE_EMBEDDED)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
using namespace Renderer;

int runPositionReuseChecks() {
    initializeTrigTables();
    constexpr int W=160, H=120;
    // The S3 SIMD framebuffer clear requires 16-byte alignment.
    std::vector<uint16_t> storage(W*H+8), z(W*H,65535);
    auto* fb = reinterpret_cast<uint16_t*>(
        (reinterpret_cast<uintptr_t>(storage.data())+15) & ~uintptr_t(15));
    uint16_t texels[16];
    for (int i=0;i<16;++i) texels[i]=(uint16_t)(0x1200+i*1801);
    Texture texture(4,4,texels);
    Material material(0x7bef,&texture);
    Camera camera; camera.nearPlane=100; camera.farPlane=2200;
    camera.setFOV(70.0f,W);
    DirectionalLight light({20,40,0},{255,255,255});
    AmbientLight ambient({90,90,90});
    Object mesh; mesh.cullingMode=CullingMode::NO_CULLING;
    auto addFaces = [&](int first, int end) {
        for (int face=first;face<end;++face) {
            for (int corner=0;corner<3;++corner) {
                Object::Vertex v;
                v.position={corner==0?-200:200,corner==1?150:-150,corner==2?2600:300};
                v.normal={face%8*90-300,face%8*55-170,-FIXED_POINT_SCALE};
                v.uv={face%8*1700+corner*2000,face%8*800-corner*1100};
                mesh.addVertex(v);
            }
            mesh.addTriangle(face*3,face*3+1,face*3+2,&material);
        }
    };
    addFaces(0,8);
    mesh.calculateBoundingBox();
    Scene scene(fb,z.data(),W,H);
    scene.setCamera(&camera); scene.setDirectionalLight(&light);
    scene.setAmbientLight(&ambient); scene.addObject(&mesh);
    for (int frame=0;frame<48;++frame) {
        // Grow beyond the S3's 4 KiB internal scratch allocation limit, then
        // return to the small mesh without discarding the vector's capacity.
        if (frame==16) { addFaces(8,128); mesh.calculateBoundingBox(); }
        if (frame==32) {
            mesh.vertices.resize(24); mesh.triangles.resize(8);
            mesh.calculateBoundingBox();
        }
        mesh.position={frame%5*13,0,frame%4*-120};
        mesh.rotation={frame%7*3,frame%9*4,frame%3*3};
        mesh.isBillboard=frame%5==0;
        material.specular=frame%2?0:32;
        material.shadingMode=(ShadingMode)(frame%3);
        mesh.invalidatePositions();
        std::fill(fb,fb+W*H,0); std::fill(z.begin(),z.end(),65535);
        scene.prepareFrame(); scene.rasterizeBand(0,H);
        const std::vector<uint16_t> reference(fb,fb+W*H);
        const int count=scene.lastFrameDrawnTriangles;
        if (!mesh.cachePositions() || !mesh.cachedPositionSources()) return 1;
        std::fill(fb,fb+W*H,0); std::fill(z.begin(),z.end(),65535);
        scene.prepareFrame(); scene.rasterizeBand(0,H);
        if (!std::equal(reference.begin(),reference.end(),fb) || count!=scene.lastFrameDrawnTriangles) {
            std::printf("Position reuse mismatch at frame %d\n",frame); return 1;
        }
#if defined(JET_POSITION_REUSE_EMBEDDED)
        vTaskDelay(1);
#endif
    }
    std::puts("Position reuse: 48 cached/uncached frames matched, including scratch growth and fallback.");
    return 0;
}

#if !defined(JET_POSITION_REUSE_EMBEDDED)
int main() { return runPositionReuseChecks(); }
#endif
