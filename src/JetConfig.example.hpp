// JetConfig.example.hpp — Jet renderer configuration EXAMPLE / TEMPLATE.
//
// Each frontend (firmware / desktop / etc.) keeps its OWN copy named
// `JetConfig.hpp`, placed on the include path so every Jet header resolves
// `#include "JetConfig.hpp"` to the per-frontend version. This lets the
// renderer be tuned per platform without forking the library or relying on
// global build flags.
//
// To start a new frontend, copy this file to <frontend>/JetConfig.hpp and add
// that directory to the Jet target's include path (see the existing
// CMakeLists.txt files for the pattern).

// ---------------------------------------------------------------------------
// World-space scale
// ---------------------------------------------------------------------------

// JET32_WORLD_SCALE: Multiplier applied to all authored world-space
// coordinates before they enter the renderer's integer transform chain.
// Increasing this gives the fixed-point rotation math sub-pixel precision,
// eliminating PS1-style edge wobble at high output resolutions.
//   * Low-res targets (e.g. 480×320)  : 4  (scaling costs more than it gains)
//   * High-res targets (e.g. 1080p)   : 8  (sub-pixel precision is worthwhile)
#define JET32_WORLD_SCALE 8

// ---------------------------------------------------------------------------
// Shared immutable geometry
// ---------------------------------------------------------------------------

// Compile Object::SharedMesh / addInstance() and the instance-aware renderer.
// Reduces duplicated mesh storage; every placement still transforms and draws.
// Leave off to retain the ordinary Object layout and renderer cost.
#define JET_MESH_INSTANCING 0

// ---------------------------------------------------------------------------
// Core rasterizer options
// ---------------------------------------------------------------------------

// RENDER_TILE_BUFFER: When enabled, the renderer populates a per-tile dirty
// bitmap each frame so the display driver can skip uploading unchanged tiles.
// Useful for tile-based SPI displays. TILE_WIDTH / TILE_HEIGHT must divide
// SCREEN_WIDTH / SCREEN_HEIGHT exactly.
#define RENDER_TILE_BUFFER 0
#define TILE_WIDTH  32
#define TILE_HEIGHT 32

// FAST_Z: Compute a single per-triangle Z value (average of the three
// vertices) instead of interpolating Z per pixel. Much faster; accurate
// enough when triangles are small relative to the screen.
#define FAST_Z 1

// JET_PERSPECTIVE_DEPTH: Interpolate reciprocal Z for geometrically correct
// per-pixel depth on large/sloping triangles. Requires Z_BUFFERING=1 and
// FAST_Z=0. Opt-in: adds a division per tested pixel, intended for desktop
// quality rendering. Disabled configurations retain their original cost.
#define JET_PERSPECTIVE_DEPTH 0


// LAZY_Z: Use the maximum (farthest) vertex Z instead of the average.
// Only meaningful when FAST_Z is enabled. Avoids over-darkening large
// near-camera triangles with Z_BRIGHTNESS, at the cost of slightly less
// accurate depth-sort order.
#define LAZY_Z 0

// SCREEN_DOOR_ALPHA: Render semi-transparent surfaces using a 4×4 Bayer
// ordered-dither stipple pattern derived from each triangle's alpha value.
// Minimal fill-rate cost; looks good at ≥30 fps.
#define SCREEN_DOOR_ALPHA 1

// SKIP_ZERO_AREA_TRIANGLES: Cull triangles that project to zero or negative
// screen area before the rasterizer sees them. Catches back-faces and
// degenerate slivers cheaply.
#define SKIP_ZERO_AREA_TRIANGLES 1

// NOISE_ALPHA: Dither transparency using a per-pixel noise pattern instead of
// the ordered Bayer matrix. Looks better at high frame rates but creates
// flicker at low ones. Mutually exclusive with SCREEN_DOOR_ALPHA.
#define NOISE_ALPHA 0

