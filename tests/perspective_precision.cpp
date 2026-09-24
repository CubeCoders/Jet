#include "Renderer.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace Renderer;
int main(){
 static_assert(JET_HIGH_PRECISION_UVS && PERSPECTIVE_CORRECT_TEXTURES && !HALF_WIDTH_BUFFERS && !FIELD_BUFFERS);
 constexpr int w=64,h=64,guard=64;constexpr uint16_t sentinel=0xA55A;
 std::vector<uint16_t> frame(w*h+guard*2,sentinel),depth(w*h,0xffff),pixels(32*32);
 for(int y=0;y<32;++y)for(int x=0;x<32;++x)pixels[y*32+x]=uint16_t(x<<11|y*2<<5);
 Texture texture(32,32,pixels.data(),false,0,false,CLAMP);texture.bilinear=false;
 Material mat(0xffff,&texture);mat.shadingMode=ShadingMode::UNLIT;mat.perspectiveCorrect=true;
 Camera camera;camera.nearPlane=1;camera.farPlane=60000;
 Rasterizer raster(frame.data()+guard,w,h,depth.data(),&camera);
 for(int scale:{1,12}){
  // At scale 12 the projected area exceeds int32 as well as the original
  // reciprocal-depth products. The visible 64x64 portion is still valid.
  RenderVertex a,b,c;a.position={-9000*scale,-9000*scale,40};a.uv={-200,120};
  b.position={21000*scale,-9000*scale,30000};b.uv={2100,300};
  c.position={-9000*scale,21000*scale,12000};c.uv={200,1800};
  assert(raster.drawTriangle(a,b,c,&mat,nullptr,nullptr,false,true,true,0));
  for(int y=4;y<60;y+=11)for(int x=4;x<60;x+=11){
   double beta=double(x+9000*scale)/(30000*scale),gamma=double(y+9000*scale)/(30000*scale),alpha=1-beta-gamma;
   double z=alpha/40+beta/30000+gamma/12000;
   int u=int((alpha*a.uv.x/40+beta*b.uv.x/30000+gamma*c.uv.x/12000)/z);
   int v=int((alpha*a.uv.y/40+beta*b.uv.y/30000+gamma*c.uv.y/12000)/z);
   assert(frame[guard+y*w+x]==texture.getPixel(u,v));
  }
 }
 for(int i=0;i<guard;++i)assert(frame[i]==sentinel && frame[w*h+guard+i]==sentinel);
 std::puts("Full-width perspective UVs match analytic references and preserve buffer guards");
}
