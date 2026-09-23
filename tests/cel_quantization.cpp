#include "Renderer.hpp"
#include <cassert>
#include <algorithm>
#include <cstdio>
#include <vector>
using namespace Renderer;
int main() {
 static_assert(POSTFX_CELLSHADING && LIGHTING && !TEXTURE_MAPPING);
 constexpr int w=64,h=64,stride=w/2,count=stride*h/2;
 std::vector<uint16_t> pixels(count+16,0xdead),depth(stride*h+16,0xd00d);
 Camera camera;camera.nearPlane=1;camera.farPlane=2000;
 Rasterizer raster(pixels.data(),w,h,depth.data(),&camera);raster.interlacedMode=true;
 DirectionalLight light({0,0,0},{255,255,255},255);light.lightDir={0,0,-1024};AmbientLight ambient({0,0,0});
 Material material(0xffff);RenderVertex a,b,c;a.position={4,4,200};b.position={60,4,200};c.position={4,60,200};
 a.normal=b.normal=c.normal={0,0,-1024};
 auto render=[&](ShadingMode mode,int brightness,int bits,bool enabled) {
  std::fill(pixels.begin(),pixels.begin()+count,0);std::fill(depth.begin(),depth.begin()+stride*h,0xffff);
  material.shadingMode=mode;a.lambertBrightness=b.lambertBrightness=c.lambertBrightness=brightness;
  raster.celShadingEnabled=enabled;raster.celShadingBits=uint8_t(bits);
  assert(raster.drawTriangle(a,b,c,&material,&light,&ambient,false,false,false,0,255,true));
  assert(std::all_of(pixels.begin()+count,pixels.end(),[](auto p){return p==0xdead;}));
  assert(std::all_of(depth.begin()+stride*h,depth.end(),[](auto p){return p==0xd00d;}));
  return pixels[(12/2)*stride+12/2];
 };
 // Independent expected modulation for a white material and zero ambient.
 for(auto mode:{ShadingMode::FLAT,ShadingMode::GOURAUD})for(int brightness:{0,31,63,64,127,128,147,191,192,255})for(int bits:{0,1,4,6,8,255})for(bool enabled:{false,true}) {
  const int step=1<<(enabled?std::min(bits,8):0);const int q=(brightness/step)*step;
  const uint16_t expected=uint16_t((int(31*q/255.0+.5)<<11)|(int(63*q/255.0+.5)<<5)|int(31*q/255.0+.5));
  assert(render(mode,brightness,bits,enabled)==expected);
 }
 const auto unlit=render(ShadingMode::UNLIT,147,6,false);assert(unlit==0xffff && unlit==render(ShadingMode::UNLIT,147,6,true));
 // Constant-normal Phong must still quantise after its lighting shortcut.
 const auto smoothPhong=render(ShadingMode::PHONG,0,6,false);
 const auto bandedPhong=render(ShadingMode::PHONG,0,6,true);
 assert(smoothPhong!=bandedPhong);assert(smoothPhong==render(ShadingMode::PHONG,0,0,true));
 std::puts("Cel quantisation: exact flat/Gouraud bands, clamped controls, constant-normal Phong and unlit bypass pass");
}
