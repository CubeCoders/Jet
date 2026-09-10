#include "BlendSpans.hpp"
#include <algorithm>
#include <cstdio>
#if defined(ESP_PLATFORM)
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
using namespace Renderer;
namespace {
alignas(16) uint16_t input[272], actual[544], expected[544];
uint64_t checked;
uint16_t swapPixel(uint16_t p) { return (uint16_t)((p << 8) | (p >> 8)); }
// Deliberately use the original channel equations, not the kernel's weights
// or reciprocal, so /255 and the additive alpha convention are independent.
void reference(uint16_t* d, const uint16_t* s, int n, uint16_t col, uint8_t a,
               RGB565BlendMode mode, uint8_t flags, uint16_t key) {
    for (int i = 0; i < n; ++i) {
        uint16_t fg = s ? s[i] : col;
        if ((flags & BlendColorKey) && fg == key) continue;
        uint16_t bg = (flags & BlendConstantBackground) ? col :
            (flags & BlendSwapDestination) ? swapPixel(d[i]) : d[i];
        unsigned out = 0;
        for (int shift : {0, 5, 11}) {
            const unsigned mask = shift == 5 ? 63 : 31;
            const unsigned x = (fg >> shift) & mask, y = (bg >> shift) & mask;
            unsigned v;
            switch (mode) {
            case RGB565BlendMode::Alpha255: v = (x * a + y * (255 - a)) / 255; break;
            case RGB565BlendMode::Alpha256: v = (x * a + y * (256 - a)) >> 8; break;
            case RGB565BlendMode::Add: v = std::min(x + y, mask); break;
            default: v = std::min(((x * (a + 1)) >> 8) + y, mask); break;
            }
            out |= v << shift;
        }
        d[i] = flags & BlendSwapDestination ? swapPixel((uint16_t)out) : (uint16_t)out;
    }
}
bool compare(int n, int tag) {
    for (int i = 0; i < n; ++i) {
        ++checked;
        if (actual[i] != expected[i]) {
            std::printf("Blend failure tag=%d index=%d got=%04x expected=%04x\n",
                        tag, i, actual[i], expected[i]); return false;
        }
    }
    return true;
}
#if defined(ESP_PLATFORM)
// Compile each original scalar equation with fixed blend mode/flags, as at
// its old call site. Benchmark these, not the deliberately generic oracle.
template<RGB565BlendMode Mode, int Flags, bool Solid, bool Scaled = false>
inline __attribute__((always_inline)) void scalarBenchImpl(uint16_t* d, const uint16_t* s, int n, uint8_t a) {
    const unsigned inv = Mode == RGB565BlendMode::Alpha256 ? 256 - a : 255 - a;
    for (int i = 0; i < n; ++i) {
        const uint16_t fg = Solid ? 0x371b : s[Scaled ? (i * 85) >> 8 : i];
        if ((Flags & BlendColorKey) && fg == 0) continue;
        if (Mode == RGB565BlendMode::Alpha255 && a == 255) {
            d[i] = Flags & BlendSwapDestination ? swapPixel(fg) : fg; continue;
        }
        const uint16_t bg = Flags & BlendConstantBackground ? 0x371b :
            Flags & BlendSwapDestination ? swapPixel(d[i]) : d[i];
        unsigned r = fg >> 11, g = (fg >> 5) & 63, b = fg & 31;
        if constexpr (Mode == RGB565BlendMode::Add) {
            r = std::min(r + (bg >> 11), 31u);
            g = std::min(g + ((bg >> 5) & 63), 63u);
            b = std::min(b + (bg & 31), 31u);
        } else {
            r = r * a + (bg >> 11) * inv;
            g = g * a + ((bg >> 5) & 63) * inv;
            b = b * a + (bg & 31) * inv;
            if constexpr (Mode == RGB565BlendMode::Alpha255) { r /= 255; g /= 255; b /= 255; }
            else { r >>= 8; g >>= 8; b >>= 8; }
        }
        const uint16_t out = (uint16_t)((r << 11) | (g << 5) | b);
        d[i] = Flags & BlendSwapDestination ? swapPixel(out) : out;
    }
}
#define SCALAR_BENCH(NAME, MODE, FLAGS, SOLID, SCALED) \
    void IRAM_ATTR NAME(uint16_t* d, const uint16_t* s, int n, uint8_t a) { \
        scalarBenchImpl<RGB565BlendMode::MODE, FLAGS, SOLID, SCALED>(d, s, n, a); }
SCALAR_BENCH(waterBench, Alpha256, BlendConstantBackground, false, false)
SCALAR_BENCH(solidBench, Alpha256, 0, true, false)
SCALAR_BENCH(spriteBench, Alpha255, BlendSwapDestination, false, false)
SCALAR_BENCH(keyBench, Alpha255, BlendSwapDestination | BlendColorKey, false, false)
SCALAR_BENCH(addBench, Add, BlendSwapDestination, false, false)
SCALAR_BENCH(scaledBench, Add, BlendSwapDestination, false, true)
#undef SCALAR_BENCH
#endif
}