// Z_BUFFERING: Enable a per-pixel depth buffer. Eliminates painter's-algorithm
// artefacts but requires SCREEN_WIDTH × SCREEN_HEIGHT × 2 bytes of extra RAM.
// Not recommended on memory-constrained targets.
#define Z_BUFFERING 0

// SORT_TRIANGLES: Sort all triangles in the render queue by depth (farthest
// first) before rasterizing. Recommended when Z_BUFFERING is off.
#define SORT_TRIANGLES 1

// More depth buckets preserve painter ordering when increasing camera.farPlane.
// Default 64. Each extra bucket costs 8 bytes of temporary sorting stack;
// triangle keys remain one byte. Range 1..254, or 1..127 when opaque
// front-to-back depth sorting is enabled. This remains approximate sorting.
#define JET_SORT_DEPTH_BUCKETS 64

// SORT_SCENE_OBJECTS: Sort scene objects by camera distance before rendering.
// Recommended when Z_BUFFERING is on to exploit early-Z rejection.
#define SORT_SCENE_OBJECTS 0

// SORT_SCENE_REVERSE: Reverse the object sort order (farthest first, painter's
// algorithm). Only used when SORT_SCENE_OBJECTS is enabled.
#define SORT_SCENE_REVERSE 0

// DEPTH_ALPHA_BLEND: Fade distant geometry to the background colour, creating
// a cheap distance fog effect. Requires SCREEN_DOOR_ALPHA.
// Tune the near/far distances with depthFogNear / depthFogFar below.
#define DEPTH_ALPHA_BLEND 1

// TEXTURE_MAPPING: Enable affine UV texture mapping. Fast but can cause
// perspective-incorrect warping on large triangles viewed at an angle.
#define TEXTURE_MAPPING 0

// PERSPECTIVE_CORRECT_TEXTURES: Correct the affine warp with per-pixel W
// division. More accurate; noticeably slower. When compiled in, materials
// default to perspective UVs but can select affine via perspectiveCorrect=false.
// The runtime flag affects UVs, not the build's Phong normal interpolation.
#define PERSPECTIVE_CORRECT_TEXTURES 0

// Opt-in desktop-quality UV interpolation using full-width edge weights and
// double reciprocals. Avoids large projected triangle overflow and distant
// fixed-point reciprocal quantization. Costs more on embedded CPUs.
// Only affects perspective UVs; affine mapping and lighting are unchanged.
#define JET_HIGH_PRECISION_UVS 0

// BILINEAR_FILTER: Bi-linear interpolation between texels during texture
// sampling. Reduces blockiness at the cost of extra multiplies per pixel.
// When compiled in, direct RGB565 textures default to bilinear; set
// texture.bilinear=false for nearest. Indexed textures retain nearest sampling.
// Requires TEXTURE_MAPPING.
#define BILINEAR_FILTER 0

// LIGHTING: Compile in ambient + directional lighting support. When enabled,
// each vertex is lit by up to one directional light and one ambient term.
// Roughly doubles per-vertex cost.
#define LIGHTING 0

// Z_BRIGHTNESS: Darken distant pixels proportionally to their depth, giving
// a cheap depth-cue effect. If FAST_Z is on, use LAZY_Z too to avoid
// large near triangles being incorrectly darkened by their average Z.
#define Z_BRIGHTNESS 0

// FLOAT_CAMERA_ANGLES: Store camera pitch/yaw/roll as floats and convert to
// the fixed-point sin/cos table index at transform time. Avoids angle
// quantisation artefacts when the camera moves smoothly.
#define FLOAT_CAMERA_ANGLES 1

// FLOAT_SIN_CACHE_SCALE: Fixed-point scale used for the sin/cos LUT index.
// 10 gives 0.1° resolution (sufficient in practice). Larger values trade
// LUT memory for angle precision.
#define FLOAT_SIN_CACHE_SCALE 10

// FLOAT_TAN_CACHE_SCALE: Scale for the tan LUT used in the perspective
// projection. 1 uses an integer table (fast, slightly coarser). Higher
// values improve FOV accuracy at steep angles.
#define FLOAT_TAN_CACHE_SCALE 1

