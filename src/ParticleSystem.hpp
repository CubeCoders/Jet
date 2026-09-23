#ifndef JET_PARTICLESYSTEM_HPP
#define JET_PARTICLESYSTEM_HPP

// ParticleSystem.hpp
// -----------------------------------------------------------------------------
// Lightweight fixed-pool particle system for sparks, trails and smoke.
//
// Particles are stored in a flat array (no heap allocation per particle).
// update() integrates gravity + velocity each logic tick.
// render() transforms world-space particles to camera space and calls
// Rasterizer::drawTriangle() directly — bypassing the scene graph so sparks
// composite on top of all opaque geometry after scene->render() returns.
//
// Triangle orientation: each spark is a thin triangle whose apex points along
// the velocity direction in screen space, giving a "streak in flight" look.
//
// Color/alpha lifecycle (age = 1 - life/maxLife, 0=fresh, 1=dead):
//   0.00 → 0.25  white  0xFFFF           alpha 255
//   0.25 → 0.45  white → blue crossfade  alpha 255
//   0.45 → 0.65  blue   0x055F           alpha 255
//   0.65 → 1.00  blue                    alpha fades 255 → 0
//
// worldScale must be set at construction to match the frontend's WORLD_SCALE
// (the integer multiplier that converts authored world units to engine units).
// -----------------------------------------------------------------------------

#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <algorithm>

#include "Scene.hpp"
#include "Math.hpp"

namespace Renderer {

// ---- Particle pool constants -----------------------------------------------
static constexpr int PARTICLE_POOL_SIZE = 200;

// RGB565 colour keyframes
static constexpr uint16_t SPARK_COL_WHITE = 0xFFFF;
static constexpr uint16_t SPARK_COL_BLUE  = 0x055F;  // electric blue #00A8F8
static constexpr uint16_t SPLASH_COL_FOAM = 0xEFFF;
static constexpr uint16_t SPLASH_COL_BLUE = 0x5DDF;

enum class ParticleKind : uint8_t {
    Spark,
    Splash,
};

// ---- Single particle -------------------------------------------------------
struct Particle {
    Vec3f pos;
    Vec3f vel;
    float life;     ///< Seconds remaining.
    float maxLife;  ///< Total lifespan in seconds.
    ParticleKind kind = ParticleKind::Spark;
    bool  active;
};

// ---- ParticleSystem --------------------------------------------------------
class ParticleSystem {
public:
    /// World-scale factor matching the frontend's WORLD_SCALE. Set once at
    /// construction; used to convert authored units to engine units in emitter
    /// speed, LOD distance and streak length calculations.
    float worldScale = 1.0f;

    explicit ParticleSystem(float worldScale = 1.0f) : worldScale(worldScale) {}

    Particle pool[PARTICLE_POOL_SIZE] = {};

    /// Optional emissive additive sparks; water droplets retain alpha-over.
    /// False preserves the original compositing behaviour.
    bool additiveSparks = false;
    unsigned lastRenderedTriangles = 0; ///< Accepted triangles in the last render call.
    unsigned activeCount() const {
        unsigned count = 0;
        for (const auto& p : pool) count += p.active;
        return count;
    }


    // ---- Emitters ----------------------------------------------------------

    /// @brief Emit sparks from a ship-vs-ship or ship-vs-wall impact.
    /// @param origin    World-space impact point.
    /// @param normal    Unit vector pointing away from the surface (hemisphere bias).
    /// @param speed     Characteristic speed of the impact (world units/s).
    /// @param count     Number of particles to spawn (silently capped by pool).
    /// @param baseVel   Inherited velocity added to each spark (e.g. average ship vel).
    void emitSparks(const Vec3f& origin, const Vec3f& normal, float speed, int count,
                    const Vec3f& baseVel = Vec3f{0.0f, 0.0f, 0.0f}) {
        const float baseSpeed = 120.0f * worldScale + speed * 0.18f;
        for (int n = 0; n < count; ++n) {
            Particle* p = allocate();
            if (!p) break;

            // Random direction biased toward `normal` hemisphere + upward
            float rx = randF() - 0.5f;
            float ry = fabsf(randF()) * 0.6f + 0.25f;  // always upward
            float rz = randF() - 0.5f;
            Vec3f dir {
                normal.x * 0.7f + rx,
                normal.y * 0.5f + ry,
                normal.z * 0.7f + rz
            };
            float dlen = dir.length();
            if (dlen < 1e-4f) dlen = 1.0f;
            dir = dir * (1.0f / dlen);

            float spd = baseSpeed * (0.6f + randF() * 0.9f);
            p->vel    = dir * spd + baseVel;
            p->pos    = origin;
            // Lifespan scales with impact speed: brief shower for glancing hits,
            // longer arc for hard collisions.
            const float lifeFrac = std::min(1.0f, speed / (600.0f * worldScale));
            const float lifeMin  = 0.10f + 0.20f * lifeFrac;
            const float lifeVar  = 0.10f + 0.25f * lifeFrac;
            p->maxLife = lifeMin + randF() * lifeVar;
            p->life    = p->maxLife;
            p->kind    = ParticleKind::Spark;
            p->active  = true;
        }
    }

