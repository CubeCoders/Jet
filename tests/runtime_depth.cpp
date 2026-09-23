#include "Scene.hpp"
#include <algorithm>
#include <cassert>
#include <vector>
using namespace Renderer;
int main() {
    static_assert(Z_BUFFERING && JET_RUNTIME_DEPTH && !FAST_Z);
    constexpr int w=64,h=64,stride=ZBUFFER_STRIDE(w),rows=FIELD_BUFFERS?h/2:h;
    std::vector<uint16_t> color(stride*rows+16,0xbeef),depth(stride*h+16,0x1234);
    Camera camera;camera.nearPlane=1;camera.farPlane=2000;
    Scene scene(color.data(),depth.data(),w,h);scene.setCamera(&camera);
    scene.getRenderer()->interlacedMode=true;
    // Clearing obeys the runtime switch, and resuming does not reuse stale Z.
    scene.getRenderer()->setDepthTestingEnabled(false);scene.render();
    assert(std::all_of(depth.begin(),depth.end(),[](auto p){return p==0x1234;}));
    scene.getRenderer()->setDepthTestingEnabled(true);scene.render();
    assert(std::find(depth.begin(),depth.end(),0xffff)!=depth.end());
    Rasterizer raster(color.data(),w,h,nullptr,&camera);raster.interlacedMode=true;
    raster.setDepthTestingEnabled(false);
    Material red(0xf800),blue(0x001f);
    red.shadingMode=blue.shadingMode=ShadingMode::UNLIT;
    auto draw=[&](int z,Material* m,int parity) {
        RenderVertex a,b,c;a.position={2,2,z};b.position={62,2,z};c.position={2,62,z};
        return raster.drawTriangle(a,b,c,m,nullptr,nullptr,parity,false,false,0);
    };
    for(int parity=0;parity<2;++parity) {
        assert(draw(100,&red,parity));assert(draw(500,&blue,parity));
        assert(color[(FIELD_BUFFERS?(10+parity)/2:10+parity)*stride+(HALF_WIDTH_BUFFERS?5:10)]==0x001f);
        const auto before=color;
        assert(!draw(3000,&red,parity));assert(!draw(-100,&red,parity));
        assert(color==before); // Null depth and triangle-level near/far clipping.
    }
    assert(std::all_of(color.begin()+stride*rows,color.end(),[](auto p){return p==0xbeef;}));
    assert(std::all_of(depth.begin()+stride*h,depth.end(),[](auto p){return p==0x1234;}));
}
