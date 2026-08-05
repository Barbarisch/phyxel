#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>

namespace Phyxel {
namespace Utils {

struct Plane {
    glm::vec3 normal;
    float distance;
    
    Plane() = default;
    Plane(const glm::vec3& n, float d) : normal(n), distance(d) {}
    
    // Calculate signed distance from point to plane
    float distanceToPoint(const glm::vec3& point) const {
        return glm::dot(normal, point) + distance;
    }
};

struct AABB {
    glm::vec3 min;
    glm::vec3 max;
    
    AABB() = default;
    AABB(const glm::vec3& minCorner, const glm::vec3& maxCorner) 
        : min(minCorner), max(maxCorner) {}
    
    glm::vec3 getCenter() const { return (min + max) * 0.5f; }
    glm::vec3 getSize() const { return max - min; }
    glm::vec3 getHalfExtents() const { return getSize() * 0.5f; }
};

class Frustum {
public:
    enum PlaneIndex {
        LEFT = 0, RIGHT, BOTTOM, TOP, NEAR, FAR
    };

    /// Which clip-space convention the supplied matrix was built in. This is REQUIRED rather than
    /// defaulted on purpose: this engine now uses THREE different conventions simultaneously, and
    /// picking the wrong one silently swaps the near and far planes instead of failing loudly.
    /// That is exactly how a hardcoded assumption here broke shadow-light culling (caught by
    /// FrustumTest.OrthographicFrustum_AABB, 2026-08-01).
    enum class ClipConvention {
        /// Vulkan [0,1] REVERSE-Z — the scene camera. z_clip == w at the near plane, 0 at
        /// infinity. See graphics/DepthConvention.h.
        ReverseZeroToOne,
        /// Vulkan [0,1] forward — `glm::orthoRH_ZO`, i.e. the SHADOW light matrix.
        ForwardZeroToOne,
        /// OpenGL [-1,1] — plain `glm::ortho` / `glm::perspective`. Still reachable from tests and
        /// from `ShadowMap.cpp`'s own `glm::ortho` light projection.
        ForwardNegOneToOne,
    };

    std::array<Plane, 6> planes;

    // Extract frustum planes from a view-projection matrix built in `convention`.
    void extractFromMatrix(const glm::mat4& viewProjectionMatrix, ClipConvention convention);

    // Test if AABB is inside or intersecting frustum
    bool intersects(const AABB& aabb) const;
    
    // Test if point is inside frustum
    bool contains(const glm::vec3& point) const;
    
    // Test if sphere is inside or intersecting frustum
    bool intersects(const glm::vec3& center, float radius) const;
    
private:
    void normalizePlane(Plane& plane);
};

} // namespace Utils
} // namespace Phyxel
