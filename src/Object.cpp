#include "Object.hpp"
#include "Light.hpp"
#include "TrigLUT.hpp"
#include <cmath>
#include <cstdlib>
#include <new>
#include <algorithm>
#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#endif

namespace Renderer
{

    Object::Object()        
    {
    }

    bool Object::cachePositions() {
        static_assert(std::is_trivially_destructible<Vector3>::value,
                      "Position cache allocation requires trivial destruction");
        invalidatePositions();
        if (vertices.empty()) return true;
        if (vertices.size() > SIZE_MAX / sizeof(Vector3)) return false;
        const size_t bytes = vertices.size() * sizeof(Vector3);
#if defined(ESP_PLATFORM) && defined(CONFIG_SPIRAM)
        void* storage = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
        void* storage = std::malloc(bytes);
#endif
        if (!storage) return false;
        auto* positions = static_cast<Vector3*>(storage);
        for (size_t i = 0; i < vertices.size(); ++i)
            new (positions + i) Vector3(vertices[i].position);
        // Vector3 is trivially destructible; release the raw allocation once
        // the last Object sharing this immutable stream lets it go.
        positionCache = std::shared_ptr<const Vector3>(positions,
            [](const Vector3* p) { std::free(const_cast<Vector3*>(p)); });
        positionCacheSize = vertices.size();
        // Preserve the public one-position-per-vertex stream. A small map
        // points duplicates at their earliest vertex, whose projection is
        // already available when Scene reaches them. Do not merge normals/UVs.
        const size_t count = vertices.size();
        if (count >= 8 && count <= size_t(UINT16_MAX) + 1) {
            auto allocateMap = [count]() -> uint16_t* {
#if defined(ESP_PLATFORM) && defined(CONFIG_SPIRAM)
                return static_cast<uint16_t*>(heap_caps_malloc(count * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
                return static_cast<uint16_t*>(std::malloc(count * sizeof(uint16_t)));
#endif
            };
            uint16_t* order = allocateMap();
            uint16_t* sources = allocateMap();
            if (!order || !sources) { std::free(order); std::free(sources); return true; }
            for (size_t i = 0; i < count; ++i) order[i] = (uint16_t)i;
            std::sort(order, order + count, [positions](uint16_t a, uint16_t b) {
                const Vector3 &pa = positions[a], &pb = positions[b];
                if (pa.x != pb.x) return pa.x < pb.x;
                if (pa.y != pb.y) return pa.y < pb.y;
                if (pa.z != pb.z) return pa.z < pb.z;
                return a < b;
            });
            size_t duplicates = 0;
            uint16_t first = order[0];
            for (size_t i = 0; i < count; ++i) {
                const uint16_t index = order[i];
                const Vector3 &p = positions[index], &q = positions[first];
                if (p.x != q.x || p.y != q.y || p.z != q.z) first = index;
                sources[index] = first;
                duplicates += first != index;
            }
            std::free(order);
            // Avoid an extra stream for meshes with little position reuse.
            if (duplicates >= count / 4)
                positionSources = std::shared_ptr<const uint16_t>(sources,
                    [](const uint16_t* p) { std::free(const_cast<uint16_t*>(p)); });
            else std::free(sources);
        }
        return true;
    }

    void Object::calculateBoundingBox() {
        invalidatePositions();
        if (vertices.empty()) {
            boundingBoxMin = {0, 0, 0};
            boundingBoxMax = {0, 0, 0};
            return;
        }

        boundingBoxMin.assign(vertices[0].position);
        boundingBoxMax.assign(vertices[0].position);

        for (const auto& vertex : vertices) {
            if (vertex.position.x < boundingBoxMin.x) boundingBoxMin.x = vertex.position.x;
            if (vertex.position.y < boundingBoxMin.y) boundingBoxMin.y = vertex.position.y;
            if (vertex.position.z < boundingBoxMin.z) boundingBoxMin.z = vertex.position.z;

            if (vertex.position.x > boundingBoxMax.x) boundingBoxMax.x = vertex.position.x;
            if (vertex.position.y > boundingBoxMax.y) boundingBoxMax.y = vertex.position.y;
            if (vertex.position.z > boundingBoxMax.z) boundingBoxMax.z = vertex.position.z;
        }

        centreVolume = (boundingBoxMin + boundingBoxMax).divide(2);
    }

    void Object::bakeRotation()
    {
        // No-op fast path: nothing to bake.
        if (rotation.x == 0 && rotation.y == 0 && rotation.z == 0) return;

        // Match the per-vertex rotation order used by Scene::renderObject
        // exactly (X then Y then Z) so the baked result is bit-identical
        // to what the renderer would have produced at the previous
        // rotation. After the bake we zero rotation and recompute the
        // bounding box.
        const int32_t cosX = lookupCosI(rotation.x);
        const int32_t sinX = lookupSinI(rotation.x);
        const int32_t cosY = lookupCosI(rotation.y);
        const int32_t sinY = lookupSinI(rotation.y);
        const int32_t cosZ = lookupCosI(rotation.z);
        const int32_t sinZ = lookupSinI(rotation.z);

        auto rotXYZ = [&](Vector3& v) {
            // X-axis
            int32_t y1 = (v.y * cosX - v.z * sinX) / FIXED_POINT_SCALE;
            int32_t z1 = (v.y * sinX + v.z * cosX) / FIXED_POINT_SCALE;
            v.y = y1; v.z = z1;
            // Y-axis
            int32_t x2 = ( v.x * cosY + v.z * sinY) / FIXED_POINT_SCALE;
            int32_t z2 = (-v.x * sinY + v.z * cosY) / FIXED_POINT_SCALE;
            v.x = x2; v.z = z2;
            // Z-axis
            int32_t x3 = (v.x * cosZ - v.y * sinZ) / FIXED_POINT_SCALE;
            int32_t y3 = (v.x * sinZ + v.y * cosZ) / FIXED_POINT_SCALE;
            v.x = x3; v.y = y3;
        };

        for (auto& vert : vertices) {
            rotXYZ(vert.position);
            rotXYZ(vert.normal);
        }

        rotation.assign(0, 0, 0);
        calculateBoundingBox();
    }

    void Object::bakeScale(int32_t numerator, int32_t denominator)
    {
        if (denominator == 0) return;            // guard against divide-by-zero
        if (numerator == denominator) return;    // identity scale: nothing to do

        for (auto& vert : vertices) {
            // 64-bit intermediates: a position component near INT32_MAX/8
            // (~2.7e8 — i.e. several worlds wide) times a numerator of
            // ~256 still fits in int64.
            vert.position.x = (int32_t)(((int64_t)vert.position.x * numerator) / denominator);
            vert.position.y = (int32_t)(((int64_t)vert.position.y * numerator) / denominator);
            vert.position.z = (int32_t)(((int64_t)vert.position.z * numerator) / denominator);
            // Normals are unit-direction vectors and must not be scaled.
        }

        calculateBoundingBox();
    }

    void Object::addVertex(const Vertex &vertex)
    {
        invalidatePositions();
        vertices.push_back(vertex);
    }

    void Object::addTriangle(uint16_t idx1, uint16_t idx2, uint16_t idx3, Material *material)
    {
        triangles.push_back({idx1, idx2, idx3, material});
    }

    void Object::addFace(uint16_t v1, uint16_t v2, uint16_t v3, uint16_t v4, Material *material)
    {
        // Add two triangles to form a face
        addTriangle(v1, v2, v3, material); // First triangle
        addTriangle(v3, v4, v1, material); // Second triangle
    }

    void Object::computeFlatNormals()
    {
        // Per-triangle face normal via cross product on int64 to survive
        // any reasonable design-unit coordinate range (positions can be
        // thousands of units pre-WORLD_SCALE; products would overflow
        // int32 on big scenery). The length is computed from those int64
        // intermediates and we then rescale to FIXED_POINT_SCALE so the
        // resulting vector matches the convention the renderer expects.
        for (const auto& tri : triangles) {
            Vertex& a = vertices[tri.v1];
            Vertex& b = vertices[tri.v2];
            Vertex& c = vertices[tri.v3];

            const int64_t ux = (int64_t)b.position.x - a.position.x;
            const int64_t uy = (int64_t)b.position.y - a.position.y;
            const int64_t uz = (int64_t)b.position.z - a.position.z;
            const int64_t vx = (int64_t)c.position.x - a.position.x;
            const int64_t vy = (int64_t)c.position.y - a.position.y;
            const int64_t vz = (int64_t)c.position.z - a.position.z;

            const int64_t nx = uy * vz - uz * vy;
            const int64_t ny = uz * vx - ux * vz;
            const int64_t nz = ux * vy - uy * vx;

            // Integer sqrt of nx² + ny² + nz². Use double here — this is a
            // one-shot setup cost per triangle at mesh-build time, not a
            // per-frame hot path, so the floating-point detour is fine.
            const double mag = std::sqrt((double)nx*nx + (double)ny*ny + (double)nz*nz);
            if (mag <= 0.0) continue;

            const double s = (double)FIXED_POINT_SCALE / mag;
            const int32_t fx = (int32_t)(nx * s);
            const int32_t fy = (int32_t)(ny * s);
            const int32_t fz = (int32_t)(nz * s);

            a.normal.x = fx; a.normal.y = fy; a.normal.z = fz;
            b.normal.x = fx; b.normal.y = fy; b.normal.z = fz;
            c.normal.x = fx; c.normal.y = fy; c.normal.z = fz;
        }
    }

    void Object::setPosition(int32_t x, int32_t y, int32_t z)
    {
        position.x = x;
        position.y = y;
        position.z = z;
    }

    void Object::setPosition(const Vector3 &pos)
    {
        position.assign(pos);
    }

    void Object::setRotation(int32_t rotX, int32_t rotY, int32_t rotZ)
    {
        rotation.x = rotX % ANGLE_MAX;
        rotation.y = rotY % ANGLE_MAX;
        rotation.z = rotZ % ANGLE_MAX;
    }

    void Object::setRotation(const Vector3 &rot)
    {
        rotation.assign(rot);
    }

    void Object::rotate(int32_t angleX, int32_t angleY, int32_t angleZ)
    {
        rotation.x = (rotation.x + angleX + ANGLE_MAX) % ANGLE_MAX;
        rotation.y = (rotation.y + angleY + ANGLE_MAX) % ANGLE_MAX;
        rotation.z = (rotation.z + angleZ + ANGLE_MAX) % ANGLE_MAX;
    }

    void Object::rotate(const Vector3 &rot)
    {
        rotation.add(rot);
        rotation.x %= ANGLE_MAX;
        rotation.y %= ANGLE_MAX;
        rotation.z %= ANGLE_MAX;
    }

    void Object::translate(int32_t dx, int32_t dy, int32_t dz)
    {
        position.x += dx;
        position.y += dy;
        position.z += dz;
    }

    void Object::lookAt(const Vector3& target)
    {
        // Calculate the direction vector from the camera to the target
        Vector3 direction = target - position;

        // Calculate the rotation angles to look at the target in 3D space
        rotation.assign(
            -static_cast<int32_t>(std::atan2(direction.y, std::sqrt(direction.x * direction.x + direction.z * direction.z)) * 180 / M_PI),
            static_cast<int32_t>(std::atan2(direction.x, direction.z) * 180 / M_PI),
            0
        );
    }

    void Object::lookAt(const Object* targetObject)
    {
        lookAt(targetObject->position);
    }
    void Object::bakeFlatLighting(const DirectionalLight* directionalLight,
                                   const AmbientLight*     ambientLight)
    {
        // Ambient components (0 if no ambient light).
        const uint32_t ambR = ambientLight ? ambientLight->color.r : 0;
        const uint32_t ambG = ambientLight ? ambientLight->color.g : 0;
        const uint32_t ambB = ambientLight ? ambientLight->color.b : 0;

        for (auto& tri : triangles) {
            tri.colorBaked = false;
            if (!tri.material) continue;
            if (tri.material->diffuseMap) continue; // textured — skip
            const ShadingMode mode = tri.material->shadingMode;
            if (mode != ShadingMode::FLAT &&
                mode != ShadingMode::GOURAUD &&
                mode != ShadingMode::UNLIT) continue;
            if (mode == ShadingMode::UNLIT ||
                (!directionalLight && !ambientLight)) {
                // Already emissive / no lights — just stamp the raw colour.
                tri.bakedColor = tri.material->color;
                tri.colorBaked = true;
                continue;
            }

            // Compute brightness from the face normal (v1 for FLAT/GOURAUD).
            // Use the same squared-falloff Lambert as jetShadeBrightness.
            uint32_t brightness = 0;
            if (directionalLight) {
                const Vector3& N = vertices[tri.v1].normal;
                const Vector3& L = directionalLight->worldLightDir;
                int64_t dot = (int64_t)N.x * L.x + (int64_t)N.y * L.y + (int64_t)N.z * L.z;
                if (dot > 0) {
                    uint32_t lambert = (uint32_t)(dot >> 12);
                    if (lambert > 255) lambert = 255;
                    lambert = (lambert * lambert + 128) >> 8;  // squared falloff
                    lambert = (lambert * (uint32_t)directionalLight->intensity) >> 8;
                    brightness = (lambert * (uint32_t)tri.material->diffuse) >> 8;
                    const uint32_t maxBrightness = 255u + tri.material->specular;
                    if (brightness > maxBrightness) brightness = maxBrightness;
                }
            }

            // Per-channel modulation (mirrors jetModulateRGB565 in Renderer.cpp).
            const uint16_t base = tri.material->color;
            const uint32_t maxBrightness = 255u + tri.material->specular;
            const uint32_t tR = std::min(brightness + ambR, maxBrightness);
            const uint32_t tG = std::min(brightness + ambG, maxBrightness);
            const uint32_t tB = std::min(brightness + ambB, maxBrightness);
            auto ch5 = [](uint32_t base5, uint32_t t) -> uint32_t {
                if (t > 255) { const uint32_t blow = t-255; return base5 + ((31u-base5)*blow)/256u; }
                const uint32_t v = base5 * t; return (v + 128u + (v>>8)) >> 8;
            };
            auto ch6 = [](uint32_t base6, uint32_t t) -> uint32_t {
                if (t > 255) { const uint32_t blow = t-255; return base6 + ((63u-base6)*blow)/256u; }
                const uint32_t v = base6 * t; return (v + 128u + (v>>8)) >> 8;
            };
            const uint32_t r = ch5((base >> 11) & 0x1Fu, tR);
            const uint32_t g = ch6((base >>  5) & 0x3Fu, tG);
            const uint32_t b = ch5( base        & 0x1Fu, tB);
            tri.bakedColor = (uint16_t)((r << 11) | (g << 5) | b);
            tri.colorBaked = true;
        }
    }

} // namespace Renderer