int runBlendSpanChecks() {
    checked = 0;
    // All 64x64 channel pairs at every alpha for all four blend equations.
    for (int m = 0; m < 4; ++m) {
        for (int a = 0; a < 256; ++a) for (int pair = 0; pair < 4096; pair += 64) {
            const uint8_t flags = a & 1 ? BlendSwapDestination : 0;
            for (int i = 0; i < 64; ++i) {
                const int s = (pair + i) / 64, d = (pair + i) % 64;
                input[i] = (uint16_t)(((s & 31) << 11) | (s << 5) | ((s ^ 15) & 31));
                uint16_t p = (uint16_t)(((d & 31) << 11) | (d << 5) | ((d ^ 23) & 31));
                actual[i] = expected[i] = flags ? swapPixel(p) : p;
            }
            reference(expected, input, 64, 0, (uint8_t)a, (RGB565BlendMode)m, flags, 0);
            blendRGB565Span(actual, input, 64, 0, (uint8_t)a, (RGB565BlendMode)m, flags);
            if (!compare(64, m * 256 + a)) return 1;
        }
        std::printf("Blend channel sweep %d passed\n", m);
#if defined(ESP_PLATFORM)
        vTaskDelay(1);
#endif
    }
    // Alignment, tails, colour keys, constants, and byte-swapped output.
    for (int m = 0; m < 4; ++m) for (int so = 0; so < 8; ++so)
        for (int offset = 0; offset < 8; ++offset) for (int n = 0; n <= 65; ++n) {
            const uint8_t flags = (uint8_t)(n % 8);
            const uint8_t alpha = (uint8_t)(n * 19 + so * 7 + offset);
            for (int i = 0; i < 272; ++i) input[i] = i % 7 ? (uint16_t)(i * 911) : 0x7bef;
            for (int i = 0; i < 544; ++i) actual[i] = expected[i] = (uint16_t)(i * 731 + 13);
            const uint16_t* src = (n % 3) ? input + so : nullptr;
            reference(expected + offset, src, n, 0x5dab, alpha, (RGB565BlendMode)m, flags, 0x7bef);
            blendRGB565Span(actual + offset, src, n, 0x5dab, alpha, (RGB565BlendMode)m, flags, 0x7bef);
            if (!compare(544, 10000 + n)) return 1;
        }
    // Current-frame water feedback and ordinary overlapping forward blits.
    for (int m = 0; m < 4; ++m) for (int delta = -15; delta <= 15; ++delta)
        for (int flags = 0; flags < 8; ++flags) {
            for (int i = 0; i < 544; ++i) actual[i] = expected[i] = (uint16_t)(i * 971 + 1234);
            reference(expected + 32, expected + 32 + delta, 64, 0x271b, 173, (RGB565BlendMode)m, flags, 0);
            blendRGB565Span(actual + 32, actual + 32 + delta, 64, 0x271b, 173, (RGB565BlendMode)m, flags);
            if (!compare(544, 20000 + delta)) return 1;
        }
    // Scaled and clipped sprite rows, including the non-exact fp8 step for
    // scale 3 and spans longer than the internal staging tile.
    for (int scale = 2; scale <= 5; ++scale) for (int offset = 0; offset < 8; ++offset)
        for (int startX : {0, 1, 19}) for (int flags = 0; flags < 4; ++flags)
            for (int alpha : {1, 127, 255}) for (auto mode : {RGB565BlendMode::Alpha255, RGB565BlendMode::Add}) {
                for (int i = 0; i < 272; ++i) input[i] = i % 7 ? (uint16_t)(i * 911) : 0x7bef;
                for (int i = 0; i < 544; ++i) actual[i] = expected[i] = (uint16_t)(i * 731 + 13);
                for (int i = 0; i < 241; ++i)
                    reference(expected + offset + i, input + (((i + startX) * (256 / scale)) >> 8),
                              1, 0, (uint8_t)alpha, mode, (uint8_t)flags, 0x7bef);
                blendRGB565ScaledSpan(actual + offset, input, 241, startX * (256 / scale),
                                     256 / scale, (uint8_t)alpha, mode, (uint8_t)flags, 0x7bef);
                if (!compare(544, 30000 + scale)) return 1;
            }
    for (int delta = -15; delta <= 15; ++delta) for (int flags = 0; flags < 4; ++flags)
        for (auto mode : {RGB565BlendMode::Alpha255, RGB565BlendMode::Add}) {
            for (int i = 0; i < 544; ++i) actual[i] = expected[i] = (uint16_t)(i * 971 + 1234);
            for (int i = 0; i < 64; ++i)
                reference(expected + 32 + i, expected + 32 + delta + ((i * 85) >> 8),
                          1, 0, 173, mode, (uint8_t)flags, 0);
            blendRGB565ScaledSpan(actual + 32, actual + 32 + delta, 64, 0, 85, 173, mode, (uint8_t)flags, 0);
            if (!compare(544, 40000 + delta)) return 1;
        }
    std::printf("Blend spans: %llu values/guards checked, zero errors\n", (unsigned long long)checked);
#if defined(ESP_PLATFORM)
    const struct { const char* name; RGB565BlendMode mode; uint8_t flags; bool solid; uint8_t alpha;
                  void (*scalar)(uint16_t*, const uint16_t*, int, uint8_t); bool scaled; } cases[] = {
        {"water", RGB565BlendMode::Alpha256, BlendConstantBackground, false, 173, waterBench, false},
        {"solid alpha", RGB565BlendMode::Alpha256, 0, true, 128, solidBench, false},
        {"sprite alpha", RGB565BlendMode::Alpha255, BlendSwapDestination, false, 128, spriteBench, false},
        {"sprite keyed", RGB565BlendMode::Alpha255, BlendSwapDestination | BlendColorKey, false, 128, keyBench, false},
        {"sprite add", RGB565BlendMode::Add, BlendSwapDestination, false, 128, addBench, false},
        {"sprite copy", RGB565BlendMode::Alpha255, BlendSwapDestination | BlendColorKey, false, 255, keyBench, false},
        {"sprite scale3 add", RGB565BlendMode::Add, BlendSwapDestination, false, 128, scaledBench, true}
    };
    for (auto c : cases) {
        constexpr int runs = 3000;
        const uint16_t* src = c.solid ? nullptr : input;
        int64_t start = esp_timer_get_time();
        for (int i = 0; i < runs; ++i) c.scalar(expected, src, 240, c.alpha);
        int64_t scalarUs = esp_timer_get_time() - start;
        start = esp_timer_get_time();
        for (int i = 0; i < runs; ++i) {
            if (c.scaled) blendRGB565ScaledSpan(actual, src, 240, 0, 85, c.alpha, c.mode, c.flags, 0);
            else blendRGB565Span(actual, src, 240, 0x371b, c.alpha, c.mode, c.flags, 0);
        }
        int64_t simdUs = esp_timer_get_time() - start;
        std::printf("Blend benchmark %s: %d x 240 pixels, scalar %lld us, SIMD %lld us\n",
            c.name, runs, (long long)scalarUs, (long long)simdUs);
        vTaskDelay(1);
    }
#endif
    return 0;
}
#ifndef JET_BLEND_SPANS_EMBEDDED
int main() { return runBlendSpanChecks(); }
#endif