// ---------------------------------------------------------------------------
// Buffer layout
// ---------------------------------------------------------------------------

// HALF_WIDTH_BUFFERS: Store each framebuffer pixel as one uint16_t covering
// two adjacent output columns. The rasterizer writes at half horizontal
// resolution; the display driver doubles each pixel on scanout.
// Halves framebuffer RAM and fill bandwidth — critical on ESP32 SRAM.
// Set to 0 on platforms with sufficient memory (e.g. high-res / large-RAM targets).
#define HALF_WIDTH_BUFFERS 1

// FIELD_BUFFERS: Split the framebuffer into two half-height packed buffers —
// one for even output rows, one for odd. The rasterizer alternates which
// field it writes each frame (interlaced mode); the display driver reads
// the OTHER field simultaneously via DMA. Eliminates CPU/DMA bank contention
// and allows render + display to run truly in parallel.
//
// Requirements:
//   - interlacedMode must be set to true on the Rasterizer each frame
//     (Scene::getRenderer()->interlacedMode = true).
//   - Host code allocates two half-height buffers and alternates the active
//     one via Scene::setFramebuffer() at the start of each frame.
//   - Display driver pushes the PREVIOUS field's buffer while the renderer
//     fills the current one.
//
// Combine with SSR_FIELD_REFLECT for artefact-free water reflections.
#define FIELD_BUFFERS 1

// SSR_FIELD_REFLECT: When FIELD_BUFFERS is enabled, WATER_REFLECT triangles
// read mirror pixels from the *previous* field buffer (passed to the renderer
// via Rasterizer::reflectBuffer / Game::setReflectBuffer()) instead of the
// framebuffer currently being drawn. The previous field is fully committed
// (all geometry, sky, etc.), so reflections are never depth-order dependent
// and never show render-order artefacts.
//
// Trade-off: exactly 1 frame of temporal lag in reflections (imperceptible
// at ≥30 fps) and a ~1 output-row vertical offset because the two fields
// carry alternate scanlines.
//
// Set to 0 to use the classic same-framebuffer SSR path (fast but
// reflection quality depends on painter's-algorithm draw order).
//
// Has no effect when FIELD_BUFFERS is 0.
#define SSR_FIELD_REFLECT 1

// ---------------------------------------------------------------------------
// Post-processing effects
// ---------------------------------------------------------------------------

// Effects with no extra buffer required:
#define POSTFX_CRT         0  // CRT scanline overlay
#define POSTFX_CELLSHADING 0  // Quantise lighting to create a cel-shaded look

// Effects requiring an additional full-resolution buffer
// (generally not viable on memory-constrained ESP32 targets):
#define POSTFX_ANTIALIASING 0  // FXAA spatial anti-aliasing
#define POSTFX_BLOOM        0  // Additive bloom / glow
#define POSTFX_MOTION_BLUR  0  // Temporal motion blur
#define POSTFX_CHROMATIC    0  // Chromatic aberration
#define POSTFX_PIXELATE     0  // Pixelation / mosaic

// Effect parameters:
#define CRT_SCANLINE_INTENSITY 48   // CRT scanline darkness (0 = off, 255 = solid black)
#define MOTION_BLUR_STRENGTH   50   // Motion blur mix weight (0-100)
#define CHROMATIC_OFFSET        2   // Pixel offset for R/B channel split
#define PIXELATE_SIZE           4   // Mosaic block size in pixels
#define CELLSHADING_CELL_BITS   4   // Low lighting bits discarded (0 = smooth, 6 = four diffuse bands, 8 = steps of 256)

// ---------------------------------------------------------------------------
// Debug
// ---------------------------------------------------------------------------

// DEBUG_OVERDRAW: Tint each successive draw over the same pixel with orange,
// making overdraw hotspots immediately visible.
#define DEBUG_OVERDRAW 0

// ---------------------------------------------------------------------------
// Depth / fog tuning  (world-space units, scaled by JET32_WORLD_SCALE)
// ---------------------------------------------------------------------------

