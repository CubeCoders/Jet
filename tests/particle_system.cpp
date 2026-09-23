#include "ParticleSystem.hpp"
#include <cassert>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <vector>
using namespace Renderer;
int main() {
 ParticleSystem pool(1);assert(pool.activeCount()==0);pool.emitSparks({0,0,200},{0,1,0},1000,600);assert(pool.activeCount()==200);
 const auto first=pool.pool[0];pool.emitSparks({999,0,200},{0,1,0},1000,200);assert(pool.pool[0].pos.x==first.pos.x&&pool.pool[0].life==first.life);
 pool.update(.01f);assert(std::abs(pool.pool[0].vel.y-(first.vel.y-5.88f))<.001f);
 assert(std::abs(pool.pool[0].pos.y-(first.pos.y+pool.pool[0].vel.y*.01f))<.001f);
 pool.update(10);assert(pool.activeCount()==0);pool.emitWaterSplash({0,0,200},{0,1,0},{1,0,0},{0,1,0},1000,600);assert(pool.activeCount()==200);
 for(const auto& p:pool.pool)assert(p.kind==ParticleKind::Splash && p.maxLife>0 && p.life==p.maxLife);
 constexpr int w=96,h=64,stride=w/2,count=stride*h/2;std::vector<uint16_t> pixels(count+16,0xbeef);
 Scene scene(pixels.data(),nullptr,w,h);Camera camera;camera.setFOV(58.f,w);camera.nearPlane=40;camera.farPlane=4000;
 scene.setCamera(&camera);scene.getRenderer()->interlacedMode=true;scene.setBackcolor(0x3064);scene.setClearBuffer(true);
 for(auto& p:pool.pool)p.active=false;
 auto& p=pool.pool[0];p={{0,0,200},{1000,400,0},.4f,1.f,ParticleKind::Spark,true};
 auto render=[&](bool additive,int parity) {
  pool.additiveSparks=additive;scene.frameCounter=parity;scene.render();pool.render(&scene,&camera,w,h);
  assert(std::all_of(pixels.begin()+count,pixels.end(),[](auto c){return c==0xbeef;}));return pixels;
 };
 for(int parity=0;parity<2;++parity) {
  const auto over=render(false,parity);assert(pool.lastRenderedTriangles==1);
  const auto additive=render(true,parity);assert(pool.lastRenderedTriangles==1 && over!=additive);
  p.kind=ParticleKind::Splash;const auto water=render(false,parity);assert(water==render(true,parity));p.kind=ParticleKind::Spark;
 }
 p.pos.z=2000;render(true,0);assert(pool.lastRenderedTriangles==0&&pool.activeCount()==1);
 p.pos.z=20;render(true,1);assert(pool.lastRenderedTriangles==0);
 p.pos.z=200;p.life=.001f;render(true,0);assert(pool.lastRenderedTriangles==0);
 pool.render(nullptr,&camera,w,h);assert(pool.lastRenderedTriangles==0);
 std::puts("Particles: capped pool, slot reuse, gravity, lifetime, blend modes, physical fields, culling, counters and guards pass");
}
