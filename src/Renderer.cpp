#include "Renderer.hpp"
#include <algorithm>
#include <cmath>
#include "TrigLUT.hpp"
#include "FastMath.hpp"
#include "TriangleSpans.hpp"
#include "BlendSpans.hpp"
#include <type_traits>

#if defined(CHECKERBOARD_MODE) && CHECKERBOARD_MODE && defined(FIELD_BUFFERS) && FIELD_BUFFERS
#error "CHECKERBOARD_MODE and FIELD_BUFFERS (interlaced) cannot both be enabled. Each would render only one quarter of pixels per frame and interact destructively. Pick one."
#endif

// JET_FAST_SIMPLE_SPANS: private to this TU. When the rasterizer is
// configured without any per-pixel machinery (z-buffer, lighting,
// brightness, debug overdraw, tile tagging, perspective textures) the inner
// pixel loop degenerates to "maybe write a constant color, gated by a
// triangle-constant dither mask". The scanline-range solver above the inner
// loop already computes the exact [xStart, xEnd] range where the triangle
// is inside, so the per-pixel edge-accumulate/test is pure dead weight.
// Under SCREEN_DOOR_ALPHA the dither threshold reduces to two per-row
// booleans (HALF_WIDTH only steps through x%4 ∈ {0, 2}). That unlocks a
// tight fill loop that the compiler can unroll and the CPU can pipeline.
// Automatic - nothing to turn on in Config.hpp; this simply kicks in when
// none of the heavy features are enabled. Non-qualifying builds fall
// through to the full general-purpose inner loop unchanged.
//
// NOTE: TEXTURE_MAPPING is intentionally NOT in this set. When texture
// mapping is compiled in, untextured triangles still take the fast path
// (decided per-triangle via the runtime `useFastSimpleSpan` flag); only
// triangles that actually carry a diffuse map fall through to the
// general per-pixel loop. This means enabling TEXTURE_MAPPING globally
// no longer penalises scenes that mostly draw flat-shaded geometry.
#define JET_FAST_SIMPLE_SPANS                                             \
    (HALF_WIDTH_BUFFERS && !Z_BUFFERING &&                                   \
     !LIGHTING && !Z_BRIGHTNESS && !DEBUG_OVERDRAW &&                        \
     !RENDER_TILE_BUFFER && FAST_Z && !PERSPECTIVE_CORRECT_TEXTURES)

