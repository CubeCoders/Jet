#include "Sprite2D.hpp"
#include <algorithm>
#include <cstdio>
#include <vector>
using namespace Renderer;

namespace {
constexpr int width = 137, height = 43;
void reference(std::vector<uint16_t>& pixels, const Sprite2D& sp) {
    if (!sp.enabled || !sp.material || sp.scale <= 0) return;
    const int alpha = sp.alpha * sp.material->alpha / 255;
    if (!alpha) return;
    const Texture* tex = sp.material->diffuseMap;
    const int sw = tex ? tex->width * ((sp.textureFlags & Sprite2D::MIRROR_X) ? 2 : 1) : sp.width;
    const int sh = tex ? tex->height * ((sp.textureFlags & Sprite2D::MIRROR_Y) ? 2 : 1) : sp.height;
    const int w = sw * sp.scale, h = sh * sp.scale;
    for (int y = std::max(0, sp.y); y < std::min(height, sp.y + h); ++y)
        for (int x = std::max(0, sp.x); x < std::min(width, sp.x + w); ++x) {
            uint16_t s = sp.material->color;
            if (tex) {
                int tx = ((x - sp.x) * ((sw << 8) / w)) >> 8;
                int ty = ((y - sp.y) * ((sh << 8) / h)) >> 8;
                if (sp.textureFlags & Sprite2D::FLIP_X) tx = sw - 1 - tx;
                if (sp.textureFlags & Sprite2D::FLIP_Y) ty = sh - 1 - ty;
                if (tx >= tex->width) tx = sw - 1 - tx;
                if (ty >= tex->height) ty = sh - 1 - ty;
                s = tex->data[ty * tex->width + tx];
            }
            if (tex && tex->hasAlpha && s == tex->alphaColor) continue;
            const uint16_t d = pixels[y * width + x];
            unsigned out = 0;
            for (int shift : {0, 5, 11}) {
                const unsigned mask = shift == 5 ? 63 : 31;
                const unsigned sc = (s >> shift) & mask, dc = (d >> shift) & mask;
                const unsigned c = sp.blendMode == BlendMode::BLEND_ADD ? std::min(sc + dc, mask) :
                    (sc * alpha + dc * (255 - alpha)) / 255;
                out |= c << shift;
            }
            pixels[y * width + x] = (uint16_t)out;
        }
}
}

int main() {
    std::vector<uint16_t> actual(width * height), expected(actual.size()), texels(47 * 17);
    for (size_t i = 0; i < texels.size(); ++i) texels[i] = i % 5 ? (uint16_t)(i * 941) : 0x7bef;
    Texture texture(47, 17, texels.data(), true, 0x7bef);
    Material material(0x5bad), underlayMaterial(0x821f);
    Sprite2D sprite = makeSolidRect(0, 0, 47, 17, &material);
    Sprite2D underlay = makeSolidRect(3, 2, width - 6, height - 4, &underlayMaterial);
    underlay.alpha = 71; underlay.zOrder = -1;
    Sprite2D disabled = underlay;
    disabled.enabled = false;
    Sprite2D invalid = underlay;
    invalid.scale = 0;
    Sprite2D noMaterial = underlay;
    noMaterial.material = nullptr;
    Sprite2D* ordered[] = {nullptr, &underlay, &disabled, &invalid, &noMaterial, &sprite};
    std::vector<uint16_t> row(width + 2);
    unsigned frames = 0;
    for (int transform = 0; transform < 16; ++transform)
    for (int scale : {1, 2, 3, 4}) for (int x : {-151, -19, -1, 0, 7, 120, 145})
        for (int y : {-31, -1, 7, 39}) for (int kind = 0; kind < 3; ++kind)
            for (int alpha : {0, 127, 255}) for (int matAlpha : {0, 173, 255}) for (int add = 0; add < 2; ++add) {
                sprite.textureFlags = (uint8_t)transform;
                sprite.x = x; sprite.y = y; sprite.scale = scale; sprite.alpha = (uint8_t)alpha;
                sprite.blendMode = add ? BlendMode::BLEND_ADD : BlendMode::BLEND_REPLACE;
                material.alpha = (uint8_t)matAlpha; material.diffuseMap = kind ? &texture : nullptr;
                texture.hasAlpha = kind == 2;
                for (size_t i = 0; i < actual.size(); ++i) actual[i] = expected[i] = (uint16_t)(i * 7919);
                reference(expected, underlay); reference(expected, sprite);
                for (bool swapped : {false, true}) {
                    for (int yy = 0; yy < height; ++yy) {
                        row.front() = 0x1234; row.back() = 0xabcd;
                        for (int xx = 0; xx < width; ++xx) {
                            uint16_t pixel = uint16_t((yy * width + xx) * 7919);
                            row[xx + 1] = swapped ? uint16_t((pixel << 8) | (pixel >> 8)) : pixel;
                        }
                        compositeSprites(row.data() + 1, width, yy, ordered, 6, swapped);
                        if (row.front() != 0x1234 || row.back() != 0xabcd) return 2;
                        for (int xx = 0; xx < width; ++xx) {
                            uint16_t pixel = row[xx + 1];
                            actual[yy * width + xx] = swapped ? uint16_t((pixel << 8) | (pixel >> 8)) : pixel;
                        }
                    }
                if (actual != expected) {
                    std::printf("Sprite mismatch: transform %d scale %d xy %d,%d kind %d alpha %d/%d add %d\n",
                                transform, scale, x, y, kind, alpha, matAlpha, add); return 1;
                }
                }
                ++frames;
            }
    std::printf("Sprite scanline compositor (both byte orders): %u clipped/scaled/ordered frames match\n", frames);
}