    /// @brief Emit a short water spray from a ship fin skimming the surface.
    /// @param origin World-space fin contact point.
    /// @param travelDir Unit-ish direction of ship travel; splash streaks align to this.
    /// @param sideDir Unit-ish vector pointing out from the ship side.
    /// @param upDir Track/local up vector for upward spray bias.
    /// @param speed Characteristic ship speed.
    /// @param count Number of particles to spawn.
    /// @param baseVel Inherited ship velocity.
    void emitWaterSplash(const Vec3f& origin, const Vec3f& travelDir,
                         const Vec3f& sideDir, const Vec3f& upDir,
                         float speed, int count,
                         const Vec3f& baseVel = Vec3f{0.0f, 0.0f, 0.0f}) {
        const float baseSpeed = 100.0f * worldScale + speed * 0.12f;
        for (int n = 0; n < count; ++n) {
            Particle* p = allocate();
            if (!p) break;

            float rx = randF() - 0.5f;
            float rz = randF() - 0.5f;
            Vec3f dir {
                travelDir.x * 0.90f + sideDir.x * 0.55f + upDir.x * 0.60f + rx * 0.25f,
                travelDir.y * 0.90f + sideDir.y * 0.55f + upDir.y * 0.60f + fabsf(randF()) * 0.30f,
                travelDir.z * 0.90f + sideDir.z * 0.55f + upDir.z * 0.60f + rz * 0.25f
            };
            float dlen = dir.length();
            if (dlen < 1e-4f) dlen = 1.0f;
            dir = dir * (1.0f / dlen);

            float spd = baseSpeed * (0.55f + randF() * 0.75f);
            p->vel    = dir * spd + baseVel * 0.25f;
            p->pos    = origin;
            p->maxLife = 0.12f + randF() * 0.3f;
            p->life    = p->maxLife;
            p->kind    = ParticleKind::Splash;
            p->active  = true;
        }
    }

    // ---- Update ------------------------------------------------------------
    /// @brief Integrate particle physics. Call once per logic tick.
    /// @param dt Frame delta time in seconds.
    void update(float dt) {
        const float gravityY = -9.8f * worldScale * 60.0f;
        for (auto& p : pool) {
            if (!p.active) continue;
            p.vel.y += gravityY * dt;
            p.pos.x += p.vel.x * dt;
            p.pos.y += p.vel.y * dt;
            p.pos.z += p.vel.z * dt;
            p.life  -= dt;
            if (p.life <= 0.0f) p.active = false;
        }
    }

