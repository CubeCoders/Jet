# Engine memory bandwidth audit

Measured on 2026-11-03 with ESP88 on an ESP32-S3 at 240 MHz, 8 MiB octal
PSRAM at 80 MHz, 480x320 SPI output, half-width field buffers and two raster
workers. Engine baseline: `19018b52c04d92c615d1ade2db5eb7198c7af21c`.
The scene, materials, texture data, camera and effects are unchanged.

## Retained changes

- Emit surviving triangles directly into the render queue, avoiding the
  temporary record and its copy into the vector.
- `JET_COMPACT_QUEUE=1`: queue positions alone for built-in unlit, emissive,
  additive and water materials. Store normals and cached brightness separately
  only for faces that may consume them. Custom shaders retain all attributes.
  With ESP88's S3 configuration, the common triangle record drops from 100 to
  56 bytes. A lit face uses 56 + 44 bytes across the two queues, the same logical
  payload size as before. UVs remain in the existing textured-face side queue.
- `JET_COMPACT_TRIG_TABLE=1`: store one quadrant of the floating sine table
  and reconstruct the other quadrants by symmetry. Retain the original half-turn
  residual. At `FLOAT_SIN_CACHE_SCALE=10`, static DRAM falls by 10,792 bytes,
  with no reduction in angle resolution or entry precision.

The two layout options default to zero for compatibility and are enabled in
ESP88's shared JetConfig.hpp. All translation units must use the same config.
Lit-heavy workloads should measure the separate lighting queue's access cost;
equal payload sizes do not imply equal cache behavior. Compact trigonometry
requires callers to use lookupSin/lookupCos instead of the full-table symbol.

## Repeated full-film measurements

Each run renders 3,480 deterministic fields, seeking the film at field/30 seconds.
Twenty-nine 120-field windows record update, render and raster elapsed time.
Render includes scene rendering and the film effects. Raster includes worker
dispatch/join. These are elapsed costs, not pure CPU busy time or displayed FPS.
Framebuffer hashes are sampled every 30 fields outside the timed render.

| Metric | Original, two runs | Final, two runs |
|---|---:|---:|
| Update + render per field | 25.097345 ms | 24.619156 ms |
| Render per field | 22.447893 ms | 21.909208 ms |
| Raster per field | 13.667351 ms | 13.287773 ms |
| Minimum free internal RAM | 18,979 B | 20,283 B |
| Minimum free PSRAM | 7,211,604 B | 7,305,248 B |

Update + render improves by 1.91%. The original runs differ by
0.003765 ms/field;
the final runs differ by 0.006784 ms/field.
Minimum free PSRAM improves by 93,644 bytes (91.4 KiB). Not all reclaimed
static DRAM remains free: subsequent allocations can use it instead of PSRAM.
Every recorded framebuffer hash and triangle-count window matches the baseline.
This is a modest engine gain, not a claim that the slow sections now reach 60 FPS.

## Where the original buffers lived

A separate diagnostic build used esp_ptr_external_ram/esp_ptr_internal and
vector size/capacity reporting. Its serial overhead is excluded from the timing
comparison. Capacities are allocation sizes, not measured bus traffic.

| Allocation | Observed capacity | Placement |
|---|---:|---|
| Main triangle queue | 204,800 B | PSRAM |
| Sorted triangle indices | 5,400 B | Initially internal, later PSRAM |
| Per-mesh sort keys | 1,920 B | PSRAM |
| Texture-coordinate queue | 6,144 B | Initially internal, later PSRAM |
| Bucket keys | 2,048 B | Initially internal, later PSRAM |
| Transform scratch | 14,000 B | Internal RAM |
| Current framebuffer | 76,800 B | Internal RAM |
| Floating sine table | 14,400 B | Internal BSS |

Most mesh vertex data and packed positions were in PSRAM. Sampled texture
palettes were already internal; most authored texels were mapped flash data,
with some generated texels in RAM. This is an allocation/placement audit,
not a complete inventory of all assets or a hardware cache-miss counter.

The existing display path already exchanges framebuffer roles and converts
rows directly into DMA buffers. It does not copy an entire framebuffer to
present each field. Removing that nonexistent copy would not help.

## Isolated experiments

Against the first original run, before combining changes:

| Experiment | Update + render | Decision |
|---|---:|---|
| First compact queue, 68 B common record | -1.27% | Tightened further to 56 B |
| Direct queue emission | -0.34% | Retained; also avoids a redundant copy |
| Prefer internal RAM for sort indices/keys | -0.42% | Rejected: consumed another 7,540 B internal RAM |
| Quarter-wave sine table | +0.46% | Memory saving; no standalone speed claim |

Small single-run differences are exploratory. The reported final gain is from
two complete original and two complete final runs. Allocation/code layout can
affect timing, so isolated gains must not be added together.

## Correctness checks and reproduction

- 232 ESP88 poses, both field parities, and three concurrent band splits:
  serial/banded pixels, per-triangle flags and framebuffer guards match.
- 672 standalone mixed-material cases cover all shading modes, custom shaders,
  emissive/specular combinations, both windings and near/far clipping.
- All 3,600 sine entries at scale 10 compare bit-for-bit with the original
  table on the desktop. The standalone trig test also covers negative angles,
  wrapping and quadrant boundaries with a 1e-7 absolute tolerance.
- The final IDF firmware builds for the S3. Audit and timing instrumentation
  are excluded from the normal playback image.

`tests/memory_queue.cpp` prints deterministic framebuffer fingerprints; compare
the complete output with the original engine using the same JetConfig.hpp.
Compile with assertions enabled. `tests/trig_storage.cpp` checks the public
lookup API and can be built with compact storage both on and off. Both tests
link against Jet and use its normal configuration include directory.

Local full-film harness, raw serial logs, original app backup and all isolated
firmware manifests are under `jet32/build/memory-bench`. The original slot backup
is in `jet32/build/band-bench/hardware`; do not publish device backups.
The final benchmark variant is named `lean`; `normal` is the continuous demo.
The launcher app, partition table and other catalogue entries are untouched.
