#include "PostFX.hpp"
#include <algorithm>
#include <cstring>
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#include "esp_attr.h"
#endif

namespace Renderer {

#if POSTFX_CRT
namespace {
inline uint16_t crtPixel(uint16_t pixel, unsigned gain) {
    const unsigned r = (((pixel >> 11) & 31) * gain + 127) / 255;
    const unsigned g = (((pixel >> 5) & 63) * gain + 127) / 255;
    const unsigned b = ((pixel & 31) * gain + 127) / 255;
    return uint16_t((r << 11) | (g << 5) | b);
}

#if defined(CONFIG_IDF_TARGET_ESP32S3)
alignas(16) static const DRAM_ATTR uint32_t crtConstants[] = {
    0x001f001f, 0x001f001f, 0x001f001f, 0x001f001f,
    0x003f003f, 0x003f003f, 0x003f003f, 0x003f003f,
    0x007f007f, 0x007f007f, 0x007f007f, 0x007f007f,
    0x80818081, 0x80818081, 0x80818081, 0x80818081
};
// Keep the hardware loop outside any compiler-generated outer row loop.
static void IRAM_ATTR __attribute__((noinline)) crtSpan(
#else
static void crtSpan(
#endif
    uint16_t* pixels, int count, unsigned gain) {
    if (count <= 0) return;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    while (count && (uintptr_t(pixels) & 15)) {
        *pixels = crtPixel(*pixels, gain);
        ++pixels;
        --count;
    }
    const int blocks = count / 8;
    if (blocks) {
        const uint32_t gainLanes = gain | (gain << 16);
        const uint32_t* gainLanesPointer = &gainLanes;
        const uint32_t* constants = crtConstants;
        uint32_t savedSar, tmp;
        // q0=input, q1/q2=channel masks, q3=gain, q4=127, q5=32897,
        // q6=channel scratch, q7=output. Every load/store is aligned and
        // wholly inside the span. Saturated additions stay <=16192.
        // floor(v/255) == (v*32897)>>23 for these non-negative 16-bit v.
        __asm__ volatile (
            "rsr %[saved], sar\n\t"
            "ee.vld.128.ip q1, %[constants], 16\n\t"
            "ee.vld.128.ip q2, %[constants], 16\n\t"
            "ee.vld.128.ip q4, %[constants], 16\n\t"
            "ee.vld.128.ip q5, %[constants], 0\n\t"
            "ee.vldbc.32 q3, %[gain]\n\t"
            "loopgtz %[blocks], 1f\n\t"
            "movi %[tmp], 0\n\twsr %[tmp], sar\n\t"
            "ee.vld.128.ip q0, %[pixels], 0\n\t"
            "ee.andq q7, q0, q1\n\t"
            "ee.vmul.u16 q7, q7, q3\n\t"
            "movi %[tmp], 23\n\twsr %[tmp], sar\n\t"
            "ee.vadds.s16 q7, q7, q4\n\t"
            "ee.vmul.u16 q7, q7, q5\n\t"
            "movi %[tmp], 5\n\twsr %[tmp], sar\n\tnop\n\t"
            "ee.vsr.32 q6, q0\n\t"
            "movi %[tmp], 0\n\twsr %[tmp], sar\n\t"
            "ee.andq q6, q6, q2\n\t"
            "ee.vmul.u16 q6, q6, q3\n\t"
            "movi %[tmp], 23\n\twsr %[tmp], sar\n\t"
            "ee.vadds.s16 q6, q6, q4\n\t"
            "ee.vmul.u16 q6, q6, q5\n\t"
            "movi %[tmp], 5\n\twsr %[tmp], sar\n\tnop\n\t"
            "ee.vsl.32 q6, q6\n\t"
            "movi %[tmp], 11\n\twsr %[tmp], sar\n\t"
            "ee.orq q7, q7, q6\n\t"
            "ee.vsr.32 q6, q0\n\t"
            "movi %[tmp], 0\n\twsr %[tmp], sar\n\t"
            "ee.andq q6, q6, q1\n\t"
            "ee.vmul.u16 q6, q6, q3\n\t"
            "movi %[tmp], 23\n\twsr %[tmp], sar\n\t"
            "ee.vadds.s16 q6, q6, q4\n\t"
            "ee.vmul.u16 q6, q6, q5\n\t"
            "movi %[tmp], 11\n\twsr %[tmp], sar\n\tnop\n\t"
            "ee.vsl.32 q6, q6\n\t"
            "ee.orq q7, q7, q6\n\t"
            "ee.vst.128.ip q7, %[pixels], 16\n\t"
            "1:\n\twsr %[saved], sar\n\t"
            : [pixels] "+&r"(pixels), [constants] "+&r"(constants),
              [gain] "+&r"(gainLanesPointer), [saved] "=&r"(savedSar),
              [tmp] "=&r"(tmp)
            : [blocks] "r"(blocks)
            : "memory"
        );
        count %= 8;
    }
#endif
    while (count-- > 0) {
        *pixels = crtPixel(*pixels, gain);
        ++pixels;
    }
}
} // namespace
#endif

PostFX::PostFX(int screenWidth, int screenHeight)
    : screenWidth(screenWidth), screenHeight(screenHeight) {
    #if POSTFX_BLOOM
    bloomBuffer = new uint16_t[screenWidth * screenHeight];
    bloomTemp   = new uint16_t[screenWidth * screenHeight];
    #endif
    #if POSTFX_MOTION_BLUR
    previousFrame = new uint16_t[screenWidth * screenHeight];
    memset(previousFrame, 0, screenWidth * screenHeight * sizeof(uint16_t));
    #endif
    #if POSTFX_ANTIALIASING
    // 4 int16s per column: r, g, b, lum. Reused across rows so each
    // framebuffer pixel is unpacked exactly once per FXAA pass.
    fxaaTopRow = new int16_t[screenWidth * 4];
    #endif
}

PostFX::~PostFX() {
    #if POSTFX_BLOOM
    if (bloomBuffer) {
        delete[] bloomBuffer;
        bloomBuffer = nullptr;
    }
    if (bloomTemp) {
        delete[] bloomTemp;
        bloomTemp = nullptr;
    }
    #endif
    #if POSTFX_MOTION_BLUR
    if (previousFrame) {
        delete[] previousFrame;
        previousFrame = nullptr;
    }
    #endif
    #if POSTFX_ANTIALIASING
    if (fxaaTopRow) {
        delete[] fxaaTopRow;
        fxaaTopRow = nullptr;
    }
    #endif
}

void PostFX::applyFXAA(uint16_t* framebuffer) {
    #if POSTFX_ANTIALIASING
    const int W = screenWidth;
    const int H = screenHeight;
    if (W < 3 || H < 3) return;

    // Prime the top-row cache from row 0 (never modified by FXAA since
    // the loop starts at y=1). We only need columns [1..W-2] but filling
    // the whole row is simpler and harmless.
    {
        const uint16_t* row0 = framebuffer;
        int16_t* cache = fxaaTopRow;
        for (int x = 0; x < W; x++) {
            const uint16_t p = row0[x];
            const int r = (p >> 11) & 0x1F;
            const int g = (p >> 5)  & 0x3F;
            const int b =  p        & 0x1F;
            cache[x * 4 + 0] = (int16_t)r;
            cache[x * 4 + 1] = (int16_t)g;
            cache[x * 4 + 2] = (int16_t)b;
            cache[x * 4 + 3] = (int16_t)(r + g + b);
        }
    }

    for (int y = 1; y < H - 1; y++) {
        uint16_t*       rowC = framebuffer + y * W;
        const uint16_t* rowB = rowC + W;

        // Prime the horizontal sliding window from rowC[0] / rowC[1].
        uint16_t pL = rowC[0];
        int rL = (pL >> 11) & 0x1F;
        int gL = (pL >> 5)  & 0x3F;
        int bL =  pL        & 0x1F;
        int lumL = rL + gL + bL;

        uint16_t pC = rowC[1];
        int rC = (pC >> 11) & 0x1F;
        int gC = (pC >> 5)  & 0x3F;
        int bC =  pC        & 0x1F;
        int lumC = rC + gC + bC;

        for (int x = 1; x < W - 1; x++) {
            // Top from cache (free).
            const int rT   = fxaaTopRow[x * 4 + 0];
            const int gT   = fxaaTopRow[x * 4 + 1];
            const int bT   = fxaaTopRow[x * 4 + 2];
            const int lumT = fxaaTopRow[x * 4 + 3];

            // Bottom: fresh unpack of row y+1.
            const uint16_t pB = rowB[x];
            const int rB = (pB >> 11) & 0x1F;
            const int gB = (pB >> 5)  & 0x3F;
            const int bB =  pB        & 0x1F;
            const int lumB = rB + gB + bB;

            // Right: fresh unpack of next column.
            const uint16_t pR = rowC[x + 1];
            const int rR = (pR >> 11) & 0x1F;
            const int gR = (pR >> 5)  & 0x3F;
            const int bR =  pR        & 0x1F;
            const int lumR = rR + gR + bR;

            // Min/max with early-out: as soon as mx-mn exceeds 32 we are
            // committed to averaging, so further compares are wasted.
            int mn = lumC, mx = lumC;
            bool isEdge = false;
            if (lumT < mn) mn = lumT; else if (lumT > mx) mx = lumT;
            if (mx - mn > 32) {
                isEdge = true;
            } else {
                if (lumB < mn) mn = lumB; else if (lumB > mx) mx = lumB;
                if (mx - mn > 32) {
                    isEdge = true;
                } else {
                    if (lumL < mn) mn = lumL; else if (lumL > mx) mx = lumL;
                    if (mx - mn > 32) {
                        isEdge = true;
                    } else {
                        if (lumR < mn) mn = lumR; else if (lumR > mx) mx = lumR;
                        isEdge = (mx - mn > 32);
                    }
                }
            }

            int outR, outG, outB, outLum;
            if (isEdge) {
                outR = (rC + rT + rB + rL + rR) / 5;
                outG = (gC + gT + gB + gL + gR) / 5;
                outB = (bC + bT + bB + bL + bR) / 5;
                outLum = outR + outG + outB;
                rowC[x] = (uint16_t)((outR << 11) | (outG << 5) | outB);
            } else {
                outR = rC; outG = gC; outB = bC; outLum = lumC;
            }

            // Store post-mod centre into the top-row cache for the next
            // y iteration.
            fxaaTopRow[x * 4 + 0] = (int16_t)outR;
            fxaaTopRow[x * 4 + 1] = (int16_t)outG;
            fxaaTopRow[x * 4 + 2] = (int16_t)outB;
            fxaaTopRow[x * 4 + 3] = (int16_t)outLum;

            // Slide: post-mod centre becomes left, unmodified right
            // becomes centre.
            rL = outR; gL = outG; bL = outB; lumL = outLum;
            rC = rR;   gC = gR;   bC = bR;   lumC = lumR;
        }
    }
    #endif
}

void PostFX::applyBloom(uint16_t* framebuffer) {
    #if POSTFX_BLOOM
    // Separable two-pass blur. The previous implementation splattered each
    // bright source pixel into a (2r+1)² area — O(r²) per pixel and a
    // measurable fraction of frame time at 1080p. A separable box blur with
    // a moving-window sum is O(1) per pixel per axis: total cost
    // O(2 · W · H) regardless of radius.
    //
    // Pipeline:
    //   1. Seed pass: for each framebuffer pixel, if luminance crosses the
    //      bloom threshold, write its (R,G,B)>>3 contribution into bloomTemp
    //      (zeroed otherwise). Matches the original brightness gate.
    //   2. Horizontal box blur radius=BLOOM_RADIUS over bloomTemp, output
    //      into bloomBuffer using a per-row moving 5/6/5-bit sum.
    //   3. Vertical box blur radius=BLOOM_RADIUS over bloomBuffer, output
    //      back into bloomTemp.
    //   4. Add bloomTemp to framebuffer with channel saturation.
    //
    // This produces a smoother, more uniform bloom than the original
    // additive splatter (which clipped per accumulation step) but matches
    // the same overall radius/intensity at the default settings.
    constexpr int BLOOM_RADIUS = 3;
    constexpr int BLOOM_WINDOW = BLOOM_RADIUS * 2 + 1; // 7
    const int W = screenWidth;
    const int H = screenHeight;

    // 1. Seed: bright pixels only. Storing the contribution in RGB565 keeps
    // the buffer compact; channels stay well under their max even after
    // summing the 7-pixel window (each contribution is the source channel
    // shifted right by 3, so r:5 becomes <=3, g:6 becomes <=7, b:5 becomes
    // <=3; window sum fits in 5/6/5 bits with room to spare).
    for (int i = 0; i < W * H; i++) {
        const uint16_t pixel = framebuffer[i];
        const int r = (pixel >> 11) & 0x1F;
        const int g = (pixel >> 5)  & 0x3F;
        const int b =  pixel        & 0x1F;
        // (r+g+b)/3 > 25 ≡ r+g+b > 75. Avoid the divide.
        if (r + g + b > 75) {
            bloomTemp[i] = (uint16_t)(((r >> 3) << 11) | ((g >> 3) << 5) | (b >> 3));
        } else {
            bloomTemp[i] = 0;
        }
    }

    // 2. Horizontal blur. Maintain a per-channel running sum across a
    // BLOOM_WINDOW-pixel window centred on the output pixel. Edge pixels
    // get fewer samples — we divide by the actual window size for
    // correctness, which avoids darkening at the borders.
    for (int y = 0; y < H; y++) {
        const uint16_t* row = bloomTemp   + y * W;
        uint16_t*       out = bloomBuffer + y * W;
        int sumR = 0, sumG = 0, sumB = 0;
        // Prime the window with [0..BLOOM_RADIUS-1] (left edge has fewer
        // samples; the loop body will add the new right-edge sample on the
        // first iteration).
        for (int x = 0; x < BLOOM_RADIUS && x < W; x++) {
            const uint16_t s = row[x];
            sumR += (s >> 11) & 0x1F;
            sumG += (s >> 5)  & 0x3F;
            sumB +=  s        & 0x1F;
        }
        for (int x = 0; x < W; x++) {
            const int addX = x + BLOOM_RADIUS;
            const int subX = x - BLOOM_RADIUS - 1;
            if (addX < W) {
                const uint16_t s = row[addX];
                sumR += (s >> 11) & 0x1F;
                sumG += (s >> 5)  & 0x3F;
                sumB +=  s        & 0x1F;
            }
            if (subX >= 0) {
                const uint16_t s = row[subX];
                sumR -= (s >> 11) & 0x1F;
                sumG -= (s >> 5)  & 0x3F;
                sumB -=  s        & 0x1F;
            }
            // Window size handles the truncated cases at the edges.
            const int winLeft  = (x - BLOOM_RADIUS < 0) ? 0 : (x - BLOOM_RADIUS);
            const int winRight = (x + BLOOM_RADIUS >= W) ? (W - 1) : (x + BLOOM_RADIUS);
            const int winN     = winRight - winLeft + 1;
            const int r = sumR / winN;
            const int g = sumG / winN;
            const int b = sumB / winN;
            out[x] = (uint16_t)((r << 11) | (g << 5) | b);
        }
    }

    // 3. Vertical blur into bloomTemp. Same shape as the horizontal pass
    // but column-major; we walk down each column with a strided index.
    for (int x = 0; x < W; x++) {
        int sumR = 0, sumG = 0, sumB = 0;
        for (int y = 0; y < BLOOM_RADIUS && y < H; y++) {
            const uint16_t s = bloomBuffer[y * W + x];
            sumR += (s >> 11) & 0x1F;
            sumG += (s >> 5)  & 0x3F;
            sumB +=  s        & 0x1F;
        }
        for (int y = 0; y < H; y++) {
            const int addY = y + BLOOM_RADIUS;
            const int subY = y - BLOOM_RADIUS - 1;
            if (addY < H) {
                const uint16_t s = bloomBuffer[addY * W + x];
                sumR += (s >> 11) & 0x1F;
                sumG += (s >> 5)  & 0x3F;
                sumB +=  s        & 0x1F;
            }
            if (subY >= 0) {
                const uint16_t s = bloomBuffer[subY * W + x];
                sumR -= (s >> 11) & 0x1F;
                sumG -= (s >> 5)  & 0x3F;
                sumB -=  s        & 0x1F;
            }
            const int winTop = (y - BLOOM_RADIUS < 0) ? 0 : (y - BLOOM_RADIUS);
            const int winBot = (y + BLOOM_RADIUS >= H) ? (H - 1) : (y + BLOOM_RADIUS);
            const int winN   = winBot - winTop + 1;
            const int r = sumR / winN;
            const int g = sumG / winN;
            const int b = sumB / winN;
            bloomTemp[y * W + x] = (uint16_t)((r << 11) | (g << 5) | b);
        }
    }

    // 4. Add bloom contribution to the framebuffer with per-channel
    // saturation — keeps the same look as the original.
    for (int i = 0; i < W * H; i++) {
        const uint16_t orig  = framebuffer[i];
        const uint16_t bloom = bloomTemp[i];
        int r = ((orig >> 11) & 0x1F) + ((bloom >> 11) & 0x1F);
        int g = ((orig >> 5)  & 0x3F) + ((bloom >> 5)  & 0x3F);
        int b =  (orig        & 0x1F) +  (bloom        & 0x1F);
        if (r > 0x1F) r = 0x1F;
        if (g > 0x3F) g = 0x3F;
        if (b > 0x1F) b = 0x1F;
        framebuffer[i] = (uint16_t)((r << 11) | (g << 5) | b);
    }
    #endif
}

void PostFX::applyCRT(uint16_t* framebuffer) {
    #if POSTFX_CRT
    applyCRT(framebuffer, CRT_SCANLINE_INTENSITY, FIELD_BUFFERS != 0, false);
    #endif
}

void PostFX::applyCRT(uint16_t* framebuffer, uint8_t intensity,
                      bool interlaced, bool oddRows) {
    #if POSTFX_CRT
    if (!framebuffer || intensity == 0) return;
    const int width = HALF_WIDTH_BUFFERS ? screenWidth / 2 : screenWidth;
    const bool alternating = FIELD_BUFFERS || interlaced;
    // Work in physical display coordinates. Applying stripes to packed row
    // indices instead would make pairs of dark lines and shift with the field.
    if (alternating && !oddRows) return;
    const unsigned gain = 255u - intensity;
#if FIELD_BUFFERS
    // Every stored row in an odd packed field needs the same operation.
    crtSpan(framebuffer, width * (screenHeight / 2), gain);
#else
    for (int y = 1; y < screenHeight; y += 2) {
        crtSpan(framebuffer + y * width, width, gain);
    }
#endif
    #endif
}

void PostFX::applyMotionBlur(uint16_t* framebuffer) {
    #if POSTFX_MOTION_BLUR
    for (int i = 0; i < screenWidth * screenHeight; i++) {
        uint16_t current = framebuffer[i];
        uint16_t prev = previousFrame[i];
        
        // Blend current frame with previous frame
        int r = (((current >> 11) & 0x1F) * (100 - MOTION_BLUR_STRENGTH) + 
                ((prev >> 11) & 0x1F) * MOTION_BLUR_STRENGTH) / 100;
        int g = (((current >> 5) & 0x3F) * (100 - MOTION_BLUR_STRENGTH) + 
                ((prev >> 5) & 0x3F) * MOTION_BLUR_STRENGTH) / 100;
        int b = ((current & 0x1F) * (100 - MOTION_BLUR_STRENGTH) + 
                (prev & 0x1F) * MOTION_BLUR_STRENGTH) / 100;
        
        framebuffer[i] = (r << 11) | (g << 5) | b;
    }
    
    // Store current frame for next frame's blur
    memcpy(previousFrame, framebuffer, screenWidth * screenHeight * sizeof(uint16_t));
    #endif
}

void PostFX::applyChromatic(uint16_t* framebuffer) {
    #if POSTFX_CHROMATIC
    // We'll need a temporary buffer to avoid artifacts
    uint16_t* temp = new uint16_t[screenWidth * screenHeight];
    memcpy(temp, framebuffer, screenWidth * screenHeight * sizeof(uint16_t));
    
    for (int y = 0; y < screenHeight; y++) {
        for (int x = 0; x < screenWidth; x++) {
            uint16_t pixel = temp[y * screenWidth + x];
            int r = (pixel >> 11) & 0x1F;
            int g = (pixel >> 5) & 0x3F;
            int b = pixel & 0x1F;
            
            // Offset red and blue channels
            int rx = x + CHROMATIC_OFFSET;
            int bx = x - CHROMATIC_OFFSET;
            
            if (rx < screenWidth)
                framebuffer[y * screenWidth + x] = ((r << 11) | (g << 5) | b);
            if (bx >= 0)
                framebuffer[y * screenWidth + x] |= b;
        }
    }
    
    delete[] temp;
    #endif
}

void PostFX::applyPixelate(uint16_t* framebuffer) {
    #if POSTFX_PIXELATE
    for (int y = 0; y < screenHeight; y += PIXELATE_SIZE) {
        for (int x = 0; x < screenWidth; x += PIXELATE_SIZE) {
            // Sample the center pixel of the block
            uint16_t pixel = framebuffer[y * screenWidth + x];
            
            // Fill the block with the sampled color
            for (int by = 0; by < PIXELATE_SIZE && (y + by) < screenHeight; by++) {
                for (int bx = 0; bx < PIXELATE_SIZE && (x + bx) < screenWidth; bx++) {
                    framebuffer[(y + by) * screenWidth + (x + bx)] = pixel;
                }
            }
        }
    }
    #endif
}

} // namespace Renderer
