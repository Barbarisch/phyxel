#include <gtest/gtest.h>

#include <memory>

#include <glm/glm.hpp>

#include "core/ChunkManager.h"
#include "physics/PhysicsWorld.h"
#include "physics/VoxelDynamicsWorld.h"
#include "physics/VoxelOccupancyGrid.h"
#include "scene/AnimatedVoxelCharacter.h"

using namespace Phyxel;
using Phyxel::Scene::AnimatedVoxelCharacter;

// ============================================================================
// Residency gate (docs/StructurePipelineGaps.md 2026-08-17 "player loses ground
// while worldforge_build owns residency"): over UNSTREAMED terrain — the chunk
// at the feet AND the chunk below both absent from chunkMap — the ground is
// UNKNOWN, not empty, and a kinematic character must hold in place instead of
// free-falling through a world that hasn't arrived yet (observed y=-114k when a
// streaming-focus override starved the spawn area; same family as the recorded
// "falling player outruns its own terrain" teleport hazard). Streamed AIR is a
// different thing entirely: air chunks stay in chunkMap, so falling through
// known air keeps working. The gate is scoped to streaming-generation worlds —
// static worlds keep legacy behavior.
//
// Red-before-green: HoldsOverUnstreamedTerrain fell to y≈-30 before the gate.
// ============================================================================

namespace {

struct GateWorld {
    std::unique_ptr<Phyxel::Physics::PhysicsWorld> physics;
    ChunkManager cm;
    std::vector<std::unique_ptr<Phyxel::Physics::VoxelOccupancyGrid>> grids;

    explicit GateWorld(bool streaming) {
        physics = std::make_unique<Phyxel::Physics::PhysicsWorld>();
        physics->initialize();
        cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
        if (streaming)
            cm.configureStreamingGeneration(true, WorldGenerator::GenerationType::Flat, 1);
    }

    // (addResidentChunk made today's Chunk resident in chunkMap; the Chunk store went with D4, and with it the two
    // tests that need a resident chunk -- FallsThroughKnownAirAndLands, ResumesWhenTerrainStreamsIn.)

    // Register a 32x32 collision floor at world y = `floorY` under chunk (0,*,0).
    void addFloorGrid(int floorY) {
        auto g = std::make_unique<Phyxel::Physics::VoxelOccupancyGrid>();
        g->setChunkOrigin(glm::ivec3(0, (floorY / 32) * 32, 0));
        for (int x = 0; x < 32; ++x)
            for (int z = 0; z < 32; ++z)
                g->setCube(glm::ivec3(x, floorY % 32, z), true);
        physics->getVoxelWorld()->registerGrid(g.get());
        grids.push_back(std::move(g));
    }

    std::unique_ptr<AnimatedVoxelCharacter> makeCharacter(const glm::vec3& pos) {
        auto ch = std::make_unique<AnimatedVoxelCharacter>(physics.get(), pos);
        EXPECT_TRUE(ch->loadModel("resources/animated_characters/humanoid.anim"));
        ch->setChunkManager(&cm);
        return ch;
    }
};

void pump(AnimatedVoxelCharacter& ch, int frames) {
    for (int i = 0; i < frames; ++i) ch.update(1.0f / 60.0f);
}

}  // namespace

// The red driver: nothing resident anywhere -> the character waits where it is.
TEST(CharacterResidencyGateTest, HoldsOverUnstreamedTerrain) {
    GateWorld w(/*streaming=*/true);
    auto ch = w.makeCharacter(glm::vec3(16.0f, 40.0f, 16.0f));
    pump(*ch, 120);
    EXPECT_NEAR(ch->getPosition().y, 40.0f, 0.01f)
        << "character fell through unstreamed terrain (2 s of updates)";
}

// Non-streaming worlds keep legacy behavior: absence of a chunk is not evidence of
// anything (static worlds only populate what generation placed), so no hold.
TEST(CharacterResidencyGateTest, StaticWorldsKeepLegacyFalling) {
    GateWorld w(/*streaming=*/false);
    auto ch = w.makeCharacter(glm::vec3(16.0f, 40.0f, 16.0f));
    pump(*ch, 60);
    EXPECT_LT(ch->getPosition().y, 39.0f)
        << "static-world character should fall exactly as before the gate";
}
