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
