#pragma once
#include "TileTexture.hpp"
#include <cmath>
#include <cstring>

namespace Renderer {
// For positive normal IEEE-754 floats. The exponent/mantissa seed has at most
// 12.5% relative error; three Newton steps square that error down to float
// precision. Chapel depth ratios are well inside the normal-float range.
inline float tileReciprocal(float value) {
    uint32_t bits;std::memcpy(&bits,&value,4);bits=0x7f000000u-bits;
    float estimate;std::memcpy(&estimate,&bits,4);
    estimate*=2.f-value*estimate;
    estimate*=2.f-value*estimate;
    estimate*=2.f-value*estimate;
    return estimate;
}
// Opaque perspective-correct indexed tile span, nearest, bilinear or three-point. Implemented
// once in IRAM on ESP32 so instruction fetches do not compete with textures.
void drawTiledSpan(const TileTexture& texture,uint16_t* out,int count,
                  float uq,float vq,float q,float du,float dv,float dq,
                  int feedbackLane,bool exact=false,TileFilter filter=TileFilter::Nearest);
}
