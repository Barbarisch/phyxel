/**
 * R5 (L2 half) -- a graze that changes the surface must request a re-mesh, and must NOT
 * mark the chunk for database persistence.
 * docs/VoxelDamageVisualization.md 3.7 / 6.5.
 *
 * WHY THIS IS A SEPARATE TEST FROM R1/R3. Any blast that breaks even one voxel calls
 * updateDirtyChunks() and re-meshes every touched chunk as a side effect, so grazed voxels
 * in those chunks update anyway. The bug is invisible in the common case and appears exactly
 * in the weak-hit case the crack feature exists to visualize. Only a PURE graze -- broken == 0,
 * grazed > 0 -- can see it.
 *
 * RED BEFORE GREEN. Against the pre-P0 engine:
 *   - `voxelsStageChanged` did not exist (DamageResult had three fields), and
 *   - the flush was gated `if (res.voxelsBroken > 0)`, with the graze branch `continue`-ing
 *     before any mark, so a pure graze accumulated damage and never rebuilt the mesh.
 *
 * This lives in tests/integration and not tests/core because a ChunkManager needs Vulkan
 * (ChunkManagerTestFixture -> VulkanPhysicsTestFixture). The pure stage-transition predicate
 * that this wiring consumes is unit-tested in tests/core/VoxelDamageStateTest.cpp (6.0).
 */

#include <gtest/gtest.h>

#include "core/Chunk.h"
#include "core/ChunkManager.h"
#include "core/Cube.h"
#include "core/DamageStage.h"
#include "core/DamageSystem.h"
#include "core/MaterialRegistry.h"
#include "IntegrationTestFixture.h"

#include <filesystem>
#include <glm/glm.hpp>

using namespace Phyxel;
using Phyxel::Core::damageStage;
using Phyxel::Core::kDamageDisplayRef;

namespace {

// One isolated Stone voxel at the blast centre, so the energy that reaches it is exactly the
// energy we pass: falloff is 1.0 at d = 0, and with nothing around it, shielding is 0.
constexpr int   kVX = 16, kVY = 16, kVZ = 16;
constexpr float kStoneToughness = 110.0f;   // resources/materials.json break block

} // namespace

class VoxelGrazeRemeshIntegrationTest : public Testing::ChunkManagerTestFixture {
protected:
    void SetUp() override {
        Testing::ChunkManagerTestFixture::SetUp();
        if (!isEnvironmentReady() || !chunkManager) return;
        for (const char* p : {"resources/materials.json", "../resources/materials.json",
                              "../../resources/materials.json", "../../../resources/materials.json"}) {
            if (std::filesystem::exists(p)) { Core::MaterialRegistry::instance().loadFromJson(p); break; }
        }
        chunkManager->createChunk(glm::ivec3(0, 0, 0), false);   // empty chunk, world 0..31
    }

    glm::vec3 centre() const { return glm::vec3(kVX + 0.5f, kVY + 0.5f, kVZ + 0.5f); }

    Chunk* homeChunk() {
        return chunkManager->getChunkAtCoord(
            ChunkManager::worldToChunkCoord(glm::ivec3(kVX, kVY, kVZ)));
    }

    Cube* target() { return chunkManager->getCubeAt(glm::ivec3(kVX, kVY, kVZ)); }

    void placeStone() {
        chunkManager->addCubeWithMaterial(glm::ivec3(kVX, kVY, kVZ), "Stone");
        // The chunk is dirty from the placement itself; clear that so the assertions below
        // measure what the GRAZE did, not what building the rig did.
        chunkManager->updateDirtyChunks();
        if (Chunk* c = homeChunk()) c->setDirty(false);
    }
};

// ---------------------------------------------------------------------------
// The red: a pure graze reports a visible change and re-meshes.
// ---------------------------------------------------------------------------

