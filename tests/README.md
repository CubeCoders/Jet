# Triangle span coverage check

This standalone C++17 test compares incremental scanline coverage with
independent 64-bit edge functions for 12,000 deterministic triangles. It
includes clipped triangles, horizontal and vertical edges, both horizontal
pixel steps, field parity, and a band starting on an odd row. It also checks
that unsupported inputs select the original solver.

From this directory with a C++ compiler available:

```sh
c++ -O2 -std=c++17 triangle_spans.cpp -o triangle_spans
./triangle_spans
```

Or in an MSVC developer prompt:

```bat
cl /O2 /EHsc /std:c++17 triangle_spans.cpp
triangle_spans.exe
```

This tests span geometry. Firmware timing and framebuffer comparisons must
also use the target's actual renderer configuration and effects.

## Affine texture check

`affine_textures.cpp` exercises the actual rasterizer against an independent
double-precision barycentric reference. It covers negative/repeating UVs,
large projected triangles, UVs beyond the incremental-path bounds,
power-of-two and non-power-of-two tiles, WRAP/CLAMP/ZERO addressing,
palettes, alpha blending, texture LOD blending and flat fallback, clipping,
checkerboard, interlacing and rendering bands.

Provide a `JetConfig.hpp` with `TEXTURE_MAPPING=1`, `FAST_Z=1`, `LIGHTING=0`,
`Z_BUFFERING=0`, `PERSPECTIVE_CORRECT_TEXTURES=0`, `BILINEAR_FILTER=0`,
`SCREEN_DOOR_ALPHA=0` and fog beyond the test depth of 500. Run separately
with full-width, half-width, and half-width plus field buffers. For example,
from this directory with the config in `/path/to/config`:

```sh
c++ -O2 -std=c++17 -I/path/to/config -I../src affine_textures.cpp \
  ../src/Renderer.cpp ../src/BlendSpans.cpp ../src/Camera.cpp ../src/TrigLUT.cpp \
  ../src/Material.cpp ../src/Texture.cpp -o affine_textures
./affine_textures
```

The check permits a neighbouring texel only when it is reachable within one
fixed-point UV unit of the exact result. More than 1% boundary differences
or any larger error fails. The London experiment checked 35,819,585 pixels
across the three layouts with 724 boundary differences and zero errors.

`texture_spans.cpp` checks the internal power-of-two RGB565 affine sampler
and block blending against independent pixel equations. It covers the 32x64
specialization and other dimensions (including 1x1024 and 1024x1), signed UVs
and steps, every destination halfword alignment, short spans, 128-pixel tile
boundaries, LOD fades, alpha and additive blending, and guard pixels.
Build with `BlendSpans.cpp` and an application config, as for the blend-span
fixture below. It checks 22,560,768 values. For S3 validation, define
`JET_TEXTURE_SPANS_EMBEDDED` and call `runTextureSpanChecks()` before rendering.
Keep the fixture out of production firmware.

## Scene texture queue check

`scene_texture_queue.cpp` exercises mixed flat/textured geometry through
Scene's transform, near-plane clipping, culling, sort and band replay. It
sweeps all near-plane masks and culling modes, reversed winding, repeated
and negative UVs, geometry/texture LOD, shading modes, checkerboard and
interlacing. It runs two concurrent bands at varying odd/even boundaries and
asserts whole-frame pixel equivalence and unique rasterized-triangle counts.

Build against the same `JetConfig.hpp` and sources as the renderer under
test (for this fixture, use `Z_BUFFERING=0`). For example:

```sh
c++ -O2 -std=c++17 -I/path/to/config -I../src scene_texture_queue.cpp \
  ../src/Scene.cpp ../src/Sprite2D.cpp ../src/Renderer.cpp ../src/BlendSpans.cpp ../src/Object.cpp ../src/Camera.cpp \
  ../src/TrigLUT.cpp ../src/Material.cpp ../src/Texture.cpp \
  ../src/Light.cpp ../src/PostFX.cpp -o scene_texture_queue
./scene_texture_queue
./scene_texture_queue <baseline-hex-hash>
```

The optional hash makes this a pixel-exact comparison with a baseline built
using the same configuration and compiler. The compact UV queue matched
its pre-change Scene baseline across all 144 frames (the expanded fixture
submits 1,476 triangles and also covers rotated meshes, billboards,
camera rotations, zero camera Z, depth bias and background/overlay flags)
for full-width, half-width field buffers, lighting, perspective
textures and textures disabled. Five London preview views also matched
pixel for pixel. Build all translation units with the same Scene header;
the private queue layout changes Scene's size.

## Display pixel expansion

