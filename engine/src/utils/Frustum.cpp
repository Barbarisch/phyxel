#include "utils/Frustum.h"
#include <algorithm>

namespace Phyxel {
namespace Utils {

void Frustum::extractFromMatrix(const glm::mat4& viewProjectionMatrix) {
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
    
    // Near plane: m[3] + m[2]
    planes[NEAR].normal.x = m[0][3] + m[0][2];
    planes[NEAR].normal.y = m[1][3] + m[1][2];
    planes[NEAR].normal.z = m[2][3] + m[2][2];
    planes[NEAR].distance = m[3][3] + m[3][2];
    
    // Far plane: m[3] - m[2]
    planes[FAR].normal.x = m[0][3] - m[0][2];
    planes[FAR].normal.y = m[1][3] - m[1][2];
    planes[FAR].normal.z = m[2][3] - m[2][2];
    planes[FAR].distance = m[3][3] - m[3][2];
    
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
