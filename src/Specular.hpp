#pragma once
#include "Light.hpp"
#include <algorithm>

namespace Renderer::detail {
// Q15 repeated squaring gives a tight highlight without pow(), a lookup
// texture, or a per-pixel half-vector normalisation. Products fit uint32_t.
inline uint16_t phongSpecular(const Vector3& normal, const Vector3& half,
                             uint8_t exponent, uint8_t strength, uint16_t intensity) {
    if (!exponent || !strength || !intensity) return 0;
    const int64_t dot = Vector3::dotProduct(normal, half);
    if (dot <= 0) return 0;
    uint32_t base = uint32_t(std::min<int64_t>(32768, dot >> 5));
    uint32_t power = 32768;
    if (exponent == 32) {
        // Below this Q15 cosine, the exact repeated-squaring result rounds
        // to zero even at maximum strength. Most of the surface misses the
        // small glossy lobe, so reject it before the five squarings.
        if (base < 26971) return 0;
        power = (base * base + 16384) >> 15;
        power = (power * power + 16384) >> 15;
        power = (power * power + 16384) >> 15;
        power = (power * power + 16384) >> 15;
        power = (power * power + 16384) >> 15;
        return uint16_t(((power * strength + 16384) >> 15)
                        * std::min<uint16_t>(255, intensity) / 255);
    }
    while (exponent) {
        if (exponent & 1) power = (power * base + 16384) >> 15;
        exponent >>= 1;
        if (exponent) base = (base * base + 16384) >> 15;
    }
    return uint16_t(((power * strength + 16384) >> 15)
                    * std::min<uint16_t>(255, intensity) / 255);
}
inline uint16_t addSpecular(uint16_t base, uint16_t amount, Color light) {
    const uint32_t r = std::min<uint32_t>(31u, uint32_t(base >> 11) + (amount * light.r * 31u + 32512) / 65025u);
    const uint32_t g = std::min<uint32_t>(63u, uint32_t((base >> 5) & 63) + (amount * light.g * 63u + 32512) / 65025u);
    const uint32_t b = std::min<uint32_t>(31u, uint32_t(base & 31) + (amount * light.b * 31u + 32512) / 65025u);
    return uint16_t((r << 11) | (g << 5) | b);
}
}
