#pragma once

#include <algorithm>
#include <cstdint>
#include <climits>

namespace Renderer::Detail {

struct SpanPoint { int32_t x, y; };

struct ScanEdge {
    int32_t x = 0, step = 0;
    uint32_t remainder = 0, stepRemainder = 0, divisor = 1;

    // Requires a.y < b.y and inc in {1,2}. x/remainder represent the
    // exact intersection as floor(x) plus a nonnegative fraction. Keeping
    // the remainder avoids fixed-point drift at inclusive triangle edges.
    void reset(SpanPoint a, SpanPoint b, int32_t y, int inc) {
        const int32_t dx = b.x - a.x;
        const int32_t dy = b.y - a.y;
        divisor = (uint32_t)dy;
        int32_t unitStep = dx / dy;
        int32_t unitRemainder = dx - unitStep * dy;
        if (unitRemainder < 0) { --unitStep; unitRemainder += dy; }
        step = unitStep * inc;
        stepRemainder = (uint32_t)unitRemainder * inc;
        if (stepRemainder >= divisor) { ++step; stepRemainder -= divisor; }

        const int32_t offset = y - a.y;
        if (offset == 0) { x = a.x; remainder = 0; }
        else if (offset == 1) { x = a.x + unitStep; remainder = unitRemainder; }
        else if (offset == inc) { x = a.x + step; remainder = stepRemainder; }
        else {
            const int64_t n = (int64_t)dx * offset;
            int32_t q = n >= INT32_MIN && n <= INT32_MAX
                      ? (int32_t)n / dy : (int32_t)(n / dy);
            int32_t r = (int32_t)(n - (int64_t)q * dy);
            if (r < 0) { --q; r += dy; }
            x = a.x + q;
            remainder = (uint32_t)r;
        }
    }

    void advance() {
        const uint32_t sum = remainder + stepRemainder;
        const bool carry = sum >= divisor;
        x += step + carry;
        remainder = sum - (carry ? divisor : 0);
    }
};

// The bounded coordinate domain keeps row state in int32_t, including
// the harmless final advance past a segment. Larger inputs use the
// original wide edge-function solver in the caller. The caller starts at
// firstY, calls beginRow before reading bounds, then advance after every
// row (including empty spans). Rows must advance by the same inc (1 or 2).
struct TriangleSpans {
    ScanEdge left, right;
    SpanPoint middle{}, bottom{};
    int32_t firstY = 0, switchY = INT32_MAX;
    bool shortLeft = false, valid = false;

    TriangleSpans(SpanPoint a, SpanPoint b, SpanPoint c, int32_t y, int inc) {
        constexpr int32_t limit = 1 << 20;
        for (const auto p : {a,b,c})
            if (p.x < -limit || p.x > limit || p.y < -limit || p.y > limit) return;
        const int64_t area = (int64_t)(b.x-a.x)*(c.y-a.y) - (int64_t)(b.y-a.y)*(c.x-a.x);
        if (area <= 0) return; // Preserve original degenerate/back-facing behavior.
        if (a.y > b.y) std::swap(a,b);
        if (b.y > c.y) std::swap(b,c);
        if (a.y > b.y) std::swap(a,b);
        firstY = y;
        if (firstY < a.y) firstY += ((a.y-firstY + inc-1)/inc)*inc;
        if (firstY > c.y) return;
        middle = b; bottom = c;
        shortLeft = (int64_t)(b.x-a.x)*(c.y-a.y) < (int64_t)(c.x-a.x)*(b.y-a.y);
        ScanEdge& longEdge = shortLeft ? right : left;
        ScanEdge& shortEdge = shortLeft ? left : right;
        longEdge.reset(a,c,firstY,inc);
        if (firstY < b.y || b.y == c.y) {
            shortEdge.reset(a,b,firstY,inc);
            if (b.y < c.y) switchY = b.y;
        } else shortEdge.reset(b,c,firstY,inc);
        valid = true;
    }

    void beginRow(int32_t y, int inc) {
        if (y >= switchY) {
            (shortLeft ? left : right).reset(middle,bottom,y,inc);
            switchY = INT32_MAX;
        }
    }

    void advance() { left.advance(); right.advance(); }
};

} // namespace Renderer::Detail
