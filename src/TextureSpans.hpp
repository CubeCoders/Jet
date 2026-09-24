#ifndef JET_TEXTURE_SPANS_HPP
#define JET_TEXTURE_SPANS_HPP

#include "BlendSpans.hpp"
#include "FastMath.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace Renderer {
namespace TextureSpans {

#ifdef _MSC_VER
#define JET_TEXTURE_INLINE __forceinline
#else
#define JET_TEXTURE_INLINE inline __attribute__((always_inline))
#endif

// Internal affine sampler: power-of-two RGB565 WRAP textures, Q16 UVs in
// Jet's 1/1024 units, contiguous output, no colour key or texture feedback.
// Keep truncation toward zero before wrapping, including negative fractions.
template<bool Facade>
JET_TEXTURE_INLINE uint16_t sample(const uint16_t* texels, unsigned width, unsigned height,
                       int32_t u, int32_t v) {
    const unsigned iu = static_cast<unsigned>(u / 65536);
    const unsigned iv = static_cast<unsigned>(v / 65536);
    if constexpr (Facade) {
        return texels[((iv >> 4) & 63u) * 32u + ((iu >> 5) & 31u)];
    } else {
        const unsigned x = (iu & (FIXED_POINT_SCALE - 1)) * width / FIXED_POINT_SCALE;
        const unsigned y = (iv & (FIXED_POINT_SCALE - 1)) * height / FIXED_POINT_SCALE;
        return texels[y * width + x];
    }
}

template<bool Facade>
JET_TEXTURE_INLINE void sampleRow(uint16_t* dst, int count, const uint16_t* texels,
                      unsigned width, unsigned height, int32_t& u, int32_t& v,
                      int32_t du, int32_t dv) {
    auto next = [&]() {
        const auto pixel = sample<Facade>(texels, width, height, u, v);
        u += du; v += dv;
        return pixel;
    };
    if (count && (reinterpret_cast<uintptr_t>(dst) & 3)) {
        *dst++ = next(); --count;
    }
    while (count >= 2) {
        const uint32_t lo = next(), hi = next();
        // All supported targets are little endian; dst is now 32-bit aligned.
        const uint32_t pair = lo | (hi << 16);
#if defined(__GNUC__)
        // IDF disables ordinary memcpy builtins; request the intrinsic so
        // this remains one store rather than a library call per pixel pair.
        __builtin_memcpy(__builtin_assume_aligned(dst, 4), &pair, sizeof(pair));
#else
        std::memcpy(dst, &pair, sizeof(pair));
#endif
        dst += 2; count -= 2;
    }
    if (count) *dst = next();
}

template<bool Facade>
#if defined(__GNUC__)
__attribute__((noinline))
#endif
static void PERF_CRITICAL draw(uint16_t* dst, int count, const uint16_t* texels,
                               unsigned width, unsigned height,
                               int32_t u, int32_t v, int32_t du, int32_t dv,
                               uint8_t alpha, bool additive,
                               const RGB565ConstantBlend* fade) {
    if (!fade && alpha == 255 && !additive) {
        sampleRow<Facade>(dst, count, texels, width, height, u, v, du, dv);
        return;
    }
    // Match the destination's alignment so the blend can use direct loads.
    // Bounded stack storage is private to each raster worker.
    alignas(16) uint16_t storage[128 + 8];
    uint16_t* pixels = storage + ((reinterpret_cast<uintptr_t>(dst) & 15) / 2);
    while (count > 0) {
        const int n = std::min(count, 128);
        sampleRow<Facade>(pixels, n, texels, width, height, u, v, du, dv);
        if (fade) {
            if (alpha == 255 && !additive) {
                fade->blend(dst, pixels, n);
                dst += n; count -= n;
                continue;
            }
            // Preserve both blends and their separate integer rounding.
            fade->blend(pixels, pixels, n);
        }
        const auto mode = additive
            ? (alpha == 255 ? RGB565BlendMode::Add : RGB565BlendMode::AddAlpha256)
            : RGB565BlendMode::Alpha256;
        blendRGB565Span(dst, pixels, n, 0, alpha, mode);
        dst += n; count -= n;
    }
}

static inline void draw(uint16_t* dst, int count, const uint16_t* texels,
                 unsigned width, unsigned height,
                 int32_t u, int32_t v, int32_t du, int32_t dv,
                 uint8_t alpha, bool additive, const RGB565ConstantBlend* fade) {
    if (width == 32 && height == 64 && FIXED_POINT_SCALE == 1024)
        draw<true>(dst, count, texels, width, height, u, v, du, dv, alpha, additive, fade);
    else
        draw<false>(dst, count, texels, width, height, u, v, du, dv, alpha, additive, fade);
}

// Static indexed8 palettes use the same UV arithmetic, paired stores and
// staged RGB565 blending. Format selection happens once per span. The index
// load is one byte; no alignment, padding or reads beyond that texel are needed.
template<bool Facade>
JET_TEXTURE_INLINE uint16_t sampleIndexed8(const uint8_t* texels, const uint16_t* palette, unsigned width, unsigned height,
                       int32_t u, int32_t v) {
    const unsigned iu = static_cast<unsigned>(u / 65536);
    const unsigned iv = static_cast<unsigned>(v / 65536);
    if constexpr (Facade) {
        return palette[texels[((iv >> 4) & 63u) * 32u + ((iu >> 5) & 31u)]];
    } else {
        const unsigned x = (iu & (FIXED_POINT_SCALE - 1)) * width / FIXED_POINT_SCALE;
        const unsigned y = (iv & (FIXED_POINT_SCALE - 1)) * height / FIXED_POINT_SCALE;
        return palette[texels[y * width + x]];
    }
}

template<bool Facade>
JET_TEXTURE_INLINE void sampleIndexed8Row(uint16_t* dst, int count, const uint8_t* texels, const uint16_t* palette,
                      unsigned width, unsigned height, int32_t& u, int32_t& v,
                      int32_t du, int32_t dv) {
    auto next = [&]() {
        const auto pixel = sampleIndexed8<Facade>(texels, palette, width, height, u, v);
        u += du; v += dv;
        return pixel;
    };
    if (count && (reinterpret_cast<uintptr_t>(dst) & 3)) {
        *dst++ = next(); --count;
    }
    while (count >= 2) {
        const uint32_t lo = next(), hi = next();
        // All supported targets are little endian; dst is now 32-bit aligned.
        const uint32_t pair = lo | (hi << 16);
#if defined(__GNUC__)
        // IDF disables ordinary memcpy builtins; request the intrinsic so
        // this remains one store rather than a library call per pixel pair.
        __builtin_memcpy(__builtin_assume_aligned(dst, 4), &pair, sizeof(pair));
#else
        std::memcpy(dst, &pair, sizeof(pair));
#endif
        dst += 2; count -= 2;
    }
    if (count) *dst = next();
}

template<bool Facade>
#if defined(__GNUC__)
__attribute__((noinline))
#endif
static void PERF_CRITICAL drawIndexed8(uint16_t* dst, int count, const uint8_t* texels, const uint16_t* palette,
                               unsigned width, unsigned height,
                               int32_t u, int32_t v, int32_t du, int32_t dv,
                               uint8_t alpha, bool additive,
                               const RGB565ConstantBlend* fade) {
    if (!fade && alpha == 255 && !additive) {
        sampleIndexed8Row<Facade>(dst, count, texels, palette, width, height, u, v, du, dv);
        return;
    }
    // Match the destination's alignment so the blend can use direct loads.
    // Bounded stack storage is private to each raster worker.
    alignas(16) uint16_t storage[128 + 8];
    uint16_t* pixels = storage + ((reinterpret_cast<uintptr_t>(dst) & 15) / 2);
    while (count > 0) {
        const int n = std::min(count, 128);
        sampleIndexed8Row<Facade>(pixels, n, texels, palette, width, height, u, v, du, dv);
        if (fade) {
            if (alpha == 255 && !additive) {
                fade->blend(dst, pixels, n);
                dst += n; count -= n;
                continue;
            }
            // Preserve both blends and their separate integer rounding.
            fade->blend(pixels, pixels, n);
        }
        const auto mode = additive
            ? (alpha == 255 ? RGB565BlendMode::Add : RGB565BlendMode::AddAlpha256)
            : RGB565BlendMode::Alpha256;
        blendRGB565Span(dst, pixels, n, 0, alpha, mode);
        dst += n; count -= n;
    }
}

static inline void drawIndexed8(uint16_t* dst, int count, const uint8_t* texels, const uint16_t* palette,
                 unsigned width, unsigned height,
                 int32_t u, int32_t v, int32_t du, int32_t dv,
                 uint8_t alpha, bool additive, const RGB565ConstantBlend* fade) {
    if (width == 32 && height == 64 && FIXED_POINT_SCALE == 1024)
        drawIndexed8<true>(dst, count, texels, palette, width, height, u, v, du, dv, alpha, additive, fade);
    else
        drawIndexed8<false>(dst, count, texels, palette, width, height, u, v, du, dv, alpha, additive, fade);
}

#undef JET_TEXTURE_INLINE

} // namespace TextureSpans
} // namespace Renderer
#endif
