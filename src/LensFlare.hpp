#pragma once

#include <cstdint>
#include <cmath>
#include "Scene.hpp"
#include "Camera.hpp"
#include "Picking.hpp"
#include "Sprite2D.hpp"
#include "Material.hpp"
#include "Texture.hpp"
#include "Math.hpp"

namespace Renderer {

/// @brief Lens-flare effect driven by a directional light source.
///
/// Each frame, the helper:
///   1. Projects the light direction to a screen-space point.
///   2. When MAX_PICK_QUERIES > 0, uses a PickQuery on that pixel to test
///      whether any geometry occludes the "sun". No hit => sun visible.
///      When picking is compiled out, the sun is treated as unobstructed.
///   3. Repositions a fixed set of Sprite2D elements along the line that
///      passes through both the sun's screen position and the screen centre,
///      and sets their alpha to reflect the current visibility (with optional
///      fade-in/out smoothing).
///
/// Usage:
/// @code
///     // Create materials/textures for each element (caller owns them).
///     LensFlare::Element elems[3] = {
///         { &glowMat,   64, 64,  0.0f  },   // pinned to sun
///         { &ringMat,   32, 32,  0.5f  },   // halfway to centre
///         { &sparkMat,  16, 16, -0.3f  },   // beyond sun
///     };
///     LensFlare flare(scene, elems, 3, pickSlot, light);
///
///     // Each frame, before render():
///     flare.prepare(camera, screenW, screenH);
///
///     // After render():
///     flare.update(dt);
/// @endcode
///
/// @note MAX_PICK_QUERIES > 0 enables occlusion testing; the flare still
///       renders without occlusion when picking is compiled out.
/// @note The LensFlare registers all sprites with the scene on construction;
///       do not call scene->addSprite() for the same sprites separately.

class LensFlare {
public:
    /// @brief One sprite element in the flare chain.
    struct Element {
        Material* material  = nullptr; ///< Material (and optional texture) to draw.
        int       width     = 32;      ///< Sprite width in screen pixels.
        int       height    = 32;      ///< Sprite height in screen pixels.
        /// Position along the sun→centre axis.
        ///   0.0 = pinned to sun position.
        ///   1.0 = screen centre.
        ///  -0.3 = 30% past the sun (away from centre).
        ///   1.5 = 50% past the centre (away from sun).
        float     axisT     = 0.0f;
        /// Base alpha scale [0..1] for this element before global visibility.
        float     baseAlpha = 1.0f;
        /// Blend mode applied when compositing this element.
        /// Defaults to BLEND_ADD — the natural choice for glows and flares.
        BlendMode blendMode = BlendMode::BLEND_ADD;
        /// Integer upscale factor (1 = native texture size, 2 = 2×, etc.).
        int       scale     = 1;
        /// Sprite2D texture flags. Width/height describe the expanded image
        /// before scaling, including any mirrored halves.
        uint8_t   textureFlags = 0;
    };

    /// @param scene         Scene to register sprites with and to submit
    ///                      the pick query against. Borrowed; must outlive
    ///                      this object.
    /// @param elements      Array of element descriptors. Copied by value.
    /// @param count         Number of elements (must be <= MAX_FLARE_ELEMENTS).
    /// @param pickSlot      Which pick query index to use (0..MAX_PICK_QUERIES-1).
    ///                      The caller must not use this slot for other purposes.
    /// @param flareDir      World-space unit vector pointing FROM the scene TOWARD
    ///                      the visual sun (same convention as DirectionalLight::
    ///                      worldLightDir). Decoupled from the shadow/shading light
    ///                      so each can be tuned independently.
    /// @param fadeSpeed     How quickly visibility fades in/out (units: per second).
    ///                      Set to a very large value for instant on/off.
    LensFlare(Scene* scene,
              const Element* elements, int count,
              int pickSlot,
              Vec3f flareDir,
              float fadeSpeed = 4.0f)
        : scene(scene), flareDir(flareDir), pickSlot(pickSlot), fadeSpeed(fadeSpeed),
          elementCount(count > MAX_FLARE_ELEMENTS ? MAX_FLARE_ELEMENTS : count),
          visibility(0.0f), sunScreenX(0), sunScreenY(0), sunInFrustum(false)
    {
        for (int i = 0; i < elementCount; ++i) {
            elems[i]   = elements[i];
            sprites[i] = {};
            sprites[i].material  = elements[i].material;
            sprites[i].width     = elements[i].width;
            sprites[i].height    = elements[i].height;
            sprites[i].alpha     = 0;
            sprites[i].enabled   = false;
            sprites[i].zOrder    = 100 + i;  // draw above normal HUD
            sprites[i].blendMode = elements[i].blendMode;
            sprites[i].scale     = elements[i].scale;
            sprites[i].textureFlags = elements[i].textureFlags;
            scene->addSprite(&sprites[i]);
        }
    }