TEST_F(VoxelGrazeRemeshIntegrationTest, PureGrazeReportsAStageChange) {
    if (!isEnvironmentReady()) GTEST_SKIP() << "Vulkan environment unavailable";
    placeStone();
    ASSERT_NE(target(), nullptr);

    // 20 energy against toughness 110: far below the break threshold, but well above the
    // first stage boundary (kDamageDisplayRef/15 = 2.0), so it is visible.
    DamageSystem dmg(chunkManager.get(), nullptr);
    const auto res = dmg.applyDamage(centre(), 1.0f, 20.0f);

    EXPECT_EQ(res.voxelsBroken, 0) << "this must be a PURE graze or it proves nothing -- "
                                      "a break re-meshes the chunk as a side effect";
    EXPECT_GT(res.voxelsGrazed, 0) << "the blast must actually reach the voxel";
    EXPECT_GT(res.voxelsStageChanged, 0)
        << "a graze from pristine to stage " << int(damageStage(20.0f, kDamageDisplayRef))
        << " changes what is drawn and must request a re-mesh";

    ASSERT_NE(target(), nullptr) << "the voxel must survive a sub-threshold hit";
    EXPECT_NEAR(target()->getAccumulatedDamage(), 20.0f, 1e-3f);
    EXPECT_GT(damageStage(target()->getAccumulatedDamage(), kDamageDisplayRef), 0);
}

// ---------------------------------------------------------------------------
// The tier guard: mesh-stale is not the same as data-changed.
// ---------------------------------------------------------------------------

TEST_F(VoxelGrazeRemeshIntegrationTest, GrazeDoesNotMarkTheChunkForDatabasePersistence) {
    if (!isEnvironmentReady()) GTEST_SKIP() << "Vulkan environment unavailable";
    placeStone();

    DamageSystem dmg(chunkManager.get(), nullptr);
    const auto res = dmg.applyDamage(centre(), 1.0f, 20.0f);
    ASSERT_GT(res.voxelsStageChanged, 0) << "precondition: the graze must have been visible";

    Chunk* c = homeChunk();
    ASSERT_NE(c, nullptr);
    EXPECT_FALSE(c->getIsDirty())
        << "the graze path must use markChunkForRemesh, NOT markChunkDirty. markChunkDirty "
           "also sets the DB-dirty flag, which makes the streaming evictor re-save the chunk "
           "to SQLite -- and damage has no DB field, so every such write persists nothing. "
           "One blast grazes thousands of voxels across many chunks; that is precisely the "
           "mass-eviction case that caused multi-hundred-ms save stalls.";
}

// ---------------------------------------------------------------------------
// Control 1: a graze that changes nothing visible must not request a rebuild.
// ---------------------------------------------------------------------------

TEST_F(VoxelGrazeRemeshIntegrationTest, SubStageGrazeRequestsNoRebuild) {
    if (!isEnvironmentReady()) GTEST_SKIP() << "Vulkan environment unavailable";
    placeStone();

    DamageSystem dmg(chunkManager.get(), nullptr);
    const auto first = dmg.applyDamage(centre(), 1.0f, 20.0f);
    ASSERT_GT(first.voxelsStageChanged, 0);

    // A second, tiny hit: 20.0 -> 20.5 energy. One stage band is 2.0 wide, so both land on
    // stage 10 and not a single pixel changes. Re-meshing for this would be pure cost.
    ASSERT_EQ(damageStage(20.0f,  kDamageDisplayRef),
              damageStage(20.5f, kDamageDisplayRef)) << "rig assumption: same stage band";

    const auto second = dmg.applyDamage(centre(), 1.0f, 0.5f);
    EXPECT_GT(second.voxelsGrazed, 0)      << "the hit must still land";
    EXPECT_EQ(second.voxelsStageChanged, 0)
        << "a graze that stays inside one stage band changes zero pixels and must not "
           "request a re-mesh -- rebuild count is bounded by VISIBLE changes, not by hits";
    EXPECT_NEAR(target()->getAccumulatedDamage(), 20.5f, 1e-3f)
        << "...but the damage must still accumulate";
}

// ---------------------------------------------------------------------------
// Control 2: the rig can break a voxel at all, so `broken == 0` above is a real pure
// graze rather than a blast that simply missed.
// ---------------------------------------------------------------------------

TEST_F(VoxelGrazeRemeshIntegrationTest, ControlTheSameRigCanBreakTheVoxel) {
    if (!isEnvironmentReady()) GTEST_SKIP() << "Vulkan environment unavailable";
    placeStone();

    DamageSystem dmg(chunkManager.get(), nullptr);
    const auto res = dmg.applyDamage(centre(), 1.0f, kStoneToughness * 2.0f);

    EXPECT_GT(res.voxelsBroken, 0)
        << "the control proves the blast geometry reaches the voxel; without it, "
           "'broken == 0' in the graze tests could mean the blast simply missed";
    EXPECT_EQ(chunkManager->getCubeAt(glm::ivec3(kVX, kVY, kVZ)), nullptr);
}
