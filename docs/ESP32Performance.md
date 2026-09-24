# ESP32-S3 renderer optimisations

Jet's ESP32 paths reuse culling bounds, cache exact reciprocal depth-bucket
mapping and large-mesh triangle sort keys, sample affine textures in spans,
and use RGB565 SIMD blending and CRT gain kernels. These preserve the existing
integer rounding and rendering options. Static indexed8 textures now share
the optimized texture path.

## Lossless indexed textures in ESP 88

The [ESP 88 example](https://github.com/CubeCoders/JetExamples/tree/main/esp32-neon-film)
stores 17 static images as byte indices into small RGB565 palettes in internal
RAM. Indices stay in mapped flash. The conversion preserves every original
RGB565 colour; it does not reduce texture resolution or quantize colours again.

Measurements on 24 September 2026 used an ESP32-S3, octal PSRAM at 80 MHz and
an ST7796 SPI display at 80 MHz. Output was 480×320, with half-width RGB565
buffers, alternating fields, two raster workers and overlapped DMA scanout.
The baseline already included the earlier renderer optimisations.

| Comparison | Mean render time | Mean raster time | Sampled field throughput |
| --- | ---: | ---: | ---: |
| RGB565 baseline | 23.550 ms | 14.866 ms | 36.52 fields/s |
| Retained indexed8 assets and sampler | 21.578 ms | 13.030 ms | 39.33 fields/s |
| Identical-engine control, RGB565 assets | 23.608 ms | 14.900 ms | 36.45 fields/s |
| Identical-engine control, indexed8 assets | 21.589 ms | 13.041 ms | 39.31 fields/s |

The retained change reduces mean render time by **8.37%** and raster time by
**12.35%**. A separate comparison using identical renderer code on both sides
confirms **8.55%** less render time and **12.48%** less raster time from the
asset representation and associated data placement. All 36 sampled views
improved in the first comparison; the largest render-time reduction was 16.48%.

Each comparison ran baseline/candidate/candidate/baseline across 36 fixed
views, three per cut, with 12 warmup and 48 timed fields per view. Both displayed
field hashes and triangle counts matched across all runs. These are equally
weighted fixed-view measurements, not the moving film's average FPS. A full
playback also completed every cut and the intentional reboot without a fault.

## Memory tradeoff

The 17 converted images occupy 323,584 bytes as RGB565, or 164,448 bytes as
indices plus palettes: **159,136 bytes saved**. The palettes contain 2,656 raw
bytes. In the linked benchmark, firmware shrank by 155,864 bytes while static
internal memory grew by **5,800 bytes**: 2,984 bytes of instructions in IRAM and
2,816 bytes of initialized palette data including alignment. IRAM shares
physical capacity with data RAM on the S3.

These images originally lived in flash, so this is a flash/cache-footprint
saving, not a PSRAM-allocation saving. Cache misses were not counted separately.
The smallest observed free internal heap changed from 20,595 to 19,019 bytes;
heap placement also affects that measurement.

Four filtered 32×32 facade mipmaps remain RGB565 in 8 KiB of DRAM. Indexing those
as well saved another 3,120 linked internal bytes but added about 0.108 ms to
mean render time, so that variant was not retained.

## Supported paths and checks

The direct indexed path uses static palettes (`paletteSize=0`), WRAP addressing,
power-of-two dimensions up to 1024, and a nearest-only renderer build
(`BILINEAR_FILTER=0`). Other supported cases retain the general sampler.
Animated palettes keep their offset/modulo behavior. Colour keys compare the
decoded RGB565 colour. Source or palette overlap with the framebuffer disables
staging to preserve feedback order.

Indexed lookup adds a dependent palette read; it is not always faster. Measure
the intended scene and account for internal RAM. Indexed textures remain
nearest-sampled in bilinear-capable builds. Sprite2D's row compositor consumes
RGB565 textures, so ESP 88's glow sprites and credits remain RGB565, as does its
dynamic speed display. No 4-bit format is added.

The retained [tests](../tests/README.md) include independent indexed8 and RGB565
span equations, exact depth-bucket comparisons, constant-blend feedback and
exhaustive CRT gain checks. The indexed fixture alone checks 124,824,864 UV,
pixel and guard cases. Paired raster validation also covered perspective,
lighting, alpha keys, address modes, field layouts and framebuffer feedback.
