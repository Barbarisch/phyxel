#pragma once

#include "core/Types.h"
#include "physics/VoxelOccupancyGrid.h"
#include <glm/glm.hpp>
#include <functional>

namespace Phyxel {

class Cube;
class Subcube;
class Microcube;

namespace Physics {

class PhysicsWorld;

class ChunkPhysicsManager {
public:
    ChunkPhysicsManager();
    ~ChunkPhysicsManager();

    ChunkPhysicsManager(const ChunkPhysicsManager&) = delete;
    ChunkPhysicsManager& operator=(const ChunkPhysicsManager&) = delete;

    ChunkPhysicsManager(ChunkPhysicsManager&& other) noexcept;
    ChunkPhysicsManager& operator=(ChunkPhysicsManager&& other) noexcept;

    void initialize(PhysicsWorld* world, const glm::ivec3& chunkOrigin);

    void setPhysicsWorld(PhysicsWorld* world) { physicsWorld = world; }
    PhysicsWorld* getPhysicsWorld() const     { return physicsWorld; }

    // Callback function typedefs for accessing chunk data
    using CubeAccessFunc             = std::function<const Cube*(const glm::ivec3&)>;
    using SubcubeAccessFunc          = std::function<Subcube*(const glm::ivec3&, const glm::ivec3&)>;
    using MicrocubesAccessFunc       = std::function<std::vector<Microcube*>(const glm::ivec3&, const glm::ivec3&)>;
    using StaticSubcubesAccessFunc   = std::function<std::vector<Subcube*>(const glm::ivec3&)>;
    using CubesArrayAccessFunc       = std::function<const std::vector<std::unique_ptr<Cube>>&()>;
    using IndexToLocalFunc           = std::function<glm::ivec3(size_t)>;
    using StaticMicrocubesAccessFunc = std::function<const std::vector<std::unique_ptr<Microcube>>&()>;

    // Physics body lifecycle
    void createChunkPhysicsBody(const CubesArrayAccessFunc& getCubes,
                                const StaticSubcubesAccessFunc& getStaticSubcubes,
                                const StaticMicrocubesAccessFunc& getStaticMicrocubes,
                                const IndexToLocalFunc& indexToLocal,
                                const CubeAccessFunc& getCube);
    void updateChunkPhysicsBody(const CubesArrayAccessFunc& getCubes,
                                const StaticSubcubesAccessFunc& getStaticSubcubes,
                                const StaticMicrocubesAccessFunc& getStaticMicrocubes,
                                const IndexToLocalFunc& indexToLocal,
                                const CubeAccessFunc& getCube);
    void forcePhysicsRebuild(const CubesArrayAccessFunc& getCubes,
                             const StaticSubcubesAccessFunc& getStaticSubcubes,
                             const StaticMicrocubesAccessFunc& getStaticMicrocubes,
                             const IndexToLocalFunc& indexToLocal,
                             const CubeAccessFunc& getCube);
    void cleanupPhysicsResources();

    // Collision entity management
    void addCollisionEntity(const glm::ivec3& localPos);
    void removeCollisionEntities(const glm::ivec3& localPos);

    void buildInitialCollisionShapes(const CubesArrayAccessFunc& getCubes,
                                     const StaticSubcubesAccessFunc& getStaticSubcubes,
                                     const StaticMicrocubesAccessFunc& getStaticMicrocubes,
                                     const IndexToLocalFunc& indexToLocal,
                                     const CubeAccessFunc& getCube);

    void batchUpdateCollisions(const CubesArrayAccessFunc& getCubes,
                               const StaticSubcubesAccessFunc& getStaticSubcubes,
                               const StaticMicrocubesAccessFunc& getStaticMicrocubes,
                               const IndexToLocalFunc& indexToLocal,
                               const CubeAccessFunc& getCube);

    void updateNeighborCollisionShapes(const glm::ivec3& localPos,
                                       const CubeAccessFunc& getCube,
                                       const MicrocubesAccessFunc& getMicrocubes,
                                       const StaticSubcubesAccessFunc& getStaticSubcubes);
    void endBulkOperation(const CubesArrayAccessFunc& getCubes,
                          const StaticSubcubesAccessFunc& getStaticSubcubes,
                          const StaticMicrocubesAccessFunc& getStaticMicrocubes,
                          const IndexToLocalFunc& indexToLocal,
                          const CubeAccessFunc& getCube);

    bool isInBulkOperation() const       { return m_isInBulkOperation; }
    void setInBulkOperation(bool inBulk) { m_isInBulkOperation = inBulk; }

    bool  getCollisionNeedsUpdate() const              { return collisionNeedsUpdate; }
    void  setCollisionNeedsUpdate(bool needsUpdate)    { collisionNeedsUpdate = needsUpdate; }
    bool& getCollisionNeedsUpdateRef()                 { return collisionNeedsUpdate; }

    // Occupancy grid — queried by VoxelDynamicsWorld for terrain collision
    VoxelOccupancyGrid&       getOccupancyGrid()       { return m_occupancyGrid; }
    const VoxelOccupancyGrid& getOccupancyGrid() const { return m_occupancyGrid; }

    // Debugging stubs
    void   validateCollisionSystem()    const;
    void   debugLogSpatialGrid()        const;
    size_t getCollisionEntityCount()    const { return 0; }
    size_t getCubeEntityCount()         const { return 0; }
    size_t getSubcubeEntityCount()      const { return 0; }
    void   debugPrintSpatialGridStats() const;

    // Collision shape helpers (occupancy grid only, no Bullet compound shape)
    void createCubeCollisionShape(const glm::ivec3& localPos,
                                  const CubeAccessFunc& getCube);
    void createSubcubeCollisionShape(const glm::ivec3& cubePos, const glm::ivec3& subcubePos,
                                     const SubcubeAccessFunc& getSubcube);
    void createMicrocubeCollisionShape(const glm::ivec3& cubePos, const glm::ivec3& subcubePos,
                                       const Microcube* microcube);

    void addCollisionEntity(const glm::ivec3& localPos,
                            const CubeAccessFunc& getCube,
                            const MicrocubesAccessFunc& getMicrocubes,
                            const StaticSubcubesAccessFunc& getStaticSubcubes);

    bool hasExposedFaces(const glm::ivec3& localPos, const CubeAccessFunc& getCube) const;

private:
    bool               collisionNeedsUpdate = false;
    bool               m_isInBulkOperation  = false;
    VoxelOccupancyGrid m_occupancyGrid;
    PhysicsWorld*      physicsWorld = nullptr;
    glm::ivec3         chunkOrigin  = glm::ivec3(0);
};

} // namespace Physics
} // namespace Phyxel
