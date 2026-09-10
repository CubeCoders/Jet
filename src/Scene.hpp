#ifndef SCENE_HPP
#define SCENE_HPP

#include <vector>
#include <algorithm>
#include "Object.hpp"
#include "Camera.hpp"
#include "Light.hpp"
#include "Renderer.hpp"
#include "JetConfig.hpp"
#include "PostFX.hpp"
#include "Sprite2D.hpp"

namespace Renderer {

/// @brief Top-level container that owns the scene graph and drives rendering.
///
/// Holds the active camera, lights, object list and per-frame state, and
/// exposes a single `render()` entry point that runs the full
/// transform/cull/raster/post-FX pipeline.
class Scene {
public:
    /// @brief Construct a scene bound to caller-owned framebuffers.
    /// @param framebuffer RGB565 colour buffer of size screenWidth*screenHeight.
    /// @param zBuffer Depth buffer of size ZBUFFER_STRIDE(screenWidth)*screenHeight; pass nullptr when Z_BUFFERING is disabled.
    /// @param screenWidth Output width in pixels.
    /// @param screenHeight Output height in pixels.
    Scene(uint16_t* framebuffer, uint16_t* zBuffer, int screenWidth, int screenHeight);
    ~Scene();

    int   frameCounter = 0;       ///< Incremented once per render(); useful for animations and dither parity.
    float waterTime    = 0.0f;    ///< Accumulated wall-clock seconds; set each frame by the caller before render/prepareFrame.

    /// @name Per-frame counters populated by render()
    /// @{
    /// `lastFrameDrawnObjects` is the number of enabled objects that
    /// survived the AABB frustum cull. `lastFrameDrawnTriangles` is the
    /// number of triangles submitted to the rasteriser (renderQueue size
    /// after all culling). `lastFrameRasterizedTriangles` is the subset of
    /// those that produced rasterizer work (drawTriangle returned true).
    int lastFrameDrawnObjects        = 0;
    int lastFrameDrawnTriangles      = 0;
    int lastFrameRasterizedTriangles = 0;
    /// @}

    /// @brief Add an object to the scene.
    /// @param obj Object to add. Pointer is borrowed; caller retains ownership.
    void addObject(Object* obj);

    /// @brief Add a point light to the scene.
    /// @param light Light to add. Pointer is borrowed; caller retains ownership.
    void addPointLight(PointLight* light);

    /// @brief Register a 2D screen-space overlay to be drawn after every render().
    /// @param sprite Sprite to add. Pointer is borrowed; caller retains ownership.
    void addSprite(Sprite2D* sprite);

    /// @brief Set the active camera.
    /// @param cam Camera pointer (borrowed).
    void setCamera(Camera* cam);
    /// @brief Get the active camera.
    /// @return Pointer to the current camera, or nullptr if none is set.
    Camera* getCamera() { return camera; }

    /// @brief Set the active directional light.
    /// @param light Directional light (borrowed). May be nullptr.
    void setDirectionalLight(DirectionalLight* light);
    /// @brief Get the active directional light.
    DirectionalLight* getDirectionalLight() { return directionalLight; }

    /// @brief Set the active ambient light.
    /// @param light Ambient light (borrowed). May be nullptr.
    void setAmbientLight(AmbientLight* light);
    /// @brief Get the active ambient light.
    AmbientLight* getAmbientLight() { return ambientLight; }

    using RasterExecutor = void (*)(Scene&);
    /// @brief Run the full pipeline for one frame: cull, transform, rasterise, post-FX.
    /// An optional frontend executor replaces the full-screen raster pass.
    /// It must join its workers and publish statistics before returning.
    void render(RasterExecutor executor = nullptr);

    /// @brief Phase 1 of split rendering: clear [yBandMin, yBandMax) on the rasteriser,
    ///        transform and depth-sort all objects. Does NOT rasterise triangles.
    ///
    ///        Call this once per frame before any rasterizeBand() calls.
    ///        The rasteriser's yBandMin/yBandMax gate which rows clearBuffers() clears
    ///        so the framebuffer pointer can be a virtual base (adjusted for band offset).
    void prepareFrame();

    /// @brief Phase 2 of split rendering: rasterise the sorted render queue for
    ///        rows [yMin, yMax) only. Uses a thread-local copy of the rasteriser so
    ///        concurrent calls with non-overlapping y ranges are safe when Z_BUFFERING==0.
    ///
    ///        May be called from multiple threads simultaneously with disjoint bands.
    // Parallel callers supply separate zeroed flags (lastFrameDrawnTriangles
    // bytes each), then OR them after joining to count unique triangles.
    // With flags supplied this does not write shared frame statistics.
    void rasterizeBand(int yMin, int yMax, uint8_t* triangleFlags = nullptr);

