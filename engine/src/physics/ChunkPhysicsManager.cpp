#include "physics/ChunkPhysicsManager.h"
#include "core/Cube.h"
#include "core/Subcube.h"
#include "core/Microcube.h"
#include "physics/PhysicsWorld.h"
#include "utils/Logger.h"

namespace Phyxel {
namespace Physics {

ChunkPhysicsManager::ChunkPhysicsManager()
    : collisionNeedsUpdate(false)
    , m_isInBulkOperation(false)
    , physicsWorld(nullptr)
    , chunkOrigin(0)
{
}

ChunkPhysicsManager::~ChunkPhysicsManager() {
    cleanupPhysicsResources();
}

ChunkPhysicsManager::ChunkPhysicsManager(ChunkPhysicsManager&& other) noexcept
    : collisionNeedsUpdate(other.collisionNeedsUpdate)
    , m_isInBulkOperation(other.m_isInBulkOperation)
    , m_occupancyGrid(std::move(other.m_occupancyGrid))
    , physicsWorld(other.physicsWorld)
    , chunkOrigin(other.chunkOrigin)
{
    other.physicsWorld = nullptr;
}

ChunkPhysicsManager& ChunkPhysicsManager::operator=(ChunkPhysicsManager&& other) noexcept {
    if (this != &other) {
        cleanupPhysicsResources();

        collisionNeedsUpdate = other.collisionNeedsUpdate;
        m_isInBulkOperation  = other.m_isInBulkOperation;
        m_occupancyGrid      = std::move(other.m_occupancyGrid);
        physicsWorld         = other.physicsWorld;
        chunkOrigin          = other.chunkOrigin;

        other.physicsWorld = nullptr;
    }
    return *this;
}

void ChunkPhysicsManager::initialize(PhysicsWorld* world, const glm::ivec3& origin) {
    physicsWorld = world;
    chunkOrigin  = origin;
    m_occupancyGrid.setChunkOrigin(origin);
    LOG_TRACE("ChunkPhysicsManager", "Initialized for chunk");
}

void ChunkPhysicsManager::cleanupPhysicsResources() {
    if (physicsWorld) {
        if (auto* vw = physicsWorld->getVoxelWorld())
            vw->unregisterGrid(&m_occupancyGrid);
    }
    m_occupancyGrid.clear();
}

void ChunkPhysicsManager::createChunkPhysicsBody(const CubesArrayAccessFunc& getCubes,
                                                 const StaticSubcubesAccessFunc& getStaticSubcubes,
                                                 const StaticMicrocubesAccessFunc& getStaticMicrocubes,
                                                 const IndexToLocalFunc& indexToLocal,
                                                 const CubeAccessFunc& getCube) {
    if (!physicsWorld) return;

    buildInitialCollisionShapes(getCubes, getStaticSubcubes, getStaticMicrocubes, indexToLocal, getCube);

    if (auto* vw = physicsWorld->getVoxelWorld())
        vw->registerGrid(&m_occupancyGrid);

    LOG_TRACE("ChunkPhysicsManager", "Chunk occupancy grid registered");
}

void ChunkPhysicsManager::updateChunkPhysicsBody(const CubesArrayAccessFunc& getCubes,
                                                 const StaticSubcubesAccessFunc& getStaticSubcubes,
                                                 const StaticMicrocubesAccessFunc& getStaticMicrocubes,
                                                 const IndexToLocalFunc& indexToLocal,
                                                 const CubeAccessFunc& getCube) {
    // Occupancy grid is updated incrementally via add/removeCollisionEntities
    batchUpdateCollisions(getCubes, getStaticSubcubes, getStaticMicrocubes, indexToLocal, getCube);
}

void ChunkPhysicsManager::forcePhysicsRebuild(const CubesArrayAccessFunc& getCubes,
                                              const StaticSubcubesAccessFunc& getStaticSubcubes,
                                              const StaticMicrocubesAccessFunc& getStaticMicrocubes,
                                              const IndexToLocalFunc& indexToLocal,
                                              const CubeAccessFunc& getCube) {
    buildInitialCollisionShapes(getCubes, getStaticSubcubes, getStaticMicrocubes, indexToLocal, getCube);
    LOG_TRACE("ChunkPhysicsManager", "Occupancy grid rebuilt");
}

void ChunkPhysicsManager::addCollisionEntity(const glm::ivec3& /*localPos*/) {
    // No-op without callback context; use overload with callbacks
}

void ChunkPhysicsManager::addCollisionEntity(const glm::ivec3& localPos,
                                             const CubeAccessFunc& getCube,
                                             const MicrocubesAccessFunc& getMicrocubes,
                                             const StaticSubcubesAccessFunc& getStaticSubcubes) {
    const Cube* cube = getCube(localPos);
    if (cube && cube->isVisible()) {
        createCubeCollisionShape(localPos, getCube);
        return;
    }

    for (int sx = 0; sx < 3; ++sx) {
        for (int sy = 0; sy < 3; ++sy) {
            for (int sz = 0; sz < 3; ++sz) {
                glm::ivec3 subcubePos(sx, sy, sz);
                auto mcList = getMicrocubes(localPos, subcubePos);
                if (!mcList.empty()) {
                    for (const Microcube* mc : mcList)
                        createMicrocubeCollisionShape(localPos, subcubePos, mc);
                } else {
                    auto subcubes = getStaticSubcubes(localPos);
                    auto getSubcube = [&subcubes](const glm::ivec3&, const glm::ivec3& sPos) -> Subcube* {
                        for (Subcube* s : subcubes)
                            if (s->getLocalPosition() == sPos) return s;
                        return nullptr;
                    };
                    createSubcubeCollisionShape(localPos, subcubePos, getSubcube);
                }
            }
        }
    }
}

void ChunkPhysicsManager::removeCollisionEntities(const glm::ivec3& localPos) {
    m_occupancyGrid.setCube(localPos, false);
    m_occupancyGrid.markSubdivided(localPos, false);
    collisionNeedsUpdate = true;
}

bool ChunkPhysicsManager::hasExposedFaces(const glm::ivec3& localPos, const CubeAccessFunc& getCube) const {
    static const glm::ivec3 dirs[6] = {
        {0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}
    };
    for (const auto& d : dirs) {
        glm::ivec3 n = localPos + d;
        if (n.x < 0 || n.x >= 32 || n.y < 0 || n.y >= 32 || n.z < 0 || n.z >= 32)
            return true;
        const Cube* nc = getCube(n);
        if (!nc || !nc->isVisible()) return true;
    }
    return false;
}

void ChunkPhysicsManager::batchUpdateCollisions(const CubesArrayAccessFunc& getCubes,
                                                const StaticSubcubesAccessFunc& getStaticSubcubes,
                                                const StaticMicrocubesAccessFunc& getStaticMicrocubes,
                                                const IndexToLocalFunc& indexToLocal,
                                                const CubeAccessFunc& getCube) {
    if (!collisionNeedsUpdate) return;
    buildInitialCollisionShapes(getCubes, getStaticSubcubes, getStaticMicrocubes, indexToLocal, getCube);
    collisionNeedsUpdate = false;
}

void ChunkPhysicsManager::buildInitialCollisionShapes(const CubesArrayAccessFunc& getCubes,
                                                      const StaticSubcubesAccessFunc& getStaticSubcubes,
                                                      const StaticMicrocubesAccessFunc& getStaticMicrocubes,
                                                      const IndexToLocalFunc& indexToLocal,
                                                      const CubeAccessFunc& /*getCube*/) {
    m_occupancyGrid.clear();
    m_occupancyGrid.setChunkOrigin(chunkOrigin);

    const auto& cubes = getCubes();
    for (size_t i = 0; i < cubes.size(); ++i) {
        const Cube* cube = cubes[i].get();
        if (!cube || !cube->isVisible()) continue;
        m_occupancyGrid.setCube(indexToLocal(i), true);
    }

    auto staticSubcubes = getStaticSubcubes(glm::ivec3(0));
    for (const Subcube* subcube : staticSubcubes) {
        if (!subcube || subcube->isBroken() || !subcube->isVisible()) continue;
        glm::ivec3 parentLocalPos = subcube->getPosition() - chunkOrigin;
        if (parentLocalPos.x < 0 || parentLocalPos.x >= 32 ||
            parentLocalPos.y < 0 || parentLocalPos.y >= 32 ||
            parentLocalPos.z < 0 || parentLocalPos.z >= 32) continue;
        glm::ivec3 sLocal = subcube->getLocalPosition();
        m_occupancyGrid.setCube(parentLocalPos, true);
        m_occupancyGrid.markSubdivided(parentLocalPos, true);
        m_occupancyGrid.setSubcube(parentLocalPos, sLocal, true);
    }

    const auto& staticMicrocubes = getStaticMicrocubes();
    for (const auto& mc : staticMicrocubes) {
        if (!mc || mc->isBroken() || !mc->isVisible()) continue;
        glm::ivec3 parentLocalPos = mc->getParentCubePosition() - chunkOrigin;
        if (parentLocalPos.x < 0 || parentLocalPos.x >= 32 ||
            parentLocalPos.y < 0 || parentLocalPos.y >= 32 ||
            parentLocalPos.z < 0 || parentLocalPos.z >= 32) continue;
        glm::ivec3 subcubePos   = mc->getSubcubeLocalPosition();
        glm::ivec3 microcubePos = mc->getMicrocubeLocalPosition();
        m_occupancyGrid.setCube(parentLocalPos, true);
        m_occupancyGrid.markSubdivided(parentLocalPos, true);
        m_occupancyGrid.setSubcube(parentLocalPos, subcubePos, true);
        m_occupancyGrid.markSubcubeSubdivided(parentLocalPos, subcubePos, true);
        m_occupancyGrid.setMicrocube(parentLocalPos, subcubePos, microcubePos, true);
    }

    LOG_TRACE("ChunkPhysicsManager", "Built occupancy grid for chunk");
}

void ChunkPhysicsManager::updateNeighborCollisionShapes(const glm::ivec3& /*localPos*/,
                                                        const CubeAccessFunc& /*getCube*/,
                                                        const MicrocubesAccessFunc& /*getMicrocubes*/,
                                                        const StaticSubcubesAccessFunc& /*getStaticSubcubes*/) {
    // Occupancy grid tracks all filled positions — no neighbor exposure update needed.
}

void ChunkPhysicsManager::endBulkOperation(const CubesArrayAccessFunc& getCubes,
                                           const StaticSubcubesAccessFunc& getStaticSubcubes,
                                           const StaticMicrocubesAccessFunc& getStaticMicrocubes,
                                           const IndexToLocalFunc& indexToLocal,
                                           const CubeAccessFunc& getCube) {
    if (!m_isInBulkOperation) return;
    m_isInBulkOperation = false;
    buildInitialCollisionShapes(getCubes, getStaticSubcubes, getStaticMicrocubes, indexToLocal, getCube);
}

void ChunkPhysicsManager::validateCollisionSystem()    const {}
void ChunkPhysicsManager::debugLogSpatialGrid()        const {}
void ChunkPhysicsManager::debugPrintSpatialGridStats() const {}

void ChunkPhysicsManager::createCubeCollisionShape(const glm::ivec3& localPos,
                                                    const CubeAccessFunc& /*getCube*/) {
    m_occupancyGrid.setCube(localPos, true);
    m_occupancyGrid.markSubdivided(localPos, false);
}

void ChunkPhysicsManager::createSubcubeCollisionShape(const glm::ivec3& cubePos,
                                                       const glm::ivec3& subcubePos,
                                                       const SubcubeAccessFunc& /*getSubcube*/) {
    m_occupancyGrid.setCube(cubePos, true);
    m_occupancyGrid.markSubdivided(cubePos, true);
    m_occupancyGrid.setSubcube(cubePos, subcubePos, true);
}

void ChunkPhysicsManager::createMicrocubeCollisionShape(const glm::ivec3& cubePos,
                                                         const glm::ivec3& subcubePos,
                                                         const Microcube* microcube) {
    glm::ivec3 mLocalPos = microcube->getMicrocubeLocalPosition();
    m_occupancyGrid.setCube(cubePos, true);
    m_occupancyGrid.markSubdivided(cubePos, true);
    m_occupancyGrid.setSubcube(cubePos, subcubePos, true);
    m_occupancyGrid.markSubcubeSubdivided(cubePos, subcubePos, true);
    m_occupancyGrid.setMicrocube(cubePos, subcubePos, mLocalPos, true);
}

} // namespace Physics
} // namespace Phyxel
