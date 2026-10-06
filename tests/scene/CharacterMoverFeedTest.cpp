// CharacterMoverFeedTest — DebrisInteractionPlan Phase 3a: every AnimatedVoxelCharacter feeds its
// limbs to GPU debris as oriented mover boxes with per-limb velocity, and ONLY for full ticks.
//
// The contract (AnimatedVoxelCharacter::collectMoverBoxes):
//   * every frame feeds the limbs (the feed calls it once per frame);
//   * a tick deferred by update-LOD feeds the last pose EXTRAPOLATED along each limb's velocity by
//     the banked time - dropping it made the boxes vanish and reappear deep inside a pile (live:
//     547 mm peak for a far NPC vs 199 mm ticking every frame);
//   * boxes are the bind-pose half extents (oriented), never the inflated AABB refit;
//   * per-limb velocity is finite and clamped (a teleport must not fling debris).

#include <gtest/gtest.h>

#include <cmath>

#include "core/Chunk.h"
#include "core/ChunkManager.h"
#include "physics/PhysicsWorld.h"
#include "physics/VoxelOccupancyGrid.h"
#include "scene/AnimatedVoxelCharacter.h"

using namespace Phyxel;
using Phyxel::Scene::AnimatedVoxelCharacter;

namespace {

// Update-LOD is process-wide state (switch default ON, no viewer): every test restores BOTH - a
// viewer left 5 km away made every later character test's characters defer their ticks.
struct LodGuard {
    ~LodGuard() {
        AnimatedVoxelCharacter::setLODEnabled(true);
        AnimatedVoxelCharacter::clearViewerPosition();
    }
};

constexpr const char* kHumanoid = "resources/animated_characters/humanoid.anim";
constexpr float kDt = 1.0f / 60.0f;

struct FloorWorld {
    std::unique_ptr<Phyxel::Physics::PhysicsWorld> physics;
    Phyxel::ChunkManager cm;
    std::unique_ptr<Phyxel::Physics::VoxelOccupancyGrid> grid;

    FloorWorld() {
        physics = std::make_unique<Phyxel::Physics::PhysicsWorld>();
        physics->initialize();
        cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
        grid = std::make_unique<Phyxel::Physics::VoxelOccupancyGrid>();
        grid->setChunkOrigin(glm::ivec3(0, 0, 0));
        for (int x = 0; x < 32; ++x)
            for (int z = 0; z < 32; ++z) grid->setCube(glm::ivec3(x, 15, z), true);
        physics->getVoxelWorld()->registerGrid(grid.get());
    }
    ~FloorWorld() { physics->getVoxelWorld()->unregisterGrid(grid.get()); }
};

}  // namespace

TEST(CharacterMoverFeed, EveryFrameFeedsTheSameLimbsWhenNoTimeIsBanked) {
    FloorWorld w;
    LodGuard lod;
    AnimatedVoxelCharacter::setLODEnabled(false);
    AnimatedVoxelCharacter ch(w.physics.get(), glm::vec3(16, 16.05f, 16));
    if (!ch.loadModel(kHumanoid)) GTEST_SKIP() << "repo-root CWD required for the rig";
    ch.setChunkManager(&w.cm);
    for (int i = 0; i < 5; ++i) ch.update(kDt);

    std::vector<AnimatedVoxelCharacter::MoverBox> boxes;
    ASSERT_TRUE(ch.collectMoverBoxes(boxes)) << "a character that just ticked must feed its limbs";
    const auto first = boxes;
    EXPECT_GT(first.size(), 4u);
    boxes.clear();
    ASSERT_TRUE(ch.collectMoverBoxes(boxes)) << "a second frame with no new tick still feeds";
    ASSERT_EQ(boxes.size(), first.size());
    for (size_t i = 0; i < boxes.size(); ++i)
        EXPECT_LT(glm::length(boxes[i].center - first[i].center), 1e-5f) << "no banked time: same pose";
}

