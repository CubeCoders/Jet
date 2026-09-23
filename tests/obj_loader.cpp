#include "ObjLoader.h"
#include <cassert>
int main() {
 using namespace Renderer;
 uint16_t pixels[]={0xffff};Texture unnamed(1,1,pixels),named(1,1,pixels);
 char name[]="a b.png";named.name=name;
 std::vector<Texture*> textures{&unnamed,&named};std::vector<Material*> mats;
 Loader::LoadMtlData("newmtl red\nKd 1 0 .5\nd .5\nmap_Kd a b.png\nnewmtl missing\nmap_Kd absent.png\n",&mats,&textures);
 assert(mats.size()==2 && mats[0]->color==0xf80f && mats[0]->alpha==127);
 assert(mats[0]->diffuseMap==&named && mats[1]->diffuseMap==nullptr);
 std::vector<Material*> noTextures;
 Loader::LoadMtlData("newmtl fallback\nmap_Kd absent.png\nKd 0 1 0\n",&noTextures,nullptr);
 assert(noTextures.size()==1 && noTextures[0]->color==0x07e0);
 Material fallback(0xffff);
 auto* obj=Loader::LoadFromObjData(
  "mtllib model with spaces.mtl\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
  "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvt .5 .5\nvn 0 0 1\n"
  "f 1/1/1 2/2/1 3/3/1 4/4/1\nusemtl red\nf 1/5/1 2/2/1 3/3/1\n"
  "f 99/1/1 2/2/1 3/3/1\n",&fallback,&mats,2.f);
 assert(obj->triangles.size()==3 && obj->vertices.size()==5);
 assert(obj->triangles[0].material==&fallback && obj->triangles[2].material==mats[0]);
 assert(obj->vertices[1].position.x==256 && obj->vertices[4].uv.x==512);
 assert(obj->vertices[0].normal.z==1024);delete obj;
 for(auto* m:mats){std::free(m->name);delete m;}
 for(auto* m:noTextures){std::free(m->name);delete m;}
}
