#pragma once

#include "core/Types.h"
#include "physics/VoxelOccupancyGrid.h"
#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <vector>

namespace Phyxel {

class Cube;
class Subcube;
class Microcube;
class ChunkVoxelStore;

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

    // 4.2b: the chunk's palette store — the occupancy build reads static voxels from it (a
    // materialized overlay Cube in the cubes vector wins where present). Set by Chunk alongside
    // initialize(); may be null in unit tests that drive the manager with bare cube vectors.
    void setVoxelStore(const ChunkVoxelStore* store) { m_voxelStore = store; }

    // Callback function typedefs for accessing chunk data.
    // VisibleSolidFunc (4.2b, was CubeAccessFunc returning const Cube*): "is there a visible
    // solid cube at this local cell?" — a bool answer lets Chunk back it with the palette store
    // (hybrid read) instead of materializing Cubes just to probe presence. Both live consumers
    // (addCollisionEntity, hasExposedFaces) only ever asked exactly that.
    using VisibleSolidFunc           = std::function<bool(const glm::ivec3&)>;
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
                                const VisibleSolidFunc& visibleSolidAt);
    void updateChunkPhysicsBody(const CubesArrayAccessFunc& getCubes,
                                const StaticSubcubesAccessFunc& getStaticSubcubes,
                                const StaticMicrocubesAccessFunc& getStaticMicrocubes,
                                const IndexToLocalFunc& indexToLocal,
                                const VisibleSolidFunc& visibleSolidAt);
    void forcePhysicsRebuild(const CubesArrayAccessFunc& getCubes,
                             const StaticSubcubesAccessFunc& getStaticSubcubes,
                             const StaticMicrocubesAccessFunc& getStaticMicrocubes,
                             const IndexToLocalFunc& indexToLocal,
                             const VisibleSolidFunc& visibleSolidAt);
    /// Register an ALREADY-BUILT occupancy grid with the dynamics world. The async
    /// chunk-generation worker pre-fills the grid off-thread (forcePhysicsRebuild —
    /// pure CPU); only this registration must happen on the main thread.
    void registerPrebuiltGrid();
    /// Phase 4.4: take this chunk's grid OUT of the dynamics-world query list (sealed chunks —
    /// their interior is unreachable). The grid keeps its contents and incremental updates;
    /// registerPrebuiltGrid() re-adds it on unseal (registration dedups).
    void unregisterGridFromWorld();
    void cleanupPhysicsResources();

    // Collision entity management
    void addCollisionEntity(const glm::ivec3& localPos);
    void removeCollisionEntities(const glm::ivec3& localPos);

    void buildInitialCollisionShapes(const CubesArrayAccessFunc& getCubes,
                                     const StaticSubcubesAccessFunc& getStaticSubcubes,
                                     const StaticMicrocubesAccessFunc& getStaticMicrocubes,
                                     const IndexToLocalFunc& indexToLocal,
                                     const VisibleSolidFunc& visibleSolidAt);

    void batchUpdateCollisions(const CubesArrayAccessFunc& getCubes,
                               const StaticSubcubesAccessFunc& getStaticSubcubes,
                               const StaticMicrocubesAccessFunc& getStaticMicrocubes,
                               const IndexToLocalFunc& indexToLocal,
                               const VisibleSolidFunc& visibleSolidAt);

    void updateNeighborCollisionShapes(const glm::ivec3& localPos,
                                       const VisibleSolidFunc& visibleSolidAt,
                                       const MicrocubesAccessFunc& getMicrocubes,
                                       const StaticSubcubesAccessFunc& getStaticSubcubes);
    void endBulkOperation(const CubesArrayAccessFunc& getCubes,
                          const StaticSubcubesAccessFunc& getStaticSubcubes,
                          const StaticMicrocubesAccessFunc& getStaticMicrocubes,
                          const IndexToLocalFunc& indexToLocal,
                          const VisibleSolidFunc& visibleSolidAt);

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
    size_t getCollisionEntityCount()    const { return m_occupancyGrid.cubeCount(); }   // was a 0 stub
    size_t getCubeEntityCount()         const { return 0; }
    size_t getSubcubeEntityCount()      const { return 0; }
    void   debugPrintSpatialGridStats() const;

    // Collision shape helpers (occupancy grid only, no Bullet compound shape)
    void createCubeCollisionShape(const glm::ivec3& localPos,
                                  const VisibleSolidFunc& visibleSolidAt);
    void createSubcubeCollisionShape(const glm::ivec3& cubePos, const glm::ivec3& subcubePos,
                                     const SubcubeAccessFunc& getSubcube);
    void createMicrocubeCollisionShape(const glm::ivec3& cubePos, const glm::ivec3& subcubePos,
                                       const Microcube* microcube);

    void addCollisionEntity(const glm::ivec3& localPos,
                            const VisibleSolidFunc& visibleSolidAt,
                            const MicrocubesAccessFunc& getMicrocubes,
                            const StaticSubcubesAccessFunc& getStaticSubcubes);

    bool hasExposedFaces(const glm::ivec3& localPos, const VisibleSolidFunc& visibleSolidAt) const;

private:
    bool               collisionNeedsUpdate = false;
    bool               m_isInBulkOperation  = false;
    VoxelOccupancyGrid m_occupancyGrid;
    PhysicsWorld*      physicsWorld = nullptr;
    glm::ivec3         chunkOrigin  = glm::ivec3(0);
    // 4.2b: palette store of the owning chunk (see setVoxelStore); null in bare-vector tests.
    const ChunkVoxelStore* m_voxelStore = nullptr;
};

} // namespace Physics
} // namespace Phyxel
