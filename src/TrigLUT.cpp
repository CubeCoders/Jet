#include "TrigLUT.hpp"
#include "FastMath.hpp"
#include <cmath>
#include <limits>

namespace Renderer {

int32_t sin_table[ANGLE_MAX];
int32_t tan_table[ANGLE_MAX];

#if FLOAT_CAMERA_ANGLES
#if JET_COMPACT_TRIG_TABLE
float float_sin_quarter[FLOAT_SIN_QUARTER + 1];
float float_sin_half_turn;
#else
float float_sin_table[FLOAT_SIN_ANGLE_MAX];
#endif
#if FLOAT_TAN_CACHE_SCALE > 1
float float_tan_table[FLOAT_TAN_ANGLE_MAX];
#endif
#endif

void initializeTrigTables() {
    for (int i = 0; i < ANGLE_MAX; ++i) {
        double radians = i * M_PI / 180.0;
        sin_table[i] = static_cast<int32_t>(std::sin(radians) * FIXED_POINT_SCALE);
        
        // Calculate cos using sin with phase shift
        double cos_val = std::sin((i + 90) * M_PI / 180.0);
        int32_t cos = static_cast<int32_t>(cos_val * FIXED_POINT_SCALE);
        
        // Handle tan() calculation
        if (cos != 0) {
            tan_table[i] = (sin_table[i] * FIXED_POINT_SCALE) / cos;
        } else {
            tan_table[i] = INT32_MAX;
        }
    }

    #if FLOAT_CAMERA_ANGLES
#if JET_COMPACT_TRIG_TABLE
    float_sin_half_turn = static_cast<float>(std::sin(180.0 * M_PI / 180.0));
    for (int i = 0; i <= FLOAT_SIN_QUARTER; ++i) {
        const double degrees = static_cast<double>(i) / FLOAT_SIN_SUBDIVISIONS;
        float_sin_quarter[i] = static_cast<float>(std::sin(degrees * M_PI / 180.0));
    }
#else
    for (int i = 0; i < FLOAT_SIN_ANGLE_MAX; ++i) {
        double degrees = static_cast<double>(i) / FLOAT_SIN_SUBDIVISIONS;
        double radians = degrees * M_PI / 180.0;
        float_sin_table[i] = static_cast<float>(std::sin(radians));
    }

    #endif // JET_COMPACT_TRIG_TABLE
    #if FLOAT_TAN_CACHE_SCALE > 1
    for (int i = 0; i < FLOAT_TAN_ANGLE_MAX; ++i) {
        double degrees = static_cast<double>(i) / FLOAT_TAN_SUBDIVISIONS;
        double radians = degrees * M_PI / 180.0;
        float cos_val = std::cos(radians);
        if (std::abs(cos_val) > 1e-6) {
            float_tan_table[i] = static_cast<float>(std::sin(radians) / cos_val);
        } else {
            float_tan_table[i] = std::copysign(std::numeric_limits<float>::max(), std::sin(radians));
        }
    }
    #endif
    #endif
}

} // namespace Renderer
