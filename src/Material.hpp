#ifndef MATERIAL_HPP
#define MATERIAL_HPP

#include <cstdint>
#include "Texture.hpp"
#include "Shader.hpp"

namespace Renderer {

/// @brief Per-triangle shading model selection.
enum class ShadingMode {
    FLAT,          ///< Single colour per triangle.
    GOURAUD,       ///< Per-vertex lit, interpolated across the face.
    PHONG,         ///< Per-pixel lit (more expensive).
    WIREFRAME,     ///< Edges only, no fill.
    UNLIT,         ///< No lighting at all — raw material colour, fully bright.
    WATER_REFLECT, ///< Screen-space gradient reflection with animated ripple.
                   ///<   Samples backgroundGradientColors[screenH-1-y + ripple(x,t)]
                   ///<   then blends toward material->color by material->alpha/255.
                   ///<   material->specular controls ripple amplitude (0=flat, 32=gentle, 128=stormy).
    ADDITIVE,      ///< Saturating-add blend: each source channel (scaled by material->alpha/255)
                   ///<   is added to the destination channel and clamped at the channel maximum.
                   ///<   Use for neon signs, lamp coronas, explosion halos, and faked dynamic lights.
                   ///<   Lighting is always bypassed (fully emissive); material->color is the glow tint.
};

/// @brief Surface description: base colour, optional texture/shader and lighting parameters.
class Material {
public:
    uint16_t color;             ///< Flat-shaded base colour (RGB565).
    Texture* diffuseMap;        ///< Optional diffuse texture; nullptr to use `color`.
    Shader* shader;             ///< Optional custom shader; nullptr to use the built-in path.
    bool emissive;              ///< When true, lighting is bypassed and the surface uses its raw colour.
    uint8_t alpha;              ///< Per-material alpha (0=invisible, 255=opaque).
    uint8_t diffuse;            ///< Diffuse reflectance coefficient (0..255).
    uint8_t specular;           ///< Specular reflectance coefficient (0..255).
    uint8_t specularExponent = 0; ///< PHONG only: 0 keeps the legacy broad highlight;
                                  ///< 1..255 enables additive Blinn-Phong gloss (try 32).
                                  ///< Uses a fixed view direction and a per-triangle half-vector.
    uint8_t waterYBias = 0;     ///< WATER_REFLECT only: pixels to subtract from the mirror-row index,
                                ///<   biasing the reflection axis upward to align with the true
                                ///<   waterline. Stored in device-native pixels (no resolution scaling).
                                ///<   Set via WaterSurface::setReflectionMode(alpha, rippleAmp, yBias).
    ShadingMode shadingMode = ShadingMode::FLAT; ///< Shading model.
    char* name;                 ///< Optional name; used for `usemtl` matching in OBJ loading.

    /// @brief Construct a material.
    /// @param color Base colour in RGB565.
    /// @param diffuseMap Optional diffuse texture.
    /// @param shader Optional custom shader.
    /// @param specular Specular coefficient (0..255). Also widens the
    ///        per-pixel brightness ceiling (the renderer caps brightness
    ///        at 255 + specular), so non-zero values let lit surfaces
    ///        blow past 100% base colour toward white. Default 0 keeps
    ///        GOURAUD/PHONG on a physically-plausible 1.0× response;
    ///        the FLAT path supplies its own headroom locally.
    Material(uint16_t color = 0xFFFF, Texture* diffuseMap = nullptr, Shader* shader = nullptr, bool emissive = false, uint8_t alpha = 255, uint8_t diffuse = 255, uint8_t specular = 0);

    /// @brief Convenience constructor for an untextured material with explicit alpha.
    /// @param color Base colour in RGB565.
    /// @param alpha Per-material alpha (0..255).
    Material(uint16_t color, uint8_t alpha);

    /// @brief Sample the material colour at a UV coordinate.
    /// @param u Fixed-point U.
    /// @param v Fixed-point V.
    /// @return Sampled RGB565 colour, or `color` if no diffuse map is bound.
    uint16_t getColor(int u, int v);

    /// @brief Sample the material colour at a UV coordinate.
    /// @param uv UV pair in fixed point.
    /// @return Sampled RGB565 colour, or `color` if no diffuse map is bound.
    uint16_t getColor(Vector2 uv);
};

} // namespace Renderer

#endif // MATERIAL_HPP
