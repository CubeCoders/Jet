#ifndef RENDERER_HPP
#define RENDERER_HPP

#include <cstdint>
#include "Object.hpp"
#include "Material.hpp"
#include "Light.hpp"
#include "Camera.hpp"
#include "JetConfig.hpp"
#include "Picking.hpp"

#ifndef JET_RUNTIME_DEPTH
#define JET_RUNTIME_DEPTH 0
#endif

/// @def ZBUFFER_STRIDE(w)
/// @brief Z-buffer row stride in pixels for a framebuffer of width @p w.
///
/// The depth buffer is stored at half horizontal resolution only when
/// HALF_WIDTH_BUFFERS is enabled (one z-cell per two output pixels);
/// otherwise it is per-pixel. Frontends should size their z-buffer
/// allocation as `ZBUFFER_STRIDE(w) * h * sizeof(uint16_t)`.
#if HALF_WIDTH_BUFFERS
#define ZBUFFER_STRIDE(w) ((w) / 2)
#else
#define ZBUFFER_STRIDE(w) (w)
#endif

namespace Renderer
{
/// @brief Vertex attributes consumed by the rasteriser.
///
/// `Object::Vertex` is the authoring type: game/mesh code always has uv,
/// normal, etc. available regardless of build configuration. RenderVertex
/// carries only fields consumed by the configured rasteriser. Scene uses
/// a smaller internal vertex without UVs for transformation and queuing,
/// stores UVs separately for textured faces, and assembles RenderVertex
/// values just before rasterisation. With TEXTURE_MAPPING and LIGHTING
/// both off, RenderVertex is just 12 bytes of position instead of 36.
///
/// `position` is screen-space x/y with camera-space z after projection.
struct RenderVertex {
    Vector3 position = {0, 0, 0};
#if TEXTURE_MAPPING
    Vector2 uv = {0, 0};            ///< Texture coordinates.
#endif
#if LIGHTING
    Vector3 normal = {0, 0, 0};     ///< View-space (or mesh-local, see lambertBrightness) normal.
    /// @brief Precomputed Lambert brightness for the object-local-light
    /// path (see Scene.cpp "objectLocalLight"). Only meaningful when the
    /// triangle was queued with brightnessPrecomputed == true.
    uint16_t lambertBrightness = 0;
#endif
};

/// @brief Low-level triangle rasteriser owning a colour and depth buffer.
///
/// Scene drives this class on every render(). External users normally
/// access it through `Scene::getRenderer()` rather than constructing one
/// directly.
class Rasterizer
{
    private:
        uint16_t *framebuffer;
        int screenWidth;
        int screenHeight;
        uint16_t *zBuffer;
        int lastRandom = 0;

    public:
        /// @brief Construct a rasteriser bound to caller-owned buffers.
        /// @param framebuffer RGB565 colour buffer of size screenWidth*screenHeight.
        /// @param screenWidth Width in pixels.
        /// @param screenHeight Height in pixels.
        /// @param zBuffer Depth buffer of size ZBUFFER_STRIDE(screenWidth)*screenHeight, or nullptr if Z_BUFFERING is disabled.
        /// @param camera Optional Camera; can be set later.
        Rasterizer(uint16_t *framebuffer, int screenWidth, int screenHeight, uint16_t *zBuffer, Camera *camera = nullptr)
            : framebuffer(framebuffer), screenWidth(screenWidth), screenHeight(screenHeight), zBuffer(zBuffer), camera(camera) {}

        Camera *camera;                 ///< Camera supplying view/projection state.
        bool interlacedMode = false;    ///< When true, only every other row is drawn each frame.
        bool checkerboardMode = false;  ///< When true, alternates between two complementary checkerboard pixel patterns each frame. Requires double-buffering in the frontend for the reconstruction pass.
        bool wireframeMode = false;     ///< When true, triangles are drawn as outlines (in their material colour) instead of being filled. Skips lighting, texturing and the z-buffer; intended as a debug/visualisation aid. Scene::clearBuffers forces a black background while this is on.
        #if POSTFX_CELLSHADING
        /// Quantise scalar lighting in the existing raster pass, with no image
        /// buffer. Low bits discarded: 0 = smooth, 6 = steps of 64 (four diffuse
        /// bands), 8 = steps of 256. Ambient and additive gloss stay continuous.
        bool celShadingEnabled = true;
        uint8_t celShadingBits = CELLSHADING_CELL_BITS;
        #endif

        int randomSeed = 255;           ///< Seed for the screen-door / dither random source.

