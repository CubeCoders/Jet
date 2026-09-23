#include "Renderer.hpp"
#include <cassert>
#include <cstdio>
#include <algorithm>
#include <vector>
using namespace Renderer;
int main() {
 static_assert(HALF_WIDTH_BUFFERS && FIELD_BUFFERS && !Z_BUFFERING && FAST_Z);
 constexpr int w=64,h=64,n=w*h/4;
 std::vector<uint16_t> pixels(n+16,0xbeef),reference(n);
 Camera camera;camera.nearPlane=1;camera.farPlane=2000;
 Rasterizer raster(pixels.data(),w,h,nullptr,&camera);raster.interlacedMode=true;
 Material mat(0x39db);mat.shadingMode=ShadingMode::UNLIT;
 uint32_t seed=9131;auto next=[&](){seed=seed*1664525+1013904223;return int(seed%1024)-480;};
 for(int i=0;i<600;++i) {
  RenderVertex a,b,c;a.position={next(),next(),500};b.position={next(),next(),500};c.position={next(),next(),500};
  auto area=[&](){return int64_t(b.position.y-c.position.y)*(a.position.x-c.position.x)+int64_t(c.position.x-b.position.x)*(a.position.y-c.position.y);};
  if(area()<0)std::swap(b,c);if(area()==0)continue;
  // Preserve the rasterizer's even-aligned bounding box, including odd apex exclusion.
  const int maxY=std::max({a.position.y,b.position.y,c.position.y}) & ~1;
  const int minY=std::min({a.position.y,b.position.y,c.position.y}) & ~1;
  const int minX=std::min({a.position.x,b.position.x,c.position.x}) & ~1;
  const int maxX=std::max({a.position.x,b.position.x,c.position.x}) & ~1;
  for(int parity=0;parity<2;++parity) {
   std::fill(pixels.begin(),pixels.begin()+n,0);std::fill(reference.begin(),reference.end(),0);
   raster.drawTriangle(a,b,c,&mat,nullptr,nullptr,parity,false,false,0);
   for(int y=parity;y<h;y+=2)for(int x=0;x<w;x+=2) {
    int64_t e0=int64_t(b.position.y-c.position.y)*(x-c.position.x)+int64_t(c.position.x-b.position.x)*(y-c.position.y);
    int64_t e1=int64_t(c.position.y-a.position.y)*(x-c.position.x)+int64_t(a.position.x-c.position.x)*(y-c.position.y);
    int64_t e2=area()-e0-e1;
    if(y<=maxY && e0>=0&&e1>=0&&e2>=0
#if SKIP_ZERO_AREA_TRIANGLES
       && minX!=maxX && minY!=maxY
#endif
    )reference[(y/2)*(w/2)+x/2]=mat.color;
   }
   if(!std::equal(reference.begin(),reference.end(),pixels.begin())) { for(int k=0;k<n;++k)if(reference[k]!=pixels[k]) { std::printf("Mismatch case %d parity %d pixel %d expected %x got %x A %d,%d B %d,%d C %d,%d\n",i,parity,k,reference[k],pixels[k],a.position.x,a.position.y,b.position.x,b.position.y,c.position.x,c.position.y); return 1; } }
   assert(std::all_of(pixels.begin()+n,pixels.end(),[](auto p){return p==0xbeef;}));
  }
 }
}
