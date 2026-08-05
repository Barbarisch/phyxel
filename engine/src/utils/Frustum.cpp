#include "utils/Frustum.h"
#include "graphics/DepthConvention.h"
#include <algorithm>

namespace Phyxel {
namespace Utils {

void Frustum::extractFromMatrix(const glm::mat4& viewProjectionMatrix, ClipConvention convention) {
    const glm::mat4& m = viewProjectionMatrix;
    
    // Left plane: m[3] + m[0]
    planes[LEFT].normal.x = m[0][3] + m[0][0];
    planes[LEFT].normal.y = m[1][3] + m[1][0];
    planes[LEFT].normal.z = m[2][3] + m[2][0];
    planes[LEFT].distance = m[3][3] + m[3][0];
    
    // Right plane: m[3] - m[0]
    planes[RIGHT].normal.x = m[0][3] - m[0][0];
    planes[RIGHT].normal.y = m[1][3] - m[1][0];
    planes[RIGHT].normal.z = m[2][3] - m[2][0];
    planes[RIGHT].distance = m[3][3] - m[3][0];
    
    // Bottom plane: m[3] + m[1]
    planes[BOTTOM].normal.x = m[0][3] + m[0][1];
    planes[BOTTOM].normal.y = m[1][3] + m[1][1];
    planes[BOTTOM].normal.z = m[2][3] + m[2][1];
    planes[BOTTOM].distance = m[3][3] + m[3][1];
    
    // Top plane: m[3] - m[1]
    planes[TOP].normal.x = m[0][3] - m[0][1];
    planes[TOP].normal.y = m[1][3] - m[1][1];
    planes[TOP].normal.z = m[2][3] - m[2][1];
    planes[TOP].distance = m[3][3] - m[3][1];
    
    // --- The depth pair, which is the ONLY part that depends on the clip convention.
    //
    // VULKAN clip conditions are  0 <= z_clip <= w_clip.
    // OPENGL clip conditions are -w <= z_clip <= w.
    //
    // So the candidate planes are:
    //   rowZ      =  row2           -> "z_clip >= 0"      (Vulkan only)
    //   rowWmZ    =  row3 - row2    -> "z_clip <= w_clip" (both)
    //   rowWpZ    =  row3 + row2    -> "z_clip >= -w"     (OpenGL only)
    //
    // and they map to near/far differently per convention:
    //   ForwardNegOneToOne : near = rowWpZ, far = rowWmZ   (the original code, GL)
    //   ForwardZeroToOne   : near = rowZ,   far = rowWmZ
    //   ReverseZeroToOne   : near = rowWmZ, far = rowZ     (SWAPPED — z is 1 at near, 0 at far)
    //
    // ⚠️ The original implementation hardcoded the OpenGL form while this renderer is Vulkan, so
    // its NEAR plane was always wrong here (harmless only because a 0.1-unit near plane culls
    // nothing). Do not "simplify" this back to one fixed form: the scene camera is reverse-Z
    // Vulkan, the shadow light matrix is forward Vulkan ortho, and tests still build OpenGL
    // matrices — all three go through this function.
    //
    // Under ReverseZeroToOne with an INFINITE projection the far plane is degenerate:
    // row2 = (0,0,0,near), i.e. a zero normal, i.e. NO far constraint at all. That is correct and
    // intended — nothing is ever too far to draw. normalizePlane leaves a zero normal alone, so
    // distanceToPoint returns +near for every point and the plane always passes.
    // See graphics/DepthConvention.h.
    Plane rowZ, rowWmZ, rowWpZ;
    rowZ.normal     = glm::vec3(m[0][2], m[1][2], m[2][2]);
    rowZ.distance   = m[3][2];
    rowWmZ.normal   = glm::vec3(m[0][3] - m[0][2], m[1][3] - m[1][2], m[2][3] - m[2][2]);
    rowWmZ.distance = m[3][3] - m[3][2];
    rowWpZ.normal   = glm::vec3(m[0][3] + m[0][2], m[1][3] + m[1][2], m[2][3] + m[2][2]);
    rowWpZ.distance = m[3][3] + m[3][2];

    switch (convention) {
        case ClipConvention::ReverseZeroToOne:
            planes[NEAR] = rowWmZ; planes[FAR] = rowZ;    break;
        case ClipConvention::ForwardZeroToOne:
            planes[NEAR] = rowZ;   planes[FAR] = rowWmZ;  break;
        case ClipConvention::ForwardNegOneToOne:
            planes[NEAR] = rowWpZ; planes[FAR] = rowWmZ;  break;
    }


    // Normalize all planes
    for (auto& plane : planes) {
        normalizePlane(plane);
    }
}

void Frustum::normalizePlane(Plane& plane) {
    float length = glm::length(plane.normal);
    if (length > 0.0f) {
        plane.normal /= length;
        plane.distance /= length;
    }
}

bool Frustum::intersects(const AABB& aabb) const {
    // Test AABB against each frustum plane
    for (const auto& plane : planes) {
        // Get the positive vertex (farthest point along plane normal)
        glm::vec3 positiveVertex;
        positiveVertex.x = (plane.normal.x >= 0.0f) ? aabb.max.x : aabb.min.x;
        positiveVertex.y = (plane.normal.y >= 0.0f) ? aabb.max.y : aabb.min.y;
        positiveVertex.z = (plane.normal.z >= 0.0f) ? aabb.max.z : aabb.min.z;
        
        // If positive vertex is behind plane, AABB is completely outside
        if (plane.distanceToPoint(positiveVertex) < 0.0f) {
            return false; // AABB is completely outside this plane
        }
    }
    
    return true; // AABB is inside or intersecting frustum
}

bool Frustum::contains(const glm::vec3& point) const {
    for (const auto& plane : planes) {
        if (plane.distanceToPoint(point) < 0.0f) {
            return false; // Point is outside this plane
        }
    }
    return true; // Point is inside all planes
}

bool Frustum::intersects(const glm::vec3& center, float radius) const {
    for (const auto& plane : planes) {
        if (plane.distanceToPoint(center) < -radius) {
            return false; // Sphere is completely outside this plane
        }
    }
    return true; // Sphere is inside or intersecting frustum
}

} // namespace Utils
} // namespace Phyxel
