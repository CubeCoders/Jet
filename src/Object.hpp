#ifndef OBJECT_HPP
#define OBJECT_HPP

#include <vector>
#include <cstdint>
#include <memory>
#include "Texture.hpp"
#include "Material.hpp"
#include "Shader.hpp"

namespace Renderer {

class DirectionalLight;
class AmbientLight;

/// @brief Triangle culling mode for an Object.
enum class CullingMode {
    CULL_BACKFACES,     ///< Cull triangles facing away from the camera.
    CULL_FRONTFACES,    ///< Cull triangles facing the camera.
    NO_CULLING,         ///< Draw both winding orders.
};

/// @brief Per-pixel blend mode applied when writing this object to the framebuffer.
enum class BlendMode {
    BLEND_REPLACE,      ///< Overwrite destination.
    BLEND_ADD,          ///< Saturating add.
    BLEND_SUBTRACT,     ///< Saturating subtract.
    BLEND_SCALE,        ///< Multiply destination by source.
    BLEND_AVERAGE,      ///< Average source and destination.
    BLEND_XOR,          ///< Bitwise XOR.
};

/// @brief Renderable mesh: vertices, triangles, transform, render flags and per-object fade bands.
class Object {
public:
    /// @brief Single triangle indexing into the parent Object's `vertices`.
    struct Triangle {
        uint16_t v1, v2, v3;        ///< Vertex indices.
        Material* material;         ///< Material applied to this face.
        /// @brief Pre-lit RGB565 colour baked by bakeFlatLighting().
        /// When `colorBaked` is true this overrides material->color and
        /// bypasses all per-frame lighting computation for this face.
        uint16_t bakedColor  = 0;
        bool     colorBaked  = false;
    };

    /// @brief Single mesh vertex.
    struct Vertex {
        Vector3 position = {0,0,0}; ///< World-local position.
        Vector2 uv = {0,0};         ///< Texture coordinates.
        Vector3 normal = {0,0,0};   ///< Normal vector.
        uint16_t color = 0x0000;    ///< Per-vertex colour (RGB565).
        /// @brief Precomputed Lambert+ambient-clipped brightness in
        /// [0..255+specular]. Populated by Scene.cpp ONLY for objects
        /// whose materials are all non-specular; the rasterizer's per-
        /// triangle flag `brightnessPrecomputed` selects between this
        /// cached value and re-computing from `normal` + `lightDir`.
        /// Skipping the per-vertex view-space normal transform is the
        /// whole point of the precompute path.
        uint16_t lambertBrightness = 0;
    };

    std::vector<Vertex> vertices;       ///< Mesh vertices.
    std::vector<int> indices;           ///< Index buffer (unused by the default rasteriser).
    std::vector<Triangle> triangles;    ///< Mesh triangles.

    /// Build an optional packed stream for a mesh whose positions are static.
    /// UVs, normals and object transforms remain live. Direct edits to public
    /// vertices require invalidatePositions() or another cachePositions() call.
    /// addVertex(), calculateBoundingBox() and baking helpers invalidate it.
    /// Uses PSRAM on ESP builds with PSRAM; returns false if the position
    /// buffer cannot be allocated, leaving the ordinary vertex path active.
    /// Copies of an Object share the immutable cache until either invalidates it.
    bool cachePositions();
    const Vector3* cachedPositions() const {
        return positionCacheSize == vertices.size() ? positionCache.get() : nullptr;
    }
    void invalidatePositions() { positionCache.reset(); positionCacheSize = 0; }

    Vector3 boundingBoxMin = {0,0,0};   ///< Local-space AABB minimum (recomputed by calculateBoundingBox).
    Vector3 boundingBoxMax = {0,0,0};   ///< Local-space AABB maximum.
    Vector3 centreVolume = {0,0,0};     ///< Local-space AABB centre.

    Vector3 rotation = {0,0,0};                                                 ///< Euler rotation in degrees.
    Vector3 position = {0,0,0};                                                 ///< World-space position.
    Vector3 scale = {FIXED_POINT_SCALE, FIXED_POINT_SCALE, FIXED_POINT_SCALE};  ///< Per-axis scale (FIXED_POINT_SCALE = 1.0).

    CullingMode cullingMode = CullingMode::CULL_BACKFACES;  ///< Triangle culling mode.
    BlendMode blendMode = BlendMode::BLEND_REPLACE;         ///< Blend mode.
    bool isBillboard = false;                               ///< When true, the object is auto-rotated to face the camera.
    bool ignoreZBuffer = false;                             ///< When true, depth test is skipped (always passes).
    bool noWriteZBuffer = false;                            ///< When true, depth is not written.