    /// @brief Call once per frame BEFORE scene->render() to submit the pick
    ///        query, when enabled, and compute sun screen position.
    ///
    /// @param camera     Active camera. Must match what scene will render with.
    /// @param screenW    Render width in pixels  (framebuffer coords).
    /// @param screenH    Render height in pixels (framebuffer coords).
    void prepare(const Camera* camera, int screenW, int screenH)
    {
        // --- Project flare direction to screen space ------------------
        // Use the caller-supplied direction (independent of shading light).
        // worldLightDir uses fixed-point scale 2^10 = 1024; match that here.
        Vector3 viewDir = camera->transformDirection({(int32_t)(flareDir.x * 1024.0f),
                                                      (int32_t)(flareDir.y * 1024.0f),
                                                      (int32_t)(flareDir.z * 1024.0f)});

        // Only visible if in front of the camera.
        sunInFrustum = (viewDir.z > 0);
        if (!sunInFrustum) {
            // Disable all sprites immediately.
            for (int i = 0; i < elementCount; ++i) sprites[i].enabled = false;
#if MAX_PICK_QUERIES > 0
            // Still need to clear the pick slot so it doesn't report stale results.
            PickQuery queries[MAX_PICK_QUERIES] = {};
            const int slot = pickSlot < 0 ? 0 : (pickSlot >= MAX_PICK_QUERIES ? MAX_PICK_QUERIES - 1 : pickSlot);
            for (int i = 0; i <= slot; ++i) { queries[i].x = -1; queries[i].y = -1; }
            scene->setPickQueries(queries, slot + 1);
#endif
            return;
        }

        // Perspective project: fovFactor encodes (screenW/2)/tan(fov/2).
        const float inv_z = camera->fovFactor / (float)viewDir.z;
        const float sx    = screenW  * 0.5f + viewDir.x * inv_z;
        const float sy    = screenH  * 0.5f - viewDir.y * inv_z;

        sunScreenX = (int)sx;
        sunScreenY = (int)sy;

        // Clamp pick-query coordinates to framebuffer bounds. The sun may
        // project slightly off-screen (e.g. near the top at sunrise) but the
        // intermediate flare elements are still on-screen; we only fully abort
        // when the sun is behind the camera (viewDir.z <= 0, caught above).
        const int pqx = sunScreenX < 0 ? 0 : (sunScreenX >= screenW  ? screenW  - 1 : sunScreenX);
        const int pqy = sunScreenY < 0 ? 0 : (sunScreenY >= screenH ? screenH - 1 : sunScreenY);

    #if MAX_PICK_QUERIES > 0
        // Submit pick query for the sun pixel. Scene::setPickQueries replaces
        // all queries, so fill disabled slots up through our selected slot.
        PickQuery queries[MAX_PICK_QUERIES] = {};
        const int slot = pickSlot < 0 ? 0 : (pickSlot >= MAX_PICK_QUERIES ? MAX_PICK_QUERIES - 1 : pickSlot);
        for (int i = 0; i <= slot; ++i) { queries[i].x = -1; queries[i].y = -1; }
        queries[slot].x = (int16_t)pqx;
        queries[slot].y = (int16_t)pqy;
        scene->setPickQueries(queries, slot + 1);
    #else
        (void)pqx;
        (void)pqy;
    #endif
    }

