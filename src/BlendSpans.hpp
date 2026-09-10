#ifndef JET_BLEND_SPANS_HPP
#define JET_BLEND_SPANS_HPP
#include <cstdint>

namespace Renderer {
enum class RGB565BlendMode : uint8_t { Alpha256, Alpha255, Add, AddAlpha256 };
enum RGB565BlendFlags : uint8_t {
    BlendSwapDestination = 1,
    BlendColorKey = 2,
    BlendConstantBackground = 4
};

// Forward span blend. A null source means a solid foreground; with
// BlendConstantBackground, solidColor is the background instead (water).
// Alpha255 preserves sprite /255 rounding; Alpha256 preserves triangle /256.
// Add is the sprite's unscaled saturating add; AddAlpha256 scales source by
// (alpha+1)/256 before adding. Byte-swapped destinations are supported.
void blendRGB565Span(uint16_t* dst, const uint16_t* src, int count,
                    uint16_t solidColor, uint8_t alpha, RGB565BlendMode mode,
                    uint8_t flags = 0, uint16_t key = 0);

// Prepared /256 blend with one uniform colour. The variable pixels are
// foreground for water, or destination for a solid translucent triangle.
// Caller may reuse this state across spans with the same colour and alpha.
// Call prepare() before blend(). Source is required only for background=true;
// both modes accept native RGB565 and preserve forward framebuffer feedback.
struct alignas(16) RGB565ConstantBlend {
    uint32_t lanes[20];
    uint16_t color;
    uint8_t alpha;
    bool background;
    void prepare(uint16_t solidColor, uint8_t a, bool constantBackground);
    void blend(uint16_t* dst, const uint16_t* src, int count) const;
};

// Nearest-neighbour sprite row, using the compositor's original 8-bit fixed
// point source coordinate and step. Overlapping spans retain forward order.
void blendRGB565ScaledSpan(uint16_t* dst, const uint16_t* src, int count,
                          int sourceX256, int step256, uint8_t alpha,
                          RGB565BlendMode mode, uint8_t flags = 0, uint16_t key = 0);
}
#endif
