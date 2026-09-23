// The fill helper is private to Renderer.cpp. Standalone builds include that
// translation unit here instead of linking a separate Renderer.cpp object.
// For S3 checks, temporarily include this file at the end of Renderer.cpp
// with JET_FILL_SPANS_EMBEDDED defined, then call runFillSpanChecks().
#ifndef JET_FILL_SPANS_EMBEDDED
#include "../src/Renderer.cpp"
#endif
#include <algorithm>
#include <cstdio>

int runFillSpanChecks() {
    alignas(16) uint16_t pixels[512];
    uint32_t checked = 0;
    for (uint16_t color : {uint16_t(0), uint16_t(0xffff), uint16_t(0x1234), uint16_t(0x5a3c)}) {
        for (int offset = 0; offset < 8; ++offset) {
            for (int n = -1; n <= 480; ++n) {
                std::fill(pixels, pixels + 512, 0xa55a);
                fillRGB565Span(pixels, offset, n, color);
                for (int i = 0; i < 512; ++i) {
                    const uint16_t expected = i >= offset && i < offset + n ? color : 0xa55a;
                    if (pixels[i] != expected) {
                        std::printf("Fill span failure: offset=%d count=%d pixel=%d\n", offset, n, i);
                        return 1;
                    }
                    ++checked;
                }
            }
        }
    }
    // Exercise every RGB565 value through the vector path as well as guards.
    for (unsigned color = 0; color < 65536; ++color) {
        std::fill(pixels, pixels + 32, 0xa55a);
        fillRGB565Span(pixels, 0, 24, (uint16_t)color);
        for (int i = 0; i < 32; ++i) {
            const uint16_t expected = i < 24 ? (uint16_t)color : uint16_t(0xa55a);
            if (pixels[i] != expected) return 1;
            ++checked;
        }
    }
    std::printf("Fill spans: %u values/guards checked, zero errors\n", (unsigned)checked);
    return 0;
}

#ifndef JET_FILL_SPANS_EMBEDDED
int main() { return runFillSpanChecks(); }
#endif
