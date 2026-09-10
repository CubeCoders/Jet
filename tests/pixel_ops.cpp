// Standalone on desktop. For S3 validation, define JET_PIXEL_OPS_EMBEDDED,
// compile into a test firmware, and call runPixelOpsChecks() before rendering.
#include "PixelOps.hpp"
#include <algorithm>
#include <cstdio>
#if defined(ESP_PLATFORM)
#include "esp_timer.h"
#endif

#if defined(_MSC_VER)
#define PIXEL_NOINLINE __declspec(noinline)
#else
#define PIXEL_NOINLINE __attribute__((noinline))
#endif
static PIXEL_NOINLINE void scalarExpand(uint16_t* dst, const uint16_t* src, size_t n) {
    auto* out = reinterpret_cast<uint32_t*>(dst);
    for (size_t i = 0; i < n; ++i) {
        uint16_t p = (uint16_t)((src[i] << 8) | (src[i] >> 8));
        out[i] = ((uint32_t)p << 16) | p;
    }
}
static PIXEL_NOINLINE void vectorExpand(uint16_t* dst, const uint16_t* src, size_t n) {
    Renderer::expandSwapRGB565(dst, src, n);
}

int runPixelOpsChecks() {
    alignas(16) uint16_t src[264], dst[544];
    uint32_t checked = 0;
    // Every RGB565 value must survive byte swapping and duplication exactly.
    for (int base = 0; base < 65536; base += 240) {
        const int n = std::min(240, 65536 - base);
        for (int i = 0; i < n; ++i) src[i] = (uint16_t)(base + i);
        vectorExpand(dst, src, n);
        for (int i = 0; i < n * 2; ++i) {
            uint16_t p = src[i / 2];
            if (dst[i] != (uint16_t)((p << 8) | (p >> 8))) return 1;
            ++checked;
        }
    }
    // All halfword alignments, zero/short spans, vector tails and guards.
    for (int i = 0; i < 264; ++i) src[i] = (uint16_t)(i * 911 + 1234);
    for (int a = 0; a < 8; ++a) for (int b = 0; b < 8; ++b)
        for (int n = 0; n <= 33; ++n) {
            std::fill(dst, dst + 544, 0xa55a);
            vectorExpand(dst + b, src + a, n);
            for (int i = 0; i < 544; ++i) {
                uint16_t expected = 0xa55a;
                if (i >= b && i < b + n * 2) {
                    uint16_t p = src[a + (i - b) / 2];
                    expected = (uint16_t)((p << 8) | (p >> 8));
                }
                if (dst[i] != expected) {
                    std::printf("Pixel expansion failure a=%d b=%d n=%d i=%d\n", a, b, n, i);
                    return 1;
                }
                ++checked;
            }
        }
    std::printf("Pixel expansion: %u values/guards checked, zero errors\n", (unsigned)checked);
#if defined(ESP_PLATFORM)
    constexpr int runs = 10000;
    int64_t start = esp_timer_get_time();
    for (int i = 0; i < runs; ++i) scalarExpand(dst, src, 240);
    int64_t scalarUs = esp_timer_get_time() - start;
    start = esp_timer_get_time();
    for (int i = 0; i < runs; ++i) vectorExpand(dst, src, 240);
    int64_t vectorUs = esp_timer_get_time() - start;
    std::printf("Pixel expansion: %d x 240 pixels, scalar %lld us, SIMD %lld us\n",
                runs, (long long)scalarUs, (long long)vectorUs);
#endif
    return 0;
}
#ifndef JET_PIXEL_OPS_EMBEDDED
int main() { return runPixelOpsChecks(); }
#endif
