#include "Sprite2D.hpp"
#include "BlendSpans.hpp"
#include <algorithm>

namespace Renderer {
void compositeSprites(uint16_t* line, int width, int y,
                      Sprite2D* const* sprites, int count, bool swapDestination) {
    if (!line || !sprites || width <= 0 || count <= 0) return;
    for (int i = 0; i < count; ++i) {
        const Sprite2D* sp = sprites[i];
        if (!sp || !sp->enabled || !sp->material || sp->scale <= 0) continue;
        const int alpha = sp->alpha * sp->material->alpha / 255;
        const int sw = sp->sourceWidth(), sh = sp->sourceHeight();
        if (!alpha || sw <= 0 || sh <= 0) continue;
        const int64_t sy = int64_t(y) - sp->y;
        if (sy < 0 || sy >= int64_t(sh) * sp->scale) continue;
        const int left = sp->x < 0 ? 0 : sp->x;
        const int64_t right = std::min<int64_t>(width, int64_t(sp->x) + int64_t(sw) * sp->scale);
        if (left >= right) continue;
        if (sp->material->diffuseMap) {
            sp->blendTextureRow(line + left, int(right - left), left - sp->x,
                                int(sy), uint8_t(alpha), swapDestination);
        } else {
            blendRGB565Span(line + left, nullptr, int(right - left), sp->material->color,
                uint8_t(alpha), sp->blendMode == BlendMode::BLEND_ADD
                    ? RGB565BlendMode::Add : RGB565BlendMode::Alpha255,
                swapDestination ? BlendSwapDestination : 0);
        }
    }
}

void Sprite2D::blendTextureRow(uint16_t* dst, int count, int outputX, int outputY,
                               uint8_t combinedAlpha, bool swapDestination) const {
    const Texture& tex = *material->diffuseMap;
    const int step = 256 / scale;
    int sy = (outputY * step) >> 8;
    if (textureFlags & MIRROR_Y) {
        if (sy >= tex.height) sy = 2 * tex.height - 1 - sy;
    } else if (textureFlags & FLIP_Y) {
        sy = tex.height - 1 - sy;
    }
    const uint16_t* row = tex.data + sy * tex.width;
    const auto mode = blendMode == BlendMode::BLEND_ADD
        ? RGB565BlendMode::Add : RGB565BlendMode::Alpha255;
    const uint8_t flags = (swapDestination ? BlendSwapDestination : 0)
                       | (tex.hasAlpha ? BlendColorKey : 0);
    auto span = [&](int n, int x256, int delta) {
        if (delta == 256)
            blendRGB565Span(dst, row + (x256 >> 8), n, 0, combinedAlpha,
                           mode, flags, tex.alphaColor);
        else
            blendRGB565ScaledSpan(dst, row, n, x256, delta, combinedAlpha,
                                 mode, flags, tex.alphaColor);
    };
    const int x256 = outputX * step;
    const int edge256 = tex.width << 8;
    if (textureFlags & MIRROR_X) {
        // Retain the full sprite's fp8 phase across the seam. A horizontal
        // flip of this expanded symmetric image is a no-op.
        blendRGB565MirroredSpan(dst, row, tex.width, count, x256, step,
                               combinedAlpha, mode, flags, tex.alphaColor);
    } else if (textureFlags & FLIP_X) {
        // Subtract from the last fractional coordinate, not the last texel,
        // so floor() samples the same texel as a pre-flipped source image.
        span(count, edge256 - 1 - x256, -step);
    } else {
        span(count, x256, step);
    }
}
} // namespace Renderer
