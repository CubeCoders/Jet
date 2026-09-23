#include "Specular.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace Renderer;
int main() {
    const Vector3 half{0,0,-1024};
    for (int exponent : {1,2,8,32,128,255}) {
        unsigned previous=0;
        for (int cosine=0;cosine<=1024;++cosine) {
            const auto value=detail::phongSpecular({0,0,-cosine},half,uint8_t(exponent),255,255);
            const double expected=255*std::pow(cosine/1024.0,exponent);
            assert(std::abs(value-expected)<2.0);
            assert(value>=previous && value<=255);
            previous=value;
        }
    }
    // Exhaust the exact Q15 inputs around and beyond the fast lobe cutoff.
    for (int cosine=0;cosine<=32768;++cosine) {
        uint32_t power=uint32_t(cosine);
        for (int bit=0;bit<5;++bit) power=(power*power+16384)>>15;
        for (int strength : {1,32,128,255}) for (int intensity : {0,1,128,255}) {
            const auto reference=((power*strength+16384)>>15)*intensity/255;
            assert(detail::phongSpecular({cosine,0,0},{32,0,0},32,
                uint8_t(strength),uint16_t(intensity))==reference);
        }
    }
    assert(detail::phongSpecular(half,half,0,255,255)==0);
    assert(detail::phongSpecular(half,half,32,0,255)==0);
    assert(detail::phongSpecular(half,half,32,255,0)==0);
    assert(detail::phongSpecular({0,0,1024},half,32,255,255)==0);
    assert(detail::phongSpecular(half,{0,0,0},32,255,255)==0);
    assert(detail::phongSpecular(half,half,32,255,65535)==255);
    assert(detail::phongSpecular(half,half,32,255,128)==128);
    assert(detail::addSpecular(0,255,{255,255,255})==0xffff);
    assert(detail::addSpecular(0,255,{255,0,0})==0xf800);
    for (unsigned base=0;base<=65535;++base) {
        assert(detail::addSpecular(uint16_t(base),0,{255,255,255})==base);
        assert(detail::addSpecular(uint16_t(base),255,{255,255,255})==0xffff);
    }
    std::puts("Specular: fixed-point lobe agrees with reference, intensity/colour/zero cases and RGB565 saturation pass");
}
