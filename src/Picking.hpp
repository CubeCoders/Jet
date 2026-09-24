#ifndef JET_PICKING_HPP
#define JET_PICKING_HPP

#include <cstdint>
#include "JetConfig.hpp"

/// @file Picking.hpp
/// @brief Screen-space cursor picking.
///
/// Active only when MAX_PICK_QUERIES > 0; the rest of the renderer guards
/// every reference behind that same #if so the entire feature compiles
/// out cleanly when not needed.
///
/// Example usage (host side, once per frame, before Scene::render()):
/// @code
///     Renderer::PickQuery q[2];
///     q[0] = { mouseX, mouseY };
///     scene.setPickQueries(q, 1);
///     scene.render();
///     const auto* r = scene.getPickResults();
///     if (r[0].hit) { ... r[0].object, r[0].triangleIndex ... }
/// @endcode

namespace Renderer {

class Object;

#if MAX_PICK_QUERIES > 0

/// @brief Screen-space pick request.
struct PickQuery {
    int16_t x = -1;             ///< Pixel X. Negative disables the slot.
    int16_t y = -1;             ///< Pixel Y. Negative disables the slot.
};

/// @brief Result of a single pick query for the most recent frame.
struct PickResult {
    bool    hit           = false;  ///< True if any non-cleared geometry covered the pixel this frame.
    Object* object        = nullptr;///< Closest object hit (borrowed from Scene::objects). Do not free.
#if JET_MESH_INSTANCING
    /// Actual mesh used, including a selected LOD or shared prototype.
    const Object* mesh = nullptr;
    /// Instance within the selected LOD batch; -1 for its ordinary geometry.
    int32_t instanceIndex = -1;
    /// Index into mesh->triangles, stable across per-frame sorting. -1 when !hit.
#else
    /// Index into `object->triangles` of the hit triangle. -1 when !hit.
    /// When SORT_TRIANGLES is enabled the source mesh is sorted in place
    /// every frame, so this index is valid until the next render() call.
#endif // JET_MESH_INSTANCING
    int32_t triangleIndex = -1;
    int32_t depth         = 0;      ///< Camera-space Z of the hit (smaller = closer). Same units as Camera::nearPlane / farPlane.
    int16_t x             = -1;     ///< Pixel actually sampled (echoes input x).
    int16_t y             = -1;     ///< Pixel actually sampled (input y, possibly nudged for FIELD_BUFFERS parity).
};

#endif // MAX_PICK_QUERIES > 0

} // namespace Renderer

#endif // JET_PICKING_HPP

