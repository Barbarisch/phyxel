#include "IntegrationTestFixture.h"
#include "core/ChunkManager.h"
#include "core/DamageSystem.h"
#include "core/CoherentFragmentManager.h"
#include "core/KinematicVoxelManager.h"
#include "core/MaterialRegistry.h"
#include "physics/PhysicsWorld.h"
#include "physics/VoxelDynamicsWorld.h"
#include <glm/glm.hpp>
#include <filesystem>
#include <vector>

namespace Phyxel {
namespace Testing {

/**
 * Integration test for coherent world collapse (docs/DestructionSystemV2.md §5.B, P1.2b).
 *
 * DamageSystem operates on a real (Vulkan-initialized) ChunkManager, so this glue can't
 * be exercised headlessly — it lives here. The falsifiable A/B the design doc asks for:
 * a severed floating block toppled with the coherent flag ON becomes ONE rigid body;
 * with the flag OFF it scatters (no coherent body). Also pins the no-manager fallback.
 */
class CoherentCollapseIntegrationTest : public ChunkManagerTestFixture {
protected:
    void SetUp() override {
        ChunkManagerTestFixture::SetUp();
        if (!isEnvironmentReady() || !chunkManager) return;
        for (const char* p : {"resources/materials.json", "../resources/materials.json",
                              "../../resources/materials.json", "../../../resources/materials.json"}) {
            if (std::filesystem::exists(p)) { Core::MaterialRegistry::instance().loadFromJson(p); break; }
        }
        // populate=false: an EMPTY chunk, so our test block truly floats (disconnected
        // from any anchor). A populated chunk embeds the block in terrain -> never severs.
        chunkManager->createChunk(glm::ivec3(0, 0, 0), false);
        chunkManager->initializeAllChunkVoxelMaps();
    }

    // A floating solid block of cubes (empty world -> disconnected from any anchor).
    std::vector<glm::ivec3> buildBlock(glm::ivec3 origin, int w, int h) {
        std::vector<glm::ivec3> cells;
        for (int x = 0; x < w; ++x)
            for (int y = 0; y < h; ++y) {
                glm::ivec3 wp = origin + glm::ivec3(x, y, 0);
                chunkManager->addCube(wp);
                cells.push_back(wp);
            }
        return cells;
    }
    int countSolid(const std::vector<glm::ivec3>& cells) {
        int n = 0;
        for (const auto& wp : cells) if (chunkManager->hasVoxelAt(wp)) ++n;
        return n;
    }
};

TEST_F(CoherentCollapseIntegrationTest, FlagOn_TopplesSeveredComponentAsOneBody) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "Vulkan/physics env not ready";
    auto* voxelWorld = physicsWorld->getVoxelWorld();
    ASSERT_NE(voxelWorld, nullptr);

    auto cells = buildBlock(glm::ivec3(5, 5, 5), 4, 4);
    ASSERT_EQ(countSolid(cells), 16);

    Core::KinematicVoxelManager   kin;
    Core::CoherentFragmentManager mgr;
    mgr.setDeps(voxelWorld, &kin);
    const size_t bodiesBefore = voxelWorld->getBodyCount();

    DamageSystem dmg(chunkManager.get(), nullptr);
    dmg.setFragmentManager(&mgr);
    auto res = dmg.applyDamage(glm::vec3(5.5f, 5.5f, 5.5f), 1.8f, 600.0f, "force",
                               glm::vec3(0.0f), DamageSystem::NO_SUPPORT,
                               /*collapse*/ true, /*coherent*/ true);

    EXPECT_EQ(mgr.count(), 1u)                         << "severed block did not topple as ONE body";
    EXPECT_EQ(voxelWorld->getBodyCount(), bodiesBefore + 1);
    EXPECT_EQ(kin.count(), 1u);
    EXPECT_GE(res.debrisSpawned, 1);
    EXPECT_EQ(countSolid(cells), 0)                    << "cells left behind after topple";
}

TEST_F(CoherentCollapseIntegrationTest, FlagOff_ScattersWithNoCoherentBody) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "Vulkan/physics env not ready";
    auto* voxelWorld = physicsWorld->getVoxelWorld();
    ASSERT_NE(voxelWorld, nullptr);

    auto cells = buildBlock(glm::ivec3(20, 5, 5), 4, 4);
    ASSERT_EQ(countSolid(cells), 16);