`fill_spans.cpp` checks constant RGB565 span fills for every halfword alignment,
lengths from -1 through 480, scalar/vector tails, guard pixels and every RGB565
colour. It includes `Renderer.cpp` to exercise its private fill helper: build
with the scene fixture's source list above, omitting the separate `Renderer.cpp`
entry. For S3 checks, temporarily include the test at the end of `Renderer.cpp`
with `JET_FILL_SPANS_EMBEDDED` defined and call `runFillSpanChecks()` before
starting rendering. Remove that test include from production firmware.

`pixel_ops.cpp` checks every RGB565 value, all source/destination halfword
alignments, empty/short spans, vector tails and output guard regions.

```sh
c++ -O2 -std=c++17 -I../src pixel_ops.cpp -o pixel_ops
./pixel_ops
```

On ESP32-S3, compile this source into a test firmware with
`JET_PIXEL_OPS_EMBEDDED` defined and call `runPixelOpsChecks()` before
starting rendering. It exercises the real PIE instructions and benchmarks
10,000 rows of 240 input pixels against the old paired-store scalar loop.
Both the desktop fallback and S3 SIMD path passed 1,314,816 value/guard
checks. Keep the harness out of the production build.

## Alpha, water and sprite blending

`sprite_scanline.cpp` checks the public `compositeSprites()` helper against an
independent pixel reference for both native and byte-swapped output. It covers
clipping, scaling, all texture transforms, alpha/additive blending, colour keys,
invalid/disabled sprites and row guard pixels. Build with `Sprite2D.cpp`,
`BlendSpans.cpp`, `Material.cpp` and `Texture.cpp` and an application config.
Unlike the Scene fixture below, it also runs with half-width/field configs.

`blend_spans.cpp` independently checks all four RGB565 blend equations:
triangle /256, sprite /255, sprite saturating add and triangle scaled add.
It covers all channel pairs at every alpha, every halfword alignment,
tails, colour keys, swapped destinations, solid/constant inputs, overlapping
forward writes with forward/reverse source reads (including framebuffer
feedback), and scaled/mirrored rows crossing the symmetry axis and staging
tile boundary.

```sh
c++ -O2 -std=c++17 -I/path/to/config -I../src blend_spans.cpp \
  ../src/BlendSpans.cpp -o blend_spans
./blend_spans
```

For S3 validation, define `JET_BLEND_SPANS_EMBEDDED`, compile the fixture
into test firmware, and call `runBlendSpanChecks()` before rendering starts.
The actual PIE code and desktop fallback each passed 23,730,432 pixel/guard
checks. The embedded fixture also compares long-row performance against
IRAM scalar loops specialized by blend mode and flags. Do not leave it in
production firmware.

`sprite_compositor.cpp` checks 96,768 full-width Scene renders against an
independent compositor: negative/offscreen coordinates, clipping on all
edges, scale 1–4, combined material/sprite alpha, keyed and solid sprites,
additive blending, all 16 combinations of horizontal/vertical flips and
mirrored halves, and registration in reverse z-order. Build it against
Jet with the same full-width, non-interlaced configuration as the library,
or the same source list as `scene_texture_queue.cpp` above.

`blend_raster.cpp` fingerprints 768 frames / 2,295 drawn triangles through
the real alpha/additive/water raster paths. It exercises short and long
spans, all alpha values, reflection buffers and current-frame reflection,
sky fallback, ripples, band clipping and interlacing. Use the source list
for `affine_textures.cpp` above. Build a saved renderer and the candidate
with identical headers/configuration, then pass the saved hex fingerprint
as argv[1] to assert equivalence. Test full width, half width and fields.

`constant_blend.cpp` checks the prepared constant-colour /256 SIMD kernel
against independent channel equations. It covers every channel pair at every
alpha, both constant foreground and background, all pixel alignments, tails,
and forward/reverse/in-place framebuffer feedback: 13,888,512 pixel/guard
comparisons. Build with `BlendSpans.cpp`, as for `blend_spans.cpp`. On ESP,
call `runConstantBlendChecks()` in temporary test firmware; it also times
3,000 aligned 240-pixel rows against the existing generic SIMD helper.

Define `JET_TEST_POSITION_CACHE=1` when compiling `scene_texture_queue.cpp`
to exercise cached/uncached positions and LODs, cache rebuilding after direct
edits, Object copies and invalidation by geometry-changing helpers. Its
fingerprint must still match the uncached baseline in every configuration.

`position_reuse.cpp` compares cached and uncached rendering over 48 frames
with duplicate positions carrying distinct normals and UVs, near/far clipping,
object rotations, billboards and changing shading modes. It grows a mesh
from 24 to 384 vertices and then shrinks it, exercising the S3 transform
scratch allocator's size limit and larger-buffer fallback. Build it with the
same source list as `scene_texture_queue.cpp`, and test full width, fields,
lighting, perspective textures and textures disabled. For on-device checks,
define `JET_POSITION_REUSE_EMBEDDED` and call `runPositionReuseChecks()` from
temporary firmware. The fixture aligns its framebuffer for the S3 SIMD clear.