    /// @brief Clear only the rows [yMin, yMax) of the current framebuffer without
    ///        re-running the transform or sort pipeline. Use this for bands 1+ when the
    ///        render queue from the preceding prepareFrame() call is still valid.
    ///
    ///        Sets the rasteriser's yBandMin/yBandMax before clearing so the
    ///        band-aware clear writes only into the correct region.
    void clearBand(int yMin, int yMax);

    /// @brief Advance the internal frame counter by one. Normally called automatically
    ///        by render(); use this when driving the pipeline via prepareFrame()/rasterizeBand().
    void advanceFrameCounter() { frameCounter++; }

    /// @brief Get total scene statistics (independent of camera position).
    /// @param objectCount Out: number of enabled objects.
    /// @param triangleCount Out: total triangle count across enabled objects.
    /// @param vertexCount Out: total vertex count across enabled objects.
    void getStatistics(int& objectCount, int& triangleCount, int& vertexCount);

    /// @brief Set the colour used to clear the framebuffer.
    /// @param color RGB565 clear colour.
    void setBackcolor(uint16_t color) { backcolor = color; }

    /// @brief Replace the colour buffer pointer (without changing dimensions).
    /// @param framebuffer New caller-owned RGB565 buffer.
    void setFramebuffer(uint16_t *framebuffer);

    /// @brief Enable or disable per-frame framebuffer clearing.
    /// @param clear True to clear before rendering, false to preserve previous content.
    void setClearBuffer(bool clear) { clearRenderBuffer = clear; }

    /// @brief Hot-swap framebuffer, z-buffer and dimensions (e.g. on window resize).
    ///
    /// The caller owns both buffers and is responsible for freeing the old
    /// ones AFTER this call returns. PostFX is recreated internally to
    /// pick up the new dimensions.
    /// @param newFramebuffer New caller-owned colour buffer.
    /// @param newZBuffer New caller-owned depth buffer.
    /// @param newWidth New width in pixels.
    /// @param newHeight New height in pixels.
    void resize(uint16_t* newFramebuffer, uint16_t* newZBuffer,
                int newWidth, int newHeight);

    /// @brief Get the underlying rasteriser.
    Rasterizer* getRenderer() { return renderer; }
    /// @brief Get the mutable list of scene objects.
    std::vector<Object*>& getObjects() { return objects; }
    /// @brief Get the mutable list of point lights.
    std::vector<PointLight*>& getPointLights() { return pointLights; }
    /// @brief Get the mutable list of materials owned by the scene.
    std::vector<Material*>& getMaterials() { return materials; }
    /// @brief Get the list of registered 2D sprites.
    /// On HALF_WIDTH_BUFFERS builds the display layer composites sprites
    /// during scanout at full resolution; expose the list so it can do so.
    std::vector<Sprite2D*>& getSprites() { return sprites; }

#if MAX_PICK_QUERIES > 0
    /// @brief Submit screen-space pick points to be tested during the next render().
    ///
    /// Excess queries beyond MAX_PICK_QUERIES are silently dropped. Pass
    /// count == 0 (or just don't call this) to disable picking. The renderer
    /// reads queries during render() and writes the matching slots in the
    /// pick result array. Both arrays are owned by Scene; the host should
    /// copy queries in by value and read results back after render().
    /// @param queries Caller-owned array of pick queries.
    /// @param count Number of valid entries in @p queries.
    void setPickQueries(const PickQuery* queries, int count);

    /// @brief Get the pick results from the most recent render() call.
    /// @return Pointer to an internal array of MAX_PICK_QUERIES results.
    const PickResult* getPickResults() const { return pickResults; }

    /// @brief Get the number of active pick queries set for the next render().
    int getPickQueryCount() const { return pickQueryCount; }
#endif


    uint16_t* backgroundGradientColors = nullptr;   ///< Optional per-row background gradient (screenHeight entries) used during clear.

