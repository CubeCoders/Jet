#include "FastMath.hpp"
#include <cassert>
#include <cstdio>
#include <cmath>

int main() {
    unsigned checks=0;
    auto check=[&](int64_t value) {
        const int64_t expected=value>0?int64_t(std::sqrt(double(value))):0;
        assert(FastMath::approximateSqrt(value)==expected);
        ++checks;
    };
    check(-1);
    for(int64_t n=0;n<=3*1024*1024;++n) check(n);
    for(int64_t root=1;root<=65536;++root)
        for(int offset=-1;offset<=1;++offset) check(root*root+offset);
    uint64_t random=0x12345678;
    for(int i=0;i<100000;++i) {
        random=random*6364136223846793005ULL+1442695040888963407ULL;
        check(uint32_t(random));
        check(int64_t(random>>1));
    }
    std::printf("Square root: %u exact comparisons, normal range and 32/64-bit boundaries pass\n",checks);
}
