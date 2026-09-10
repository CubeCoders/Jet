#ifndef JET_SPRITE2D_HPP
#define JET_SPRITE2D_HPP

/// @file Sprite2D.hpp
/// @brief Screen-space 2D overlay: solid rectangle or unscaled upright sprite.
///
/// A Sprite2D is owned by the caller and registered with Scene::addSprite().
/// After registration it is drawn automatically at the end of every
/// Scene::render() call, after all 3D geometry and PostFX, so it always
/// composites on top of the world.
///
/// Rendering rules:
///   - `material->diffuseMap != nullptr`  →  blit the texture at (x, y);
///     pixels equal to `diffuseMap->alphaColor` are skipped (colour-key)
///     when `diffuseMap->hasAlpha` is true.
///   - `material->diffuseMap == nullptr`  →  fill a `width × height` solid
///     rectangle with `material->color`.
///
/// The effective alpha is `material->alpha * alpha / 255`.  At 255 the blit
/// is a straight copy (fastest path).  Any other value blends against the
/// existing framebuffer content with a fast 5-bit lerp.
///
/// `zOrder` controls draw order within the sprite pass: lower values are
/// drawn first (further back).  There is no framebuffer depth test — sprites
/// always paint over everything rendered before them.

#include <cstdint>
#include "Material.hpp"
#include "Object.hpp"   // for BlendMode enum

