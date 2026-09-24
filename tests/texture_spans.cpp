// Independent pixel equations for the internal affine span helper.
#include "../src/TrigLUT.hpp"
#include "../src/TextureSpans.hpp"
#include <cstdio>
#include <algorithm>
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace {
uint16_t referenceBlend(uint16_t dst, uint16_t src, unsigned alpha, bool add) {
    unsigned result = 0;
    for (int shift : {0, 5, 11}) {
        const unsigned mask = shift == 5 ? 63 : 31;
        unsigned s = (src >> shift) & mask, d = (dst >> shift) & mask;
        unsigned value = add ? d + (s * (alpha + 1) / 256)
                             : (s * alpha + d * (256 - alpha)) / 256;
        result |= std::min(value, mask) << shift;
    }
    return static_cast<uint16_t>(result);
}
}

int runTextureSpanChecks() {
    using namespace Renderer;
    alignas(16) static uint16_t texels[64 * 64], actual[288], expected[288];
    uint32_t seed = 4171;
    auto random = [&]() { seed = seed * 1664525u + 1013904223u; return seed; };
    for (auto& texel : texels) texel = static_cast<uint16_t>(random());
    const int lengths[] = {0, 1, 2, 7, 8, 15, 16, 17, 31, 32, 33, 127, 128, 129, 239, 240, 257};
    const int alphas[] = {0, 1, 63, 128, 254, 255};
    uint64_t checked = 0;
    const unsigned widths[] = {32, 2, 4, 8, 16, 1024, 1, 64};
    const unsigned heights[] = {64, 16, 8, 4, 2, 1, 1024, 64};
    for (int shape = 0; shape < 8; ++shape) {
        const unsigned width = widths[shape], height = heights[shape];
        for (int offset = 0; offset < 8; ++offset)
        for (int n : lengths)
        for (int alpha : alphas)
        for (int fadeAlpha : {-1, 0, 1, 128, 254, 255})
        for (bool add : {false, true}) {
            for (int i = 0; i < 288; ++i) expected[i] = actual[i] = static_cast<uint16_t>(random());
            const int32_t u = int32_t(random() % 134217728) - 67108864;
            const int32_t v = int32_t(random() % 134217728) - 67108864;
            const int32_t du = int32_t(random() % 524288) - 262144;
            const int32_t dv = int32_t(random() % 524288) - 262144;
            RGB565ConstantBlend fade;
            const uint16_t flat = static_cast<uint16_t>(random());
            if (fadeAlpha >= 0) fade.prepare(flat, static_cast<uint8_t>(fadeAlpha), true);
            TextureSpans::draw(actual + 8 + offset, n, texels, width, height,
                u, v, du, dv, static_cast<uint8_t>(alpha), add, fadeAlpha < 0 ? nullptr : &fade);
            for (int i = 0; i < n; ++i) {
                const int uq = (u + i * du) / 65536, vq = (v + i * dv) / 65536;
                const int tu = (uq % FIXED_POINT_SCALE + FIXED_POINT_SCALE) % FIXED_POINT_SCALE;
                const int tv = (vq % FIXED_POINT_SCALE + FIXED_POINT_SCALE) % FIXED_POINT_SCALE;
                uint16_t pixel = texels[(tv * height / FIXED_POINT_SCALE) * width + tu * width / FIXED_POINT_SCALE];
                if (fadeAlpha >= 0) pixel = referenceBlend(flat, pixel, fadeAlpha, false);
                const int at = 8 + offset + i;
                expected[at] = alpha == 255 && !add ? pixel : referenceBlend(expected[at], pixel, alpha, add);
            }
            for (int i = 0; i < 288; ++i) {
                ++checked;
                if (actual[i] != expected[i]) {
                    printf("Texture span mismatch shape=%d offset=%d n=%d alpha=%d fade=%d add=%d pixel=%d\n",
                        shape, offset, n, alpha, fadeAlpha, add, i);
                    return 1;
                }
            }
        }
#ifdef ESP_PLATFORM
        vTaskDelay(1);
#endif
    }
    printf("Texture spans: %llu pixels/guards checked, zero errors\n", (unsigned long long)checked);
    return 0;
}

#ifndef JET_TEXTURE_SPANS_EMBEDDED
int main() { return runTextureSpanChecks(); }
#endif