// Standard "over" alpha blend in RGB565. Used when SCREEN_DOOR_ALPHA is
// disabled — the renderer still has to honour material->alpha and the
// per-object fade, just by lerping channels into the framebuffer instead of
// stippling. Uses (256-a) and >>8 (vs (255-a)/255) for a single shift per
// channel; the off-by-one bias is invisible at 5/6-bit channel widths.
static inline uint16_t blendRGB565(uint16_t dst, uint16_t src, uint8_t alpha)
{
    const uint32_t a   = alpha;
    const uint32_t inv = 256u - a;
    const uint32_t sr = (src >> 11) & 0x1F;
    const uint32_t sg = (src >> 5)  & 0x3F;
    const uint32_t sb =  src        & 0x1F;
    const uint32_t dr = (dst >> 11) & 0x1F;
    const uint32_t dg = (dst >> 5)  & 0x3F;
    const uint32_t db =  dst        & 0x1F;
    const uint32_t r = (sr * a + dr * inv) >> 8;
    const uint32_t g = (sg * a + dg * inv) >> 8;
    const uint32_t b = (sb * a + db * inv) >> 8;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

// Saturating-add blend in RGB565.  Each source channel is scaled by
// alpha/256 (using a=alpha+1 so a>>8 is exact at alpha=255, giving full
// source intensity) then added to the corresponding destination channel and
// clamped at the channel maximum (0x1F for R/B, 0x3F for G).
// Use for neon glows, lamp coronas, explosion halos, and faked dynamic lights.
static inline uint16_t addBlendRGB565(uint16_t dst, uint16_t src, uint8_t alpha)
{
    const uint32_t a  = (uint32_t)alpha + 1u;
    const uint32_t sr = (((src >> 11) & 0x1Fu) * a) >> 8;
    const uint32_t sg = (((src >>  5) & 0x3Fu) * a) >> 8;
    const uint32_t sb = (( src        & 0x1Fu) * a) >> 8;
    const uint32_t dr =  (dst >> 11) & 0x1Fu;
    const uint32_t dg =  (dst >>  5) & 0x3Fu;
    const uint32_t db =   dst        & 0x1Fu;
    uint32_t r = dr + sr; if (r > 0x1Fu) r = 0x1Fu;
    uint32_t g = dg + sg; if (g > 0x3Fu) g = 0x3Fu;
    uint32_t b = db + sb; if (b > 0x1Fu) b = 0x1Fu;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static inline void fillRGB565Span(uint16_t* framebuffer, int32_t bufferIndex, int32_t count, uint16_t color)
{
    if (count <= 0) return;

    int32_t idx = bufferIndex;
    int32_t remaining = count;
    if (idx & 1) {
        framebuffer[idx++] = color;
        --remaining;
    }

    const uint32_t color32 = ((uint32_t)color << 16) | color;
    int32_t pairs = remaining >> 1;
    uint32_t* out = reinterpret_cast<uint32_t*>(&framebuffer[idx]);

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    if (pairs >= 8) {
        while (pairs > 0 && (((uintptr_t)out & 0x0F) != 0)) {
            *out++ = color32;
            --pairs;
        }

        const int32_t quads = pairs >> 2;
        if (quads > 0) {
            // GCC will not form a hardware loop around inline asm. Keep the
            // vector setup and explicit loop together, with no nested LOOP.
            // Early-clobber keeps the advancing pointer distinct from inputs.
            const int stride = 16;
            __asm__ volatile (
                "ee.movi.32.q q0, %[v], 0\n\t"
                "ee.movi.32.q q0, %[v], 1\n\t"
                "ee.movi.32.q q0, %[v], 2\n\t"
                "ee.movi.32.q q0, %[v], 3\n\t"
                "loopnez %[n], .Ljet_fill_end_%=\n\t"
                "ee.vst.128.xp q0, %[p], %[s]\n\t"
                ".Ljet_fill_end_%=:\n\t"
                : [p] "+&r"(out)
                : [s] "r"(stride), [n] "r"(quads), [v] "r"(color32)
                : "memory"
            );
            pairs -= quads << 2;
        }
    }
#endif

    for (int32_t i = 0; i < pairs; ++i) {
        *out++ = color32;
    }
    if (remaining & 1) {
        *reinterpret_cast<uint16_t*>(out) = color;
    }
}

// -----------------------------------------------------------------------------
// Directional lighting (lit-only contribution).
//
// Returns the directional + view-facing-specular term as a scalar in
// [0, 255 + specularCoef]. The ambient component is intentionally NOT folded
// in here: ambient is a per-channel tint applied during colour modulation,
// so that an `AmbientLight({160, 164, 180})` actually produces a cool-blue
// shadow rather than the same neutral grey it would as a scalar floor.
//
// Inputs are integer view-space vectors with nominal magnitude
// FIXED_POINT_SCALE (1024). The dot product therefore lives in
// [-FPS², +FPS²] == [-1,048,576, +1,048,576].
//
// Sign convention: `L` points FROM the surface TOWARD the light source (this
// is what DirectionalLight::calculateLightDirection() produces from its
// azimuth/elevation pair). When N and L are aligned (N·L = +|N||L|) the
// surface fully faces the light; when opposed (N·L = -|N||L|) the surface
// faces away and the directional contribution is zero.
//
// View-facing "specular": rather than a true Blinn-Phong H·N evaluation
// (which needs a normalise and a power per pixel), the renderer cheats with
// the fact that in view space the camera looks down +Z, so the vector from
// the surface back toward the camera is V = (0, 0, -1). A surface facing
// the camera has N.z < 0, and (-N.z)/FPS is the cosine of the angle to V.
// Squaring it tightens the falloff so the result reads more like a specular
// lobe than a flat fresnel term, and it's gated on the Lambert factor so
// the highlight only appears on the lit hemisphere.
// -----------------------------------------------------------------------------
static inline uint16_t jetShadeBrightness(const Vector3& N, const Vector3& L,
                                          uint16_t lightIntensity,
                                          uint8_t diffuseCoef,
                                          uint8_t specularCoef,
                                          uint16_t lambertGainQ8 = 256)
{
    if (lightIntensity > 255) lightIntensity = 255;
    const uint32_t maxBrightness = 255u + specularCoef;

    // Lambertian diffuse term. L points toward the light source, so a
    // positive dot product means the surface faces the light.
    int64_t lit = Vector3::dotProduct(N, L);
    if (lit <= 0) return 0;

    // Map [0, FPS²] → [0, 255]. With FPS=1024, FPS²=1,048,576, so >>12
    // gives [0, 256]; clamp the top end.
    uint32_t lambert = (uint32_t)(lit >> 12);
    if (lambert > 255) lambert = 255;

    // Falloff exaggeration. Square the Lambert term to bend the cosine
    // curve down into the midtones: a face that's 70% facing the light
    // would naturally return 0.70 brightness but now returns 0.49, and
    // 50% drops to 0.25. Faces directly under the sun (1.0) are
    // unchanged at 1.0, so peaks aren't blown — only the falloff is
    // amplified. This makes a rotating object darken visibly as it
    // turns away from the sun, rather than the gentle linear tint that
    // raw Lambert produces. (lambert * lambert + 128) >> 8 keeps the
    // result in [0, 255] with one rounding bit of headroom.
    lambert = (lambert * lambert + 128) >> 8;

    // Optional lambert gain (Q8). Default 256 (1.0×) preserves the
    // physically-plausible cosine response used by GOURAUD and PHONG,
    // which interpolate brightness across the triangle and benefit from
    // smooth midtones. FLAT shades the whole face from a single averaged
    // normal, so the response is constant across a polygon and a 1.0×
    // gain reads as a gentle tint rather than "this face is in the sun".
    // The FLAT call site passes a value >256 to push lit faces past
    // 100% base colour into the per-material specular headroom
    // (maxBrightness = 255 + specular), producing the strong lit/shadow
    // split typical of early-hardware flat-shaded scenes.
    if (lambertGainQ8 != 256)
    {
        lambert = (lambert * lambertGainQ8) >> 8;
        if (lambert > maxBrightness) lambert = maxBrightness;
    }
    lambert = (lambert * lightIntensity) >> 8;
    uint32_t diffuseTerm = (lambert * diffuseCoef) >> 8;
    if (diffuseTerm > maxBrightness) diffuseTerm = maxBrightness;

    uint32_t specularTerm = 0;
    if (specularCoef && N.z < 0 && lambert > 0)
    {
        uint32_t viewFacing = (uint32_t)(-N.z);
        if (viewFacing > FIXED_POINT_SCALE) viewFacing = FIXED_POINT_SCALE;
        uint32_t vf8 = viewFacing >> 2;
        if (vf8 > 255) vf8 = 255;
        uint32_t vf2 = (vf8 * vf8) >> 8;
        vf2 = (vf2 * lambert) >> 8;
        vf2 = (vf2 * lightIntensity) >> 8;
        specularTerm = (vf2 * specularCoef) >> 8;
    }

    uint32_t total = diffuseTerm + specularTerm;
    if (total > maxBrightness) total = maxBrightness;
    return (uint16_t)total;
}

// Per-channel coloured modulation.
//
// Combines a triangle-constant ambient tint (per-channel 0..255) with a
// per-pixel scalar directional/specular brightness contribution. The lit
// scalar is added to each channel-specific ambient level before the
// base-colour scaling, so a cool ambient under a warm sun produces visibly
// warm lit faces and cool shadowed faces from a single neutral material.
//
// Each channel is handled independently; channels that overshoot 255 lerp
// toward their max value (31/63/31), giving the blowout-to-white headroom
// that `material->specular` opts into.
// Per-channel coloured modulation.
//
// Lighting model (per channel, independently):
//   scale_c = ambient_c + brightness                    (saturating)
//   final_c = base_c × scale_c / 255                    (+ blow-out to max
//                                                         above 255)
//
// `brightness` carries the directional + view-facing-specular contribution
// in [0 .. 255 + specularCoef]; the band above 255 lerps the channel
// toward its maximum (the headroom `material->specular` opts into).
// `ambR/G/B` is the ambient light colour (0..255 per channel) added to the
// modulation factor BEFORE the base multiply — i.e. ambient tints the
// material rather than replacing it. A brown cliff facing away from the
// sun is still recognisably brown (just dimmer and tinted by the ambient
// hue) rather than going pure ambient-colour as it would with an additive
// post-multiply ambient.
//
// Inputs are RGB565 base, 16-bit `brightness` (post-clamp by caller), 8-bit
// per-channel ambient. Output is RGB565.
static inline uint16_t jetModulateRGB565(uint16_t color,
                                         uint16_t brightness,
                                         uint8_t ambR, uint8_t ambG, uint8_t ambB,
                                         uint16_t maxBrightness)
{
    const uint32_t base_r = (color >> 11) & 0x1F;
    const uint32_t base_g = (color >>  5) & 0x3F;
    const uint32_t base_b =  color        & 0x1F;

    uint32_t tR = (uint32_t)brightness + ambR;
    uint32_t tG = (uint32_t)brightness + ambG;
    uint32_t tB = (uint32_t)brightness + ambB;
    if (tR > maxBrightness) tR = maxBrightness;
    if (tG > maxBrightness) tG = maxBrightness;
    if (tB > maxBrightness) tB = maxBrightness;

    auto channel5 = [](uint32_t base5, uint32_t t) -> uint32_t {
        if (t > 255) {
            const uint32_t blow = t - 255;
            return base5 + ((31u - base5) * blow) / 256u;
        }
        const uint32_t v = base5 * t;
        return (v + 128u + (v >> 8)) >> 8;
    };
    auto channel6 = [](uint32_t base6, uint32_t t) -> uint32_t {
        if (t > 255) {
            const uint32_t blow = t - 255;
            return base6 + ((63u - base6) * blow) / 256u;
        }
        const uint32_t v = base6 * t;
        return (v + 128u + (v >> 8)) >> 8;
    };

    const uint32_t r = channel5(base_r, tR);
    const uint32_t g = channel6(base_g, tG);
    const uint32_t b = channel5(base_b, tB);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

namespace Renderer
{
    inline bool Rasterizer::shouldDrawPixel(int x, int y, uint8_t alpha)
    {
        #if NOISE_ALPHA
            //Generate a random number between 0 and 255 using a simple LCG
            uint8_t random = (251 * lastRandom + randomSeed);
            lastRandom = random;
            //If the random number is greater than the alpha value, don't draw the pixel
            return (random & 0xFF) <= alpha;
        #endif
        if (alpha > 240)
        {
            return true; // Draw all pixels
        }

        // Precomputed threshold matrix flattened into a 1D array for faster access
        constexpr uint8_t thresholdMatrix[16] = {
            15, 135, 45, 165,
            195, 75, 225, 105,
            60, 180, 30, 150,
            240, 120, 210, 90};

        // Determine the position within the threshold matrix
        uint8_t threshold = thresholdMatrix[(x & 3) | ((y & 3) << 2)];

        // Compare alpha to the threshold to determine if the pixel should be drawn
        return alpha >= threshold;
    }

    uint16_t Rasterizer::grayscaleToRGB565(uint8_t grayscale)
    {
        // Convert the 8-bit grayscale value to 5-bit (for R and B) and 6-bit (for G).
        uint8_t r = grayscale >> 3; // Convert 8-bit to 5-bit
        uint8_t g = grayscale >> 2; // Convert 8-bit to 6-bit
        uint8_t b = grayscale >> 3; // Convert 8-bit to 5-bit

        // Pack RGB into RGB565 format
        uint16_t rgb565 = (r << 11) | (g << 5) | b;

        return rgb565;
    }

    bool PERF_CRITICAL Rasterizer::drawTriangle(
        const RenderVertex& v1,const RenderVertex& v2,const RenderVertex& v3,
        Material* material,DirectionalLight* directionalLight,AmbientLight* ambientLight,
        bool renderEvenLines,bool ignoreZBuffer,bool noWriteZBuffer,int zBias,
        uint8_t objAlpha,bool brightnessPrecomputed,int32_t avgZHint)
    {
#if JET_FAST_SIMPLE_SPANS && TEXTURE_MAPPING
        bool textured=material->diffuseMap!=nullptr;
        if(textured && textureLodEnabled && textureLodFar>textureLodNear) {
    #if LAZY_Z
            const int32_t lodZ=std::max({v1.position.z,v2.position.z,v3.position.z});
    #else
            const int32_t lodZ=avgZHint!=INT32_MIN?avgZHint:
                (int32_t)(((int64_t)v1.position.z+v2.position.z+v3.position.z)/3);
    #endif
            textured=lodZ<textureLodFar;
        }
        if(!textured)
            return drawFlatTriangle(v1,v2,v3,material,directionalLight,ambientLight,
                renderEvenLines,ignoreZBuffer,noWriteZBuffer,zBias,objAlpha,brightnessPrecomputed,avgZHint);
#endif
        return drawTexturedTriangle(v1,v2,v3,material,directionalLight,ambientLight,
            renderEvenLines,ignoreZBuffer,noWriteZBuffer,zBias,objAlpha,brightnessPrecomputed,avgZHint);
    }

    // Separate instantiations let the compiler retain the compact flat-fill
    // kernel in a texture-enabled build, without UV/general-loop register
    // pressure spilling into the overwhelmingly common untextured path.
    template<bool SampleTextures>
#if defined(ESP_PLATFORM)
    __attribute__((always_inline))
#endif
    inline bool Rasterizer::drawTriangleImpl(
        const RenderVertex &v1,
        const RenderVertex &v2,
        const RenderVertex &v3,
        Material *material,
        DirectionalLight *directionalLight,
        AmbientLight *ambientLight,
        bool renderEvenLines,
        bool ignoreZBuffer,
        bool noWriteZBuffer,
        int zBias,
        uint8_t objAlpha,
        bool brightnessPrecomputed,
        int32_t avgZHint)
    {
        // Fold per-object fade alpha into the material alpha up front so
        // every downstream alpha decision (early-out, depth fog, stipple
        // under SCREEN_DOOR_ALPHA, traditional blend without it) sees the
        // combined value. (objAlpha defaults to 255 for objects that don't
        // use the per-object distance fade.)
        uint8_t alpha = (objAlpha == 255)
                            ? material->alpha
                            : (uint8_t)(((uint16_t)material->alpha * objAlpha) / 255);

        if (alpha == 0) // Don't render fully-transparent triangles
        {
            return false;
        }

        (void)avgZHint; // Only consumed on the FAST_Z && !LAZY_Z path.

#if TEXTURE_MAPPING
        Texture *diffuseMap = SampleTextures ? material->diffuseMap : nullptr;
#endif
        const bool isWaterReflect = (material->shadingMode == ShadingMode::WATER_REFLECT);
        const bool isAdditive     = (material->shadingMode == ShadingMode::ADDITIVE);
        // Parallel-band ordering guards (see Renderer.hpp skipWaterReflect /
        // waterReflectOnly).  skipWaterReflect is set on all parallel-band
        // copies so SSR-reading triangles are deferred; waterReflectOnly is
        // set on the subsequent serial full-frame pass that draws only water
        // after all geometry is committed to the framebuffer.
        if (skipWaterReflect && isWaterReflect)  return false;
        if (waterReflectOnly && !isWaterReflect) return false;
#if LIGHTING
        bool emissive = material->emissive
                     || (material->shadingMode == ShadingMode::UNLIT)
                     || isWaterReflect
                     || isAdditive;  // additive glows never receive directional light
#endif

#if JET_FAST_SIMPLE_SPANS
    #if TEXTURE_MAPPING
        // Per-triangle decision: untextured triangles take the fast simple
        // span path even in builds with TEXTURE_MAPPING enabled. Only
        // triangles that actually have a diffuse map pay for the general
        // per-pixel loop. Non-const because the texture-LOD pass below may
        // promote a fully-flat-LOD textured triangle back onto the fast
        // path (texture is dropped entirely past textureLodFar).
        bool useFastSimpleSpan = (diffuseMap == nullptr);
    #else
        // No textures compiled in the fast path is unconditional and
        // this constexpr lets the optimiser fold the runtime branch out.
        constexpr bool useFastSimpleSpan = true;
    #endif
#endif
        int32_t nearPlane = camera->nearPlane;
        int32_t farPlane = camera->farPlane;

        // Compute bounding box of the triangle
        int32_t minX = std::min({v1.position.x, v2.position.x, v3.position.x}) & ~1;
        int32_t maxX = std::max({v1.position.x, v2.position.x, v3.position.x}) & ~1;
        int32_t minY = std::min({v1.position.y, v2.position.y, v3.position.y}) & ~1;
        int32_t maxY = std::max({v1.position.y, v2.position.y, v3.position.y}) & ~1;

        // If the triangle has no area, skip it
        #if SKIP_ZERO_AREA_TRIANGLES
        if (minX == maxX || minY == maxY)
        {
            return false;
        }
        #endif

#if FAST_Z
#if LAZY_Z
        // LAZY_Z needs the max Z, not the average — the caller's avgZ hint
        // doesn't apply here.
        int32_t z = std::max({v1.position.z, v2.position.z, v3.position.z});
        if (z < nearPlane || z > farPlane)
        {
            return false;
        }
#else
        int32_t z;
        if (avgZHint != INT32_MIN)
        {
            // Scene::emitTri computed this exact average at queue time and
            // already culled against near/far — trust it and skip both.
            z = avgZHint;
        }
        else
        {
            z = (v1.position.z + v2.position.z + v3.position.z) / 3;
            if (z < nearPlane || z > farPlane)
            {
                return false;
            }
        }
#endif
        #if Z_BUFFERING
        // Z buffer stores raw int32_t z, narrowed to uint16_t. The project's
        // farPlane (~4096) fits comfortably; we clamp to UINT16_MAX so any
        // future absurdly-far geometry saturates rather than wrapping.
        // Per-object zBias pulls the surface toward the camera in real
        // depth units (no longer divided by 4) so a small bias really is
        // a small distance — 1 unit of bias = 1 unit of world depth.
        int32_t zbRaw = z - zBias;
        if (zbRaw < 0)     zbRaw = 0;
        if (zbRaw > 65535) zbRaw = 65535;
        uint16_t zb = (uint16_t)zbRaw;
        #endif
#endif

        // Clamp to screen bounds
        minX = std::max(minX, static_cast<int32_t>(0));
        maxX = std::min(maxX, static_cast<int32_t>(screenWidth - 1));
        minY = std::max(minY, static_cast<int32_t>(yBandMin));
        maxY = std::min(maxY, static_cast<int32_t>(std::min(yBandMax - 1, screenHeight - 1)));
        if (minY > maxY) return false;

#if LIGHTING
        Vector3 normal = {0, 0, 0};
        uint16_t vertexBrightness[3] = {0, 0, 0};

        // Per-channel ambient tint. The ambient light contributes a constant
        // offset that is added to each colour channel independently during
        // the final modulation step, so a cool-toned AmbientLight actually
        // casts a cool tint into shadowed areas rather than just dimming
        // them by a single scalar.
        const uint8_t ambR = ambientLight ? ambientLight->color.r : 0;
        const uint8_t ambG = ambientLight ? ambientLight->color.g : 0;
        const uint8_t ambB = ambientLight ? ambientLight->color.b : 0;
        const uint16_t lightIntensity = directionalLight ? directionalLight->intensity : 0;

        if (directionalLight)
        {
            if (material->shadingMode == ShadingMode::FLAT)
            {
                // For FLAT shading we expect per-face vertex duplication
                // (computeFlatNormals stamps the same face normal onto all
                // three verts of each triangle). Pick the first vertex's
                // normal directly — it already has unit magnitude FPS, no
                // averaging or sqrt-normalize required. This is the per-
                // triangle hot path on the ESP32-S3 where FLAT lighting
                // was costing nearly 2x the no-lighting frame time; the
                // sqrt+divide that lived here was the bulk of that cost.
                //
                // For meshes where vertex normals genuinely differ across
                // a face (i.e. smooth-shaded geometry erroneously assigned
                // FLAT), the result is identical to picking ANY one of the
                // three normals — which is acceptable because FLAT shading
                // is not the right pipeline for that geometry anyway.
                normal = v1.normal;
            }
            else if (material->shadingMode == ShadingMode::GOURAUD)
            {
                // Per-vertex Lambertian + view-facing specular. The triangle
                // body of the rasterizer interpolates vertexBrightness[]
                // linearly across the face (plane-equation incremental form
                // in the inner loops below).
                if (brightnessPrecomputed)
                {
                    // Object-local-light path: Scene.cpp has already
                    // evaluated diffuse Lambert per-vertex in mesh-local
                    // space; just read it back. Saves three dot/clamp/
                    // square-falloff sequences per triangle.
                    vertexBrightness[0] = v1.lambertBrightness;
                    vertexBrightness[1] = v2.lambertBrightness;
                    vertexBrightness[2] = v3.lambertBrightness;
                }
                else
                {
                    const RenderVertex* verts[3] = { &v1, &v2, &v3 };
                    for (int i = 0; i < 3; i++)
                    {
                        vertexBrightness[i] = jetShadeBrightness(
                            verts[i]->normal,
                            directionalLight->lightDir,
                            lightIntensity,
                            material->diffuse,
                            material->specular);
                    }
                }
            }
        }
#endif

    #if TEXTURE_MAPPING
        uint16_t color = SampleTextures ? material->getColor({0, 0}) : material->color;
    #else
        uint16_t color = material->color;
    #endif
        // Untextured triangles use this single material colour as the
        // per-pixel modulation input. Keeping the unmodulated base in a
        // separate variable is critical: previously the per-pixel
        // modulation wrote back into `color`, so each subsequent pixel
        // on the scanline modulated an already-modulated colour and the
        // result converged toward black across the span — the entire
        // reason untextured surfaces (clouds, walls) were rendering as
        // solid black bands. Textured triangles re-sample `color` from
        // the diffuse map each pixel and don't read `baseColor`. FLAT
        // materials short-circuit via `flatColorPrecomputed` and bake
        // the modulation into `color` once at triangle setup — they
        // intentionally skip per-pixel modulation entirely.
        const uint16_t baseColor = color;

#if TEXTURE_MAPPING
        // Distance-based texture LOD. textureLodFade goes from 255 (full
        // texture) at textureLodNear down to 0 (fully flat) at
        // textureLodFar. Past textureLodFar we drop the texture entirely:
        // the local `color` is overwritten with material->color and the
        // triangle is promoted onto the fast simple-span path (in builds
        // where it's compiled in), which is the actual perf win - distant
        // scenery falls out of the per-pixel textured loop and into the
        // paired-32-bit fill loop. The fade band still pays the textured
        // loop cost, but composites the texel toward material->color
        // using whichever blend method the build is configured with
        // (stipple under SCREEN_DOOR_ALPHA, alpha lerp otherwise).
        // Triangle-constant: lodZ is taken from the same source as the
        // FAST_Z depth (or the triangle average when FAST_Z is off).
        uint8_t textureLodFade = 255;
        if (textureLodEnabled && diffuseMap && textureLodFar > textureLodNear)
        {
        #if FAST_Z
            const int32_t lodZ = z;
        #else
            const int32_t lodZ = (v1.position.z + v2.position.z + v3.position.z) / 3;
        #endif
            if (lodZ >= textureLodFar)
            {
                textureLodFade = 0;
                color = material->color; // Flat fallback for the fast path.
            #if JET_FAST_SIMPLE_SPANS
                useFastSimpleSpan = true;
            #endif
            }
            else if (lodZ > textureLodNear)
            {
                textureLodFade = (uint8_t)(255 - ((lodZ - textureLodNear) * 255)
                                                  / (textureLodFar - textureLodNear));
            }
        }
#endif

#if TEXTURE_MAPPING || !FAST_Z || LIGHTING
        // Edge-function denominator. Computed in int64 (vertex projected
        // x/y near the camera can blow well past int32 range), then
        // narrowed to int32 for the per-pixel divides. Previously this
        // path had an `if (denom64 > INT32_MAX) return false;` bail-out
        // which silently dropped near-camera triangles at high resolutions
        // / large WORLD_SCALE (triangles popping out of existence as you
        // closed in). We now accept the truncation: the triangle is huge
        // and near-camera so any per-pixel interpolation error is bounded
        // and far less objectionable than the triangle disappearing.
        int64_t denom64 = (int64_t)(v2.position.y - v3.position.y) * (v1.position.x - v3.position.x) +
                          (int64_t)(v3.position.x - v2.position.x) * (v1.position.y - v3.position.y);

        if (denom64 == 0)
            return false; // Degenerate triangle
        int32_t denom = (int32_t)denom64;
        if (denom == 0)
            return false; // Truncation landed on zero; treat as degenerate
#if LIGHTING
        // Precompute FPU reciprocal once per triangle. Replaces two int64
        // __divdi3 calls (per-triangle Gouraud step + per-row brightness
        // init) with float multiplies — ~70 cy → ~5 cy each on Xtensa LX7.
        const float invDenom64f = 1.0f / (float)denom64;
#endif

#if TEXTURE_MAPPING && !PERSPECTIVE_CORRECT_TEXTURES
        // Affine UVs are planes. One reciprocal per triangle and one anchor
        // per scanline replace two barycentric divides at every pixel.
        // Q16 retains sub-texel precision even on wide desktop spans. Bound
        // UVs and steps so signed accumulators (including the final unused
        // increment) cannot overflow; unusual assets retain the general path.
        const float uvInvArea = 1.0f / (float)denom64;
        constexpr int uvLimit = 8192;
        const auto boundedUV = [=](const RenderVertex& v) {
            return v.uv.x >= -uvLimit && v.uv.x <= uvLimit
                && v.uv.y >= -uvLimit && v.uv.y <= uvLimit;
        };
        bool incrementalUV = diffuseMap && !diffuseMap->screenSpace && !diffuseMap->reflectionMap
            && boundedUV(v1) && boundedUV(v2) && boundedUV(v3);
        int32_t uStepQ16=0, vStepQ16=0;
    #if !BILINEAR_FILTER
        const bool directRGB565 = diffuseMap && diffuseMap->data && !diffuseMap->palette
            && diffuseMap->addressMode == WRAP
            && diffuseMap->width > 0 && diffuseMap->width <= 1024
            && diffuseMap->height > 0 && diffuseMap->height <= 1024
            && (diffuseMap->width & (diffuseMap->width-1)) == 0
            && (diffuseMap->height & (diffuseMap->height-1)) == 0;
    #endif
#endif

        Vector2 uv = {0, 0};
#endif

#if LIGHTING || Z_BRIGHTNESS
        uint16_t brightness = 0;
#endif

#if LIGHTING
        if (!directionalLight && !ambientLight)
        {
            brightness = 255;
        }
        else if (material->shadingMode == ShadingMode::FLAT)
        {
            if (directionalLight)
            {
                // FLAT shading uses the natural Lambertian response.
                // The speedup from picking v1.normal directly (no sqrt-
                // normalize) turned out to give plenty of perceived
                // contrast on its own — the previous artificial lambert
                // gain + extra specular headroom blew everything out.
                if (brightnessPrecomputed)
                {
                    // Object-local-light path: v1.lambertBrightness was
                    // precomputed by Scene.cpp using the mesh-local
                    // normal + object-local light dir. computeFlatNormals
                    // stamps identical normals on all three verts of a
                    // FLAT face, so any vertex's cached value is the
                    // face brightness.
                    brightness = v1.lambertBrightness;
                }
                else
                {
                    brightness = jetShadeBrightness(
                        normal,
                        directionalLight->lightDir,
                        lightIntensity,
                        material->diffuse,
                        material->specular);
                }
            }
            else
            {
                // Ambient-only: the per-channel ambient tint is applied in
                // the modulation step, so the lit-scalar contribution is
                // zero here.
                brightness = 0;
            }
        }
#endif

#if Z_BRIGHTNESS && FAST_Z
        brightness = 255 - ((z - zBrightNear) * zBrightScale) / (zBrightFar - zBrightNear);
        brightness = std::min(brightness, static_cast<uint16_t>(255));
#endif

#if LIGHTING
        // Per-triangle hoist of the channel modulation. For FLAT-shaded
        // (or unlit) materials with no diffuse texture, both `color` and
        // `brightness` are triangle-constants, so the per-pixel multiply +
        // shift dance below is pure repeated work. Pre-modulating once
        // here and skipping the per-pixel block recovers the bulk of the
        // perf cost of enabling LIGHTING.
        bool flatColorPrecomputed = false;
        const bool flatShaded =
            (!directionalLight && !ambientLight) ||
            material->shadingMode == ShadingMode::FLAT;
        const bool diffuseMapSamples =
#if TEXTURE_MAPPING
            (material->diffuseMap != nullptr);
#else
            false;
#endif
        if (flatShaded && !emissive && !diffuseMapSamples)
        {
            const uint16_t maxBrightness = (uint16_t)(255u + material->specular);
            color = jetModulateRGB565(color, brightness, ambR, ambG, ambB, maxBrightness);
            flatColorPrecomputed = true;
        }

        // For untextured non-flat (GOURAUD / PHONG) triangles the base
        // colour is triangle-constant; only `brightness` varies per pixel.
        // Hoist the RGB565 channel extract and the saturating maxBrightness
        // here so the inner loop becomes "build per-channel t from brightness +
        // const ambient, multiply, pack" — no function call, no re-extract.
        // This is the dominant rasterization cost in the scene (most
        // triangles are flat-coloured GOURAUD), so saving the per-pixel
        // function-call + channel extract is the single biggest LIGHTING
        // perf knob.
        const bool litPerPixelUntextured =
            !flatColorPrecomputed && !emissive && !diffuseMapSamples;
        uint32_t litBaseR5 = 0, litBaseG6 = 0, litBaseB5 = 0;
        uint16_t litMaxBrightness = 255;
        if (litPerPixelUntextured)
        {
            litBaseR5 = (baseColor >> 11) & 0x1F;
            litBaseG6 = (baseColor >>  5) & 0x3F;
            litBaseB5 =  baseColor        & 0x1F;
            litMaxBrightness = (uint16_t)(255u + material->specular);
        }
#endif

#if DEPTH_ALPHA_BLEND && FAST_Z
        // Triangle-level depth fog. With FAST_Z the z used here is constant
        // for the whole triangle (it's the triangle average or max), so
        // there is no point re-evaluating this per pixel the way the older
        // path did. We hoist the computation out; the per-pixel version
        // below remains only for the !FAST_Z path where z actually varies.
        // Compose with the existing alpha (material*objAlpha) by taking
        // the minimum, so per-object distance fades and translucent
        // materials are preserved through fog.
        {
            if (z >= depthFogFar) {
                alpha = 0;
            } else if (z > depthFogNear) {
                uint8_t fogA = (uint8_t)(255 - ((z - depthFogNear) * 255) / (depthFogFar - depthFogNear));
                if (fogA < alpha) alpha = fogA;
            }
            if (alpha == 0) return false; // Fully-transparent triangle: nothing to draw.
        }
#endif

        const bool plainOpaqueReplace = (alpha == 255 && !isWaterReflect && !isAdditive);

#if PERSPECTIVE_CORRECT_TEXTURES
        int32_t oneOverZ1 = (FIXED_POINT_SCALE * FIXED_POINT_SCALE) / v1.position.z;
        int32_t oneOverZ2 = (FIXED_POINT_SCALE * FIXED_POINT_SCALE) / v2.position.z;
        int32_t oneOverZ3 = (FIXED_POINT_SCALE * FIXED_POINT_SCALE) / v3.position.z;

#if TEXTURE_MAPPING
        // Precompute u_over_z and v_over_z at each vertex. Only with
        // TEXTURE_MAPPING: RenderVertex carries uv only in textured builds.
        // (PERSPECTIVE_CORRECT_TEXTURES without TEXTURE_MAPPING is still a
        // valid config — it drives perspective-correct PHONG normals.)
        int32_t uOverZ1 = (v1.uv.x * oneOverZ1) / FIXED_POINT_SCALE;
        int32_t vOverZ1 = (v1.uv.y * oneOverZ1) / FIXED_POINT_SCALE;
        int32_t uOverZ2 = (v2.uv.x * oneOverZ2) / FIXED_POINT_SCALE;
        int32_t vOverZ2 = (v2.uv.y * oneOverZ2) / FIXED_POINT_SCALE;
        int32_t uOverZ3 = (v3.uv.x * oneOverZ3) / FIXED_POINT_SCALE;
        int32_t vOverZ3 = (v3.uv.y * oneOverZ3) / FIXED_POINT_SCALE;
#endif // TEXTURE_MAPPING

#if LIGHTING
        // Precompute normal_component / z at each vertex so Phong shading
        // can use the same perspective-correct reconstruction as UVs.
        // Same fixed-point scale as uOverZ: n/z * FPS, range ≈ ±FPS.
        int32_t nxOverZ1 = (v1.normal.x * oneOverZ1) / FIXED_POINT_SCALE;
        int32_t nyOverZ1 = (v1.normal.y * oneOverZ1) / FIXED_POINT_SCALE;
        int32_t nzOverZ1 = (v1.normal.z * oneOverZ1) / FIXED_POINT_SCALE;
        int32_t nxOverZ2 = (v2.normal.x * oneOverZ2) / FIXED_POINT_SCALE;
        int32_t nyOverZ2 = (v2.normal.y * oneOverZ2) / FIXED_POINT_SCALE;
        int32_t nzOverZ2 = (v2.normal.z * oneOverZ2) / FIXED_POINT_SCALE;
        int32_t nxOverZ3 = (v3.normal.x * oneOverZ3) / FIXED_POINT_SCALE;
        int32_t nyOverZ3 = (v3.normal.y * oneOverZ3) / FIXED_POINT_SCALE;
        int32_t nzOverZ3 = (v3.normal.z * oneOverZ3) / FIXED_POINT_SCALE;
#endif // LIGHTING
#endif // PERSPECTIVE_CORRECT_TEXTURES

        const int inc = interlacedMode ? 2 : 1;

        // Checkerboard: precompute which (x+y) parity to render this frame.
        // renderEvenLines==true  (even frame) → render where (x+y) is even (parity 0).
        // renderEvenLines==false (odd  frame) → render where (x+y) is odd  (parity 1).
        // Only meaningful when checkerboardMode is true; otherwise unused.
        const int cbFrameParity = renderEvenLines ? 0 : 1;

        // -- Incremental edge function setup --
        // The edge function w_i(x, y) is linear in x and y, so rather than
        // recomputing it from scratch per pixel (which would need int64 to
        // stay safe against huge projected coords near the near plane, and
        // is slow on 32-bit targets), we:
        //   1. Precompute the partial derivatives dw_i/dx and dw_i/dy.
        //      These are just differences of projected vertex coords and
        //      comfortably fit in int32 even when the coords themselves
        //      are huge.
        //   2. Compute the w values at the first pixel of each row once
        //      in int64 (per-triangle setup cost; a few multiplies).
        //   3. In the hot inner loop, just ADD the int32 delta. Running
        //      accumulator is int64 so overflow is impossible, but the
        //      per-pixel work is a few 64-bit adds — much cheaper than
        //      either int64 multiplies or risking int32 overflow.
        const int32_t dw0_dx = v2.position.y - v3.position.y;
        const int32_t dw1_dx = v3.position.y - v1.position.y;
        const int32_t dw2_dx = v1.position.y - v2.position.y;
        const int32_t dw0_dy = v3.position.x - v2.position.x;
        const int32_t dw1_dy = v1.position.x - v3.position.x;
        const int32_t dw2_dy = v2.position.x - v1.position.x;

#if HALF_WIDTH_BUFFERS
        constexpr int xStep = 2;
#else
        constexpr int xStep = 1;
#endif
        const int32_t dw0_dx_step = dw0_dx * xStep;
        const int32_t dw1_dx_step = dw1_dx * xStep;
        const int32_t dw2_dx_step = dw2_dx * xStep;
        const int32_t dw0_dy_step = dw0_dy * inc;
        const int32_t dw1_dy_step = dw1_dy * inc;
        const int32_t dw2_dy_step = dw2_dy * inc;

#if TEXTURE_MAPPING && !PERSPECTIVE_CORRECT_TEXTURES
        if (incrementalUV) {
            const float du = (float)((int64_t)v1.uv.x*dw0_dx_step + (int64_t)v2.uv.x*dw1_dx_step
                                  + (int64_t)v3.uv.x*dw2_dx_step) * uvInvArea;
            const float dv = (float)((int64_t)v1.uv.y*dw0_dx_step + (int64_t)v2.uv.y*dw1_dx_step
                                  + (int64_t)v3.uv.y*dw2_dx_step) * uvInvArea;
            incrementalUV = std::abs(du) <= uvLimit && std::abs(dv) <= uvLimit;
            if (incrementalUV) {
                uStepQ16=(int32_t)std::lround(du*65536.0f);
                vStepQ16=(int32_t)std::lround(dv*65536.0f);
            }
        }
#endif

#if LIGHTING
        // Plane-equation incremental Gouraud brightness. Brightness is
        // linear in (x,y) across the triangle, so the per-pixel and per-row
        // step values are constants. Replacing the original per-pixel
        // (b0*w0 + b1*w1 + b2*w2) / denom with a single int32 add per pixel
        // is roughly 30x cheaper on Xtensa (no divide, no multi-multiply).
        // brightness is tracked in Q16 fixed-point so per-pixel deltas
        // smaller than 1 don't quantise to zero across a wide row.
        //
        // int64 throughout the setup: vertexBrightness peaks at 255+specular
        // (= 510 worst case) and dw_dx_step can be in the millions for
        // huge near-camera triangles, so the int32 product overflowed and
        // produced wildly-wrong row slopes (the "track flashes random
        // colours" artefact). Per-pixel cost is unchanged — the inner loop
        // still steps an int32 accumulator.
        int32_t brightness_dx_step_q16 = 0;
        const bool useIncrementalGouraud =
            directionalLight && material->shadingMode == ShadingMode::GOURAUD;
        if (useIncrementalGouraud)
        {
            const int64_t bDx = (int64_t)vertexBrightness[0] * dw0_dx_step
                              + (int64_t)vertexBrightness[1] * dw1_dx_step
                              + (int64_t)vertexBrightness[2] * dw2_dx_step;
            // denom is divided in int64 here. Using the int32-narrowed
            // `denom` produced randomly-dark/flashing triangles when the
            // camera got close enough that the projected vertex coords
            // pushed denom64 past INT32_MAX (the rasterizer accepts that
            // truncation for per-pixel edge math, but the lighting
            // divides are highly sensitive to denom sign/magnitude).
            brightness_dx_step_q16 = (int32_t)((float)bDx * 65536.0f * invDenom64f);
        }
#endif

        // Preserve field parity when clipping moves minY onto an odd band
        // boundary. Checkerboard mode renders every row and skips columns.
        int yStart = interlacedMode ? (minY + ((minY ^ (int)renderEvenLines) & 1)) : minY;
        Detail::TriangleSpans spans({v1.position.x, v1.position.y},
                                   {v2.position.x, v2.position.y},
                                   {v3.position.x, v3.position.y}, yStart, inc);
        // Keep the original solver for inverted horizontal bounds. In
        // half-width mode its truncating slot division can still yield a
        // slot at x=screenWidth; changing that clipping behavior is separate.
        if (minX > maxX) spans.valid = false;
        if (spans.valid) yStart = spans.firstY;
        int64_t w0_row = (int64_t)dw0_dx * (minX - v3.position.x)
                       + (int64_t)dw0_dy * (yStart - v3.position.y);
        int64_t w1_row = (int64_t)dw1_dx * (minX - v1.position.x)
                       + (int64_t)dw1_dy * (yStart - v1.position.y);
        int64_t w2_row = (int64_t)dw2_dx * (minX - v2.position.x)
                       + (int64_t)dw2_dy * (yStart - v2.position.y);

        // Max number of xStep-sized pixel slots in this row.
        // Bounded by screenWidth/xStep (≤240) so int32 is sufficient.
        const int32_t iMaxRow = (maxX - minX) / xStep;

        // Specialize once per triangle so the simple-span build can discard
        // unused barycentric row updates on the incremental path. Inlining
        // also keeps both row loops in drawTriangle's IRAM section on ESP32.
#if JET_FAST_SIMPLE_SPANS && !SCREEN_DOOR_ALPHA
        RGB565ConstantBlend constantBlend;
        bool constantReady = false;
        auto blendConstant = [&](uint16_t* dst, const uint16_t* src, int n,
                                 uint16_t col, uint8_t a, bool background) {
            // Colour/mode are fixed for this triangle; water's top-edge
            // fade can change alpha between rows. Prepare only on demand.
            if (!constantReady || constantBlend.alpha != a) {
                constantBlend.prepare(col, a, background); constantReady = true;
            }
            constantBlend.blend(dst, src, n);
        };
#endif
        auto rasterRows = [&](auto useSpans)
#if defined(__GNUC__)
            __attribute__((always_inline))
#endif
        {
            auto advanceSpans = [&]() {
                if constexpr (decltype(useSpans)::value) spans.advance();
            };
            for (int y = yStart; y <= maxY;
                 y += inc, w0_row += dw0_dy_step, w1_row += dw1_dy_step, w2_row += dw2_dy_step, advanceSpans())
            {
                int32_t iStart, iEnd;
                bool skipRow = false;
                if constexpr (decltype(useSpans)::value) {
                    spans.beginRow(y, inc);
                    const int32_t left = spans.left.x + (spans.left.remainder != 0);
                    const int32_t right = spans.right.x;
                    if (left > maxX || right < minX) continue;
                    iStart = (std::max(left, minX) - minX + xStep - 1) / xStep;
                    iEnd = (std::min(right, maxX) - minX) / xStep;
                } else {
                // --- Scanline range: find first and last x inside the triangle.
                //
                // The edge function is linear in x with known int32 slope per
                // xStep, so we can solve for the x-range where all three
                // edges are non-negative without iterating pixel-by-pixel.
                //
                // For each edge with value ew_j at x=minX and slope d_j per
                // xStep:
                //   d_j > 0 : ew_j + i*d_j >= 0 iff i >= ceil(-ew_j / d_j)
                //   d_j < 0 : ew_j + i*d_j >= 0 iff i <= floor(ew_j / -d_j)
                //   d_j == 0: ew_j < 0 kills the whole row; else no constraint
                //
                // iStart/iEnd are bounded by iMaxRow (≤240): int32 is fine.
                // The EW accumulators are int64 (near-camera vertices can push
                // them out of int32 range), but the skip-check below guarantees
                // EW fits in int32 by the time we actually divide — so we can
                // use the Xtensa hardware 32-bit divider instead of the ~70-cycle
                // soft 64-bit __divdi3/__moddi3 routines.
                iStart = 0;
                iEnd = iMaxRow;


                // ceil(num/D) > iEnd  iff  num > D*iEnd  (D, iEnd both int32,
                // product ≤ max_D * 240 which stays well inside int32).
                // floor(EW/negD) > iEnd  iff  EW > negD*iEnd  (same bounds).
                // When the skip condition is false, EW (or num) ≤ D*iEnd fits
                // in int32, so the division below uses 32-bit hardware arithmetic.
                #define JET_EDGE_RANGE(EW, D)                                                       \
                    do {                                                                              \
                        if ((D) > 0) {                                                                \
                            if ((EW) < 0) {                                                           \
                                int64_t num = -(EW);                                                  \
                                if (num > (int64_t)(D) * iEnd) {                                      \
                                    /* req > iEnd → row is empty */                                   \
                                    skipRow = true;                                                   \
                                } else {                                                              \
                                    /* num ≤ D*iEnd: fits in int32, use hw 32-bit divide */           \
                                    int32_t req = ((int32_t)num + (D) - 1) / (D);                    \
                                    if (req > iStart) iStart = req;                                   \
                                }                                                                     \
                            }                                                                         \
                        } else if ((D) < 0) {                                                         \
                            if ((EW) < 0) { skipRow = true; }                                         \
                            else {                                                                    \
                                int32_t negD = -(D);                                                  \
                                if ((EW) > (int64_t)negD * iEnd) {                                    \
                                    /* floor(EW/negD) > iEnd → no tightening needed, skip divide */  \
                                } else {                                                              \
                                    /* EW ≤ negD*iEnd: fits in int32, use hw 32-bit divide */         \
                                    int32_t req = (int32_t)(EW) / negD;                               \
                                    if (req < iEnd) iEnd = req;                                       \
                                }                                                                     \
                            }                                                                         \
                        } else { /* D == 0 */                                                         \
                            if ((EW) < 0) skipRow = true;                                             \
                        }                                                                             \
                    } while (0)

                JET_EDGE_RANGE(w0_row, dw0_dx_step);
                if (!skipRow) JET_EDGE_RANGE(w1_row, dw1_dx_step);
                if (!skipRow) JET_EDGE_RANGE(w2_row, dw2_dx_step);
                #undef JET_EDGE_RANGE

                }

                if (skipRow || iStart > iEnd) continue;

                // Advance ew accumulators to the first inside x.
                int64_t ew0 = w0_row + (int64_t)dw0_dx_step * iStart;
                int64_t ew1 = w1_row + (int64_t)dw1_dx_step * iStart;
                int64_t ew2 = w2_row + (int64_t)dw2_dx_step * iStart;

                const int xStart = minX + iStart * xStep;
                const int xEnd   = minX + iEnd   * xStep;

                // Wireframe / outline mode. The per-row solver above already
                // gave us the leftmost and rightmost x for this scanline of
                // the triangle; plotting just those two pixels (one when the
                // row is a single pixel wide, e.g. at the top/bottom apex)
                // traces the triangle's silhouette. We skip the entire span
                // body — no z-buffer, no lighting, no texturing, no dither —
                // which is what the production fill path was eating cycles
                // on for huge near-camera triangles in the previous
                // Bresenham-edge implementation. The fill is replaced with
                // at most two stores per scanline.
                if (wireframeMode)
                {
                    const uint16_t wireColor = material->color;
                    auto plotWire = [this, wireColor](int px, int py) {
    #if HALF_WIDTH_BUFFERS
        #if FIELD_BUFFERS
                        framebuffer[(py >> 1) * (screenWidth / 2) + (px >> 1)] = wireColor;
        #else
                        framebuffer[py * (screenWidth / 2) + (px >> 1)] = wireColor;
        #endif
    #else
        #if FIELD_BUFFERS
                        framebuffer[(py >> 1) * screenWidth + px] = wireColor;
        #else
                        framebuffer[py * screenWidth + px] = wireColor;
        #endif
    #endif
                    };
                    plotWire(xStart, y);
                    if (xEnd != xStart) plotWire(xEnd, y);
                    continue;
                }

    #if MAX_PICK_QUERIES > 0
                // Per-row screen-space pick test. Cheap (MAX_PICK_QUERIES is
                // tiny and compile-time bounded) and sees every triangle that
                // covers the queried pixel regardless of which fast/slow
                // span path the rasterizer takes below. We don't try to
                // emulate per-pixel screen-door stipple or texture-key holes
                // picking semantics here are "which surface intersects
                // this screen-space ray", which is what the host actually
                // wants for mouse-over / cursor selection. Z arbitration is
                // strictly closer-wins so the result matches the visible
                // painter-sorted / Z-buffered scene.
                if (pickQueries && pickResults && pickQueryCount > 0)
                {
                    // Triangle-effective Z. With FAST_Z this is already
                    // the constant `z` set up at the top of drawTriangle;
                    // without FAST_Z we don't have per-pixel Z here yet (it
                    // would need a barycentric eval per query) so fall back
                    // to the triangle average - still good enough for
                    // ordering since hit triangles are typically small.
                    int32_t pickZ;
                #if FAST_Z
                    pickZ = z;
                #else
                    pickZ = (v1.position.z + v2.position.z + v3.position.z) / 3;
                #endif
                    for (int p = 0; p < pickQueryCount; ++p)
                    {
                        const PickQuery& q = pickQueries[p];
                        if (q.x < 0 || q.y < 0) continue;          // disabled slot
                        if (q.y != y) continue;                    // wrong row
                        // HALF_WIDTH_BUFFERS rasterises in 2-pixel xStep; snap
                        // the query x to the same 2-pixel grid before the
                        // range check so an odd query x still hits the cell
                        // the rasterizer actually wrote.
                    #if HALF_WIDTH_BUFFERS
                        const int qx = q.x & ~1;
                    #else
                        const int qx = q.x;
                    #endif
                        if (qx < xStart || qx > xEnd) continue;
                        PickResult& r = pickResults[p];
                        if (!r.hit || pickZ < r.depth)
                        {
                            r.hit           = true;
                            r.object        = currentPickObject;
                            r.triangleIndex = currentPickTriangleIndex;
                            r.depth         = pickZ;
                            r.x             = (int16_t)qx;
                            r.y             = (int16_t)y;
                        }
                    }
                }
    #endif // MAX_PICK_QUERIES > 0

    #if LIGHTING
                // Per-row Gouraud brightness init. Computed in int64 because
                // the numerator can be up to 1533 * |denom| and we shift left
                // by 16 for Q16 precision; divided once per row instead of
                // once per pixel.
                int32_t brightness_q16 = 0;
                if (useIncrementalGouraud)
                {
                    const int64_t bRowNum = (int64_t)vertexBrightness[0] * ew0
                                          + (int64_t)vertexBrightness[1] * ew1
                                          + (int64_t)vertexBrightness[2] * ew2;
                    // See note at brightness_dx_step_q16 setup: divide by
                    // the full-precision int64 denom or super-near triangles
                    // flash dark when denom64 overflows int32.
                    brightness_q16 = (int32_t)((float)bRowNum * 65536.0f * invDenom64f);
                }
    #endif

                // WATER_REFLECT: precompute the framebuffer row-base for the
                // mirror scanline (screenH-1-y ± ripple).  clearBuffers() has
                // already filled every row with the sky gradient, so upper rows
                // contain sky even before any geometry is drawn.  Anything
                // rendered before this water object (rocks, hills, etc.) is
                // also present — giving true screen-space reflections for free.
                // Colour is set per-pixel in the fast-span and general paths.
                // Intentionally outside #if LIGHTING — works with LIGHTING=0.
                int32_t waterMirrorBufBase = 0;
                uint8_t waterReflectAlpha = material->alpha;
                bool waterSkyFallback = false;
                if (isWaterReflect)
                {
                    // `specular` expresses ripple amplitude in pixels at a
                    // canonical 800-pixel reference height.  Scaling by
                    // screenHeight/800 keeps the visual ripple consistent
                    // across resolutions: 28 → 28 px on 800p desktop,
                    // 28 → ~8 px on 240p ESP32.
                    const int amp    = (int)material->specular * screenHeight / 2400; // /3 of original
                    // Perspective-correct wave density: waves should appear compressed
                    // (denser) near the horizon where geometry is far away, and stretched
                    // (sparser) near the camera.  (screenHeight - y) is large at the top of
                    // the screen (horizon side) and approaches 0 at the very bottom (camera
                    // side), so squaring it produces the right quadratic phase accumulation —
                    // many wave cycles near the waterline, fewer toward the viewer.
                    // Cost: one extra multiply and a divide versus the old linear y*5.
                    const int perspY = screenHeight - y;
                    const int angle  = ((perspY * perspY * 20 / screenHeight + (int)(waterTime * 240.0f)) % 360 + 360) % 360;
                    //const int angle = ((y * 5 + (int)(waterTime * 240.0f)) % 360 + 360) % 360; //MB: This version doesn't take perspective into account, so waves look the same near and far, which is less realistic but more performant.
                    const int ripple = (lookupSinI(angle) * amp) >> 10; // Q10 → pixels
                    // Use the camera-pitch-correct waterline as the mirror axis:
                    // mirrorY = 2*waterlineY - y gives geometrically accurate
                    // reflections for objects at any distance. Falls back to
                    // screen-centre (screenHeight/2) if waterlineY wasn't set.
                    const int wl    = (waterlineY > 0) ? waterlineY : (screenHeight / 2);
                    // waterYBias nudges the sampled row downward (larger mirrorY =
                    // lower on screen) to fine-tune the apparent reflection height.
                    int mirrorY = 2 * wl - y + ripple + (int)material->waterYBias;
                    // Sky fallback: when the reflection would sample off the top of
                    // the screen, use gradientColors[0] (the topmost sky colour)
                    // as the reflected pixel so the blend picks up the right sky hue
                    // instead of repeating framebuffer row 0.
                    waterSkyFallback = mirrorY < 0 &&
                                       gradientColors != nullptr &&
                                       gradientSize > 0;
                    if (mirrorY < 0)             mirrorY = 0;
                    if (mirrorY >= screenHeight) mirrorY = screenHeight - 1;
                    constexpr int kSsrTopFadePx = 32;
                    if (mirrorY < kSsrTopFadePx) {
                        waterReflectAlpha = (uint8_t)(((uint16_t)material->alpha * mirrorY) / kSsrTopFadePx);
                    }
    #if HALF_WIDTH_BUFFERS
      #if FIELD_BUFFERS
                    waterMirrorBufBase = (mirrorY >> 1) * (screenWidth / 2);
      #else
                    waterMirrorBufBase = mirrorY * (screenWidth / 2);
      #endif
    #else
      #if FIELD_BUFFERS
                    waterMirrorBufBase = (mirrorY >> 1) * screenWidth;
      #else
                    waterMirrorBufBase = mirrorY * screenWidth;
      #endif
    #endif
                }

    #if JET_FAST_SIMPLE_SPANS
    #if TEXTURE_MAPPING
                // Untextured triangles take the fast simple-span path even in
                // builds where TEXTURE_MAPPING is compiled in. Textured ones
                // fall through to the general per-pixel loop below.
                if (useFastSimpleSpan)
    #endif
                {
                // Fast simple-span path. See JET_FAST_SIMPLE_SPANS comment at
                // the top of the TU. Skips per-pixel edge accumulate/test (the
                // solver above already bounded x to the inside range) and the
                // per-pixel dither lookup (alpha is triangle-constant in this
                // config, so the mask reduces to two per-row booleans).
                (void)ew0; (void)ew1; (void)ew2;  // Unused in this path.
    #if FIELD_BUFFERS
                int32_t bufferIndex = (y >> 1) * (screenWidth / 2) + (xStart / 2);
    #else
                int32_t bufferIndex = y * (screenWidth / 2) + (xStart / 2);
    #endif
    #if SCREEN_DOOR_ALPHA
                bool drawP0, drawP2;
                if (alpha > 240) {
                    drawP0 = drawP2 = true;
                } else {
                    // Same 4x4 Bayer matrix as shouldDrawPixel, but we only
                    // need the two phases that HALF_WIDTH actually steps
                    // through (x%4 \u2208 {0, 2} when xStart is 2-aligned).
                    constexpr uint8_t thresholdMatrix[16] = {
                        15, 135, 45, 165,
                        195, 75, 225, 105,
                        60, 180, 30, 150,
                        240, 120, 210, 90};
                    const int yRow = (y & 3) << 2;
                    drawP0 = alpha >= thresholdMatrix[0 | yRow];
                    drawP2 = alpha >= thresholdMatrix[2 | yRow];
                }
                if (!drawP0 && !drawP2) continue;  // whole row dithered out

                if (drawP0 && drawP2) {
                    // Solid fill: pair adjacent uint16 stores into 32-bit
                    // writes when alignment permits. Halves the store count on
                    // the hot path - this is where most of the per-frame time
                    // goes for near-opaque geometry.
                    fillRGB565Span(framebuffer, bufferIndex, ((xEnd - xStart) >> 1) + 1, color);
                } else {
                    // Alternating fill: only one of the two phases draws. Pick
                    // the matching phase bool and stride by 4 pixels (2 slots).
                    const bool startIsP0 = ((xStart & 3) == 0);
                    const bool drawFirst = startIsP0 ? drawP0 : drawP2;
                    int x = xStart;
                    if (!drawFirst) { x += 2; bufferIndex++; }
                    for (; x <= xEnd; x += 4, bufferIndex += 2) {
                        framebuffer[bufferIndex] = color;
                    }
                }
    #else  // !SCREEN_DOOR_ALPHA
                // Traditional alpha-blend path. For fully-opaque triangles we
                // still emit the paired 32-bit fast stores; sub-opaque ones
                // fall through to a per-pixel "over" blend against the
                // existing framebuffer contents.
                if (plainOpaqueReplace) {
                    fillRGB565Span(framebuffer, bufferIndex, ((xEnd - xStart) >> 1) + 1, color);
                } else if (isWaterReflect) {
                    // Screen-space reflection: read each pixel from the mirror
                    // row already written in the framebuffer (sky gradient +
                    // any geometry drawn before this water object).
                    // When reflectBuffer is set (SSR_FIELD_REFLECT), read from
                    // the previous field instead — depth-independent, fully
                    // committed prior frame.
                    // mirrorIdx steps in x-lockstep with bufferIndex.
                    const uint16_t* srcBuf = reflectBuffer ? reflectBuffer : framebuffer;
                    int32_t mirrorIdx = waterMirrorBufBase + xStart / 2;
                    const uint16_t skyCol = waterSkyFallback ? gradientColors[0] : 0;
                    const int spanCount = ((xEnd - xStart) >> 1) + 1;
                    if (waterSkyFallback) {
                        fillRGB565Span(framebuffer, bufferIndex, spanCount,
                            blendRGB565(material->color, skyCol, waterReflectAlpha));
                    } else if (spanCount < 32) {
                        for (int i = 0; i < spanCount; ++i)
                            framebuffer[bufferIndex + i] = blendRGB565(material->color,
                                srcBuf[mirrorIdx + i], waterReflectAlpha);
                    } else {
                        blendConstant(framebuffer + bufferIndex, srcBuf + mirrorIdx,
                            spanCount, material->color, waterReflectAlpha, true);
                    }
                } else if (isAdditive) {
                    // Saturating-add: source scaled by alpha then added to destination.
                    // alpha==255 fast path skips the per-channel multiply.
                    if (((xEnd - xStart) >> 1) + 1 >= 32) {
                        blendRGB565Span(framebuffer + bufferIndex, nullptr,
                            ((xEnd - xStart) >> 1) + 1, color, alpha,
                            alpha == 255 ? RGB565BlendMode::Add : RGB565BlendMode::AddAlpha256);
                    } else if (alpha == 255) {
                        for (int x = xStart; x <= xEnd; x += 2, bufferIndex++) {
                            const uint16_t d = framebuffer[bufferIndex];
                            uint32_t r = ((d >> 11) & 0x1Fu) + ((color >> 11) & 0x1Fu); if (r > 0x1Fu) r = 0x1Fu;
                            uint32_t g = ((d >>  5) & 0x3Fu) + ((color >>  5) & 0x3Fu); if (g > 0x3Fu) g = 0x3Fu;
                            uint32_t b = ( d        & 0x1Fu) + ( color        & 0x1Fu); if (b > 0x1Fu) b = 0x1Fu;
                            framebuffer[bufferIndex] = (uint16_t)((r << 11) | (g << 5) | b);
                        }
                    } else {
                        for (int x = xStart; x <= xEnd; x += 2, bufferIndex++) {
                            framebuffer[bufferIndex] = addBlendRGB565(framebuffer[bufferIndex], color, alpha);
                        }
                    }
                } else {
                    const int count = ((xEnd - xStart) >> 1) + 1;
                    if (count >= 32) {
                        blendConstant(framebuffer + bufferIndex, nullptr,
                            count, color, alpha, false);
                    } else {
                        for (int i = 0; i < count; ++i)
                            framebuffer[bufferIndex + i] = blendRGB565(framebuffer[bufferIndex + i], color, alpha);
                    }
                }
    #endif // SCREEN_DOOR_ALPHA
                }  // close fast simple-span block
    #if TEXTURE_MAPPING
                else
    #endif
    #endif // JET_FAST_SIMPLE_SPANS

    #if !JET_FAST_SIMPLE_SPANS || TEXTURE_MAPPING
                {  // General per-pixel path. Runs as the textured `else` when
                   // the fast path is also compiled (TEXTURE_MAPPING build),
                   // or unconditionally when JET_FAST_SIMPLE_SPANS is
                   // false (any of the heavy features enabled).
#if TEXTURE_MAPPING && !PERSPECTIVE_CORRECT_TEXTURES
                int32_t uQ16=0,vQ16=0;
                bool rowIncrementalUV=incrementalUV;
                if (rowIncrementalUV) {
                    // Full-width edge values avoid the old int32 UV*weight
                    // overflow on large projected triangles near the camera.
                    const float uRow=((float)v1.uv.x*(float)ew0 + (float)v2.uv.x*(float)ew1
                                    + (float)v3.uv.x*(float)ew2)*uvInvArea;
                    const float vRow=((float)v1.uv.y*(float)ew0 + (float)v2.uv.y*(float)ew1
                                    + (float)v3.uv.y*(float)ew2)*uvInvArea;
                    rowIncrementalUV=std::abs(uRow)<=uvLimit+1 && std::abs(vRow)<=uvLimit+1;
                    if (rowIncrementalUV) {
                        uQ16=(int32_t)std::lround(uRow*65536.0f);
                        vQ16=(int32_t)std::lround(vRow*65536.0f);
                    }
                }
                const int32_t rowUStep=rowIncrementalUV?uStepQ16:0;
                const int32_t rowVStep=rowIncrementalUV?vStepQ16:0;
    #define JET_UV_STEP , uQ16 += rowUStep, vQ16 += rowVStep
#else
    #define JET_UV_STEP
#endif
#if JET_FAST_SIMPLE_SPANS && TEXTURE_MAPPING && !PERSPECTIVE_CORRECT_TEXTURES && !BILINEAR_FILTER
                if (decltype(useSpans)::value && rowIncrementalUV && directRGB565
                    && plainOpaqueReplace && !diffuseMap->hasAlpha) {
                    // Opaque affine tiles need neither per-pixel edge tests
                    // nor the lighting/alpha/depth machinery of the general
                    // loop. Coverage comes from the exact scanline walker.
                    const uint16_t* texels=diffuseMap->data;
                    const unsigned tw=diffuseMap->width, th=diffuseMap->height;
    #if HALF_WIDTH_BUFFERS
                    constexpr int strideDiv=2;
    #else
                    constexpr int strideDiv=1;
    #endif
    #if FIELD_BUFFERS
                    const int rowBase=(y>>1)*(screenWidth/strideDiv);
    #else
                    const int rowBase=y*(screenWidth/strideDiv);
    #endif
                    auto textureSpan=[&](auto fading) {
                        int32_t uq=uQ16,vq=vQ16;
                        int index=rowBase+xStart/strideDiv;
                        for(int x=xStart;x<=xEnd;x+=xStep,++index,uq+=rowUStep,vq+=rowVStep) {
    #if !HALF_WIDTH_BUFFERS
                            if(checkerboardMode && (((x^y)&1)!=cbFrameParity)) continue;
    #endif
                            const unsigned tx=((unsigned)(uq/65536)&(FIXED_POINT_SCALE-1))*tw/FIXED_POINT_SCALE;
                            const unsigned ty=((unsigned)(vq/65536)&(FIXED_POINT_SCALE-1))*th/FIXED_POINT_SCALE;
                            uint16_t texel=texels[ty*tw+tx];
                            if constexpr(decltype(fading)::value) {
    #if SCREEN_DOOR_ALPHA
                                if(!shouldDrawPixel(x,y,textureLodFade)) texel=material->color;
    #else
                                texel=blendRGB565(material->color,texel,textureLodFade);
    #endif
                            }
                            framebuffer[index]=texel;
                        }
                    };
                    if(textureLodFade==255) textureSpan(std::false_type{});
                    else textureSpan(std::true_type{});
                } else {
#endif
    // Per-pixel brightness step appended to the for-loop header below. The
    // step has to happen unconditionally per pixel (continues elsewhere in
    // the loop body would otherwise desync the running value), so it lives
    // in the increment list rather than the body.
    #if LIGHTING
    #define JET_LIT_STEP , brightness_q16 += brightness_dx_step_q16
    #else
    #define JET_LIT_STEP
    #endif
    #if HALF_WIDTH_BUFFERS
    #if FIELD_BUFFERS
                int32_t bufferIndex = (y >> 1) * (screenWidth / 2) + (xStart / 2);
    #else
                int32_t bufferIndex = y * (screenWidth / 2) + (xStart / 2);
    #endif
            for (int x = xStart; x <= xEnd;
                 x += 2, ew0 += dw0_dx_step, ew1 += dw1_dx_step, ew2 += dw2_dx_step, bufferIndex++ JET_LIT_STEP JET_UV_STEP)
                {
    #else
    #if FIELD_BUFFERS
                int32_t bufferIndex = (y >> 1) * screenWidth + xStart;
    #else
                int32_t bufferIndex = y * screenWidth + xStart;
    #endif
            for (int x = xStart; x <= xEnd;
                 x++, ew0 += dw0_dx_step, ew1 += dw1_dx_step, ew2 += dw2_dx_step, bufferIndex++ JET_LIT_STEP JET_UV_STEP)
                {
    #endif
                // Z-buffer index. Stride matches the configured depth-buffer
                // layout: half-width (one cell per two output pixels) when
                // HALF_WIDTH_BUFFERS is on, otherwise per-pixel (same stride
                // as the colour buffer). Set per-row.
                #if Z_BUFFERING
                int32_t zBufferIndex = y * ZBUFFER_STRIDE(screenWidth);
                #endif

                    #if Z_BUFFERING
                    // If the z-buffer position is at its maximum for this pixel (as close to the camera as is possible), skip this pixel
                    // since it can't possibly be any closer.
                    if (zBuffer[zBufferIndex] == 0 && !ignoreZBuffer)
                    {
                        continue;
                    }
                    #endif

                    // Inside test on the incrementally-stepped edge function.
                    // Comparing int64 with 0 only examines the high word, so
                    // this is cheap. The scanline range above already trimmed
                    // the iteration to (mostly) inside-only pixels, but a
                    // small shoulder can still be outside due to the integer
                    // ceil/floor rounding - keep the test as a safety net.
                    if ((ew0 | ew1 | ew2) < 0)
                    {
                        continue;
                    }

    #if !HALF_WIDTH_BUFFERS
                    // Checkerboard: skip pixels that belong to the other frame's pattern.
                    // Using XOR parity: (x^y)&1 == (x+y)&1 (no carry in the lowest bit).
                    if (checkerboardMode && (((x ^ y) & 1) != cbFrameParity))
                    {
                        continue;
                    }
    #endif

                    // For any inside pixel, |ew_i| <= |denom|. If any downstream
                    // code needs int32 barycentric weights (interpolation paths)
                    // they're safe to narrow here. The compiler DCEs these on
                    // the FAST_Z / no-texture / no-lighting fast path.
                    [[maybe_unused]] int32_t w0 = (int32_t)ew0;
                    [[maybe_unused]] int32_t w1 = (int32_t)ew1;
                    [[maybe_unused]] int32_t w2 = (int32_t)ew2;

    // Pixel is inside the triangle - render it
    // Interpolate z, u, v
    #if !FAST_Z
                    int32_t z = (v1.position.z * w0 + v2.position.z * w1 + v3.position.z * w2) / denom;

                    if (z < nearPlane || z > farPlane)
                    {
                        continue;
                    }

    #if Z_BUFFERING
                    // Per-pixel Z bias in real depth units. Clamp to uint16
                    // for storage (matches the FAST_Z setup path).
                    int32_t zbRaw = z - zBias;
                    if (zbRaw < 0)     zbRaw = 0;
                    if (zbRaw > 65535) zbRaw = 65535;
                    uint32_t zb = (uint32_t)zbRaw;
    #endif

    #if Z_BRIGHTNESS
                    brightness = 255 - ((z - nearPlane) * 127) / (farPlane - nearPlane);
                    brightness = std::min(brightness, static_cast<uint16_t>(255));
    #endif
    #endif

    #if DEPTH_ALPHA_BLEND && !FAST_Z
                    // Per-pixel depth fog. Only used on the !FAST_Z path where
                    // z genuinely varies per pixel; on the FAST_Z path this is
                    // hoisted to the triangle setup above. Compose with the
                    // existing alpha (material*objAlpha) by taking the
                    // minimum into a per-pixel local — important: must NOT
                    // mutate `alpha`, which is shared across all pixels of
                    // this triangle, or each pixel's fade would compound on
                    // the previous one's.
                    uint8_t pixAlpha = alpha;
                    if (z > depthFogNear && z < depthFogFar)
                    {
                        uint8_t fogA = (uint8_t)(255 - ((z - depthFogNear) * 255) / (depthFogFar - depthFogNear));
                        if (fogA < pixAlpha) pixAlpha = fogA;
                    }
                    else if (z >= depthFogFar)
                    {
                        pixAlpha = 0;
                    }
    #else
                    uint8_t pixAlpha = alpha;
    #endif

    #if SCREEN_DOOR_ALPHA
                    // Stippling based on alpha value
                    if (!shouldDrawPixel(x, y, pixAlpha))
                    {
                        continue;
                    }
    #endif

    #if Z_BUFFERING
                    if (!ignoreZBuffer && zb > zBuffer[zBufferIndex])
                    {
                        continue;
                    }
    #endif

    #if TEXTURE_MAPPING
                    if (diffuseMap)
                    {
                        if (diffuseMap->screenSpace)
                        {
                            // **Screen-space texture mapping**
                            uv.x = x * diffuseMap->width / screenWidth;
                            uv.y = y * diffuseMap->height / screenHeight;
                        }
                        else if (diffuseMap->reflectionMap)
                        {
                            uv.x = x + z;
                            uv.y = y + z;
                        }
                        else
                        {
    #if PERSPECTIVE_CORRECT_TEXTURES
                            // Interpolate 1/z, u/z, and v/z at the current pixel
                            int32_t interpolatedOneOverZ = (oneOverZ1 * w0 + oneOverZ2 * w1 + oneOverZ3 * w2) / denom;
                            int32_t interpolatedUOverZ = (uOverZ1 * w0 + uOverZ2 * w1 + uOverZ3 * w2) / denom;
                            int32_t interpolatedVOverZ = (vOverZ1 * w0 + vOverZ2 * w1 + vOverZ3 * w2) / denom;

                            // Avoid division by zero
                            if (interpolatedOneOverZ == 0)
                                continue;

                            // Compute final texture coordinates
                            uv.x = (interpolatedUOverZ * FIXED_POINT_SCALE) / interpolatedOneOverZ;
                            uv.y = (interpolatedVOverZ * FIXED_POINT_SCALE) / interpolatedOneOverZ;
    #else // Affine texture mapping
                            if (rowIncrementalUV) {
                                uv.x = uQ16 / 65536;
                                uv.y = vQ16 / 65536;
                            } else {
                                uv.x = (int32_t)(((double)v1.uv.x * ew0 + (double)v2.uv.x * ew1 + (double)v3.uv.x * ew2) / denom64);
                                uv.y = (int32_t)(((double)v1.uv.y * ew0 + (double)v2.uv.y * ew1 + (double)v3.uv.y * ew2) / denom64);
                            }
    #endif
                        }

                        // Sample color from material
    #if !PERSPECTIVE_CORRECT_TEXTURES && !BILINEAR_FILTER
                        if (directRGB565) {
                            const unsigned tx=((unsigned)uv.x & (FIXED_POINT_SCALE-1))*diffuseMap->width/FIXED_POINT_SCALE;
                            const unsigned ty=((unsigned)uv.y & (FIXED_POINT_SCALE-1))*diffuseMap->height/FIXED_POINT_SCALE;
                            color=diffuseMap->data[ty*diffuseMap->width+tx];
                        } else
    #endif
                        color = material->getColor(uv);

                        // If the material diffuse map has a transparent color and that's what we got, skip drawing this pixel
                        if (diffuseMap->hasAlpha && color == diffuseMap->alphaColor)
                        {
                            continue;
                        }

                        // Distance-based texture LOD cross-fade. textureLodFade
                        // is triangle-constant; outside the fade band it is
                        // 255 (no work). Inside the band, swap or blend the
                        // sampled texel toward material->color using whichever
                        // composite method the build is configured for.
                        if (textureLodFade < 255)
                        {
                        #if SCREEN_DOOR_ALPHA
                            // Stipple between texel (drawn) and flat (drawn).
                            // The same Bayer matrix as material alpha so the
                            // two stipples don't beat against each other in
                            // unpleasant ways.
                            if (!shouldDrawPixel(x, y, textureLodFade))
                                color = material->color;
                        #else
                            // dst = flat, src = texel. blendRGB565 returns
                            // src*alpha + dst*(1-alpha), so alpha=textureLodFade
                            // gives full texel at 255 and full flat at 0.
                            color = blendRGB565(material->color, color, textureLodFade);
                        #endif
                        }
                    }
    #endif

    #if Z_BUFFERING
                    if (!noWriteZBuffer)
                    {
                        zBuffer[zBufferIndex] = static_cast<uint16_t>(zb);
                    }
    #endif

    #if DEBUG_OVERDRAW
                    // Orange color in RGB565 format (0xFDA0)
                    color = 0xFDA0;
                    // Apply 25% blend
                    uint8_t r = ((color >> 11) & 0x1F) / 4;
                    uint8_t g = ((color >> 5) & 0x3F) / 4;
                    uint8_t b = (color & 0x1F) / 4;

                    // Get existing color and blend
                    uint16_t existing = framebuffer[bufferIndex];
                    uint8_t existingR = (existing >> 11) & 0x1F;
                    uint8_t existingG = (existing >> 5) & 0x3F;
                    uint8_t existingB = existing & 0x1F;

                    // Add the colors
                    r = std::min(static_cast<uint8_t>(31), static_cast<uint8_t>(existingR + r));
                    g = std::min(static_cast<uint8_t>(63), static_cast<uint8_t>(existingG + g));
                    b = std::min(static_cast<uint8_t>(31), static_cast<uint8_t>(existingB + b));

                    color = (r << 11) | (g << 5) | b;

                    #if HALF_WIDTH_BUFFERS
                    framebuffer[bufferIndex] = color;
                    #else
                    uint32_t combinedColor = (static_cast<uint32_t>(color) << 16) | color;
                    reinterpret_cast<uint32_t *>(framebuffer)[bufferIndex / 2] = combinedColor;
                    #endif
                    continue;
    #else
    #if LIGHTING || Z_BRIGHTNESS
                    if (directionalLight)
                    {
                        if (material->shadingMode == ShadingMode::GOURAUD)
                        {
                            // Plane-equation incremental Gouraud brightness:
                            // brightness_q16 was set at the row start and is
                            // stepped by brightness_dx_step_q16 in the for-loop
                            // header below. Mathematically equivalent to the
                            // original (b0*w0 + b1*w1 + b2*w2)/denom but trades
                            // a per-pixel divide + 3 multiplies for a single
                            // int32 add per pixel.
                            //
                            // Clamp into the legitimate brightness band
                            // [0, 255 + specular]. Without this cap any minor
                            // overshoot from incremental rounding (or a near-
                            // camera triangle where the q16 slope is large)
                            // landed in the deep "blowout" range and the
                            // per-pixel modulation pushed channels far past
                            // full-white into wrap-around territory — the
                            // bright random-colour flashes the user was seeing.
                            int32_t b = brightness_q16 >> 16;
                            const int32_t bMax = 255 + material->specular;
                            if (b < 0) b = 0;
                            if (b > bMax) b = bMax;
                            brightness = (uint16_t)b;
                        }
                        else if (material->shadingMode == ShadingMode::PHONG)
                        {
                            // Interpolate normals and shade per pixel. The shared
                            // helper handles Lambert + view-facing specular and
                            // the brightness cap; PHONG just pays the per-pixel
                            // normal renormalisation cost on top of GOURAUD.
                            Vector3 pixelNormal;
    #if PERSPECTIVE_CORRECT_TEXTURES
                            // Perspective-correct normal interpolation: interpolate
                            // n/z at each vertex then divide by the interpolated
                            // 1/z — same reconstruction as UV perspective correction.
                            // Avoids the affine "pinching" visible on oblique faces.
                            int32_t pctOneOverZ = (oneOverZ1 * w0 + oneOverZ2 * w1 + oneOverZ3 * w2) / denom;
                            if (pctOneOverZ != 0)
                            {
                                int32_t nxOZ = (nxOverZ1 * w0 + nxOverZ2 * w1 + nxOverZ3 * w2) / denom;
                                int32_t nyOZ = (nyOverZ1 * w0 + nyOverZ2 * w1 + nyOverZ3 * w2) / denom;
                                int32_t nzOZ = (nzOverZ1 * w0 + nzOverZ2 * w1 + nzOverZ3 * w2) / denom;
                                pixelNormal.x = (nxOZ * FIXED_POINT_SCALE) / pctOneOverZ;
                                pixelNormal.y = (nyOZ * FIXED_POINT_SCALE) / pctOneOverZ;
                                pixelNormal.z = (nzOZ * FIXED_POINT_SCALE) / pctOneOverZ;
                            }
                            else
                            {
                                pixelNormal = { 0, 0, -(int32_t)FIXED_POINT_SCALE };
                            }
    #else
                            // Affine interpolation (no perspective correction).
                            pixelNormal.x = (v1.normal.x * w0 + v2.normal.x * w1 + v3.normal.x * w2) / denom;
                            pixelNormal.y = (v1.normal.y * w0 + v2.normal.y * w1 + v3.normal.y * w2) / denom;
                            pixelNormal.z = (v1.normal.z * w0 + v2.normal.z * w1 + v3.normal.z * w2) / denom;
    #endif

                            auto normalLength = pixelNormal.length();
                            if (normalLength > 0)
                            {
                                pixelNormal = (pixelNormal * static_cast<int32_t>(FIXED_POINT_SCALE)) / normalLength;
                            }

                            brightness = jetShadeBrightness(
                                pixelNormal,
                                directionalLight->lightDir,
                                lightIntensity,
                                material->diffuse,
                                material->specular);
                        }
                    }
    #endif

    #if LIGHTING || Z_BRIGHTNESS
                    // If POSTFX_CELLSHADING is enabled, kill off the bottom N bits of the brightness value to create a cell shading effect
                    if (POSTFX_CELLSHADING)
                    {
                        brightness = brightness >> CELLSHADING_CELL_BITS << CELLSHADING_CELL_BITS;
                    }

                    // The triangle-setup hoist above already computed the lit
                    // color for FLAT/unlit triangles without a diffuse texture
                    // - skip the per-pixel modulation in that case. (Z_BRIGHTNESS
                    // without LIGHTING never hoists, so guard the flag access
                    // behind the same #if it was declared under.)
    #if LIGHTING
                    if (!emissive && !flatColorPrecomputed)
    #else
                    if (!emissive)
    #endif
                    {
    #if LIGHTING
                        // Per-pixel modulation. Two paths:
                        //   1) Untextured (litPerPixelUntextured): base channels
                        //      were extracted once at triangle setup; inline the
                        //      add+saturate+multiply directly so the inner loop
                        //      has no function call and no re-extract.
                        //   2) Textured: `color` is the freshly-sampled texel,
                        //      so we need the general per-channel helper.
                        if (litPerPixelUntextured)
                        {
                            uint32_t tR = (uint32_t)brightness + ambR;
                            uint32_t tG = (uint32_t)brightness + ambG;
                            uint32_t tB = (uint32_t)brightness + ambB;
                            if (tR > litMaxBrightness) tR = litMaxBrightness;
                            if (tG > litMaxBrightness) tG = litMaxBrightness;
                            if (tB > litMaxBrightness) tB = litMaxBrightness;

                            uint32_t r, g, b;
                            if (tR > 255) { uint32_t blow = tR - 255; r = litBaseR5 + ((31u - litBaseR5) * blow) / 256u; }
                            else          { uint32_t v = litBaseR5 * tR; r = (v + 128u + (v >> 8)) >> 8; }
                            if (tG > 255) { uint32_t blow = tG - 255; g = litBaseG6 + ((63u - litBaseG6) * blow) / 256u; }
                            else          { uint32_t v = litBaseG6 * tG; g = (v + 128u + (v >> 8)) >> 8; }
                            if (tB > 255) { uint32_t blow = tB - 255; b = litBaseB5 + ((31u - litBaseB5) * blow) / 256u; }
                            else          { uint32_t v = litBaseB5 * tB; b = (v + 128u + (v >> 8)) >> 8; }
                            color = (uint16_t)((r << 11) | (g << 5) | b);
                        }
                        else
                        {
                            const uint16_t maxBrightness = (uint16_t)(255u + material->specular);
                            color = jetModulateRGB565(color, brightness, ambR, ambG, ambB, maxBrightness);
                        }
    #else
                        // Z_BRIGHTNESS-only path: no ambient light, just a
                        // scalar brightness applied to all three channels.
                        if (brightness >= 255)
                        {
                            uint16_t blowout = brightness - 255;
                            uint8_t r = ((color >> 11) & 0x1F);
                            uint8_t g = ((color >> 5) & 0x3F);
                            uint8_t b = (color & 0x1F);
                            r = r + ((31 - r) * blowout) / 256;
                            g = g + ((63 - g) * blowout) / 256;
                            b = b + ((31 - b) * blowout) / 256;
                            color = (r << 11) | (g << 5) | b;
                        }
                        else
                        {
                            const uint16_t br = ((color >> 11) & 0x1F) * brightness;
                            const uint16_t bg = ((color >>  5) & 0x3F) * brightness;
                            const uint16_t bb = ( color        & 0x1F) * brightness;
                            const uint8_t r = (uint8_t)((br + 128 + (br >> 8)) >> 8);
                            const uint8_t g = (uint8_t)((bg + 128 + (bg >> 8)) >> 8);
                            const uint8_t b = (uint8_t)((bb + 128 + (bb >> 8)) >> 8);
                            color = (r << 11) | (g << 5) | b;
                        }
    #endif
                    }
    #endif
    #endif

                // WATER_REFLECT on the general (LIGHTING=1) per-pixel path:
                // sample the mirror row from the framebuffer.  The emissive
                // flag above already bypassed all lighting modulation.
                // When reflectBuffer is set, use it instead (prev-field SSR).
                if (isWaterReflect)
                {
                    const uint16_t* srcBuf = reflectBuffer ? reflectBuffer : framebuffer;
                    uint16_t reflCol;
                    if (waterSkyFallback && gradientColors != nullptr && gradientSize > 0) {
                        reflCol = gradientColors[0];
                    } else {
    #if HALF_WIDTH_BUFFERS
                        reflCol = srcBuf[waterMirrorBufBase + (x >> 1)];
    #else
                        reflCol = srcBuf[waterMirrorBufBase + x];
    #endif
                    }
                    color = blendRGB565(material->color, reflCol, waterReflectAlpha);
                    // SSR already composited the final pixel; bypass the
                    // standard pixAlpha re-blend below or we get a double-blend
                    // (50% of 50% = 25% reflection strength on desktop).
                    pixAlpha = 255;
                }
                else if (isAdditive)
                {
                    // Pre-compute the saturating add into `color` so the
                    // standard write path below emits it without a second blend.
                    color    = addBlendRGB565(framebuffer[bufferIndex], color, pixAlpha);
                    pixAlpha = 255;
                }

    #if !DEBUG_OVERDRAW
    #if SCREEN_DOOR_ALPHA
                // Stippling already accepted/rejected this pixel via
                // shouldDrawPixel(); a straight write is correct here.
                framebuffer[bufferIndex] = color;
    #else
                // Traditional alpha blend: lerp toward `color` by pixAlpha.
                // pixAlpha already folds in material*object alpha and any
                // per-pixel depth-fog fade. Skip the blend math when the
                // material is fully opaque — common case.
                if (pixAlpha == 255) {
                    framebuffer[bufferIndex] = color;
                } else {
                    framebuffer[bufferIndex] = blendRGB565(framebuffer[bufferIndex], color, pixAlpha);
                }
    #endif
    #endif
    #if RENDER_TILE_BUFFER
                    // Mark this tile as having been drawn to
                    int tileX = x / TILE_WIDTH;
                    int tileY = y / TILE_HEIGHT;
                    if (tileX >= 0 && tileX < (screenWidth + TILE_WIDTH - 1) / TILE_WIDTH &&
                        tileY >= 0 && tileY < (screenHeight + TILE_HEIGHT - 1) / TILE_HEIGHT)
                    {
                        //tileBuffer[tileY * ((screenWidth + TILE_WIDTH - 1) / TILE_WIDTH) + tileX] = 1;
                    }

    #endif
                }
#if JET_FAST_SIMPLE_SPANS && TEXTURE_MAPPING && !PERSPECTIVE_CORRECT_TEXTURES && !BILINEAR_FILTER
                } // general fallback for textured spans
#endif
                }  // close general per-pixel path block
    #endif // !JET_FAST_SIMPLE_SPANS || TEXTURE_MAPPING
    #undef JET_LIT_STEP
    #undef JET_UV_STEP
            }
        };
        if (spans.valid) rasterRows(std::true_type{});
        else rasterRows(std::false_type{});

        return true;
    }

    // Non-template entry points keep both kernels in IRAM. GCC's template
    // COMDAT instantiations otherwise land in flash despite IRAM_ATTR.
    bool PERF_CRITICAL Rasterizer::drawFlatTriangle(
        const RenderVertex& v1,const RenderVertex& v2,const RenderVertex& v3,
        Material* material,DirectionalLight* directionalLight,AmbientLight* ambientLight,
        bool renderEvenLines,bool ignoreZBuffer,bool noWriteZBuffer,int zBias,
        uint8_t objAlpha,bool brightnessPrecomputed,int32_t avgZHint) {
        return drawTriangleImpl<false>(v1,v2,v3,material,directionalLight,ambientLight,
            renderEvenLines,ignoreZBuffer,noWriteZBuffer,zBias,objAlpha,brightnessPrecomputed,avgZHint);
    }
    bool PERF_CRITICAL Rasterizer::drawTexturedTriangle(
        const RenderVertex& v1,const RenderVertex& v2,const RenderVertex& v3,
        Material* material,DirectionalLight* directionalLight,AmbientLight* ambientLight,
        bool renderEvenLines,bool ignoreZBuffer,bool noWriteZBuffer,int zBias,
        uint8_t objAlpha,bool brightnessPrecomputed,int32_t avgZHint) {
        return drawTriangleImpl<true>(v1,v2,v3,material,directionalLight,ambientLight,
            renderEvenLines,ignoreZBuffer,noWriteZBuffer,zBias,objAlpha,brightnessPrecomputed,avgZHint);
    }
} // namespace Renderer

