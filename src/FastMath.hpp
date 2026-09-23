// FastMath.hpp
#ifndef FAST_MATH_HPP
#define FAST_MATH_HPP

#include <cstdint>
#include <cmath>
#include "JetConfig.hpp"

#ifdef ESP32
#include "esp_attr.h"
#define PERF_CRITICAL IRAM_ATTR
#else
#define PERF_CRITICAL 
#endif

// Optionally define USE_ESP_DSP before including this header to enable DSP-optimized dot product
//#define USE_ESP_DSP 1

#if defined(USE_ESP_DSP) && USE_ESP_DSP == 1
extern "C"
{
#include "dsps.h" // Include ESP32 DSP library
}
#endif

namespace FastMath
{
    inline int64_t initialEstimate(int64_t value)
    {
        // Provide an initial rough estimate for the inverse sqrt using bit shifting or a lookup table
        // Example: use a bit-shifting heuristic or simple fixed-point estimation
        return 1 << 16; // Placeholder value; replace with a better estimate
    }

    inline int64_t PERF_CRITICAL approximateSqrt(int64_t value)
    {
        if (value <= 0) return 0;
        if (static_cast<uint64_t>(value) <= UINT32_MAX) {
            // ESP32-S3 has single-precision floating point; double sqrt uses
            // software helpers. Float supplies a close candidate, then integer
            // checks recover exactly floor(sqrt(value)), including rounding at
            // perfect squares and UINT32_MAX. Normal vectors use this path.
            const uint32_t input = static_cast<uint32_t>(value);
            uint32_t root = static_cast<uint32_t>(std::sqrt(static_cast<float>(input)));
            if (root >= 65536u) return 65535;
            if (root * root > input) --root;
            else if (root < 65535u && (root + 1) * (root + 1) <= input) ++root;
            return root;
        }
        // Preserve the existing wide-input behaviour outside the fast path.
        return static_cast<int64_t>(std::sqrt(static_cast<double>(value)));
    }

    inline int64_t initialEstimateFixedPoint(int64_t value, int32_t fixedPointShift)
    {
        // Provide an initial rough estimate for the square root using bit shifting or a lookup table
        return value > 0 ? (1 << (fixedPointShift + 8)) / (value >> (fixedPointShift + 4)) : 1 << (fixedPointShift / 2); // Better initial estimate to reduce flickering
    }

    inline int64_t approximateInverseSqrt(int64_t value)
    {
        // 1/sqrt via std::sqrt; avoids the broken Newton step from the placeholder
        // initial estimate that produced near-constant output.
        if (value <= 0) return 0;
        const double s = std::sqrt(static_cast<double>(value));
        return s > 0.0 ? static_cast<int64_t>(1.0 / s) : 0;
    }

    inline int64_t approximateSqrtFixedPoint(int64_t value, int32_t fixedPointShift)
    {
        // Use a simplified method to find the approximate square root in fixed-point
        int64_t approx = initialEstimateFixedPoint(value, fixedPointShift); // Initial estimate from a LUT or bit-shifting trick
        // Optional refinement to improve precision
        approx = (approx + (value >> fixedPointShift) / approx) >> 1;
        return approx;
    }

    /**
     * @brief Manual implementation of the dot product.
     *
     * Calculates the dot product of two 3D vectors using standard arithmetic.
     *
     * @param nx Normal vector component X
     * @param ny Normal vector component Y
     * @param nz Normal vector component Z
     * @param dirX Directional light component X
     * @param dirY Directional light component Y
     * @param dirZ Directional light component Z
     * @return int32_t Clamped dot product result
     */
    inline int32_t PERF_CRITICAL dotProduct_manual(int32_t nx, int32_t ny, int32_t nz,
                                     int32_t dirX, int32_t dirY, int32_t dirZ)
    {
        int32_t dp = nx * dirX + ny * dirY + nz * dirZ;
        return (dp > 0) ? dp : 0; // Clamp to [0, ∞)
    }

    /**
     * @brief DSP-optimized implementation of the dot product.
     *
     * Utilizes the ESP32 DSP library's hardware-accelerated dot product function.
     *
     * @param nx Normal vector component X (scaled to int16_t)
     * @param ny Normal vector component Y (scaled to int16_t)
     * @param nz Normal vector component Z (scaled to int16_t)
     * @param dirX Directional light component X (scaled to int16_t)
     * @param dirY Directional light component Y (scaled to int16_t)
     * @param dirZ Directional light component Z (scaled to int16_t)
     * @return int32_t Clamped dot product result
     */
    inline int32_t dotProduct_dsp(int32_t nx, int32_t ny, int32_t nz,
                                  int32_t dirX, int32_t dirY, int32_t dirZ)
    {
#if defined(USE_ESP_DSP) && USE_ESP_DSP == 1
        // Convert 32-bit integers to 16-bit with scaling if necessary
        // Ensure that nx, ny, nz, dirX, dirY, dirZ are within int16_t range after scaling
        int16_t src1[3] = {static_cast<int16_t>(nx), static_cast<int16_t>(ny), static_cast<int16_t>(nz)};
        int16_t src2[3] = {static_cast<int16_t>(dirX), static_cast<int16_t>(dirY), static_cast<int16_t>(dirZ)};
        int16_t dest = 0;
        int8_t shift = 0; // Adjust shift based on your fixed-point scaling

        // Perform dot product using DSP function
        esp_err_t result = dsps_dotprod_s16(src1, src2, &dest, 3, 1, 1, 1, shift);
        if (result != ESP_OK)
        {
            return 0; // Handle error appropriately, possibly logging or defaulting to 0
        }

        // Convert the result back to 32-bit and clamp
        int32_t dp = static_cast<int32_t>(dest);
        return (dp > 0) ? dp : 0; // Clamp to [0, ∞)
#else
        // If DSP is not enabled, fallback to manual implementation
        return dotProduct_manual(nx, ny, nz, dirX, dirY, dirZ);
#endif
    }

    /**
     * @brief Unified dot product function.
     *
     * Selects the DSP-optimized or manual implementation based on the USE_ESP_DSP define.
     *
     * @param nx Normal vector component X
     * @param ny Normal vector component Y
     * @param nz Normal vector component Z
     * @param dirX Directional light component X
     * @param dirY Directional light component Y
     * @param dirZ Directional light component Z
     * @return int32_t Clamped dot product result
     */
    inline int32_t dotProduct(int32_t nx, int32_t ny, int32_t nz,
                              int32_t dirX, int32_t dirY, int32_t dirZ)
    {
#if defined(USE_ESP_DSP) && USE_ESP_DSP == 1
        return dotProduct_dsp(nx, ny, nz, dirX, dirY, dirZ);
#else
        return dotProduct_manual(nx, ny, nz, dirX, dirY, dirZ);
#endif
    }

} // namespace FastMath

#endif // FAST_MATH_HPP
