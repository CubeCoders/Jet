#include "../src/PostFX.hpp"
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace {
unsigned long long comparisons = 0;
unsigned channelReference[64];
uint16_t reference(uint16_t p) {
    return uint16_t((channelReference[p >> 11] << 11) |
        (channelReference[(p >> 5) & 63] << 5) | channelReference[p & 31]);
}

bool checkSpan(int count, int offset, unsigned intensity, unsigned first) {
    constexpr int capacity = 2 * 1024 + 48;
    alignas(16) static std::array<uint16_t, capacity> pixels;
    const int prefix = 16 + offset;
    const int active = prefix + (FIELD_BUFFERS ? 0 : count);
    const int checked = active + count + 16;
    std::fill(pixels.begin(), pixels.begin()+checked, uint16_t(0xd39b));
    const int width = count * (HALF_WIDTH_BUFFERS ? 2 : 1);
    for (int i = 0; i < count; ++i) pixels[active+i] = uint16_t(first+i);
    Renderer::PostFX fx(width, 2);
    fx.applyCRT(pixels.data()+prefix, uint8_t(intensity), true, true);
    for (int i = 0; i < checked; ++i) {
        const bool inside = i >= active && i < active+count;
        const uint16_t original = inside ? uint16_t(first+i-active) : uint16_t(0xd39b);
        const uint16_t expected = POSTFX_CRT && inside ? reference(original) : original;
        ++comparisons;
        if (pixels[i] != expected) {
            std::printf("CRT FAIL count=%d offset=%d intensity=%u first=%u index=%d got=%04x expected=%04x\n",
                count, offset, intensity, first, i, pixels[i], expected);
            return false;
        }
    }
    return true;
}
}

// The same function is callable before starting the S3 runtime. Yield between
// strengths so exhaustive correctness does not trip the idle-task watchdog.
int jetCrtExactTests() {
    comparisons = 0;
    for (unsigned intensity = 0; intensity < 256; ++intensity) {
        const double scale = (255-intensity) / 255.0;
        for (unsigned c = 0; c < 64; ++c) channelReference[c] = unsigned(c*scale+0.5);
        for (unsigned base = 0; base < 65536; base += 1024)
            if (!checkSpan(1024, 0, intensity, base)) return 1;
        for (int offset = 0; offset < 8; ++offset)
            for (int count = 0; count < 97; ++count)
                if (!checkSpan(count, offset, intensity, intensity * 193 + count * 797 + offset * 1777)) return 1;
#ifdef ESP_PLATFORM
        vTaskDelay(1);
#endif
    }
    std::printf("CRT EXACT PASS: %llu value/guard comparisons; all 65536 colors x 256 intensities, 8 alignments, widths 0..96\n", comparisons);
    return 0;
}
#ifndef ESP_PLATFORM
int main() { return jetCrtExactTests(); }
#endif
