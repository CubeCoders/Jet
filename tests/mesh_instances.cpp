#include "Scene.hpp"
#include <algorithm>
#include <cstdio>
#include <vector>
#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
using namespace Renderer;

namespace {
// Independent expansion of signed-axis rotations into ordinary authoring
// vertices. The renderer must match it without modifying any prototype.
Object expand(const Object& batch) {
    Object out=batch;
    out.instances.clear();
    for (const auto& instance:batch.instances) {
        const uint16_t base=(uint16_t)out.vertices.size();
        const auto& t=instance.transform;
        auto rotate=[&](const Vector3& p) {
            return Vector3{(int32_t)(t.basis[0]*p.x+t.basis[1]*p.y+t.basis[2]*p.z),
                           (int32_t)(t.basis[3]*p.x+t.basis[4]*p.y+t.basis[5]*p.z),
                           (int32_t)(t.basis[6]*p.x+t.basis[7]*p.y+t.basis[8]*p.z)};
        };
        for (auto vertex:instance.mesh->vertices) {
            vertex.position=rotate(vertex.position)+t.position;
            vertex.normal=rotate(vertex.normal);
            out.addVertex(vertex);
        }
        size_t triangleIndex=0;
        for (auto triangle:instance.mesh->triangles) {
            triangle.v1+=base; triangle.v2+=base; triangle.v3+=base;
            auto* replacement=instance.materialOverride ? instance.materialOverride :
                (instance.triangleMaterials ? (*instance.triangleMaterials)[triangleIndex] : nullptr);
            if (replacement) { triangle.material=replacement; triangle.colorBaked=false; }
            out.triangles.push_back(triangle);
            ++triangleIndex;
        }
    }
    out.calculateBoundingBox();
    return out;
}
}