namespace Renderer {

/// @brief Screen-space 2D overlay registered with a Scene.
struct Sprite2D {
    int         x        = 0;       ///< Left edge in pixels (screen space, 0 = left).
    int         y        = 0;       ///< Top edge in pixels (screen space, 0 = top).
    int         width    = 0;       ///< Width in pixels.  Ignored when diffuseMap is set (texture dimensions are used).
    int         height   = 0;       ///< Height in pixels.  Ignored when diffuseMap is set.
    Material*   material = nullptr; ///< Surface description: color + optional diffuseMap.  Required.
    uint8_t     alpha    = 255;     ///< Per-sprite alpha multiplied with material->alpha.
    int         zOrder   = 0;       ///< Draw order within the 2D pass; lower = drawn first (behind).
    bool        enabled  = true;    ///< When false the sprite is skipped entirely.
    /// @brief Blend equation used when writing this sprite onto the framebuffer.
    ///
    /// BLEND_REPLACE — normal alpha-lerp composite (default).
    /// BLEND_ADD     — saturating additive: dst = clamp(dst + src * alpha).
    ///                 Ideal for glows, coronas and lens-flare elements drawn as
    ///                 grayscale soft-edged textures; produces bright halos that
    ///                 never over-darken the scene.
    BlendMode   blendMode = BlendMode::BLEND_REPLACE;
    /// @brief Integer upscale factor applied at blit time (1 = native size).
    ///
    /// When > 1, the sprite is rendered at texture_size × scale pixels using
    /// bilinear interpolation. Solid-colour (no texture) sprites scale their
    /// width/height instead. Useful for soft glow textures that would look too
    /// small at 1:1 — at 2× or 3× the smooth falloff still reads well.
    int         scale     = 1;
};

// ---------------------------------------------------------------------------
// Solid-color rectangle helpers
// ---------------------------------------------------------------------------
// Convenience functions for creating full-screen fades, letterboxes, and
// other solid-color overlay effects with alpha blending. Automatically
// optimizes for fully transparent (0% alpha → enabled=false) and fully
// opaque (100% alpha → fast blit path).
//
// OWNERSHIP: Caller owns both the returned Sprite2D and its Material.
// Both must remain alive until the sprite is removed from the scene.
//
// USAGE EXAMPLES:
//
// 1) Full-screen fade to black:
//      static Material fadeMat(0x0000, 0);  // Black, fully transparent initially
//      static Sprite2D fadeSprite = makeFullScreenFade(480, 320, 0x0000, &fadeMat);
//      scene->addSprite(&fadeSprite);
//      // ... in your game loop, fade in over time:
//      fadeMat.alpha = (uint8_t)(fadeProgress * 255);  // fadeProgress: 0.0-1.0
//
// 2) Letterbox bars for cinematic mode:
//      static Material topBarMat(0x0000, 255);     // Black, opaque
//      static Material botBarMat(0x0000, 255);
//      static Sprite2D topBar = makeLetterboxBar(480, 40, 0x0000, true, &topBarMat);
//      static Sprite2D botBar = makeSolidRect(0, 280, 480, 40, &botBarMat);
//      scene->addSprite(&topBar);
//      scene->addSprite(&botBar);
//
// 3) Custom colored overlay with dynamic alpha:
//      static Material overlayMat(0xF800, 128);  // Red at 50% alpha
//      static Sprite2D overlay = makeSolidRect(100, 100, 200, 150, &overlayMat);
//      scene->addSprite(&overlay);
//      // Toggle visibility:
//      setSolidRectAlpha(overlay, wantVisible ? 128 : 0);
//
// PERFORMANCE NOTES:
//   - alpha == 0:   sprite.enabled = false; rendering skipped entirely (zero cost)
//   - alpha == 255: fast opaque blit; no per-pixel blending (~3x faster than blended)
//   - alpha 1-254:  per-pixel alpha blend; moderate cost

/// @brief Create a Sprite2D configured as a solid-color rectangle.
/// @param x Left edge in screen pixels.
/// @param y Top edge in screen pixels.
/// @param width Width in pixels.
/// @param height Height in pixels.
/// @param material Material containing the RGB565 color and alpha (0-255).
///        Caller retains ownership; must remain valid while sprite is in use.
/// @return Configured Sprite2D. Caller owns this; must remain valid while
///         registered with Scene.
inline Sprite2D makeSolidRect(int x, int y, int width, int height,
                              Material* material) {
    Sprite2D sprite;
    sprite.x        = x;
    sprite.y        = y;
    sprite.width    = width;
    sprite.height   = height;
    sprite.material = material;
    sprite.alpha    = 255;  // Use material->alpha for transparency control
    sprite.zOrder   = 0;
    sprite.enabled  = (material && material->alpha > 0);  // Auto-skip when fully transparent
    sprite.blendMode = BlendMode::BLEND_REPLACE;
    sprite.scale    = 1;
    return sprite;
}

/// @brief Update a sprite's alpha, automatically disabling it when fully transparent.
/// @param sprite Sprite to update (typically a solid rectangle).
/// @param alpha New alpha value (0=transparent, 255=opaque).
///
/// Optimizations:
///   alpha == 0   → sprite.enabled = false (skips rendering entirely)
///   alpha == 255 → fast opaque blit path (no per-pixel blending)
///   1..254       → per-pixel alpha blend
inline void setSolidRectAlpha(Sprite2D& sprite, uint8_t alpha) {
    if (sprite.material) {
        sprite.material->alpha = alpha;
        sprite.enabled = (alpha > 0);  // Skip rendering when fully transparent
    }
}

/// @brief Convenience: create a full-screen fade overlay (typically black or white).
/// @param screenWidth Framebuffer width in pixels.
/// @param screenHeight Framebuffer height in pixels.
/// @param color RGB565 color (0x0000 for black, 0xFFFF for white).
/// @param material Material to use (caller must provide and keep alive).
/// @return Configured Sprite2D for a full-screen fade. Caller owns.
inline Sprite2D makeFullScreenFade(int screenWidth, int screenHeight,
                                   uint16_t color, Material* material) {
    if (material) material->color = color;
    return makeSolidRect(0, 0, screenWidth, screenHeight, material);
}

/// @brief Convenience: create a letterbox bar for cinematic mode.
/// @param screenWidth Framebuffer width in pixels.
/// @param barHeight Height of each letterbox bar in pixels.
/// @param color RGB565 color (typically 0x0000 for black bars).
/// @param topBar True for top bar, false for bottom bar.
/// @param material Material to use (caller must provide and keep alive).
/// @return Configured Sprite2D for one letterbox bar. Caller owns.
inline Sprite2D makeLetterboxBar(int screenWidth, int barHeight,
                                 uint16_t color, bool topBar,
                                 Material* material) {
    if (material) {
        material->color = color;
        material->alpha = 255;  // Letterboxes are always opaque
    }
    const int y = topBar ? 0 : -1;  // -1 sentinel: caller must set to (screenHeight - barHeight)
    return makeSolidRect(0, y, screenWidth, barHeight, material);
}

} // namespace Renderer

#endif // JET_SPRITE2D_HPP
