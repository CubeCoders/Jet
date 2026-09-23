#include "EnvironmentMapping.hpp"
#include <cassert>
using namespace Renderer;
int main() {
 auto center=environmentReflectionUV({0,0,0},{0,0,1024},{0,0,100});
 assert(center.x==512 && center.y==512);
 auto left=environmentReflectionUV({0,0,0},{0,0,1024},{100,0,100});
 auto right=environmentReflectionUV({0,0,0},{0,0,1024},{-100,0,100});
 assert(left.x==384 && right.x==640);
 auto up=environmentReflectionUV({0,0,0},{0,1024,0},{0,100,0});
 assert(up.y==0);
 auto same=environmentReflectionUV({100,20,-30},{0,0,17},{100,20,70});
 assert(same.x==center.x && same.y==center.y);
 auto zero=environmentReflectionUV({0,0,0},{0,0,0},{100,0,0});
 assert(zero.x==512 && zero.y==512);
 assert(unwrapEnvironmentU(10,1010)==1034);
 assert(unwrapEnvironmentU(1010,10)==-14);
 for(int i=0;i<360;++i) {
  float a=i*3.14159265359f/180;
  auto uv=environmentReflectionUV({0,0,0},{0,0,1024},{int(1000*std::sin(a)),250,int(1000*std::cos(a))});
  assert(uv.x>=0 && uv.x<=1024 && uv.y>=0 && uv.y<1024);
 }
}
