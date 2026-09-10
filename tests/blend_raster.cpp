// Fingerprint the actual alpha/additive/water raster paths against a saved
// renderer built with the same configuration. Optional argv[1] asserts hash.
#include "Renderer.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace Renderer;
int main(int argc, char** argv) {
    constexpr int W = 480, H = 320;
    initializeTrigTables();
    Camera camera; camera.nearPlane = 1; camera.farPlane = 100000;
    std::vector<uint16_t> fb(W * H), reflection(W * H), sky(H);
    for (size_t i = 0; i < reflection.size(); ++i) reflection[i] = (uint16_t)(i * 7919);
    for (int i = 0; i < H; ++i) sky[i] = (uint16_t)(i * 113);
    Rasterizer raster(fb.data(), W, H, nullptr, &camera);
    raster.gradientColors = sky.data(); raster.gradientSize = H;
    Material mat(0x5bad);
    uint64_t hash = 1469598103934665603ull;
    unsigned drawn = 0;
    for (auto mode : {ShadingMode::UNLIT, ShadingMode::ADDITIVE, ShadingMode::WATER_REFLECT})
        for (int alpha = 0; alpha < 256; ++alpha) {
            mat.shadingMode = mode; mat.alpha = (uint8_t)alpha;
            mat.specular = (uint8_t)(alpha % 33);
            raster.waterTime = alpha * 0.03f;
            raster.waterlineY = alpha % 3 == 0 ? 0 : alpha % 3 == 1 ? H / 2 : H;
            raster.reflectBuffer = alpha % 2 ? reflection.data() : nullptr;
            raster.interlacedMode = FIELD_BUFFERS || alpha % 2;
            raster.checkerboardMode = false;
            raster.yBandMin = alpha % 4 == 0 ? 61 : 0;
            raster.yBandMax = alpha % 4 == 0 ? 253 : H;
            for (size_t i = 0; i < fb.size(); ++i) fb[i] = (uint16_t)(i * 941 + alpha);
            for (int t = 0; t < 3; ++t) {
                RenderVertex a, b, c;
                a.position = {-40 + t * 39, -20, 500};
                b.position = {520 - t * 209, 30, 500};
                c.position = {73 + t * 7, 350, 500};
                drawn += raster.drawTriangle(a, b, c, &mat, nullptr, nullptr, alpha % 2 == 0, false, false, 0);
            }
            for (auto pixel : fb) { hash ^= pixel; hash *= 1099511628211ull; }
        }
    std::printf("%016llx (%u alpha/additive/water triangles)\n", (unsigned long long)hash, drawn);
    return !drawn || (argc > 1 && hash != std::strtoull(argv[1], nullptr, 16));
}
