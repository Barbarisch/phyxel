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
    
    std::array<Plane, 6> planes;
    
    // Extract frustum planes from view-projection matrix
    void extractFromMatrix(const glm::mat4& viewProjectionMatrix);
    
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