TEST(CharacterMoverFeed, BoxesAreOrientedBindPoseExtentsWithFiniteClampedVelocity) {
    FloorWorld w;
    LodGuard lod;
    AnimatedVoxelCharacter::setLODEnabled(false);
    AnimatedVoxelCharacter ch(w.physics.get(), glm::vec3(16, 16.05f, 16));
    if (!ch.loadModel(kHumanoid)) GTEST_SKIP() << "repo-root CWD required for the rig";
    ch.setChunkManager(&w.cm);
    for (int i = 0; i < 5; ++i) ch.update(kDt);

    std::vector<AnimatedVoxelCharacter::MoverBox> boxes;
    ASSERT_TRUE(ch.collectMoverBoxes(boxes));
    const auto info = ch.getSegmentBoxInfo();
    for (size_t i = 0; i < boxes.size(); ++i) {
        const auto& b = boxes[i];
        EXPECT_NEAR(glm::length(glm::vec4(b.rotation.x, b.rotation.y, b.rotation.z, b.rotation.w)), 1.0f, 1e-3f);
        for (int a = 0; a < 3; ++a) EXPECT_TRUE(std::isfinite(b.velocity[a]));
        EXPECT_LE(glm::length(b.velocity), 20.0f + 1e-3f);
    }
    // Oriented extents are the BIND-POSE ones: never larger than the per-frame AABB refit.
    size_t k = 0;
    for (const auto& s : info) {
        if (s.halfExtents.x <= 0.0f) continue;
        if (k >= boxes.size()) break;
        EXPECT_LE(boxes[k].halfExtents.x, s.worldHalfExtents.x + 1e-4f + std::max({s.halfExtents.x, s.halfExtents.y, s.halfExtents.z}));
        ++k;
    }

    // A teleport between ticks: the velocity it implies is clamped, not passed on.
    ch.setPosition(glm::vec3(16, 16.05f, 16) + glm::vec3(50.0f, 0.0f, 0.0f));
    ch.update(kDt);
    boxes.clear();
    ASSERT_TRUE(ch.collectMoverBoxes(boxes));
    for (const auto& b : boxes) EXPECT_LE(glm::length(b.velocity), 20.0f + 1e-3f) << "teleport flings debris";
}

TEST(CharacterMoverFeed, ATickDeferredByUpdateLodIsExtrapolatedAlongLimbVelocity) {
    FloorWorld w;
    LodGuard lod;
    AnimatedVoxelCharacter::setLODEnabled(true);
    AnimatedVoxelCharacter::setViewerPosition(glm::vec3(16, 16, 16));
    AnimatedVoxelCharacter ch(w.physics.get(), glm::vec3(16, 16.05f, 16));
    if (!ch.loadModel(kHumanoid)) GTEST_SKIP() << "repo-root CWD required for the rig";
    ch.setChunkManager(&w.cm);
    for (int i = 0; i < 5; ++i) ch.update(kDt);   // near the viewer: full ticks
    std::vector<AnimatedVoxelCharacter::MoverBox> before;
    ASSERT_TRUE(ch.collectMoverBoxes(before));

    // Far from the viewer the character ticks at a reduced rate: this frame is deferred, kDt banked.
    AnimatedVoxelCharacter::setViewerPosition(glm::vec3(16, 16, 16) + glm::vec3(5000.0f, 0.0f, 0.0f));
    ch.update(kDt);
    std::vector<AnimatedVoxelCharacter::MoverBox> after;
    ASSERT_TRUE(ch.collectMoverBoxes(after)) << "a deferred character must keep pushing debris";
    ASSERT_EQ(after.size(), before.size());
    for (size_t i = 0; i < after.size(); ++i)
        EXPECT_LT(glm::length(after[i].center - (before[i].center + before[i].velocity * kDt)), 1e-4f)
            << "limb " << i << ": the deferred pose is the last one moved along its velocity";
}
