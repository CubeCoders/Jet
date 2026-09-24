#include "../src/DepthBuckets.hpp"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdio>
static uint32_t state=0xcea49b13;
static uint32_t next(){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
static uint64_t checks=0;
template<unsigned Count> void exercise(){
    Renderer::DepthBuckets<Count> buckets;
    auto check=[&](int32_t d,int32_t r){
        const int32_t q=int32_t(int64_t(d)*Count/std::max(r,1));
        const unsigned expected=unsigned(std::clamp<int32_t>(q,0,Count-1));
        assert(buckets.index(d)==expected);++checks;
    };
    for(int32_t r=-2;r<=4096;++r){
        buckets.setRange(r);
        for(int32_t d=-2;d<=r+2;++d)check(d,r);
    }
    for(int bit=0;bit<31;++bit)for(int offset=-3;offset<=3;++offset){
        const int64_t wide=(int64_t(1)<<bit)+offset;
        if(wide<=0 || wide>INT_MAX)continue;
        const int32_t r=int32_t(wide);buckets.setRange(r);
        for(unsigned b=0;b<=Count;++b)for(int adjacent=-2;adjacent<=2;++adjacent){
            const int64_t d=int64_t(b)*r/Count+adjacent;
            if(d>=INT_MIN && d<=INT_MAX)check(int32_t(d),r);
        }
    }
    for(int i=0;i<300000;++i){
        const int32_t r=1+int32_t(next()%uint32_t(INT_MAX));buckets.setRange(r);
        check(int32_t(next()),r);check(r-1,r);check(r,r);check(INT_MAX,r);check(INT_MIN,r);
    }
}
int main(){
    exercise<1>();exercise<2>();exercise<3>();exercise<7>();exercise<63>();
    exercise<64>();exercise<127>();exercise<128>();exercise<253>();exercise<254>();
    std::printf("Exact depth buckets: %llu comparisons passed\n",(unsigned long long)checks);
}