    Core::KinematicVoxelManager   kin;
    Core::CoherentFragmentManager mgr;
    mgr.setDeps(voxelWorld, &kin);
    const size_t bodiesBefore = voxelWorld->getBodyCount();

    DamageSystem dmg(chunkManager.get(), nullptr);
    dmg.setFragmentManager(&mgr);
    dmg.applyDamage(glm::vec3(20.5f, 5.5f, 5.5f), 1.8f, 600.0f, "force",
                    glm::vec3(0.0f), DamageSystem::NO_SUPPORT,
                    /*collapse*/ true, /*coherent*/ false);

    EXPECT_EQ(mgr.count(), 0u)                       << "flag OFF must not create a coherent body";
    EXPECT_EQ(voxelWorld->getBodyCount(), bodiesBefore);
    EXPECT_EQ(countSolid(cells), 0)                  << "flag OFF still detaches (scatter)";
}

TEST_F(CoherentCollapseIntegrationTest, NoFragmentManager_FallsBackToScatter) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "Vulkan/physics env not ready";
    auto* voxelWorld = physicsWorld->getVoxelWorld();
    ASSERT_NE(voxelWorld, nullptr);

    auto cells = buildBlock(glm::ivec3(5, 5, 20), 4, 4);
    const size_t bodiesBefore = voxelWorld->getBodyCount();

    DamageSystem dmg(chunkManager.get(), nullptr);   // no setFragmentManager
    dmg.applyDamage(glm::vec3(5.5f, 5.5f, 20.5f), 1.8f, 600.0f, "force",
                    glm::vec3(0.0f), DamageSystem::NO_SUPPORT,
                    /*collapse*/ true, /*coherent*/ true);

    EXPECT_EQ(voxelWorld->getBodyCount(), bodiesBefore) << "no sink -> no coherent body";
    EXPECT_EQ(countSolid(cells), 0)                     << "still scatters";
}

// The user's actual scenario (not a floating block): a STONE tower ROOTED to the ground.
// Cut its base and the section above must sever and topple as a coherent body, while the
// rooted stub + floor stay. Proves structure felling works for a non-tree material (no
// species/cargo/rooted-trunk tree rules — plain connectivity + the coherent gather, whose
// "wood" partition is really "non-leaf structural material", so Stone flows through it).
TEST_F(CoherentCollapseIntegrationTest, RootedStoneTower_BaseCut_TopTopplesCoherently) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "Vulkan/physics env not ready";
    auto* voxelWorld = physicsWorld->getVoxelWorld();
    ASSERT_NE(voxelWorld, nullptr);

    // Stone floor (ground anchor via supportY=5) + a 1x1 stone tower rooted on it.
    for (int x = 0; x < 12; ++x)
        for (int z = 0; z < 12; ++z)
            chunkManager->addCubeWithMaterial(glm::ivec3(x, 5, z), "Stone");
    std::vector<glm::ivec3> tower;
    for (int y = 6; y <= 20; ++y) {
        glm::ivec3 wp(6, y, 6);
        chunkManager->addCubeWithMaterial(wp, "Stone");
        tower.push_back(wp);
    }

    Core::KinematicVoxelManager   kin;
    Core::CoherentFragmentManager mgr;
    mgr.setDeps(voxelWorld, &kin);

    DamageSystem dmg(chunkManager.get(), nullptr);
    dmg.setFragmentManager(&mgr);
    // Cut the tower at y=12 (r=0.9 reaches only that cell; neighbours are 1.0 away).
    auto res = dmg.applyDamage(glm::vec3(6.5f, 12.5f, 6.5f), 0.9f, 1500.0f, "force",
                               glm::vec3(0.0f), /*supportY*/ 5.0f, /*collapse*/ true, /*coherent*/ true);

    EXPECT_GE(mgr.count(), 1u) << "tower top did not topple as a coherent body";
    int aboveCut = 0;
    for (const auto& wp : tower) if (wp.y > 12 && chunkManager->hasVoxelAt(wp)) ++aboveCut;
    EXPECT_EQ(aboveCut, 0) << "tower top stayed up after the base cut (should sever + fall)";
    EXPECT_TRUE(chunkManager->hasVoxelAt(glm::ivec3(6, 6, 6))) << "rooted base stub wrongly fell";
    EXPECT_TRUE(chunkManager->hasVoxelAt(glm::ivec3(0, 5, 0))) << "anchored floor wrongly fell";
    EXPECT_GE(res.debrisSpawned, 1);
}

} // namespace Testing
} // namespace Phyxel