    /// @name Distance-based level of detail (LOD)
    /// @brief Global LOD selection driven by camera-to-object distance.
    ///
    /// `lodScale` is the world-units-per-LOD-step. Setting it to 0 (the
    /// default) disables global LOD and every Object renders its own
    /// mesh as before. With `lodScale = 4096`, an Object with two LOD
    /// meshes attached renders LOD 0 below 4096 units, LOD 1 from 4096
    /// to 8191, LOD 2 from 8192 onward (or fades out / persists past
    /// the last LOD depending on the Object's `lodPersist` flag).
    ///
    /// `lodBias` is added to the computed level globally — a bias of -1
    /// pushes everything one LOD step higher in detail, +1 cheaper. This
    /// composes with the per-Object `lodBias` and is intended for runtime
    /// quality knobs (e.g. perf-driven dynamic adjustment).
    /// @{
    int32_t lodScale = 0;   ///< World units per LOD step; 0 disables global LOD.
    int8_t  lodBias  = 0;   ///< Scene-wide LOD level offset (added to each object's choice).
    /// @}

private:
    // UVs are immutable mesh attributes, not transformed attributes. Keep
    // them out of the scratch vertices and the common triangle queue.
    struct PipelineVertex {
        Vector3 position;
#if LIGHTING
        Vector3 normal;
        uint16_t lambertBrightness = 0;
#endif
        template<class V> void assign(const V& v) {
            position = v.position;
#if LIGHTING
            normal = v.normal;
            lambertBrightness = v.lambertBrightness;
#endif
        }
        RenderVertex expand() const {
            RenderVertex v;
            v.position = position;
#if LIGHTING
            v.normal = normal;
            v.lambertBrightness = lambertBrightness;
#endif
            return v;
        }
    };
#if TEXTURE_MAPPING
    struct TriangleUV { Vector2 a, b, c; };
    std::vector<TriangleUV> textureQueue;
#endif
    struct RenderTri {
        PipelineVertex v1, v2, v3;
#if TEXTURE_MAPPING
        uint32_t uvIndex;
#endif
        Material* material;
        int32_t avgZ;
        // Pack the three booleans together so the UV index replaces
        // padding rather than growing the total payload of textured faces.
        bool ignoreZBuffer : 1;
        bool noWriteZBuffer : 1;
        // When true, v1/v2/v3.lambertBrightness has been precomputed in
        // object-local space by renderObject (see "objectLocalLight" path
        // in Scene.cpp). drawTriangle skips its own jetShadeBrightness
        // calls in that case and reads the cached values directly. Only
        // ever set for objects whose materials are all non-specular.
        bool brightnessPrecomputed : 1;
        int8_t zBias;
        // Per-object alpha multiplier (255 = no per-object fade); folded
        // into the per-pixel screen-door alpha at raster time.
        uint8_t objAlpha;
#if MAX_PICK_QUERIES > 0
        // Source object + ORIGINAL triangle index (in obj->triangles) for
        // pick attribution. Carried through the painter sort.
        Object* sourceObject;
        int32_t sourceTriangleIndex;
#endif
    };
    std::vector<RenderTri> renderQueue;
    // One byte per triangle: background, 64 far-to-near depth buckets,
    // then overlay. Sorting never needs to fetch the full triangle payload.
    static constexpr int SortBucketCount = 66;
    std::vector<uint8_t> renderBuckets;
    // Painter's-sort output as indices into renderQueue, rebuilt by
    // prepareFrame() each frame. Sorting (scattering) 4-byte indices
    // instead of whole RenderTri structs avoids a full second copy of the
    // queue per frame; rasterizeBand() walks this to draw in depth order.
    std::vector<int32_t> renderOrder;

    Camera* camera;
    DirectionalLight* directionalLight;
    AmbientLight* ambientLight;
    Rasterizer* renderer = nullptr;
    PostFX* postFX = nullptr;

    uint16_t* framebuffer;
    uint16_t* zBuffer;
    int screenWidth;
    int screenHeight;
    bool* scanlinesUpdated;

    std::vector<Object*> objects;
    std::vector<PointLight*> pointLights;
    std::vector<Material*> materials;
    std::vector<Sprite2D*> sprites;

    uint16_t backcolor = 0;
    bool clearRenderBuffer = true;
    bool renderEvenLines = false;

    float cameraMatrix[9] = {}; // Same composed camera transform for the entire frame.

    // Frustum side-plane normal lengths for the quick sphere cull in
    // cullObject(): |(fovFactor, ±screenW/2)| and |(fovFactor, ±screenH/2)|.
    // Recomputed once per frame in prepareFrame() because fovFactor
    // changes at runtime (boost FOV kick).
    float cullPlaneLh = 1.0f;
    float cullPlaneLv = 1.0f;

    bool cullObject(Object* obj,
                    int32_t camCosX, int32_t camSinX,
                    int32_t camCosY, int32_t camSinY,
                    int32_t camCosZ, int32_t camSinZ) const;

    void renderObject(Object* obj,
                      int32_t camCosX, int32_t camSinX,
                      int32_t camCosY, int32_t camSinY,
                      int32_t camCosZ, int32_t camSinZ,
                      uint8_t objAlpha,
                      Object* meshSource = nullptr);
    void reconstructCheckerboard();
    void clearBuffers();

public:
    /// @brief Composite all enabled sprites onto the current framebuffer.
    ///        Normally called automatically by render(); call explicitly when
    ///        driving the pipeline via prepareFrame()/rasterizeBand().
    void drawSprites();

private:

#if MAX_PICK_QUERIES > 0
    PickQuery  pickQueries[MAX_PICK_QUERIES];
    PickResult pickResults[MAX_PICK_QUERIES];
    int        pickQueryCount = 0;
#endif
};

} // namespace Renderer

#endif // SCENE_HPP
