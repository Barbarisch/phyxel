#include "physics/CollisionSpatialGrid.h"
#include "utils/Logger.h"
#include <algorithm>

// Bullet Physics includes
class btCollisionShape;

namespace Phyxel {
namespace Physics {

// CollisionEntity implementation
CollisionSpatialGrid::CollisionEntity::CollisionEntity(btCollisionShape* s, Type t, const glm::vec3& center, float radius)
    : shape(s), type(t), isInCompound(false), worldCenter(center), boundingRadius(radius) {
    // Initialize hierarchy data for subcubes
    if (type == SUBCUBE) {
        parentChunkPos = glm::ivec3(0);  // Will be set by caller
        subcubeLocalPos = glm::ivec3(0); // Will be set by caller
    }
}

CollisionSpatialGrid::CollisionEntity::~CollisionEntity() {
    // Only delete if not in compound shape (Bullet manages compound children lifetime)
    if (!isInCompound && shape) {
        delete shape;
        shape = nullptr;
    }
}

// CollisionSpatialGrid implementation
void CollisionSpatialGrid::addEntity(const glm::ivec3& gridPos, std::shared_ptr<CollisionEntity> entity) {
    if (!isValidGridPosition(gridPos)) return;
    
    auto& cell = grid[gridPos.x][gridPos.y][gridPos.z];
    cell.push_back(entity);
    
    // Update counters
    totalEntities++;
    if (entity->isCube()) {
        cubeEntities++;
    } else {
        subcubeEntities++;
    }
}

void CollisionSpatialGrid::removeEntity(const glm::ivec3& gridPos, std::shared_ptr<CollisionEntity> entity) {
    if (!isValidGridPosition(gridPos)) return;
    
    auto& cell = grid[gridPos.x][gridPos.y][gridPos.z];
    auto it = std::find(cell.begin(), cell.end(), entity);
    if (it != cell.end()) {
        cell.erase(it);
        
        // Update counters
        totalEntities--;
        if (entity->isCube()) {
            cubeEntities--;
        } else {
            subcubeEntities--;
        }
    }
}

void CollisionSpatialGrid::removeAllAt(const glm::ivec3& gridPos) {
    if (!isValidGridPosition(gridPos)) return;
    
    auto& cell = grid[gridPos.x][gridPos.y][gridPos.z];
    
    // Update counters
    for (const auto& entity : cell) {
        totalEntities--;
        if (entity->isCube()) {
            cubeEntities--;
        } else {
            subcubeEntities--;
        }
    }
    
    cell.clear();
}

std::vector<std::shared_ptr<CollisionSpatialGrid::CollisionEntity>>& CollisionSpatialGrid::getEntitiesAt(const glm::ivec3& gridPos) {
    static std::vector<std::shared_ptr<CollisionEntity>> emptyVector;
    if (!isValidGridPosition(gridPos)) return emptyVector;
    return grid[gridPos.x][gridPos.y][gridPos.z];
}

const std::vector<std::shared_ptr<CollisionSpatialGrid::CollisionEntity>>& CollisionSpatialGrid::getEntitiesAt(const glm::ivec3& gridPos) const {
    static const std::vector<std::shared_ptr<CollisionEntity>> emptyVector;
    if (!isValidGridPosition(gridPos)) return emptyVector;
    return grid[gridPos.x][gridPos.y][gridPos.z];
}

void CollisionSpatialGrid::clear() {
    for (int x = 0; x < GRID_SIZE; ++x) {
        for (int y = 0; y < GRID_SIZE; ++y) {
            for (int z = 0; z < GRID_SIZE; ++z) {
                grid[x][y][z].clear();
            }
        }
    }
    totalEntities = 0;
    cubeEntities = 0;
    subcubeEntities = 0;
}

void CollisionSpatialGrid::reserve(size_t expectedEntities) {
    // Reserve space in cells that are likely to be used
    size_t entitiesPerCell = std::max(static_cast<size_t>(1), expectedEntities / (GRID_SIZE * GRID_SIZE * GRID_SIZE / 8));
    for (int x = 0; x < GRID_SIZE; ++x) {
        for (int y = 0; y < GRID_SIZE; ++y) {
            for (int z = 0; z < GRID_SIZE; ++z) {
                grid[x][y][z].reserve(entitiesPerCell);
            }
        }
    }
}

size_t CollisionSpatialGrid::getOccupiedCellCount() const {
    size_t count = 0;
    for (int x = 0; x < GRID_SIZE; ++x) {
        for (int y = 0; y < GRID_SIZE; ++y) {
            for (int z = 0; z < GRID_SIZE; ++z) {
                if (!grid[x][y][z].empty()) {
                    count++;
                }
            }
        }
    }
    return count;
}

bool CollisionSpatialGrid::validateGrid() const {
    size_t actualTotal = 0, actualCubes = 0, actualSubcubes = 0;
    
    for (int x = 0; x < GRID_SIZE; ++x) {
        for (int y = 0; y < GRID_SIZE; ++y) {
            for (int z = 0; z < GRID_SIZE; ++z) {
                const auto& cell = grid[x][y][z];
                actualTotal += cell.size();
                for (const auto& entity : cell) {
                    if (entity->isCube()) {
                        actualCubes++;
                    } else {
                        actualSubcubes++;
                    }
                }
            }
        }
    }
    
    return (actualTotal == totalEntities && 
            actualCubes == cubeEntities && 
            actualSubcubes == subcubeEntities);
}

void CollisionSpatialGrid::debugPrintStats() const {
    LOG_DEBUG("CollisionSpatialGrid", "CollisionSpatialGrid Stats:");
    LOG_DEBUG_FMT("CollisionSpatialGrid", "  Total entities: " << totalEntities);
    LOG_DEBUG_FMT("CollisionSpatialGrid", "  Cube entities: " << cubeEntities);
    LOG_DEBUG_FMT("CollisionSpatialGrid", "  Subcube entities: " << subcubeEntities);
    LOG_DEBUG_FMT("CollisionSpatialGrid", "  Occupied cells: " << getOccupiedCellCount() << "/" << (GRID_SIZE * GRID_SIZE * GRID_SIZE));
    LOG_DEBUG_FMT("CollisionSpatialGrid", "  Average entities per occupied cell: " << 
        (getOccupiedCellCount() > 0 ? (double)totalEntities / getOccupiedCellCount() : 0.0));
}

} // namespace Physics
} // namespace Phyxel
