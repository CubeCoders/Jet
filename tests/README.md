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
and forward/reverse/in-place framebuffer feedback: 13,057,920 pixel/guard
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


## Depth, square roots and gloss

`depth_buffer.cpp` requires `Z_BUFFERING=1`, `FAST_Z=0`. It checks full-height
depth-row clearing, X addressing, crossing depth slopes in both draw orders,
and allocation guards. Run with half-width field buffers and both full-frame
width layouts. Compile against the same configuration as the Jet library.

`sqrt.cpp` checks the exact small-input floor square root against the original
double implementation over normal magnitudes, square boundaries and random
32/64-bit values. `specular.cpp` checks the optional PHONG gloss lobe against a
floating-point power reference, intensity/colour handling and RGB565 saturation.
Both can be built as standalone C++17 executables with `src` and the frontend's
`JetConfig.hpp` on the include path; keep assertions enabled.

`depth_sort.cpp` requires `Z_BUFFERING=1` and
`JET_DEPTH_SORT_OPAQUE_FRONT_TO_BACK=1`. It checks 24 object submission orders
with opaque occlusion, blended faces, no-depth-write backgrounds and depth-
ignoring overlays. Build with the Scene sources and matching frontend config.


`texture_options.cpp` checks independent per-material UV mapping and per-texture
filtering, analytic affine/perspective reference texels, bilinear interpolation,
palette safety and framebuffer guards. Build with TEXTURE_MAPPING=1 and each
combination of PERSPECTIVE_CORRECT_TEXTURES/BILINEAR_FILTER to verify disabled
capabilities remain disabled. Match the test and library configurations.

`wide_depth.cpp` checks large projected triangles against a 64-bit depth
reference (within one integer depth unit), both parities and guard storage.
Requires Z_BUFFERING=1, FAST_Z=0. It covers overflow in depth-weight products
and triangles whose area exceeds int32.

`water_reflection.cpp` verifies the optional scene-reflection source-row cutoff:
excluded rows cannot influence output, valid rows still reflect, and disabling
the cutoff restores unrestricted sampling. Run with both the simple span path
and the general depth/lighting paths. The cutoff requires a sky gradient.


## CRT scanlines and compact fields

`crt_layout.cpp` tests the real PostFX implementation against independent channel
scaling, with prefix/suffix guards, full/half-width storage, packed fields, odd
output heights, both physical row parities and intensity endpoints. Build it
together with `../src/PostFX.cpp`, using an include path for `JetConfig.hpp`.
Run combinations of `HALF_WIDTH_BUFFERS=0/1` and `FIELD_BUFFERS=0/1`, plus
`POSTFX_CRT=0` as a no-op check. Set other POSTFX flags to zero. The examples
repository's `esp32-postfx-crt/tests` CMake project runs all five configurations
and an integrated Scene test, including matched serial/parallel output.

CRT now scales each channel proportionally by `(255-intensity)/255` instead of
subtracting raw 5/6-bit channel values. This corrects the documented 0-255
intensity range and avoids a green cast. Existing CRT-enabled applications may
need to retune their intensity. `Scene::crtEnabled` and `crtIntensity` are present
when `POSTFX_CRT=1`; disabled builds preserve their previous rendering path.
