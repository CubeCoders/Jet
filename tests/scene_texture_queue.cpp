// Scene-level regression fingerprint. Compare with the previous Scene build
// using the SAME JetConfig.hpp; pass its printed hash to assert equivalence.
// Also checks that immutable prepared geometry can be replayed in bands.
#include "Scene.hpp"
#include <cstdio>
#include <cstdlib>
#include <thread>
using namespace Renderer;

#if JET_TEST_POSITION_CACHE
static bool checkPositionCache() {
    Object mesh;
    mesh.addVertex({{1,2,3}});
    if (!mesh.cachePositions() || !mesh.cachedPositions()) return false;
    Object copy = mesh;
    mesh.vertices[0].position.x = 17;
    mesh.invalidatePositions();
    if (mesh.cachedPositions() || copy.cachedPositions()[0].x != 1) return false;
    if (!mesh.cachePositions() || mesh.cachedPositions()[0].x != 17) return false;
    mesh.addVertex({{4,5,6}});
    if (mesh.cachedPositions()) return false;
    if (!mesh.cachePositions()) return false;
    mesh.bakeScale(2,1);
    if (mesh.cachedPositions()) return false;
    if (!mesh.cachePositions() || mesh.cachedPositions()[0].x != 34) return false;
    mesh.rotation = {0,90,0}; mesh.bakeRotation();
    if (mesh.cachedPositions()) return false;
    if (!mesh.cachePositions()) return false;
    mesh.calculateBoundingBox();
    if (mesh.cachedPositions()) return false;
    mesh.vertices.clear();
    return mesh.cachePositions() && !mesh.cachedPositions();
}
#endif

int main(int argc, char** argv) {
#if JET_TEST_POSITION_CACHE
    if (!checkPositionCache()) { std::printf("Position cache failure\n"); return 1; }
#endif
    constexpr int W = 160, H = 120;
    initializeTrigTables();
    std::vector<uint16_t> fb(W * H), z(W * H, 65535);
    uint16_t pixels[64];
    for (int i = 0; i < 64; ++i) pixels[i] = (uint16_t)(0x1234 + i * 911);
    Texture texture(8, 8, pixels);
    Material textured(0x7bef, &texture), flat(0xf800);
    textured.specular = flat.specular = 0;
    Camera camera; camera.nearPlane = 128; camera.farPlane = 2400;
    DirectionalLight light({20, 40, 0}, {255, 255, 255});
    AmbientLight ambient({90, 90, 90});
    Object object, lod, backdrop;
    for (int i = 0; i < 18; ++i) {
        Object::Vertex v;
        v.position = {(i % 3 - 1) * 170, (i % 3 == 1 ? 130 : -130), 400};
        v.normal = {0, 0, -FIXED_POINT_SCALE};
        v.uv = {(i % 3 - 1) * 3500, (i % 3 == 1 ? 7000 : -2000)};
        object.addVertex(v);
    }
    for (int i = 0; i < 6; ++i)
        object.addTriangle(i * 3, i * 3 + (i % 2 ? 2 : 1),
                           i * 3 + (i % 2 ? 1 : 2), i % 2 ? &flat : &textured);
    lod.vertices = object.vertices; lod.triangles = object.triangles;
    object.lodMeshes.push_back(&lod);
    backdrop.vertices = object.vertices; backdrop.triangles = object.triangles;
    backdrop.position.z = 300;
    backdrop.cullingMode = CullingMode::NO_CULLING;
    backdrop.calculateBoundingBox();
#if JET_TEST_POSITION_CACHE
    if (!backdrop.cachePositions()) return 1;
#endif
    Scene scene(fb.data(), z.data(), W, H);
    scene.setCamera(&camera); scene.setDirectionalLight(&light);
    scene.setAmbientLight(&ambient); scene.addObject(&object);
    scene.addObject(&backdrop);
    auto* raster = scene.getRenderer();
    raster->textureLodNear = 300; raster->textureLodFar = 900;
    uint64_t hash = 1469598103934665603ull;
    int submitted = 0;
    for (int f = 0; f < 144; ++f) {
        // Sweep all eight near-plane masks, both winding orders, and all
        // culling modes; distant cases exercise texture and geometry LOD.
        for (int i = 0; i < 18; ++i) {
            object.vertices[i].position.z = (f & (1 << (i % 3))) ? (f % 2 ? 40 : 0) : 450 + i * 3;
            lod.vertices[i] = object.vertices[i];
            lod.vertices[i].uv.x += 17000;
        }
        object.position.z = f % 4 == 0 ? 450 : 0;
        object.rotation = {f % 5 == 0 ? 10 : 0, f % 7 == 0 ? 20 : 0, 0};
        object.isBillboard = f % 9 == 0;
        camera.setRotation(f % 6 == 0 ? 5 : 0, f % 10 == 0 ? 15 : 0, 0);
        object.ignoreZBuffer = f % 7 == 0;
        object.noWriteZBuffer = f % 11 == 0;
        backdrop.ignoreZBuffer = f % 5 == 0;
        backdrop.noWriteZBuffer = f % 13 == 0;
        object.zBias = (int8_t)((f % 9 - 4) * 10);
        object.cullingMode = (CullingMode)(f / 8 % 3);
        object.calculateBoundingBox(); lod.calculateBoundingBox();
#if JET_TEST_POSITION_CACHE
        if (f % 3 != 0 && (!object.cachePositions() || !lod.cachePositions())) return 1;
#endif
        scene.lodScale = f % 5 == 0 ? 200 : 0;
        raster->textureLodEnabled = f % 3 == 0;
        raster->interlacedMode = FIELD_BUFFERS || f % 7 == 0;
        raster->checkerboardMode = f % 11 == 0;
        textured.shadingMode = flat.shadingMode = (ShadingMode)(f / 24 % 3);
        std::fill(fb.begin(), fb.end(), 0);
        std::fill(z.begin(), z.end(), 65535);
        scene.prepareFrame();
        scene.rasterizeBand(0, H);
        submitted += scene.lastFrameDrawnTriangles;
        const auto whole = fb;
        const int wholeCount = scene.lastFrameRasterizedTriangles;
        std::fill(fb.begin(), fb.end(), 0);
        std::fill(z.begin(), z.end(), 65535);
        std::vector<uint8_t> upper(std::max(1, scene.lastFrameDrawnTriangles), 0), lower(upper.size(), 0);
        // Odd boundaries exercise the field parity adjustment too.
        const int split = 1 + (f * 7) % (H - 1);
        std::thread worker([&]() { scene.rasterizeBand(split, H, lower.data()); });
        scene.rasterizeBand(0, split, upper.data());
        worker.join();
        int unique = 0;
        for (size_t i = 0; i < upper.size(); ++i) unique += (upper[i] | lower[i]) != 0;
        if (unique != wholeCount || scene.lastFrameRasterizedTriangles != wholeCount) {
            std::printf("Band count mismatch at frame %d: %d != %d\n", f, unique, wholeCount); return 1;
        }
        if (fb != whole) { std::printf("Band mismatch at frame %d\n", f); return 1; }
        for (auto pixel : fb) { hash ^= pixel; hash *= 1099511628211ull; }
        scene.advanceFrameCounter();
    }
    std::printf("%016llx (%d submitted triangles)\n", (unsigned long long)hash, submitted);
    return submitted == 0 || (argc > 1 && hash != std::strtoull(argv[1], nullptr, 16));
}