    /// @brief Call once per frame AFTER scene->render() to read pick results,
    ///        update visibility and reposition sprites.
    ///
    /// @param dt         Delta time in seconds since last update().
    /// @param screenW    Render width  (same value passed to prepare()).
    /// @param screenH    Render height (same value passed to prepare()).
    void update(float dt, int screenW, int screenH)
    {
        if (!sunInFrustum) {
            visibility = 0.0f;
            for (int i = 0; i < elementCount; ++i) sprites[i].enabled = false;
            return;
        }

        // Edge-proximity fade: find the largest drawn half-size among elements
        // pinned to the sun (axisT ≈ 0) and use it as the fade-in margin so
        // the flare gracefully disappears as the sun slides to the screen edge.
        // Note: sunScreenX/Y may be slightly outside [0,screenW/H) when the sun
        // is in front of the camera but just past a screen edge — that is fine;
        // edgeFade will naturally go to zero and the sprites become invisible.
        int sunHalfW = 32, sunHalfH = 32;
        for (int i = 0; i < elementCount; ++i) {
            if (fabsf(elems[i].axisT) < 0.01f) {
                const int dw = elems[i].width  * elems[i].scale;
                const int dh = elems[i].height * elems[i].scale;
                if (dw / 2 > sunHalfW) sunHalfW = dw / 2;
                if (dh / 2 > sunHalfH) sunHalfH = dh / 2;
            }
        }
        const float mW = (float)sunHalfW;
        const float mH = (float)sunHalfH;
        const float fL = ((float)sunScreenX             < mW) ? (float)sunScreenX             / mW : 1.0f;
        const float fR = ((float)(screenW-1-sunScreenX) < mW) ? (float)(screenW-1-sunScreenX) / mW : 1.0f;
        const float fT = ((float)sunScreenY             < mH) ? (float)sunScreenY             / mH : 1.0f;
        const float fB = ((float)(screenH-1-sunScreenY) < mH) ? (float)(screenH-1-sunScreenY) / mH : 1.0f;
        float edgeFade = fL < fR ? fL : fR;
        if (fT < edgeFade) edgeFade = fT;
        if (fB < edgeFade) edgeFade = fB;
        // Clamp to [0,1]: negative when sun is off-screen; sprites get alpha=0.
        if (edgeFade < 0.0f) edgeFade = 0.0f;

        bool occluded = false;
    #if MAX_PICK_QUERIES > 0
        const PickResult* results = scene->getPickResults();
        const int slot = pickSlot < 0 ? 0 : (pickSlot >= MAX_PICK_QUERIES ? MAX_PICK_QUERIES - 1 : pickSlot);
        occluded = results && scene->getPickQueryCount() > slot && results[slot].hit;
    #endif

        // Fade visibility toward target.
        const float target = occluded ? 0.0f : 1.0f;
        if (visibility < target)
            visibility = visibility + fadeSpeed * dt;
        else
            visibility = visibility - fadeSpeed * dt;
        if (visibility < 0.0f) visibility = 0.0f;
        if (visibility > 1.0f) visibility = 1.0f;

        if (visibility <= 0.0f) {
            for (int i = 0; i < elementCount; ++i) sprites[i].enabled = false;
            return;
        }

        // Screen centre.
        const float cx = screenW * 0.5f;
        const float cy = screenH * 0.5f;

        // Normalised direction: sun → screen centre (and beyond).
        // axisT is re-defined so that:
        //   -1 = screen edge BEHIND the sun (in the -d direction)
        //    0 = sun position
        //   +1 = screen edge AHEAD (in the +d direction, past centre)
        // We find those extents via a parametric line-rect intersection.
        const float rawX = cx - (float)sunScreenX;
        const float rawY = cy - (float)sunScreenY;
        const float len  = sqrtf(rawX * rawX + rawY * rawY);

        float ddx = 0.0f, ddy = 0.0f;
        float t_forward = 0.0f, t_behind = 0.0f;

        if (len > 0.5f) {
            ddx = rawX / len;
            ddy = rawY / len;

            // Uniform radius = distance from sun to the farthest screen corner.
            // This keeps axisT values on a consistent scale regardless of the
            // direction from sun to centre, so elements trace smooth circular
            // arcs as the camera rotates instead of varying with the angle.
            const float sunX = (float)sunScreenX;
            const float sunY = (float)sunScreenY;
            const float W    = (float)screenW;
            const float H    = (float)screenH;

            const float dc0 = sqrtf(sunX*sunX           + sunY*sunY);
            const float dc1 = sqrtf((W-sunX)*(W-sunX)   + sunY*sunY);
            const float dc2 = sqrtf(sunX*sunX           + (H-sunY)*(H-sunY));
            const float dc3 = sqrtf((W-sunX)*(W-sunX)   + (H-sunY)*(H-sunY));
            float maxDist = dc0;
            if (dc1 > maxDist) maxDist = dc1;
            if (dc2 > maxDist) maxDist = dc2;
            if (dc3 > maxDist) maxDist = dc3;

            t_forward = maxDist;  // positive axisT scaled against this
            t_behind  = maxDist;  // negative axisT symmetric
        }

        // Position and enable each sprite.
        for (int i = 0; i < elementCount; ++i) {
            const Element& e = elems[i];
            // Map axisT to a world-pixel offset: positive axisT scales against
            // the forward extent, negative against the behind extent.
            const float t  = (e.axisT >= 0.0f) ? e.axisT * t_forward
                                                : e.axisT * t_behind;
            const float px = (float)sunScreenX + ddx * t;
            const float py = (float)sunScreenY + ddy * t;
            // Centre on the actual drawn size (texture size × scale).
            const int dw = e.width  * e.scale;
            const int dh = e.height * e.scale;
            sprites[i].x       = (int)(px - dw * 0.5f);
            sprites[i].y       = (int)(py - dh * 0.5f);
            sprites[i].alpha   = (uint8_t)(visibility * edgeFade * e.baseAlpha * 255.0f);
            sprites[i].enabled = true;
        }
    }

    /// @brief Current sun visibility [0..1]. Useful for driving other effects
    ///        (e.g. sun glare tint on the background gradient).
    float getVisibility() const { return visibility; }

    /// @brief Whether the sun is currently within the camera frustum.
    bool isSunOnScreen() const { return sunInFrustum; }

    static constexpr int MAX_FLARE_ELEMENTS = 12;

private:
    Scene*   scene;
    Vec3f    flareDir;   ///< World-space direction FROM scene TOWARD visual sun.
    int      pickSlot;
    float    fadeSpeed;
    int      elementCount;
    float    visibility;
    int      sunScreenX, sunScreenY;
    bool     sunInFrustum;

    Element  elems[MAX_FLARE_ELEMENTS];
    Sprite2D sprites[MAX_FLARE_ELEMENTS];
};

} // namespace Renderer
