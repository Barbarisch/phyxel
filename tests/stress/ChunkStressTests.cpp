#include "StressTestFixture.h"
#include "core/ChunkManager.h"
#include "physics/PhysicsWorld.h"
#include <memory>
#include <vector>

namespace Phyxel {
namespace Testing {

/**
 * @brief Stress tests for Chunk system under extreme load
 */
class ChunkStressTests : public StressTestFixture {
protected:
    void SetUp() override {
        physicsWorld = std::make_unique<Physics::PhysicsWorld>();
        physicsWorld->initialize();
    }
    
    void TearDown() override {
        physicsWorld.reset();
    }
    
    std::unique_ptr<Physics::PhysicsWorld> physicsWorld;
};

// D4: the ten Chunk stress tests (Create1000Chunks, CreateAndDestroyChunks, RapidChunkCreationBursts,
// MassiveFaceCullingOperations, ConcurrentChunkFaceRebuilding, RapidVoxelModifications, AllocateMassiveChunkArray,
// ChunkCreationDestruction_MemoryLeak, LongRunningChunkOperations, ExtremeChunkCount) were retired with today's
// Chunk store. The file and fixture stay so phyxel_stress_tests keeps building.

} // namespace Testing
} // namespace Phyxel