    // ---- Render ------------------------------------------------------------
    /// @brief Draw all active particles on top of already-rendered geometry.
    ///
    /// Call after scene->render() has returned for the frame. Uses
    /// Rasterizer::drawTriangle() directly so sparks composite above all
    /// scene objects without re-sorting.
    ///
    /// @param scene   The scene whose rasteriser to draw through.
    /// @param cam     Active camera (position + projection).
    /// @param screenW Render width in pixels.
    /// @param screenH Render height in pixels.
    void render(Scene* scene, Camera* cam, int screenW, int screenH) {
        lastRenderedTriangles = 0;
        if (!scene || !cam) return;
        Rasterizer* raster = scene->getRenderer();
        if (!raster) return;

        const int32_t FPS = FIXED_POINT_SCALE;   // 1024

        // Build camera rotation matrix (matches Scene::renderObject() order:
        // K = Rx * Ry, M = Rz * K).
        int32_t camCosX, camSinX, camCosY, camSinY, camCosZ, camSinZ;
        cam->getRotationMatrix(camCosX, camSinX, camCosY, camSinY, camCosZ, camSinZ);

        int32_t camM00, camM01, camM02;
        int32_t camM10, camM11, camM12;
        int32_t camM20, camM21, camM22;
        {
            const int32_t cx = camCosX, sx = camSinX;
            const int32_t cy = camCosY, sy = camSinY;
            const int32_t cz = camCosZ, sz = camSinZ;
            const int32_t k00 =  cy;
            const int32_t k01 =  0;
            const int32_t k02 =  sy;
            const int32_t k10 = (int32_t)((int64_t) sx * sy / FPS);
            const int32_t k11 =  cx;
            const int32_t k12 = (int32_t)(-(int64_t)sx * cy / FPS);
            const int32_t k20 = (int32_t)(-(int64_t)cx * sy / FPS);
            const int32_t k21 =  sx;
            const int32_t k22 = (int32_t)((int64_t) cx * cy / FPS);
            camM00 = (int32_t)(((int64_t)cz * k00 - (int64_t)sz * k10) / FPS);
            camM01 = (int32_t)(((int64_t)cz * k01 - (int64_t)sz * k11) / FPS);
            camM02 = (int32_t)(((int64_t)cz * k02 - (int64_t)sz * k12) / FPS);
            camM10 = (int32_t)(((int64_t)sz * k00 + (int64_t)cz * k10) / FPS);
            camM11 = (int32_t)(((int64_t)sz * k01 + (int64_t)cz * k11) / FPS);
            camM12 = (int32_t)(((int64_t)sz * k02 + (int64_t)cz * k12) / FPS);
            camM20 = k20;
            camM21 = k21;
            camM22 = k22;
        }

        // scene->render() already incremented frameCounter, so the parity
        // used this frame was ((frameCounter - 1) % 2 == 0).
        const bool rEL = (raster->interlacedMode || raster->checkerboardMode)
                       ? ((scene->frameCounter - 1) % 2 == 0)
                       : false;

        // Single shared material — emissive so lighting math is skipped.
        // color and alpha are mutated per particle below.
        Material mat;
        mat.emissive    = true;
        mat.diffuse     = 0;
        mat.specular    = 0;
        mat.alpha       = 255;
        mat.diffuseMap  = nullptr;
        mat.shader      = nullptr;
        mat.shadingMode = ShadingMode::FLAT;
        mat.name        = nullptr;

        const int32_t nearZ = cam->nearPlane;
        const int32_t farZ  = cam->farPlane;
        const float   fovF  = cam->fovFactor;
        const float   lodDistSq = (1500.0f * worldScale) * (1500.0f * worldScale);

        for (auto& p : pool) {
            if (!p.active) continue;

            // LOD: skip particles too far from the camera.
            const float ldx = p.pos.x - (float)cam->position.x;
            const float ldy = p.pos.y - (float)cam->position.y;
            const float ldz = p.pos.z - (float)cam->position.z;
            if (ldx*ldx + ldy*ldy + ldz*ldz > lodDistSq) continue;

            // Age / lifecycle
            const float age = 1.0f - (p.life / p.maxLife);  // 0=fresh, 1=dead

            uint8_t objAlpha = 255;
            if (age > 0.65f) {
                float t = (age - 0.65f) / 0.35f;
                int a = (int)(255.0f * (1.0f - t));
                objAlpha = (uint8_t)(a < 0 ? 0 : (a > 255 ? 255 : a));
            }
            if (objAlpha < 4) continue;

            // Smooth white → blue crossfade over age 0.25–0.45.
            // White: R=31 G=63 B=31 (0xFFFF); Blue: R=0 G=42 B=31 (0x055F).
            uint16_t particleCol;
            if (p.kind == ParticleKind::Splash) {
                if (age < 0.35f) {
                    particleCol = SPLASH_COL_FOAM;
                } else {
                    const float t = std::min(1.0f, (age - 0.35f) / 0.65f);
                    const uint8_t rC = (uint8_t)(29.0f * (1.0f - t) + 11.0f * t + 0.5f);
                    const uint8_t gC = (uint8_t)(63.0f * (1.0f - t) + 46.0f * t + 0.5f);
                    particleCol = ((uint16_t)rC << 11) | ((uint16_t)gC << 5) | 31u;
                }
            } else if (age < 0.25f) {
                particleCol = SPARK_COL_WHITE;
            } else if (age < 0.45f) {
                const float t  = (age - 0.25f) * 5.0f;
                const uint8_t rC = (uint8_t)(31.0f * (1.0f - t) + 0.5f);
                const uint8_t gC = (uint8_t)(63.0f * (1.0f - t) + 42.0f * t + 0.5f);
                particleCol = ((uint16_t)rC << 11) | ((uint16_t)gC << 5) | 31u;
            } else {
                particleCol = SPARK_COL_BLUE;
            }
            mat.color = particleCol;
            mat.shadingMode = additiveSparks && p.kind == ParticleKind::Spark
                ? ShadingMode::ADDITIVE : ShadingMode::FLAT;

            // Transform world pos to camera space
            const int32_t wx = (int32_t)p.pos.x - cam->position.x;
            const int32_t wy = (int32_t)p.pos.y - cam->position.y;
            const int32_t wz = (int32_t)p.pos.z - cam->position.z;

            const int32_t camX = (int32_t)(((int64_t)wx * camM00 + (int64_t)wy * camM01 + (int64_t)wz * camM02) / FPS);
            const int32_t camY = (int32_t)(((int64_t)wx * camM10 + (int64_t)wy * camM11 + (int64_t)wz * camM12) / FPS);
            const int32_t camZ = (int32_t)(((int64_t)wx * camM20 + (int64_t)wy * camM21 + (int64_t)wz * camM22) / FPS);

            if (camZ <= nearZ || camZ >= farZ) continue;

            // Project base point
            const float invCamZ = fovF / (float)camZ;
            const int32_t bx = (int32_t)(camX * invCamZ) + screenW / 2;
            const int32_t by = screenH / 2 - (int32_t)(camY * invCamZ);

            // Project velocity tip (for streak orientation)
            const float vlen = sqrtf(p.vel.x * p.vel.x + p.vel.y * p.vel.y + p.vel.z * p.vel.z);
            const float streakTime = p.kind == ParticleKind::Splash ? 0.03f : 0.02f;
            const float streakLen = (vlen > 1.0f) ? (vlen * streakTime) : (2.0f * worldScale);

            float vnx = 0.0f, vny = 0.0f, vnz = 0.0f;
            if (vlen > 1e-3f) {
                const float invLen = 1.0f / vlen;
                vnx = p.vel.x * invLen; vny = p.vel.y * invLen; vnz = p.vel.z * invLen;
            } else {
                vny = 1.0f;
            }

            const int32_t twx = (int32_t)(p.pos.x + vnx * streakLen) - cam->position.x;
            const int32_t twy = (int32_t)(p.pos.y + vny * streakLen) - cam->position.y;
            const int32_t twz = (int32_t)(p.pos.z + vnz * streakLen) - cam->position.z;

            const int32_t tcX = (int32_t)(((int64_t)twx * camM00 + (int64_t)twy * camM01 + (int64_t)twz * camM02) / FPS);
            const int32_t tcY = (int32_t)(((int64_t)twx * camM10 + (int64_t)twy * camM11 + (int64_t)twz * camM12) / FPS);
            const int32_t tcZ = (int32_t)(((int64_t)twx * camM20 + (int64_t)twy * camM21 + (int64_t)twz * camM22) / FPS);

            if (tcZ <= 0) continue;
            const float invTcZ = fovF / (float)tcZ;
            const int32_t tx = (int32_t)(tcX * invTcZ) + screenW / 2;
            const int32_t ty = screenH / 2 - (int32_t)(tcY * invTcZ);

            // Build screen-space oriented triangle
            const int32_t ddx = tx - bx;
            const int32_t ddy = ty - by;
            const float slen = sqrtf((float)(ddx * ddx + ddy * ddy));

            const int32_t HW = p.kind == ParticleKind::Splash ? 3 : 2;
            int32_t perpX, perpY;
            if (slen < 0.5f) {
                perpX = HW; perpY = 0;
            } else {
                const float invSlen = (float)HW / slen;
                perpX = (int32_t)(-(float)ddy * invSlen);
                perpY = (int32_t)( (float)ddx * invSlen);
            }

            // v0 = tip (velocity direction), v1/v2 = base ± perp.
            // noWriteZ so sparks don't occlude each other or later geometry.
            RenderVertex v0, v1, v2;
            v0.position = { tx,         ty,         tcZ  };
            v1.position = { bx + perpX, by + perpY, camZ };
            v2.position = { bx - perpX, by - perpY, camZ };

            if (raster->drawTriangle(v0, v1, v2, &mat,
                                 nullptr, nullptr,
                                 rEL,
                                 /*ignoreZ*/ false, /*noWriteZ*/ true,
                                 /*zBias*/   0,
                                 objAlpha)) ++lastRenderedTriangles;
        }
    }

private:
    Particle* allocate() {
        for (auto& p : pool) {
            if (!p.active) return &p;
        }
        return nullptr;
    }

    static float randF() {
        return (float)(rand() & 0x7FFF) / 32767.0f;
    }
};

} // namespace Renderer

#endif // JET_PARTICLESYSTEM_HPP