        /// @brief Y-band clip for partial-screen rendering (band / parallel passes).
        ///
        /// Only rows in [yBandMin, yBandMax) are rasterised. Defaults cover the
        /// full screen so existing callers need no changes. Set both fields before
        /// calling drawTriangle(), or use Scene::rasterizeBand() which manages
        /// them automatically via a thread-local copy of the rasteriser.
        int yBandMin = 0;           ///< First row (inclusive) to rasterise. 0 = top of screen.
        int yBandMax = 0x7FFFFFFF;  ///< First row (exclusive) NOT to rasterise. 0x7FFFFFFF = full height.

        /// @name Water reflection support
        /// @brief Set by Scene before rasterising to enable WATER_REFLECT shading mode.
        /// @{
        const uint16_t* gradientColors = nullptr; ///< Per-row sky gradient (screenHeight entries); mirrors backgroundGradientColors.
        int             gradientSize   = 0;        ///< Number of entries in gradientColors (== screenHeight).
        int             frameCounter   = 0;        ///< Scene frame counter (dither parity, etc.).
        float           waterTime      = 0.0f;     ///< Accumulated wall-clock seconds; drives ripple animation.
        /// @}

        /// @brief Optional alternate colour buffer for SSR mirror reads.
        ///
        /// When non-null (and SSR_FIELD_REFLECT is defined), WATER_REFLECT
        /// triangles sample mirror pixels from this buffer instead of the
        /// current `framebuffer`. Intended for the previous interlaced field
        /// buffer so reflections see a fully-rendered prior frame rather than
        /// the partially-drawn current one.  Set via
        /// Game::setReflectBuffer() from the render loop each frame.
        /// nullptr (default) falls back to the current framebuffer.
        uint16_t* reflectBuffer = nullptr;

        /// @name Parallel-band water-reflect ordering
        /// @brief When rasterising in parallel bands across multiple threads,
        /// WATER_REFLECT reads mirror rows that may lie in a band being rendered
        /// simultaneously by another thread.
        /// Setting skipWaterReflect on all band copies lets parallel threads
        /// rasterise everything else first; then a single serial pass with
        /// waterReflectOnly=true draws only the water after all bands have joined,
        /// guaranteeing that geometry is in the framebuffer before SSR reads it.
        /// @{
        bool skipWaterReflect = false;  ///< Skip WATER_REFLECT triangles (parallel-band first pass).
        bool waterReflectOnly = false;  ///< Draw ONLY WATER_REFLECT triangles (serial second pass).
        /// @}

        /// @brief Screen row of the water/sky horizon, computed from camera pitch each frame.
        ///        Used as the reflection axis: mirrorY = 2*waterlineY - y.
        ///        Defaults to screenHeight/2 (level camera); Scene::prepareFrame() updates it.
        int waterlineY = 0;

        /// @name Distance-based texture LOD
        /// @brief Beyond `textureLodFar`, textured triangles drop their texture
        /// and render as flat `material->color`, taking the fast simple-span
        /// fill path for free. Between `textureLodNear` and `textureLodFar`
        /// the sampled texel is cross-faded toward the flat colour, using
        /// screen-door stipple under `SCREEN_DOOR_ALPHA` and a per-pixel
        /// alpha blend otherwise. Disabled by default; the host (Scene)
        /// configures it once with whatever distances suit the scene.
        /// Has no effect when `TEXTURE_MAPPING` is compiled out.
        /// @{
        bool textureLodEnabled = false; ///< Master enable for the texture LOD fade.
        int32_t textureLodNear = 0;     ///< Z below which textures render at full detail.
        int32_t textureLodFar  = 0;     ///< Z at and beyond which textures are dropped entirely.
        /// @}

#if MAX_PICK_QUERIES > 0
        /// Pointers to host-owned pick query/result arrays. Storage is owned
        /// by Scene; the rasterizer just reads queries and writes results.
        const PickQuery* pickQueries = nullptr;
        PickResult*      pickResults = nullptr;
        int              pickQueryCount = 0;     ///< Number of slots in use this frame (0..MAX_PICK_QUERIES).
        Object*          currentPickObject = nullptr;       ///< Set by Scene before each drawTriangle for hit attribution.
        int32_t          currentPickTriangleIndex = -1;     ///< Source-mesh triangle index for hit attribution.
#endif

        /// True when this frame uses depth testing/writes and depth clears.
        bool isDepthTestingEnabled() const {
#if Z_BUFFERING && JET_RUNTIME_DEPTH
            return depthTestingEnabled;
#else
            return Z_BUFFERING != 0;
#endif
        }
#if Z_BUFFERING && JET_RUNTIME_DEPTH
        /// Change only between frames, before Scene::prepareFrame(). This also
        /// selects painter ordering when disabled. Retains the caller-owned
        /// depth allocation; re-enabling requires normal buffer clearing.
        void setDepthTestingEnabled(bool enabled) { depthTestingEnabled = enabled; }
#endif