## Far-plane clipping

`far_plane_clipping.cpp` checks that large ground/water faces retain their
visible area when their average depth is beyond the far plane. It covers
crossings of both Z planes, reversed winding and fully distant geometry.
Build it with the same Scene sources as `scene_texture_queue.cpp`, with
`HALF_WIDTH_BUFFERS=0` and `FIELD_BUFFERS=0`, and keep assertions enabled.

## Shared mesh instances

`mesh_instances.cpp` compares shared instances with independently expanded
ordinary geometry over 144 frames. It covers mixed unique/shared geometry,
rigid transforms, material overrides, textures, clipping, culling, fades, LODs,
lighting and billboards. With triangle sorting enabled, its reference submits
separate ordinary meshes to preserve the same per-mesh sort boundaries.
It also checks immutable triangle order, copying, ownership, rejected nesting,
baked rotation/scale and instance-aware picking when `MAX_PICK_QUERIES > 0`.
Material-table checks cover independent per-triangle replacements, null-entry
fallback to baked colours, uniform-override precedence, table lifetime and
rejection of incorrectly sized tables.

Build using the same source list as `scene_texture_queue.cpp`, replacing that
test filename with `mesh_instances.cpp`. Run full-width, field-buffer, lit,
perspective-texture and untextured configurations, plus a picking-enabled build.
For an S3 fixture, define `JET_MESH_INSTANCES_EMBEDDED` and call
`runMeshInstanceChecks()` from temporary firmware. Keep it out of production.

## Exact depth-bucket reciprocals

`depth_buckets.cpp` independently compares the cached reciprocal with the
original 64-bit division, int32 narrowing and clamp. It exercises 100,078,200
cases across ten bucket counts, exhaustive small ranges, power-of-two
boundaries, random signed depths and extreme values. It needs no Jet config.
Keep assertions enabled.

```sh
c++ -O2 -std=c++17 depth_buckets.cpp -o depth_buckets
./depth_buckets
```

```bat
cl /O2 /EHsc /std:c++17 /UNDEBUG depth_buckets.cpp
depth_buckets.exe
```

## Legacy CRT exactness

`crt_legacy.cpp` checks the older one-argument CRT operation against independent
subtract-and-clamp channel equations. It covers all 65,536 RGB565 colours at
256 strengths, every halfword alignment, widths 0 through 96 and untouched
rows/guards: 60,201,984 comparisons. This API uses the full stored width and
height supplied to `PostFX`; the newer examples have a separate proportional,
field-aware API. Normal game CRT settings are unchanged.

Compile this test and `src/PostFX.cpp` against the same test-only `JetConfig.hpp`:

```cpp
#pragma once
extern unsigned jetCrtTestIntensity;
#define POSTFX_CRT 1
#define CRT_SCANLINE_INTENSITY jetCrtTestIntensity
#define HALF_WIDTH_BUFFERS 1
#define FIELD_BUFFERS 1
#define POSTFX_BLOOM 0
#define POSTFX_ANTIALIASING 0
#define POSTFX_MOTION_BLUR 0
#define POSTFX_CHROMATIC 0
#define POSTFX_PIXELATE 0
```

The test defines the variable strength; keep this configuration out of normal
firmware. Run the desktop executable, or call `jetGameCrtExactTests()` from
temporary S3 test firmware. It yields between strengths and uses static guard
storage. Require `LEGACY CRT EXACT PASS` and a zero return code.

## Indexed8 texture spans

`indexed8_spans.cpp` compares indexed sampling with independently decoded
RGB565 and modulo-based UV addressing. It checks negative fractional UVs,
power-of-two sizes, source/palette/output alignment, lengths 0 through 480,
fade, alpha/additive rounding and untouched guards: 124,824,864 comparisons.

Compile it with `src/BlendSpans.cpp`, C++17 and a shared test `JetConfig.hpp`
enabling nearest texture mapping, no lighting, half-width field buffers and
`FAST_Z`. For example, from this directory:

```sh
c++ -O2 -std=c++17 -I/path/to/test-config indexed8_spans.cpp ../src/BlendSpans.cpp -o indexed8_spans
./indexed8_spans
```

Require `INDEXED8 SPANS PASS` and a zero return code. Static palettes use
`paletteSize=0`; animated palettes keep the generic sampling path. This older
engine's bilinear sampler does not support indexed textures. Sprite textures
remain RGB565.
