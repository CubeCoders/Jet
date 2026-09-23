#include "PostFX.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>
using Renderer::PostFX;
static uint16_t expected(uint16_t p,int intensity) {
 const double factor=(255-intensity)/255.0;
 return uint16_t((int(((p>>11)&31)*factor+.5)<<11)|(int(((p>>5)&63)*factor+.5)<<5)|int((p&31)*factor+.5));
}
int main() {
 // Odd output heights exercise the shorter odd field's last-row boundary.
 for(int height:{8,9})for(bool interlaced:{false,true})for(bool odd:{false,true})for(int intensity:{0,1,48,112,254,255}) {
  if(FIELD_BUFFERS && !interlaced)continue;
  constexpr int width=16,guard=32;const int stride=HALF_WIDTH_BUFFERS?width/2:width;
  const int rows=FIELD_BUFFERS?(height+(odd?0:1))/2:height;
  std::vector<uint16_t> pixels(guard+rows*stride+guard,0xdead);
  for(int i=0;i<rows*stride;++i)pixels[guard+i]=uint16_t((i*1789+0xffff)&0xffff);
  const auto before=pixels;PostFX fx(width,height);
  fx.applyCRT(pixels.data()+guard,uint8_t(intensity),interlaced,odd);
  for(int i=0;i<rows*stride;++i) {
   const int physicalY=FIELD_BUFFERS?2*(i/stride)+int(odd):i/stride;
   const bool touched=POSTFX_CRT && (physicalY&1) && (!interlaced||odd);
   assert(pixels[guard+i]==(touched?expected(before[guard+i],intensity):before[guard+i]));
  }
  assert(std::equal(pixels.begin(),pixels.begin()+guard,before.begin()));
  assert(std::equal(pixels.end()-guard,pixels.end(),before.end()-guard));
 }
 // The original single-argument entry point remains valid for full-height frames.
 if(!FIELD_BUFFERS) {
  const int stride=HALF_WIDTH_BUFFERS?8:16;std::vector<uint16_t> pixels(stride*8,0xffff);PostFX fx(16,8);fx.applyCRT(pixels.data());
  for(int y=0;y<8;++y)assert(pixels[y*stride]==((POSTFX_CRT&&(y&1))?expected(0xffff,CRT_SCANLINE_INTENSITY):0xffff));
 }
 std::puts("CRT: physical parity, intensity, compact layouts and guards pass");
}
