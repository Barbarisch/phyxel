#pragma once

#include <vector>
#include <memory>
#include <glm/glm.hpp>

// Forward declarations
class btCollisionShape;

namespace Phyxel {
namespace Physics {

/**
 * @brief Spatial grid for O(1) collision entity management
 * 
 * Provides efficient storage and retrieval of collision entities in a 32x32x32 grid,
 * matching chunk dimensions. Optimized for quick lookups and minimal overhead.
 */
class CollisionSpatialGrid {
public:
    static constexpr int GRID_SIZE = 32;  // Match chunk dimensions (32x32x32)
    static constexpr size_t MAX_ENTITIES_PER_CELL = 27;  // Max subcubes per position
    
    /**
     * @brief Collision entity wrapper for physics shapes
     * 
     * Tracks collision shapes with spatial data and hierarchy information.
     */
    struct CollisionEntity {
        btCollisionShape* shape;
        enum Type { CUBE, SUBCUBE } type;
        bool isInCompound;                    // Track if shape is managed by Bullet compound
        
        // Spatial data for optimization
        glm::vec3 worldCenter;                // World-space center position
        float boundingRadius;                 // Bounding radius for spatial queries
        
        // Hierarchy data (for subcubes only)
        glm::ivec3 parentChunkPos;           // Parent position within chunk (0-31 range)
        glm::ivec3 subcubeLocalPos;          // Local position within parent (0-2 range)
        
        CollisionEntity(btCollisionShape* s, Type t, const glm::vec3& center, float radius = 0.5f);
        ~CollisionEntity();
        
        // Utility methods
        bool isSubcube() const { return type == SUBCUBE; }
        bool isCube() const { return type == CUBE; }
    };
    
    // Core operations - all O(1) or O(k) where k is entities per cell (≤27)
    void addEntity(const glm::ivec3& gridPos, std::shared_ptr<CollisionEntity> entity);
    void removeEntity(const glm::ivec3& gridPos, std::shared_ptr<CollisionEntity> entity);
    void removeAllAt(const glm::ivec3& gridPos);
    std::vector<std::shared_ptr<CollisionEntity>>& getEntitiesAt(const glm::ivec3& gridPos);
    const std::vector<std::shared_ptr<CollisionEntity>>& getEntitiesAt(const glm::ivec3& gridPos) const;
    
    // Batch operations
    void clear();
    void reserve(size_t expectedEntities);
    
    // Performance metrics and debugging
    size_t getTotalEntityCount() const { return totalEntities; }
    size_t getCubeEntityCount() const { return cubeEntities; }
    size_t getSubcubeEntityCount() const { return subcubeEntities; }
    size_t getOccupiedCellCount() const;
    
    // Validation
    bool validateGrid() const;
    void debugPrintStats() const;
    
private:
    // 3D spatial grid - each cell contains entities at that position
    std::vector<std::shared_ptr<CollisionEntity>> grid[GRID_SIZE][GRID_SIZE][GRID_SIZE];
    
    // Performance counters
    mutable size_t totalEntities = 0;
    mutable size_t cubeEntities = 0;
    mutable size_t subcubeEntities = 0;
    
    // Bounds checking
    bool isValidGridPosition(const glm::ivec3& pos) const {
        return pos.x >= 0 && pos.x < GRID_SIZE &&
               pos.y >= 0 && pos.y < GRID_SIZE &&
               pos.z >= 0 && pos.z < GRID_SIZE;
    }
};

} // namespace Physics
} // namespace Phyxel
