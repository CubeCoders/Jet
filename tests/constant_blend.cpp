// Prepared constant-colour kernel versus the original /256 channel equations.
#include "BlendSpans.hpp"
#include <cstdio>
#include <algorithm>
#if defined(ESP_PLATFORM)
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
using namespace Renderer;
namespace {
alignas(16) uint16_t source[272], actual[288], expected[288];
unsigned long long checks;
uint16_t reference(uint16_t dst, uint16_t src, unsigned alpha) {
    const unsigned inv = 256 - alpha;
    return (uint16_t)(((((src >> 11) * alpha + (dst >> 11) * inv) >> 8) << 11)
        | (((((src >> 5) & 63) * alpha + ((dst >> 5) & 63) * inv) >> 8) << 5)
        | (((src & 31) * alpha + (dst & 31) * inv) >> 8));
}
bool compare() {
    for (int i = 0; i < 288; ++i) {
        ++checks;
        if (actual[i] != expected[i]) {
            std::printf("Constant blend mismatch at %d: %04x != %04x\n", i, actual[i], expected[i]);
            return false;
        }
    }
    return true;
}
void yield() {
#if defined(ESP_PLATFORM)
    vTaskDelay(1);
#endif
}
}
int runConstantBlendChecks() {
    RGB565ConstantBlend state;
    checks = 0;
    for (bool bg : {false, true}) for (int alpha = 0; alpha < 256; ++alpha) {
        for (int c = 0; c < 64; ++c) {
            const uint16_t color = (uint16_t)(((c & 31) << 11) | (c << 5) | ((c ^ 15) & 31));
            for (int i = 0; i < 288; ++i) actual[i] = expected[i] = 0xa55a;
            for (int i = 0; i < 64; ++i) {
                const uint16_t v = (uint16_t)(((i & 31) << 11) | (i << 5) | ((i ^ 23) & 31));
                source[i] = v; actual[i] = v;
                expected[i] = bg ? reference(color, v, alpha) : reference(v, color, alpha);
            }
            state.prepare(color, (uint8_t)alpha, bg);
            state.blend(actual, bg ? source : nullptr, 64);
            if (!compare()) return 1;
        }
        if (alpha % 16 == 0) yield();
    }
    for (bool bg : {false, true}) for (int alpha : {0,1,31,127,128,254,255})
      for (int dstOffset = 0; dstOffset < 8; ++dstOffset)
        for (int srcOffset = 0; srcOffset < 8; ++srcOffset)
          for (int n : {0,1,7,8,15,16,17,31,32,33,63,64,127,240}) {
            for (int i = 0; i < 272; ++i) source[i] = (uint16_t)(i * 7919 + alpha * 37);
            for (int i = 0; i < 288; ++i) actual[i] = expected[i] = (uint16_t)(i * 941 + alpha * 119);
            const uint16_t color = (uint16_t)(alpha * 951 + 7919);
            for (int i = 0; i < n; ++i)
                expected[dstOffset+i] = bg ? reference(color, source[srcOffset+i], alpha)
                    : reference(expected[dstOffset+i], color, alpha);
            state.prepare(color, (uint8_t)alpha, bg);
            state.blend(actual + dstOffset, bg ? source + srcOffset : nullptr, n);
            if (!compare()) return 1;
          }
    // Current-frame water may read pixels written earlier in this same span.
    for (int delta : {-16,-8,-1,0,1,8,16}) for (int alpha : {0,1,127,255}) {
        for (int i = 0; i < 288; ++i) actual[i] = expected[i] = (uint16_t)(i * 719);
        for (int i = 0; i < 240; ++i)
            expected[24+i] = reference(0x7bef, expected[24+delta+i], alpha);
        state.prepare(0x7bef, (uint8_t)alpha, true);
        state.blend(actual+24, actual+24+delta, 240);
        if (!compare()) return 1;
    }
    std::printf("Constant blend: %llu pixels/guards passed\n", checks);
#if defined(ESP_PLATFORM)
    for (bool bg : {false, true}) {
        state.prepare(0x5bad,113,bg);
        const int64_t start = esp_timer_get_time();
        for (int i = 0; i < 3000; ++i)
            blendRGB565Span(actual, bg ? source : nullptr, 240, 0x5bad, 113,
                RGB565BlendMode::Alpha256, bg ? BlendConstantBackground : 0);
        const int64_t middle = esp_timer_get_time();
        for (int i = 0; i < 3000; ++i) state.blend(actual, bg ? source : nullptr, 240);
        const int64_t end = esp_timer_get_time();
        std::printf("Constant blend benchmark bg=%d: generic %lld us, prepared %lld us\n",
            bg, (long long)(middle-start), (long long)(end-middle));
        yield();
    }
#endif
    return 0;
}
#if !defined(ESP_PLATFORM)
int main() { return runConstantBlendChecks(); }
#endif
