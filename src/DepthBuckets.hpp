#ifndef JET_DEPTH_BUCKETS_HPP
#define JET_DEPTH_BUCKETS_HPP
#include <cstdint>

namespace Renderer {
// Exact legacy division, narrowing and clamp with a shared per-frame divisor.
template<unsigned Count> class DepthBuckets {
    static_assert(Count > 0 && Count <= 256);
    uint32_t reciprocal = 0;
    uint32_t range = 1;
    unsigned originalIndex(int32_t depth) const {
        const int32_t q = int32_t(int64_t(depth) * Count / range);
        return q < 0 ? 0 : q >= int32_t(Count) ? Count - 1 : unsigned(q);
    }
public:
    void setRange(int32_t value) {
        range = value > 0 ? uint32_t(value) : 1;
        // A range wider than Count keeps this rounded reciprocal in uint32.
        reciprocal = range > Count
            ? uint32_t(((uint64_t(Count) << 32) + range - 1) / range) : 0;
    }
    unsigned index(int32_t depth) const {
        // Preserve legacy narrowing on tiny ranges, including extreme depths.
        if (range <= Count) return originalIndex(depth);
        if (depth <= 0) return 0;
        const uint32_t d = uint32_t(depth);
        if (d >= range) return Count - 1;
        const uint32_t q = uint32_t((uint64_t(d) * reciprocal) >> 32);
        // q is the exact quotient or one too high. The true product difference
        // is in [-range+1, range-1], so signed interpretation of its uint32
        // modular subtraction is exact even if either product wraps.
        return q - (int32_t(q * range - d * Count) > 0);
    }
};
}
#endif