    /// @brief Depth bias applied at raster time in z-buffer units.
    ///
    /// Positive values pull the surface toward the camera for both the Z
    /// test and the Z write, so a biased surface wins depth fights against
    /// coplanar or near-coplanar geometry but still loses to anything
    /// genuinely closer. Use for decals / shadows / ground markings sitting
    /// just above another surface. Small values (1..8) are typical;
    /// 0 disables.
    int8_t zBias = 0;

    bool transformScale = false;    ///< When true, scale is included in the world transform; otherwise scale is baked-in.
    bool enabled = true;            ///< When false, the object is skipped entirely.

    /// @brief Distance at which the object starts fading out (world units, 0 disables).
    ///
    /// When the camera is between `fadeNear` and `fadeFar` from
    /// (position + centreVolume), the object is drawn with a screen-door
    /// alpha that ramps linearly from 255 at fadeNear to 0 at fadeFar.
    /// Beyond fadeFar the object is skipped entirely. Both default to 0
    /// (disabled); the global depth fog still applies independently.
    int32_t fadeNear = 0;
    /// @brief Distance at which the object becomes fully transparent / culled (paired with fadeNear).
    int32_t fadeFar  = 0;

    /// @brief Distance at which the object starts fading in (LOD pop-in, 0 disables).
    ///
    /// Inverse of fadeNear/fadeFar: invisible when closer than `appearNear`,
    /// fades in linearly between `appearNear` and `appearFar`, fully visible
    /// beyond `appearFar`. Pair with a high-detail object using matching
    /// fadeNear/fadeFar to get a screen-door dissolve LOD swap.
    int32_t appearNear = 0;
    /// @brief Distance at which the object is fully visible (paired with appearNear).
    int32_t appearFar  = 0;

    /// @name Distance-based level of detail (LOD)
    ///
    /// Optional alternative meshes used when this object is far from the
    /// camera. The head Object IS LOD 0; entries in `lodMeshes` are LOD 1,
    /// 2, ... in order. The Scene picks a level per frame based on the
    /// head's distance to the camera and a Scene-wide `lodScale`:
    ///   level = max(0, distance / lodScale + sceneLodBias + lodBias)
    /// The Scene then uses `lodMeshes[level - 1]` for the mesh data
    /// (vertices + triangles) while keeping THIS object's transform,
    /// flags, AABB, fade ramps, etc. — i.e. the LOD entries are pure mesh
    /// substitutions.
    ///
    /// LOD entries are typically authored as separate Objects with
    /// `enabled = false` (so they don't render independently) and held
    /// either in the Scene or in user-side storage; the head only borrows
    /// their pointers.
    ///
    /// When the chosen level exceeds the available LODs, behaviour is
    /// controlled by `lodPersist`: if true (the default), the object
    /// keeps drawing using the lowest-detail mesh available — or, if
    /// no LOD chain was provided at all, the head mesh itself. This
    /// makes "stay visible" the default so simply enabling Scene LOD
    /// doesn't quietly delete distant geometry the author never wired
    /// LODs for. Set to false on foliage / minor scenery that should
    /// drop out past the configured range.
    /// @{
    std::vector<Object*> lodMeshes;     ///< Reduced-detail alternatives; index 0 = LOD 1.
    int8_t lodBias = 0;                 ///< Per-object LOD level bias (negative = use higher detail).
    bool lodPersist = true;             ///< When true (default), clamp to the last LOD instead of culling.
    /// @}

    /// @brief Construct an empty object.
    Object();

    /// @brief Append a vertex to the mesh.
    /// @param vertex Vertex to append.
    void addVertex(const Vertex& vertex);

    /// @brief Append a triangle indexing existing vertices.
    /// @param idx1 First vertex index.
    /// @param idx2 Second vertex index.
    /// @param idx3 Third vertex index.
    /// @param material Material applied to the triangle.
    void addTriangle(uint16_t idx1, uint16_t idx2, uint16_t idx3, Material* material);

    /// @brief Append a quad as two triangles.
    /// @param v1 First vertex index.
    /// @param v2 Second vertex index.
    /// @param v3 Third vertex index.
    /// @param v4 Fourth vertex index.
    /// @param material Material applied to both triangles.
    void addFace(uint16_t v1, uint16_t v2, uint16_t v3, uint16_t v4, Material* material);

