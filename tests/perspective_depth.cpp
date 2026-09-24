#include "Renderer.hpp"
#include <cassert>
#include <cmath>
#include <vector>
using namespace Renderer;
int main(){
 static_assert(JET_PERSPECTIVE_DEPTH && Z_BUFFERING && !FAST_Z && !HALF_WIDTH_BUFFERS);
 constexpr int w=64,h=64,guard=64;constexpr uint16_t sentinel=0xa55a;
 std::vector<uint16_t> pixels(w*h+guard*2,sentinel),depth(w*h+guard*2,sentinel);
 Camera camera;camera.nearPlane=1;camera.farPlane=60000;
 Rasterizer raster(pixels.data()+guard,w,h,depth.data()+guard,&camera);
 Material front(0xf800),flat(0x001f);front.shadingMode=flat.shadingMode=ShadingMode::UNLIT;
 for(int scale:{1,2000})for(bool reverse:{false,true}){
  std::fill(pixels.begin()+guard,pixels.end()-guard,0);
  std::fill(depth.begin()+guard,depth.end()-guard,0xffff);
  RenderVertex a,b,c;a.position={-20*scale,-20*scale,100};b.position={80*scale,-20*scale,1000};c.position={-20*scale,80*scale,1000};
  auto d=a,e=b,f=c;d.position.z=e.position.z=f.position.z=400;
  auto sloped=[&]{raster.drawTriangle(a,b,c,&front,nullptr,nullptr,false,false,false,0);};
  auto constant=[&]{raster.drawTriangle(d,e,f,&flat,nullptr,nullptr,false,false,false,0);};
  if(reverse){sloped();constant();}else{constant();sloped();}
  for(int y=4;y<24;y+=5)for(int x=4;x<24;x+=5){
   double beta=double(x+20*scale)/(100*scale),gamma=double(y+20*scale)/(100*scale);
   double z=1.0/((1-beta-gamma)/100+beta/1000+gamma/1000);
   size_t i=guard+y*w+x;assert(pixels[i]==(z<400?front.color:flat.color));
   assert(std::abs(int(depth[i])-std::min(400,int(z)))<=1);
  }
 }
 for(int i=0;i<guard;++i){assert(pixels[i]==sentinel&&pixels[guard+w*h+i]==sentinel);assert(depth[i]==sentinel&&depth[guard+w*h+i]==sentinel);}
}
