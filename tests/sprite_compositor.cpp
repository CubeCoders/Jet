#include "Scene.hpp"
#include <algorithm>
#include <cstdio>
#include <vector>
using namespace Renderer;

namespace {
constexpr int width = 137, height = 43;
void reference(std::vector<uint16_t>& pixels, const Sprite2D& sp) {
    if (!sp.enabled || !sp.material) return;
    const int alpha = sp.alpha * sp.material->alpha / 255;
    if (!alpha) return;
    const Texture* tex = sp.material->diffuseMap;
    const int sw = tex ? tex->width : sp.width, sh = tex ? tex->height : sp.height;
    const int w = sw * sp.scale, h = sh * sp.scale;
    for (int y = std::max(0, sp.y); y < std::min(height, sp.y + h); ++y)
        for (int x = std::max(0, sp.x); x < std::min(width, sp.x + w); ++x) {
            const uint16_t s = tex ? tex->data[(((y - sp.y) * ((sh << 8) / h)) >> 8) * sw +
                                                  (((x - sp.x) * ((sw << 8) / w)) >> 8)] : sp.material->color;
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
    static_assert(!HALF_WIDTH_BUFFERS && !FIELD_BUFFERS, "Use a full-width non-interlaced test configuration");
    std::vector<uint16_t> actual(width * height), expected(actual.size()), texels(47 * 17);
    for (size_t i = 0; i < texels.size(); ++i) texels[i] = i % 5 ? (uint16_t)(i * 941) : 0x7bef;
    Texture texture(47, 17, texels.data(), true, 0x7bef);
    Material material(0x5bad), underlayMaterial(0x821f);
    Sprite2D sprite = makeSolidRect(0, 0, 47, 17, &material);
    Sprite2D underlay = makeSolidRect(3, 2, width - 6, height - 4, &underlayMaterial);
    underlay.alpha = 71; underlay.zOrder = -1;
    Scene scene(actual.data(), nullptr, width, height);
    Scene background(expected.data(), nullptr, width, height);
    Camera camera;
    scene.setCamera(&camera); scene.setClearBuffer(false);
    background.setCamera(&camera); background.setClearBuffer(false);
    // Register in reverse order to verify the stable z-order pass as well.
    scene.addSprite(&sprite); scene.addSprite(&underlay);
    unsigned frames = 0;
    for (int scale : {1, 2, 3, 4}) for (int x : {-151, -19, -1, 0, 7, 120, 145})
        for (int y : {-31, -1, 7, 39}) for (int kind = 0; kind < 3; ++kind)
            for (int alpha : {0, 127, 255}) for (int matAlpha : {0, 173, 255}) for (int add = 0; add < 2; ++add) {
                sprite.x = x; sprite.y = y; sprite.scale = scale; sprite.alpha = (uint8_t)alpha;
                sprite.blendMode = add ? BlendMode::BLEND_ADD : BlendMode::BLEND_REPLACE;
                material.alpha = (uint8_t)matAlpha; material.diffuseMap = kind ? &texture : nullptr;
                texture.hasAlpha = kind == 2;
                for (size_t i = 0; i < actual.size(); ++i) actual[i] = expected[i] = (uint16_t)(i * 7919);
                background.render(); // Same configured PostFX, without sprites.
                reference(expected, underlay); reference(expected, sprite);
                scene.render();
                if (actual != expected) {
                    std::printf("Sprite mismatch: scale %d xy %d,%d kind %d alpha %d/%d add %d\n",
                                scale, x, y, kind, alpha, matAlpha, add); return 1;
                }
                ++frames;
            }
    std::printf("Sprite compositor: %u clipped/scaled/ordered frames match\n", frames);
}
