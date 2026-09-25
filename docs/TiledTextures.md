# Experimental tiled texture sampling

The [ESP32 megatexture example](https://github.com/CubeCoders/JetMegatexturesDemo)
uses the opt-in `Texture::tiled` view. Ordinary textures keep their existing path
when this pointer is null. Tile storage, residency, mip choice, cache admission
and lifetime belong to the application; Jet allocates no texture cache.

## View contract

`TileTexture` describes one immutable material/mip view during a render. Source
tiles contain 32×32 unsigned palette indices in row-major tile order, with a
256-entry RGB565 palette. Edge tiles must be padded. Coarse mips use the separate
row-major `coarse` view. Provide positive dimensions and valid backing, palette,
and any enabled cache pointers; storage is caller-owned. The view must outlive
all workers using it. UVs use Jet's 0..1023 integer domain and are **clamped**.
Ordinary texture wrap/zero addressing and animated palette offsets do not apply
to this experimental view.

Set `direct=true` for backing-store reads. Otherwise `slots` maps source pages
to cache slots, with 255 meaning a miss; misses retain detail by reading backing.
The optional per-worker feedback arrays need one byte per source page, initialized
by the application. Their counts saturate. There are two feedback lanes for the
renderer configuration with two joined, disjoint raster bands. More workers
require a different feedback arrangement.

With `BILINEAR_FILTER` compiled in, `Texture::tiledFilter` chooses nearest,
bilinear, three-point triangular interpolation or cached bilinear. The existing
`bilinear=false` switch still forces nearest. Palette indices are decoded before
interpolation. Three-point is N64-style interpolation, not bit-exact RDP emulation.

For cached bilinear, `hotSlots` maps the 16×16 UV-page grid to a shared RGB565
pool. Each slot contains 64×64 samples of the existing integer UV domain; 255
means nearest fallback. `hotKeyBase` distinguishes material/mip feedback keys;
reserve 65535 as the feedback-table sentinel. The example builds exact bilinear
samples incrementally, publishes only complete tiles, and manages eviction.
`filteredUV` is an alternative full 1024×1024 bilinear lookup used by a separate
fixed-cache benchmark. Neither mechanism automatically tracks mutable source
pixels or palettes: the application must invalidate affected entries.

Only mutate views, source data, slot tables and pools after all raster workers
have joined. Feedback tables are worker-owned during rendering and read/reset
between renders. The bounded `TileFeedback` hash table can drop demand samples
when crowded; dropped feedback must never be treated as invalid texture data.

## Optimized span path

Opaque unlit perspective materials can use `TiledSpan.cpp` when texturing,
perspective mapping and `FAST_Z` are enabled, with lighting, Z buffering,
Z brightness and depth-alpha blending disabled. Reflections, screen-space
textures and alpha materials fall back to ordinary rasterization. The span path
uses one-to-eight-pixel segments and an estimated maximum 1/8 source-texel
perspective curvature error per segment. It is an approximation, not exact
per-pixel perspective. `exactPerspective` bypasses that optimized renderer path
for comparisons. Its reciprocal helper requires positive normal IEEE-754 inputs
within the guarded depth range; it is not a general division replacement.

Hot-cache demand feedback is collected by the specialized span path. In other
renderer configurations the sampler can use prepopulated entries, but the caller
must arrange demand collection if automatic warming is required.

On ESP32 the specialized span is in IRAM to reduce instruction/texture contention.
Include `TiledSpan.cpp` when manually listing renderer sources; the supplied CMake
builds collect it automatically. Keeping `BILINEAR_FILTER=0` omits filtered span
variants. Non-tiled materials do not opt into this behavior.

## Validation

`tests/tiled_textures.cpp` checks procedural nearest and bilinear references,
tile boundaries, coarse mips, source-cache hits/misses, hot lookup/fallback,
filter dispatch, reciprocal precision and span guards. Compile with
`BILINEAR_FILTER=1` and the same `JetConfig.hpp` for all translation units:

```sh
c++ -O2 -std=c++17 -I/path/to/config -Isrc tests/tiled_textures.cpp src/Texture.cpp src/TiledSpan.cpp -o tiled_textures
./tiled_textures
```

The megatexture example adds real-asset oracles, cache publication/eviction tests,
moving-camera serial/parallel image checks, camera-control tests and S3 timing
evidence. Its README explains the measured memory/performance tradeoffs.