int runMeshInstanceChecks() {
    constexpr int W=160,H=120;
    const int pixels=(W/(HALF_WIDTH_BUFFERS?2:1))*(H/(FIELD_BUFFERS?2:1));
    std::vector<uint16_t> storage(pixels+8), z(pixels,65535);
    auto* fb=(uint16_t*)((uintptr_t(storage.data())+15)&~uintptr_t(15));
    uint16_t texels[16]; for(int i=0;i<16;++i) texels[i]=(uint16_t)(0x1234+i*1771);
    Texture texture(4,4,texels);
    Material material(0x4e9f,&texture), alternate(0xf8a4,&texture);
    Object authored;
    for (int face=0;face<4;++face) {
        const int y=face*60-90;
        authored.addVertex({{-120,y,-80},{-1024,0},{0,0,-1024}});
        authored.addVertex({{0,y+120,40},{1024,2048},{0,0,-1024}});
        authored.addVertex({{120,y,120},{2048,-1024},{0,0,-1024}});
        authored.addTriangle(face*3,face*3+1,face*3+2,&material);
    }
    authored.triangles[1].colorBaked=true;
    authored.triangles[1].bakedColor=0x07e0;
    auto mesh=Object::freezeMesh(std::move(authored));
    if (!mesh || !mesh->cachedPositions()) return 1;
    const auto original=mesh->triangles;
    Object batch;
    auto triangleMaterials=std::make_shared<const std::vector<Material*>>(
        std::vector<Material*>{&alternate,nullptr,&material,&alternate});
    if (batch.addInstance(mesh,{},nullptr,
        std::make_shared<const std::vector<Material*>>(1,&alternate))) return 1;
    // A batch can retain unique geometry alongside its repeated meshes.
    batch.addVertex({{-100,-240,500},{0,0},{0,0,-1024}});
    batch.addVertex({{0,-120,500},{1024,1024},{0,0,-1024}});
    batch.addVertex({{100,-240,500},{2048,0},{0,0,-1024}});
    batch.addTriangle(0,1,2,&material);
    for (int i=0;i<12;++i) {
        auto t=Object::InstanceTransform::rotated({(i%3)*90,0,(i%4)*90},{(i%3-1)*280,(i/3-2)*160,100+i*130});
        if (!batch.addInstance(mesh,t,i%3==0?&alternate:nullptr,i%2 ? triangleMaterials : nullptr)) return 1;
    }
    batch.calculateBoundingBox();
    Object flat=expand(batch);
    if (batch.vertexCount()!=flat.vertices.size() || batch.triangleCount()!=flat.triangles.size()) return 1;
    Object lod=batch; lod.instances.resize(3); lod.calculateBoundingBox();
    Object flatLod=expand(lod);
    batch.lodMeshes.push_back(&lod); flat.lodMeshes.push_back(&flatLod);
    Camera camera; camera.nearPlane=100; camera.farPlane=1500; camera.setFOV(70.0f,W);
    DirectionalLight light({20,40,0},{255,255,255}); AmbientLight ambient({90,90,90});
    Scene scene(fb,z.data(),W,H); scene.setCamera(&camera);
    scene.setDirectionalLight(&light); scene.setAmbientLight(&ambient);
    scene.addObject(&flat); scene.addObject(&batch);
#if SORT_TRIANGLES
    // SORT_TRIANGLES sorts within each submitted mesh. Independent ordinary
    // Objects provide the matching reference for independently sorted instances.
    std::vector<Object> separate;
    separate.reserve(batch.instances.size()+1);
    separate.push_back(batch);separate.back().instances.clear();
    scene.addObject(&separate.back());
    for (const auto& instance:batch.instances) {
        Object one;one.instances.push_back(instance);
        separate.push_back(expand(one));
        scene.addObject(&separate.back());
    }
#endif
    unsigned submitted[2]={0,0};
    for (int frame=0;frame<144;++frame) {
        const bool billboard=frame>=72;
        batch.isBillboard=flat.isBillboard=billboard;
        camera.setRotation(0,billboard?90:0,0);
        material.shadingMode=alternate.shadingMode=(ShadingMode)(frame%3);
        material.specular=alternate.specular=frame%2?32:0;
        scene.lodScale=frame%3==0?400:0;
        const Vector3 position=billboard ? Vector3{800,0,(frame%6)*20} : Vector3{0,0,(frame%6)*70-180};
        batch.position=flat.position=position;
        batch.rotation=flat.rotation={0,0,(frame%4)*90};
        batch.cullingMode=flat.cullingMode=(CullingMode)(frame%3);
        batch.fadeNear=flat.fadeNear=frame%2?500:0;
        batch.fadeFar=flat.fadeFar=frame%2?2000:0;
        flat.enabled=true; batch.enabled=false;
#if SORT_TRIANGLES
        flat.enabled=false;
        scene.lodScale=0;
        for (size_t i=0;i<separate.size();++i) {
            auto& part=separate[i];
            part.isBillboard=billboard;
            part.position=flat.position;part.rotation=flat.rotation;
            part.cullingMode=flat.cullingMode;part.fadeNear=flat.fadeNear;part.fadeFar=flat.fadeFar;
            part.boundingBoxMin=flat.boundingBoxMin;part.boundingBoxMax=flat.boundingBoxMax;part.centreVolume=flat.centreVolume;
            part.enabled=frame%3!=0 || i<=3;
        }
#endif
        std::fill(fb,fb+pixels,0);std::fill(z.begin(),z.end(),65535);
        scene.prepareFrame(); scene.rasterizeBand(0,H);
        const std::vector<uint16_t> expected(fb,fb+pixels);
        const int count=scene.lastFrameDrawnTriangles;
        submitted[billboard]+=(unsigned)count;
        flat.enabled=false; batch.enabled=true;
#if SORT_TRIANGLES
        for (auto& part:separate) part.enabled=false;
        scene.lodScale=frame%3==0?400:0;
#endif
        std::fill(fb,fb+pixels,0);std::fill(z.begin(),z.end(),65535);
        scene.prepareFrame(); scene.rasterizeBand(0,H);
        if (count!=scene.lastFrameDrawnTriangles || !std::equal(expected.begin(),expected.end(),fb)) {
            unsigned differences=0;for(int i=0;i<pixels;++i) differences+=expected[i]!=fb[i];
            std::printf("Instance mismatch frame %d: triangles %d/%d pixels %u\n",frame,count,scene.lastFrameDrawnTriangles,differences);return 1;
        }
#if defined(ESP_PLATFORM)
        vTaskDelay(1);
#endif
    }
    for (unsigned count:submitted)
        if (count<100) { std::printf("Instance fixture did not draw enough geometry: %u triangles\n",count); return 1; }
    for (size_t i=0;i<original.size();++i)
        if (mesh->triangles[i].v1!=original[i].v1 || mesh->triangles[i].v2!=original[i].v2 || mesh->triangles[i].v3!=original[i].v3) return 1;
#if MAX_PICK_QUERIES > 0
    {
        camera.rotation={0,0,0};
        Object pickMesh;
        pickMesh.addVertex({{-200,-200,0}});pickMesh.addVertex({{0,200,0}});pickMesh.addVertex({{200,-200,0}});
        pickMesh.addTriangle(0,1,2,&alternate);
        auto prototype=Object::freezeMesh(std::move(pickMesh));
        Object picked;
        picked.cullingMode=CullingMode::NO_CULLING;
        picked.addInstance(prototype,Object::InstanceTransform::rotated({0,0,0},{-2000,0,500}));
        picked.addInstance(prototype,Object::InstanceTransform::rotated({0,0,0},{0,0,500}));
        picked.calculateBoundingBox();
        Scene picking(fb,z.data(),W,H);picking.setCamera(&camera);picking.addObject(&picked);
        PickQuery query{W/2,H/2};picking.setPickQueries(&query,1);
        // Exercise picking without running the configuration's post-effects
        // against this compact field buffer.
        picking.prepareFrame();picking.rasterizeBand(0,H);
        const auto& hit=picking.getPickResults()[0];
        if (!hit.hit || hit.object!=&picked || hit.mesh!=prototype.get() || hit.instanceIndex!=1 || hit.triangleIndex!=0) {
            std::printf("Instance pick attribution failed\n");return 1;
        }
    }
#endif
    {
        // Baking a batch must preserve its placement, share each newly scaled
        // prototype, and leave the original prototype untouched.
        Object baked=batch, reference=expand(batch);
        baked.rotation=reference.rotation={90,180,270};
        baked.bakeRotation();reference.bakeRotation();
        baked.bakeScale(2,1);reference.bakeScale(2,1);
        Object expanded=expand(baked);
        if (expanded.vertices.size()!=reference.vertices.size() ||
            baked.instances[0].mesh==mesh || baked.instances[0].mesh!=baked.instances[1].mesh) return 1;
        for (size_t i=0;i<expanded.vertices.size();++i) {
            const auto& a=expanded.vertices[i];const auto& b=reference.vertices[i];
            if (a.position.x!=b.position.x || a.position.y!=b.position.y || a.position.z!=b.position.z ||
                a.normal.x!=b.normal.x || a.normal.y!=b.normal.y || a.normal.z!=b.normal.z) {
                std::printf("Instance baking differs at vertex %u\n",(unsigned)i);return 1;
            }
        }
    }
    Object copy=batch;
    copy.instances[0].transform.position.x+=1000;
    if (copy.instances[0].transform.position.x==batch.instances[0].transform.position.x) return 1;
    if (Object::freezeMesh(std::move(copy))) return 1; // Reject nesting without consuming the input.
    std::weak_ptr<const std::vector<Material*>> tableLifetime=triangleMaterials;
    triangleMaterials.reset();
    if (tableLifetime.expired()) return 1;
    std::weak_ptr<const Object> lifetime=mesh;
    mesh.reset();
    if (lifetime.expired()) return 1;
    batch.instances.clear();lod.instances.clear();copy.instances.clear();
    if (!lifetime.expired() || !tableLifetime.expired()) return 1;
    std::printf("Mesh instances: 144 expanded-mesh frames, picking, baking, immutable geometry, copy/lifetime checks passed\n");
    return 0;
}
#ifndef JET_MESH_INSTANCES_EMBEDDED
int main() { return runMeshInstanceChecks(); }
#endif