// Z_BRIGHTNESS fade range — objects beyond zBrightFar are fully darkened.
#define zBrightFar   (1600 * JET32_WORLD_SCALE)
#define zBrightNear  ( 200 * JET32_WORLD_SCALE)
#define zBrightScale 48

// DEPTH_ALPHA_BLEND fade range — geometry beyond depthFogFar is invisible.
#define depthFogFar  (8192 * JET32_WORLD_SCALE)
#define depthFogNear (6144 * JET32_WORLD_SCALE)

// ---------------------------------------------------------------------------
// Checkerboard rendering  (desktop / high-res targets)
// ---------------------------------------------------------------------------

// CHECKERBOARD_MODE: Each frame renders only the (x+y)-parity pixels that
// match the current frame counter, leaving the opposite-parity pixels as
// last frame's values. Halves rasterizer cost for a mild temporal blur.
// Requires a second "previous frame" buffer in the frontend.
// Mutually exclusive with FIELD_BUFFERS / interlacedMode.
#define CHECKERBOARD_MODE 0

// CHECKERBOARD_RECONSTRUCTION: After all triangles are drawn, fill each
// unrendered gap pixel from its two freshly-rendered horizontal neighbours
// (2-tap average). Runs once per frame after the render queue is flushed so
// painter's-algorithm overdraw cannot cause repeated averaging.
// Set to 0 to leave gap pixels as raw previous-frame values.
#define CHECKERBOARD_RECONSTRUCTION 0

// ---------------------------------------------------------------------------
// Screen-space picking
// ---------------------------------------------------------------------------

// MAX_PICK_QUERIES: Compile-time upper bound on simultaneous screen-space
// pick queries per frame. Set to 0 to compile out ALL picking machinery
// (zero runtime cost — the per-triangle scanline test, per-RenderTri tags,
// and Scene/Rasterizer pick state all disappear from the binary).
//
// When > 0, host code may call Scene::setPickQueries() with up to this many
// (x, y) screen-space points. After Scene::render() returns, each slot in
// Scene::getPickResults() reports: was anything drawn at that pixel, the
// closest scene object, and the triangle index within that object.
//
// With FIELD_BUFFERS enabled, queried Y coordinates are snapped to a row
// that the current field actually rendered; the other rows belong to the
// previous field buffer and cannot be reliably queried.
//
// Set to at least 1 to support LensFlare occlusion testing.
#define MAX_PICK_QUERIES 0

// ---------------------------------------------------------------------------
// Platform detection
// ---------------------------------------------------------------------------

// Define ESP32 only when building for an Espressif target. Detected via
// ESP_PLATFORM (injected by the ESP-IDF CMake toolchain) so desktop builds
// never pull in esp_attr.h / IRAM_ATTR / FreeRTOS headers.
#if defined(ESP_PLATFORM)
#define ESP32
#endif


// Optional S3 transform-scratch internal-RAM preference, in bytes. The default
// 4 KiB suits small meshes; a frontend with memory headroom may raise it (e.g.
// 32 KiB for an 822-vertex lit mesh). Larger allocations or failed internal
// allocations retain malloc placement. Vector growth may hold two buffers.
// #define JET_S3_TRANSFORM_INTERNAL_BYTES 32768

// Optional with Z_BUFFERING: order opaque depth-writing triangles near-to-far
// to skip shading hidden pixels. Blended/faded/effect faces retain far-to-near
// ordering in a later band; backgrounds and overlays keep their special bands.
// Default 0 retains the original painter ordering. Equal-depth ties can change.
// #define JET_DEPTH_SORT_OPAQUE_FRONT_TO_BACK 1

// Optional runtime depth/painter switch via Rasterizer::setDepthTestingEnabled.
// Requires Z_BUFFERING=1; builds a separate painter kernel (extra code/IRAM).
// Disabled by default: ordinary builds retain their existing static kernels.
#ifndef JET_RUNTIME_DEPTH
#define JET_RUNTIME_DEPTH 0
#endif
