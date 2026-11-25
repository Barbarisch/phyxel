#pragma once

#include <glm/glm.hpp>

namespace Phyxel {

// Structure for backward compatibility with existing chunk-based system
struct CubeLocation {
    bool valid;                 // (was the owning Chunk*; today's Chunk store went with D4)
    glm::ivec3 localPos;        // Local position within chunk
    glm::ivec3 worldPos;        // World position
    bool isSubcube;             // True if this location refers to a subcube
    bool isMicrocube;           // True if this location refers to a microcube
    glm::ivec3 subcubePos;      // Local position within parent cube (0-2 for each axis)
    glm::ivec3 microcubePos;    // Local position within parent subcube (0-2 for each axis)
    
    // Face information for cube placement
    int hitFace;                // Which face was hit: 0=left(-X), 1=right(+X), 2=bottom(-Y), 3=top(+Y), 4=back(-Z), 5=front(+Z)
    glm::vec3 hitNormal;        // Surface normal of hit face
    glm::vec3 hitPoint;         // Exact hit point on the cube surface
    
    CubeLocation() : valid(false), localPos(-1), worldPos(-1), isSubcube(false), isMicrocube(false),
                     subcubePos(-1), microcubePos(-1), hitFace(-1), hitNormal(0), hitPoint(0) {}
    CubeLocation(const glm::ivec3& local, const glm::ivec3& world)
        : valid(true), localPos(local), worldPos(world), isSubcube(false), isMicrocube(false),
          subcubePos(-1), microcubePos(-1), hitFace(-1), hitNormal(0), hitPoint(0) {}
    CubeLocation(const glm::ivec3& local, const glm::ivec3& world, const glm::ivec3& sub)
        : valid(true), localPos(local), worldPos(world), isSubcube(true), isMicrocube(false),
          subcubePos(sub), microcubePos(-1), hitFace(-1), hitNormal(0), hitPoint(0) {}
    CubeLocation(const glm::ivec3& local, const glm::ivec3& world, const glm::ivec3& sub, const glm::ivec3& micro)
        : valid(true), localPos(local), worldPos(world), isSubcube(false), isMicrocube(true),
          subcubePos(sub), microcubePos(micro), hitFace(-1), hitNormal(0), hitPoint(0) {}
    
    bool isValid() const { return valid; }
    
    // Get the world position where a new cube should be placed adjacent to this face
    glm::ivec3 getAdjacentPlacementPosition() const;
};

} // namespace Phyxel
