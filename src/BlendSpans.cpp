#include "BlendSpans.hpp"
#include "FastMath.hpp"
#include <algorithm>

namespace Renderer {
namespace {
inline uint16_t swap565(uint16_t p) { return (uint16_t)((p << 8) | (p >> 8)); }

// Packed lane factors for the S3 kernel. Values and sums stay below 32768,
// so signed saturating addition is exact even for full-strength additive G6.
struct alignas(16) KernelParams {
    // flags: public bits 0..2, then opaque copy, unaligned source,
    // unscaled additive, and constant foreground in bits 3..6.
    uint32_t sourceWeight, destWeight, normalizer, shift, flags, key;
    uint32_t padding[2];
    uint32_t normalizationLanes[4];
};
inline uint32_t repeat16(uint32_t v) { return v | (v << 16); }

#ifdef _MSC_VER
#define JET_BLEND_INLINE __forceinline
#else
#define JET_BLEND_INLINE inline __attribute__((always_inline))
#endif
template<RGB565BlendMode Mode>
static JET_BLEND_INLINE void scalarRangeImpl(
    uint16_t* dst, const uint16_t* src, int count, uint16_t col,
    uint8_t alpha, uint8_t flags, uint16_t key) {
    constexpr bool add = Mode == RGB565BlendMode::Add;
    constexpr bool scaledAdd = Mode == RGB565BlendMode::AddAlpha256;
    constexpr bool div255 = Mode == RGB565BlendMode::Alpha255;
    const unsigned sw = add ? 1u : scaledAdd ? (unsigned)alpha + 1 : alpha;
    const unsigned dw = add ? 1u : scaledAdd ? 256u : (div255 ? 255u : 256u) - alpha;
    while (count--) {
        const uint16_t s = src ? *src++ : col;
        if ((flags & BlendColorKey) && s == key) { ++dst; continue; }
        const uint16_t d = flags & BlendConstantBackground ? col :
            flags & BlendSwapDestination ? swap565(*dst) : *dst;
        unsigned r = (s >> 11) * sw + (d >> 11) * dw;
        unsigned g = ((s >> 5) & 63) * sw + ((d >> 5) & 63) * dw;
        unsigned b = (s & 31) * sw + (d & 31) * dw;
        if constexpr (div255) { r /= 255; g /= 255; b /= 255; }
        else if constexpr (!add) { r >>= 8; g >>= 8; b >>= 8; }
        if constexpr (add || scaledAdd) {
            r = std::min(r, 31u); g = std::min(g, 63u); b = std::min(b, 31u);
        }
        const uint16_t out = (uint16_t)((r << 11) | (g << 5) | b);
        *dst++ = flags & BlendSwapDestination ? swap565(out) : out;
    }
}

#if defined(CONFIG_IDF_TARGET_ESP32S3)
alignas(16) static const DRAM_ATTR uint32_t channelMasks[8] = {
    0x001f001f, 0x001f001f, 0x001f001f, 0x001f001f,
    0x003f003f, 0x003f003f, 0x003f003f, 0x003f003f
};
#define JET_BROADCAST(Q) \
    "ee.movi.32.q " Q ", %[tmp], 0\n\t" \
    "ee.movi.32.q " Q ", %[tmp], 1\n\t" \
    "ee.movi.32.q " Q ", %[tmp], 2\n\t" \
    "ee.movi.32.q " Q ", %[tmp], 3\n\t"
#define JET_SHIFT(N) "movi %[tmp], " N "\n\twsr %[tmp], sar\n\t"
// q0/q1 retain input pixels; q2/q3 retain weights; q7 accumulates output.
// Mask after the 32-bit shift isolates each 16-bit lane, including sign fill.
#define JET_CHANNEL(SHIFT, MASK_OFFSET) \
    JET_SHIFT(SHIFT) \
    "ee.vsr.32 q5, q0\n\tee.vsr.32 q6, q1\n\t" \
    "addi %[tmp], %[masks], " MASK_OFFSET "\n\tee.vld.128.ip q4, %[tmp], 0\n\t" \
    "ee.andq q5, q5, q4\n\tee.andq q6, q6, q4\n\t" \
    "l32i %[tmp], %[params], 16\n\tbbsi %[tmp], 5, 6f\n\t" \
    JET_SHIFT("0") \
    "ee.vmul.u16 q5, q5, q2\n\tee.vmul.u16 q6, q6, q3\n\t" \
    "ee.vadds.s16 q5, q5, q6\n\t" \
    "addi %[tmp], %[params], 32\n\tee.vld.128.ip q6, %[tmp], 0\n\t" \
    "l32i %[tmp], %[params], 12\n\twsr %[tmp], sar\n\t" \
    "ee.vmul.u16 q5, q5, q6\n\t" \
    "j 7f\n\t6:\n\tee.vadds.s16 q5, q5, q6\n\t7:\n\t" \
    "ee.vmin.s16 q5, q5, q4\n\t" \
    JET_SHIFT(SHIFT) \
    "ee.vsl.32 q5, q5\n\t"

static inline __attribute__((always_inline)) void blendBlocks(
    uint16_t* dst, const uint16_t* src, const uint16_t* background,
    const KernelParams& p, int blocks) {
    uint32_t savedSar, tmp;
    // One asm block owns all QR/SAR state. Restore SAR because scalar GCC
    // may keep a shift count there across the call site. No hidden QR state
    // escapes to another function or across a scalar fallback.
    __asm__ volatile (
        "rsr %[saved], sar\n\t"
        "l32i %[tmp], %[params], 0\n\t" JET_BROADCAST("q2")
        "l32i %[tmp], %[params], 4\n\t" JET_BROADCAST("q3")
        "9:\n\t"
        "ee.vld.128.ip q0, %[src], 0\n\t"
        "l32i %[tmp], %[params], 16\n\tbbci %[tmp], 4, 1f\n\t"
        "addi %[tmp], %[src], 16\n\tee.vld.128.ip q5, %[tmp], 0\n\t"
        "addi %[tmp], %[src], -1\n\tee.srcxxp.2q q5, q0, %[tmp], %[tmp]\n\t"
        "1:\n\t"
        "ee.vld.128.ip q1, %[bg], 0\n\t"
        "l32i %[tmp], %[params], 16\n\t"
        "bbci %[tmp], 0, 1f\n\t"
        "ee.orq q5, q1, q1\n\tee.vunzip.8 q1, q5\n\tee.vzip.8 q5, q1\n\t"
        "1:\n\t"
        "l32i %[tmp], %[params], 16\n\tbbci %[tmp], 3, 5f\n\t"
        "ee.orq q7, q0, q0\n\tj 4f\n\t5:\n\t"
        JET_CHANNEL("11", "0")
        "ee.orq q7, q5, q5\n\t"
        JET_CHANNEL("5", "16")
        "ee.orq q7, q7, q5\n\t"
        JET_CHANNEL("0", "0")
        "ee.orq q7, q7, q5\n\t"
        "4:\n\t"
        "l32i %[tmp], %[params], 16\n\t"
        "bbci %[tmp], 1, 2f\n\t"
        "l32i %[tmp], %[params], 20\n\t" JET_BROADCAST("q4")
        "ee.vcmp.eq.s16 q4, q0, q4\n\t"
        "ee.andq q5, q1, q4\n\tee.notq q4, q4\n\t"
        "ee.andq q7, q7, q4\n\tee.orq q7, q7, q5\n\t"
        "2:\n\t"
        "l32i %[tmp], %[params], 16\n\t"
        "bbci %[tmp], 0, 3f\n\t"
        "ee.orq q5, q7, q7\n\tee.vunzip.8 q7, q5\n\tee.vzip.8 q5, q7\n\t"
        "3:\n\t"
        "ee.vst.128.ip q7, %[dst], 0\n\t"
        "addi %[dst], %[dst], 16\n\t"
        "l32i %[tmp], %[params], 16\n\t"
        "bbsi %[tmp], 6, 8f\n\taddi %[src], %[src], 16\n\t8:\n\t"
        "bbsi %[tmp], 2, 8f\n\taddi %[bg], %[bg], 16\n\t8:\n\t"
        "addi %[blocks], %[blocks], -1\n\tbnez %[blocks], 9b\n\t"
        "wsr %[saved], sar\n\t"
        : [saved] "=&r"(savedSar), [tmp] "=&r"(tmp), [blocks] "+&r"(blocks),
          [src] "+&r"(src), [bg] "+&r"(background), [dst] "+&r"(dst)
        : [params] "r"(&p), [masks] "r"(channelMasks) : "memory"
    );
}
#undef JET_CHANNEL
#undef JET_SHIFT
#undef JET_BROADCAST
#endif
} // namespace

void PERF_CRITICAL blendRGB565Span(uint16_t* dst, const uint16_t* src, int count,
                                  uint16_t solidColor, uint8_t alpha,
                                  RGB565BlendMode mode, uint8_t flags, uint16_t key) {
    if (count <= 0) return;
    const bool swapped = (flags & BlendSwapDestination) != 0;
    const bool keyed = (flags & BlendColorKey) != 0;
    const bool constantBG = (flags & BlendConstantBackground) != 0;
    const bool add = mode == RGB565BlendMode::Add;
    const bool scaledAdd = mode == RGB565BlendMode::AddAlpha256;
    const bool divide255 = mode == RGB565BlendMode::Alpha255;
    const bool copy = divide255 && alpha == 255;
    const unsigned sw = add ? 1u : scaledAdd ? (unsigned)alpha + 1 : alpha;
    const unsigned dw = add ? 1u : scaledAdd ? 256u : (divide255 ? 255u : 256u) - alpha;

    auto scalarPixel = [&]() {
        const uint16_t s = src ? *src++ : solidColor;
        if (!keyed || s != key) {
            if (copy) {
                *dst = swapped ? swap565(s) : s;
                ++dst; --count;
                return;
            }
            const uint16_t d = constantBG ? solidColor : swapped ? swap565(*dst) : *dst;
            auto channel = [&](unsigned shift, unsigned mask) {
                unsigned v = ((s >> shift) & mask) * sw + ((d >> shift) & mask) * dw;
                if (!add) v = divide255 ? v / 255 : v / 256;
                return std::min(v, mask) << shift;
            };
            const uint16_t out = (uint16_t)(channel(11, 31) | channel(5, 63) | channel(0, 31));
            *dst = swapped ? swap565(out) : out;
        }
        ++dst; --count;
    };

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    const uintptr_t s = (uintptr_t)src, d = (uintptr_t)dst;
    // Forward overlapping reads can depend on earlier writes (current-frame
    // water). Preserve scalar ordering in that case. Other spans may peel
    // a few pixels to align both streams, without reading outside the span.
    const bool feedback = src && s < d && d - s < (uintptr_t)count * 2;
    const bool matchingAlignment = !src || ((s ^ d) & 15) == 0;
    if (count >= (matchingAlignment ? 16 : 32) && !feedback && !(constantBG && (swapped || keyed))) {
        // Unaligned vector loads round down. Consume a complete block first
        // and leave a complete block at the end, keeping both loads within
        // the caller's source range even for tightly allocated sprite rows.
        if (!matchingAlignment) for (int i = 0; i < 8; ++i) scalarPixel();
        while (count && ((uintptr_t)dst & 15)) scalarPixel();
        alignas(16) uint16_t constant[8];
        if (!src || constantBG) std::fill(constant, constant + 8, solidColor);
        // floor(x/255) == (x*32897)>>23 for every 16-bit x. Products here
        // are at most 32256, so /255 and /256 exactly match scalar rounding.
        KernelParams p = {repeat16(sw), repeat16(dw),
            repeat16(add ? 1u : divide255 ? 32897u : 256u),
            add ? 0u : divide255 ? 23u : 16u,
            (uint32_t)flags | (copy ? 8u : 0u) | (!matchingAlignment ? 16u : 0u) |
                (add ? 32u : 0u) | (!src ? 64u : 0u), repeat16(key), {}, {}};
        std::fill(p.normalizationLanes, p.normalizationLanes + 4, p.normalizer);
        const int pixels = (count - (matchingAlignment ? 0 : 8)) & ~7;
        blendBlocks(dst, src ? src : constant, constantBG ? constant : dst, p, pixels / 8);
        dst += pixels; if (src) src += pixels; count -= pixels;
    }
#endif
    if (copy) { while (count) scalarPixel(); return; }
    switch (mode) {
    case RGB565BlendMode::Alpha255:
        scalarRangeImpl<RGB565BlendMode::Alpha255>(dst, src, count, solidColor, alpha, flags, key); break;
    case RGB565BlendMode::Alpha256:
        scalarRangeImpl<RGB565BlendMode::Alpha256>(dst, src, count, solidColor, alpha, flags, key); break;
    case RGB565BlendMode::Add:
        scalarRangeImpl<RGB565BlendMode::Add>(dst, src, count, solidColor, alpha, flags, key); break;
    default:
        scalarRangeImpl<RGB565BlendMode::AddAlpha256>(dst, src, count, solidColor, alpha, flags, key); break;
    }
}

void RGB565ConstantBlend::prepare(uint16_t solidColor, uint8_t a, bool constantBackground) {
    color = solidColor; alpha = a; background = constantBackground;
    const unsigned fixedWeight = background ? 256u - a : a;
    const unsigned variableWeight = background ? a : 256u - a;
    // Preweight the constant operand, so each block only unpacks/multiplies
    // its variable pixels. Even G6 sums stay <= 63*256, below signed16 max.
    for (int i = 0; i < 4; ++i) {
        lanes[i] = repeat16(variableWeight);
        lanes[4+i] = repeat16((color >> 11) * fixedWeight);
        lanes[8+i] = repeat16(((color >> 5) & 63) * fixedWeight);
        lanes[12+i] = repeat16((color & 31) * fixedWeight);
        lanes[16+i] = repeat16(256);
    }
}
void PERF_CRITICAL RGB565ConstantBlend::blend(uint16_t* dst, const uint16_t* src, int count) const {
    if (count <= 0) return;
    const uint16_t* input = background ? src : dst;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    const auto s = (uintptr_t)input, d = (uintptr_t)dst;
    // Equal alignment permits direct vector loads. Forward feedback and
    // mismatched alignment use the existing helper with its scalar guards.
    if (count >= 16 && ((s ^ d) & 15) == 0 && !(s < d && d - s < (uintptr_t)count * 2)) {
        auto scalar = [&]() {
            const unsigned v = *input++;
            const unsigned sw = background ? alpha : 256u - alpha;
            const unsigned cw = 256u - sw;
            *dst++=(uint16_t)(((((v>>11)*sw+(color>>11)*cw)>>8)<<11)
                | (((((v>>5)&63)*sw+((color>>5)&63)*cw)>>8)<<5)
                | (((v&31)*sw+(color&31)*cw)>>8));
            --count;
        };
        while (count && ((uintptr_t)dst & 15)) scalar();
        int blocks = count / 8;
        const int pixels = blocks * 8;
        uint32_t saved, tmp;
#define CSHIFT(N) "movi %[tmp], " N "\n\twsr %[tmp], sar\n\t"
#define CCHANNEL(SH,OFF,MASK) \
        CSHIFT(SH) "ee.vsr.32 q5, q0\n\t" \
        "addi %[tmp], %[masks], " MASK "\n\tee.vld.128.ip q4, %[tmp], 0\n\t" \
        "ee.andq q5, q5, q4\n\t" CSHIFT("0") \
        "ee.vmul.u16 q5, q5, q2\n\t" \
        "addi %[tmp], %[params], " OFF "\n\tee.vld.128.ip q3, %[tmp], 0\n\t" \
        "ee.vadds.s16 q5, q5, q3\n\t" CSHIFT("16") \
        "ee.vmul.u16 q5, q5, q6\n\t" CSHIFT(SH) "ee.vsl.32 q5, q5\n\t"
        // Own all vector state inside this block; restore the scalar shift
        // register before returning to C++. No QR state crosses calls.
        __asm__ volatile (
            "rsr %[saved], sar\n\t"
            "mov %[tmp], %[params]\n\tee.vld.128.ip q2, %[tmp], 0\n\t"
            "addi %[tmp], %[params], 64\n\tee.vld.128.ip q6, %[tmp], 0\n\t"
            "9:\n\tee.vld.128.ip q0, %[input], 16\n\t"
            CCHANNEL("11","16","0") "ee.orq q7, q5, q5\n\t"
            CCHANNEL("5","32","16") "ee.orq q7, q7, q5\n\t"
            CCHANNEL("0","48","0") "ee.orq q7, q7, q5\n\t"
            "ee.vst.128.ip q7, %[dst], 16\n\t"
            "addi %[blocks], %[blocks], -1\n\tbnez %[blocks], 9b\n\t"
            "wsr %[saved], sar\n\t"
            : [saved] "=&r"(saved), [tmp] "=&r"(tmp), [blocks] "+&r"(blocks),
              [input] "+&r"(input), [dst] "+&r"(dst)
            : [params] "r"(lanes), [masks] "r"(channelMasks) : "memory");
#undef CCHANNEL
#undef CSHIFT
        count -= pixels;
        while (count) scalar();
        return;
    }
#endif
    blendRGB565Span(dst, background ? src : nullptr, count, color, alpha,
        RGB565BlendMode::Alpha256, background ? BlendConstantBackground : 0);
}
namespace {
template<bool Mirror>
JET_BLEND_INLINE void scaledSpan(uint16_t* dst, const uint16_t* src, int count,
                                int sourceX256, int step256, uint8_t alpha,
                                RGB565BlendMode mode, uint8_t flags, uint16_t key,
                                int sourceWidth = 0) {
    if (count <= 0) return;
    int firstX = sourceX256 >> 8;
    int lastX = (int)(((int64_t)sourceX256 + (int64_t)(count - 1) * step256) >> 8);
    if constexpr (Mirror) {
        // Conservative bounds also cover a span that crosses the symmetry
        // axis. Framebuffer-backed textures must preserve write feedback.
        firstX = 0;
        lastX = sourceWidth - 1;
    }
    const uintptr_t first = (uintptr_t)(src + std::min(firstX, lastX));
    const uintptr_t last = (uintptr_t)(src + std::max(firstX, lastX) + 1);
    if (first < (uintptr_t)(dst + count) && (uintptr_t)dst < last) {
        for (int i = 0; i < count; ++i, sourceX256 += step256) {
            int x = sourceX256 >> 8;
            if constexpr (Mirror) {
                if (x >= sourceWidth) x = 2 * sourceWidth - 1 - x;
            }
            blendRGB565Span(dst + i, src + x, 1, 0, alpha, mode, flags, key);
        }
        return;
    }
    // Stage a small row fragment in internal stack RAM. Matching the
    // destination phase keeps full vector loads/stores aligned. Mirrored
    // halves share the same tile to avoid two SIMD setups per row.
    alignas(16) uint16_t tile[128 + 8];
    while (count > 0) {
        const int n = std::min(count, 128);
        uint16_t* pixels = tile + (((uintptr_t)dst & 15) / 2);
        if constexpr (Mirror) {
            const int edge256 = sourceWidth << 8;
            int forward = 0;
            if (sourceX256 < edge256)
                forward = step256 ? std::min(n, (edge256 - sourceX256 + step256 - 1) / step256) : n;
            int x256 = sourceX256;
            for (int i = 0; i < forward; ++i, x256 += step256)
                pixels[i] = src[x256 >> 8];
            x256 = 2 * edge256 - 1 - x256;
            for (int i = forward; i < n; ++i, x256 -= step256)
                pixels[i] = src[x256 >> 8];
            sourceX256 += n * step256;
        } else {
            for (int i = 0; i < n; ++i, sourceX256 += step256)
                pixels[i] = src[sourceX256 >> 8];
        }
        blendRGB565Span(dst, pixels, n, 0, alpha, mode, flags, key);
        dst += n; count -= n;
    }
}
} // namespace
void PERF_CRITICAL blendRGB565ScaledSpan(uint16_t* dst, const uint16_t* src, int count,
                                        int sourceX256, int step256, uint8_t alpha,
                                        RGB565BlendMode mode, uint8_t flags, uint16_t key) {
    scaledSpan<false>(dst, src, count, sourceX256, step256, alpha, mode, flags, key);
}
void PERF_CRITICAL blendRGB565MirroredSpan(uint16_t* dst, const uint16_t* src, int sourceWidth,
                                          int count, int sourceX256, int step256, uint8_t alpha,
                                          RGB565BlendMode mode, uint8_t flags, uint16_t key) {
    scaledSpan<true>(dst, src, count, sourceX256, step256, alpha, mode, flags, key, sourceWidth);
}
} // namespace Renderer