    /// @brief Compute and assign flat (per-face) normals for every triangle.
    ///
    /// For each triangle, computes the cross-product face normal of its
    /// position vectors, normalises to `FIXED_POINT_SCALE` magnitude, and
    /// writes that normal into all three vertex slots the triangle
    /// references. Procedural geometry (boxes, cliffs, trees) built via
    /// `addTriangle`/`addFace` should call this once after the mesh is
    /// complete so the renderer's directional lighting has real normals to
    /// shade against — without this every triangle's `N·L` is zero and the
    /// surface receives only the ambient term.
    ///
    /// NOTE: When vertices are shared between triangles with different
    /// orientations this is destructive (the last triangle wins). Callers
    /// that need smooth shading should either author per-face vertices
    /// (the convention in the scene helpers) or supply explicit normals
    /// via the OBJ loader instead.
    void computeFlatNormals();

    /// @brief Set the world-space position.
    /// @param x World X.
    /// @param y World Y.
    /// @param z World Z.
    void setPosition(int32_t x, int32_t y, int32_t z);
    /// @brief Set the world-space position.
    /// @param pos World-space position.
    void setPosition(const Vector3& pos);

    /// @brief Set the absolute Euler rotation (degrees).
    /// @param rotX Rotation about X.
    /// @param rotY Rotation about Y.
    /// @param rotZ Rotation about Z.
    void setRotation(int32_t rotX, int32_t rotY, int32_t rotZ);
    /// @brief Set the absolute Euler rotation (degrees).
    /// @param rot Rotation vector.
    void setRotation(const Vector3& rot);

    /// @brief Add a delta rotation (degrees).
    /// @param angleX Delta about X.
    /// @param angleY Delta about Y.
    /// @param angleZ Delta about Z.
    void rotate(int32_t angleX, int32_t angleY, int32_t angleZ);
    /// @brief Add a delta rotation (degrees).
    /// @param rot Delta rotation vector.
    void rotate(const Vector3& rot);

    /// @brief Translate the object in world space.
    /// @param dx Delta X.
    /// @param dy Delta Y.
    /// @param dz Delta Z.
    void translate(int32_t dx, int32_t dy, int32_t dz);

    /// @brief Orient the object so its forward axis faces a target.
    /// @param target World-space point to face.
    void lookAt(const Vector3& target);
    /// @brief Orient the object so its forward axis faces another object's centre.
    /// @param targetObject Object to face.
    void lookAt(const Object* targetObject);

    /// @brief Recompute boundingBoxMin/Max and centreVolume from the current vertex set.
    void calculateBoundingBox();

    /// @brief Bake the current rotation into vertex positions and normals, then reset rotation to zero.
    ///
    /// Intended for static meshes authored with a non-zero rotation purely
    /// for placement convenience. After baking, the renderer hits its
    /// zero-rotation fast path on every subsequent frame (saves
    /// ~12 muls + 6 divs + 9 adds per vertex). One-shot operation. The
    /// bounding box is recomputed automatically.
    void bakeRotation();

    /// @brief Bake a rational scale factor into vertex positions; one-shot operation.
    ///
    /// Useful for permanently enlarging or shrinking geometry to give the
    /// renderer's integer rotation chain more sub-pixel headroom at high
    /// output resolutions. Scale is supplied as numerator/denominator to
    /// stay deterministic and avoid floating point. e.g. (8, 1) makes the
    /// object 8x larger; (1, 2) halves it. Performed with int64
    /// intermediates so positions up to ~2.1e9 / numerator survive without
    /// overflow. Normals are direction vectors and are NOT scaled. The
    /// bounding box is recomputed automatically.
    /// @param numerator Scale numerator.
    /// @param denominator Scale denominator (must be non-zero).
    void bakeScale(int32_t numerator, int32_t denominator);

    /// @brief Pre-compute FLAT shading into each triangle's `bakedColor` field
    ///        so that subsequent render() calls cost nothing for lighting.
    ///
    /// Only triangles whose material uses `ShadingMode::FLAT` (or UNLIT/GOURAUD
    /// when `forceFlat` is true) are processed; others are skipped.
    /// The original material pointers are preserved unchanged — baking does
    /// not allocate any new Material objects.
    ///
    /// After calling this, set `LIGHTING=0` in JetConfig and the scene will
    /// render at full no-lighting speed while still looking correctly lit.
    /// If the light or ambient colour changes, call this again to rebuild.
    ///
    /// @param directionalLight  Directional light to bake from (may be nullptr).
    /// @param ambientLight      Ambient light to bake from (may be nullptr).
    void bakeFlatLighting(const DirectionalLight* directionalLight,
                          const AmbientLight*     ambientLight);
private:
    std::shared_ptr<const Vector3> positionCache;
    size_t positionCacheSize = 0;

};

} // namespace Renderer

#endif // OBJECT_HPP
