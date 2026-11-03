// Build with the same JetConfig.hpp as Jet. Tests the public lookup API and
// quadrant boundaries at the configured resolution; does not read table storage.
#include "TrigLUT.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
using namespace Renderer;
int main() {
    initializeTrigTables();
#if FLOAT_CAMERA_ANGLES
    for (int i = -2 * FLOAT_SIN_ANGLE_MAX; i <= 2 * FLOAT_SIN_ANGLE_MAX; ++i) {
        const float angle = float(i) / FLOAT_SIN_SUBDIVISIONS;
        for (bool cosine : {false, true}) {
            const float shifted = cosine ? angle + 90.0f : angle;
            int index = int(shifted * FLOAT_SIN_SUBDIVISIONS) % FLOAT_SIN_ANGLE_MAX;
            if (index < 0) index += FLOAT_SIN_ANGLE_MAX;
            const float expected = float(std::sin((double(index) / FLOAT_SIN_SUBDIVISIONS) * M_PI / 180.0));
            const float actual = cosine ? lookupCos(angle) : lookupSin(angle);
            if (std::abs(actual - expected) > 1e-7f) {
                std::printf("Mismatch at %g (%s): %.9g versus %.9g\n", angle, cosine ? "cos" : "sin", actual, expected);
                return EXIT_FAILURE;
            }
        }
    }
#endif
    std::puts("PASS: trigonometric lookup, signed wrapping and quadrant boundaries");
}
