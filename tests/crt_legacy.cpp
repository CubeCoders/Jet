#include "../src/PostFX.hpp"
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
unsigned jetCrtTestIntensity = 48;
namespace {
unsigned long long comparisons = 0;
uint16_t reference(uint16_t p, unsigned intensity) {
    unsigned r = p >> 11, g = (p >> 5) & 63, b = p & 31;
    r = r < intensity ? 0 : r-intensity;
    g = g < intensity ? 0 : g-intensity;
    b = b < intensity ? 0 : b-intensity;
    return uint16_t((r << 11) | (g << 5) | b);
}
bool checkSpan(int count, int offset, unsigned intensity, unsigned first) {
    alignas(16) static std::array<uint16_t, 2*1024+48> pixels;
    const int prefix = 16+offset, active = prefix+count, checked = active+count+16;
    std::fill(pixels.begin(),pixels.begin()+checked,uint16_t(0xd39b));
    for (int i = 0; i < count; ++i) pixels[active+i] = uint16_t(first+i);
    Renderer::PostFX fx(count,2);
    fx.applyCRT(pixels.data()+prefix);
    for (int i = 0; i < checked; ++i) {
        const bool inside = i >= active && i < active+count;
        const uint16_t original = inside ? uint16_t(first+i-active) : uint16_t(0xd39b);
        const uint16_t expected = POSTFX_CRT && inside ? reference(original,intensity) : original;
        ++comparisons;
        if (pixels[i] != expected) {
            std::printf("LEGACY CRT FAIL count=%d offset=%d intensity=%u first=%u index=%d got=%04x expected=%04x\n",
                count,offset,intensity,first,i,pixels[i],expected);
            return false;
        }
    }
    return true;
}
}
int jetGameCrtExactTests() {
    comparisons = 0;
    for (unsigned intensity = 0; intensity < 256; ++intensity) {
        jetCrtTestIntensity = intensity;
        for (unsigned base = 0; base < 65536; base += 1024)
            if (!checkSpan(1024,0,intensity,base)) return 1;
        for (int offset = 0; offset < 8; ++offset)
            for (int count = 0; count < 97; ++count)
                if (!checkSpan(count,offset,intensity,intensity*193+count*797+offset*1777)) return 1;
#ifdef ESP_PLATFORM
        vTaskDelay(1);
#endif
    }
    std::printf("LEGACY CRT EXACT PASS: %llu value/guard comparisons; all 65536 colors x 256 intensities, 8 alignments, widths 0..96\n",comparisons);
    return 0;
}
#ifndef ESP_PLATFORM
int main() { return jetGameCrtExactTests(); }
#endif
