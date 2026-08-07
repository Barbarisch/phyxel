#pragma once

#include "utils/Frustum.h"
#include <glm/glm.hpp>

namespace Phyxel {
namespace Graphics {

/// Visibility predicate for one kinematic voxel object (item props, dynamic
/// furniture, doors) — the previously-missing culling for the one geometry
/// class the renderer drew unconditionally in every pass (2026-08-07 plan).
///
/// Pure function so it is unit-testable without Vulkan. Callers pass the
/// object's bounding sphere CENTER relative to the frustum's origin (camera-
/// relative for the main pass, light-space-consistent for shadow passes —
/// exactly the convention the character cull uses) plus its radius, and the
/// squared distance from the viewpoint for the distance cap.
///
/// maxDistSq <= 0 disables the distance cap (shadow passes cull by light
/// frustum only — an object behind the camera can still cast into view).
inline bool kinematicObjectVisible(const Utils::Frustum& frustum,
                                   const glm::vec3& centerRel, float radius,
                                   float distSq, float maxDistSq) {
    if (maxDistSq > 0.0f && distSq > maxDistSq) return false;
    return frustum.intersects(centerRel, radius);
}

} // namespace Graphics
} // namespace Phyxel
