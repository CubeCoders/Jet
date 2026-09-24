# README demo media

These are native renders of the projects in
[CubeCoders/JetExamples](https://github.com/CubeCoders/JetExamples), using their
ESP32-S3 renderer configurations at 480×320 with RGB565 colour and half-width
field buffers. Both field parities use the same pose; the runtime performance
overlay is omitted. They are software captures, not recordings of an LCD.

The four PNGs are unchanged copies from the examples repository's
[screenshot collection](https://github.com/CubeCoders/JetExamples/tree/2b3d835/docs/screenshots).
Its manifest and capture tools document their poses and configurations.

`neon-motorworks.gif` is a 20-second camera orbit of `esp32-neon-car`, rendered
from that same examples revision with Jet `b412c87`. It has 500 frames at
25 frames/s and loops continuously. GIF encoding uses one 256-colour palette
without dithering; it does not add smoothing or alter the scene. The animation's
playback rate is independent of the example's measured S3 field rate.

The example sources retain the artwork's provenance and licence information;
see the [teapot assets](https://github.com/CubeCoders/JetExamples/tree/main/esp32-lighting-teapot/assets)
and [car assets](https://github.com/CubeCoders/JetExamples/tree/main/esp32-neon-car/assets).