        /// @brief Test whether a pixel at (x, y) should be drawn given a screen-door alpha.
        /// @param x Pixel X.
        /// @param y Pixel Y.
        /// @param alpha Effective alpha (0..255).
        /// @return True if the pixel passes the dither / parity test.
        bool shouldDrawPixel(int x, int y, uint8_t alpha);

        /// @brief Rasterise a single triangle.
        /// @details Matching view-space vertex normals automatically share one
        /// lighting evaluation for Phong/Gouraud, retaining each mode's specular
        /// model. Cached Gouraud brightness must also match. Exact normal equality
        /// is required; custom shaders and depth-brightness builds keep their old
        /// path. This relies on the directional light and fixed view vector: any
        /// future position-dependent per-pixel lighting must bypass the shortcut.
        /// Constant Phong avoids fixed-point normal interpolation roundoff, so
        /// output need not be bit-identical to the unoptimised path. Define
        /// JET_CONSTANT_NORMAL_LIGHTING=0 when compiling Jet for comparisons.
        /// @param v1 First vertex.
        /// @param v2 Second vertex.
        /// @param v3 Third vertex.
        /// @param material Material applied to the triangle.
        /// @param directionalLight Active directional light (may be nullptr).
        /// @param ambientLight Active ambient light (may be nullptr).
        /// @param renderEvenLines Used in interlaced mode to select which row parity to draw.
        /// @param ignoreZBuffer Skip the depth test when true.
        /// @param noWriteZBuffer Skip the depth write when true.
        /// @param zBias Per-triangle depth bias in z-buffer units.
        /// @param objAlpha Per-object alpha multiplier (255 = no fade).
        /// @param brightnessPrecomputed v1/v2/v3.lambertBrightness already holds per-vertex brightness (object-local-light path).
        /// @param avgZHint Caller-supplied triangle average camera-space Z, or
        ///        INT32_MIN (default) to compute it here. Scene::rasterizeBand
        ///        passes the avgZ it already computed (and near/far-culled
        ///        against) at queue time, so the FAST_Z setup can skip the
        ///        recompute and the redundant near/far test. Ignored when
        ///        LAZY_Z is enabled (LAZY_Z needs the max, not the average).
        /// @return True if the triangle produced any rasterizer work.
        bool drawTriangle(const RenderVertex &v1, const RenderVertex &v2, const RenderVertex &v3, Material *material, DirectionalLight *directionalLight, AmbientLight *ambientLight, bool renderEvenLines, bool ignoreZBuffer, bool noWriteZBuffer, int zBias, uint8_t objAlpha = 255, bool brightnessPrecomputed = false, int32_t avgZHint = INT32_MIN);

        /// @brief Map an 8-bit grayscale value to RGB565.
        /// @param grayscale 8-bit luminance.
        /// @return RGB565 colour.
        uint16_t grayscaleToRGB565(uint8_t grayscale);

        /// @brief Replace just the colour buffer pointer.
        /// @param newBuffer New caller-owned RGB565 buffer.
        void setFramebuffer(uint16_t *newBuffer) { framebuffer = newBuffer; }

        /// @brief Hot-swap framebuffer, z-buffer and dimensions (e.g. on window resize).
        /// @param newFramebuffer New caller-owned colour buffer.
        /// @param newZBuffer New caller-owned depth buffer.
        /// @param newWidth New width in pixels.
        /// @param newHeight New height in pixels.
        void resize(uint16_t* newFramebuffer, uint16_t* newZBuffer,
                    int newWidth, int newHeight) {
            framebuffer  = newFramebuffer;
            zBuffer      = newZBuffer;
            screenWidth  = newWidth;
            screenHeight = newHeight;
        }
    private:
#if Z_BUFFERING && JET_RUNTIME_DEPTH
        bool depthTestingEnabled = true;
        bool drawPainterTriangle(const RenderVertex&, const RenderVertex&, const RenderVertex&,
            Material*, DirectionalLight*, AmbientLight*, bool, bool, bool, int, uint8_t, bool, int32_t);
#endif
        bool drawFlatTriangle(const RenderVertex&, const RenderVertex&, const RenderVertex&,
            Material*, DirectionalLight*, AmbientLight*, bool, bool, bool, int, uint8_t, bool, int32_t);
        bool drawTexturedTriangle(const RenderVertex&, const RenderVertex&, const RenderVertex&,
            Material*, DirectionalLight*, AmbientLight*, bool, bool, bool, int, uint8_t, bool, int32_t);
        template<bool SampleTextures, bool UseDepth>
        bool drawTriangleImpl(const RenderVertex&, const RenderVertex&, const RenderVertex&,
            Material*, DirectionalLight*, AmbientLight*, bool, bool, bool, int, uint8_t, bool, int32_t);
    };
} // namespace Renderer

#endif // RENDERER_HPP
