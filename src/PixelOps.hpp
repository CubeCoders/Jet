#ifndef JET_PIXEL_OPS_HPP
#define JET_PIXEL_OPS_HPP

#include <cstdint>
#include <cstddef>

namespace Renderer {

// Convert a half-width RGB565 row to the panel's byte order, duplicating
// each source pixel horizontally. Source and destination must not overlap.
inline void expandSwapRGB565(uint16_t* dst, const uint16_t* src, size_t count) {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    // PIE loads/stores round addresses down to 16 bytes. Use them only on
    // aligned buffers and complete eight-pixel blocks; never overread tails.
    if ((((uintptr_t)src | (uintptr_t)dst) & 15) == 0) {
        size_t blocks = count / 8;
        count %= 8;
        while (blocks--) {
            __asm__ volatile (
                "ee.vld.128.ip q0, %[src], 16\n\t"
                "ee.orq q1, q0, q0\n\t"
                // Separate low/high bytes, then zip in reverse order to
                // swap bytes in each RGB565 pixel. Both vectors are equal.
                "ee.vunzip.8 q0, q1\n\t"
                "ee.vzip.8 q1, q0\n\t"
                // Zip the equal vectors as 16-bit lanes to duplicate pixels.
                "ee.vzip.16 q1, q0\n\t"
                "ee.vst.128.ip q1, %[dst], 16\n\t"
                "ee.vst.128.ip q0, %[dst], 16\n\t"
                : [src] "+r"(src), [dst] "+r"(dst) : : "memory"
            );
        }
    }
#endif
    while (count--) {
        const uint16_t pixel = *src++;
        const uint16_t swapped = (uint16_t)((pixel << 8) | (pixel >> 8));
        *dst++ = swapped;
        *dst++ = swapped;
    }
}

} // namespace Renderer
#endif
