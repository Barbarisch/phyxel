/**
 * Phase 1.1 (docs/DestructionSystemV2.md §5.B) — CoherentFragmentService::mergeVoxelsToBoxes.
 *
 * The pure-geometry half of coherent-fragment creation: greedy-merge a set of
 * local-space voxels (mixed cube/subcube/microcube) into a minimal box set for a
 * compound rigid body, with a caller-supplied per-voxel mass model (H4). Pins:
 * coverage (no gaps/overlap), mass conservation, that adjacent voxels merge, and
 * mixed-resolution handling.
 */

#include <gtest/gtest.h>
#include "core/CoherentFragmentService.h"
#include "core/KinematicVoxelManager.h"
#include "physics/VoxelDynamicsWorld.h"
#include "physics/VoxelRigidBody.h"
#include <glm/gtc/matrix_transform.hpp>
#include <numeric>

using namespace Phyxel::Core;

namespace {

KinematicVoxel cube(float x, float y, float z, float scale = 1.0f) {
    KinematicVoxel v;
    v.localPos = glm::vec3(x, y, z);
    v.scale    = glm::vec3(scale);
    v.materialName = "Wood";
    return v;
}

// Unit mass per voxel — makes mass sums easy to reason about.
float unitMass(const KinematicVoxel&) { return 1.0f; }

float boxVolume(const FragmentBox& b) {
    return (2 * b.halfExtents.x) * (2 * b.halfExtents.y) * (2 * b.halfExtents.z);
}

} // namespace

TEST(CoherentFragmentServiceTest, EmptyInputYieldsEmpty) {
    EXPECT_TRUE(CoherentFragmentService::mergeVoxelsToBoxes({}, unitMass).empty());
}

TEST(CoherentFragmentServiceTest, SingleCubeIsOneBox) {
    auto boxes = CoherentFragmentService::mergeVoxelsToBoxes({cube(0, 0, 0)}, unitMass);
    ASSERT_EQ(boxes.size(), 1u);
    EXPECT_NEAR(boxes[0].center.x, 0.0f, 1e-4f);
    EXPECT_NEAR(boxes[0].halfExtents.x, 0.5f, 1e-4f);
    EXPECT_NEAR(boxes[0].halfExtents.y, 0.5f, 1e-4f);
    EXPECT_NEAR(boxes[0].halfExtents.z, 0.5f, 1e-4f);
    EXPECT_NEAR(boxes[0].mass, 1.0f, 1e-4f);
}

TEST(CoherentFragmentServiceTest, AdjacentCubesMergeIntoOneBox) {
    // Two unit cubes sharing a face on X -> a single 2x1x1 box.
    auto boxes = CoherentFragmentService::mergeVoxelsToBoxes({cube(0,0,0), cube(1,0,0)}, unitMass);
    ASSERT_EQ(boxes.size(), 1u);
    EXPECT_NEAR(boxes[0].halfExtents.x, 1.0f, 1e-4f);  // spans 2 on X
    EXPECT_NEAR(boxes[0].halfExtents.y, 0.5f, 1e-4f);
    EXPECT_NEAR(boxes[0].center.x, 0.5f, 1e-4f);
    EXPECT_NEAR(boxes[0].mass, 2.0f, 1e-4f);           // conserved
}

TEST(CoherentFragmentServiceTest, SolidBlockMergesToSingleBox) {
    // 2x2x2 solid block of unit cubes -> exactly one merged box, mass 8.
    std::vector<KinematicVoxel> vox;
    for (int x = 0; x < 2; ++x)
        for (int y = 0; y < 2; ++y)
            for (int z = 0; z < 2; ++z)
                vox.push_back(cube((float)x, (float)y, (float)z));
    auto boxes = CoherentFragmentService::mergeVoxelsToBoxes(vox, unitMass);
    ASSERT_EQ(boxes.size(), 1u);
    EXPECT_NEAR(boxes[0].halfExtents.x, 1.0f, 1e-4f);
    EXPECT_NEAR(boxes[0].halfExtents.y, 1.0f, 1e-4f);
    EXPECT_NEAR(boxes[0].halfExtents.z, 1.0f, 1e-4f);
    EXPECT_NEAR(boxes[0].mass, 8.0f, 1e-4f);
}

TEST(CoherentFragmentServiceTest, MassAndVolumeConservedForDisjointCubes) {
    // Three non-adjacent unit cubes -> 3 boxes; mass and volume both conserved.
    std::vector<KinematicVoxel> vox = {cube(0,0,0), cube(5,0,0), cube(0,5,0)};
    auto boxes = CoherentFragmentService::mergeVoxelsToBoxes(vox, unitMass);
    EXPECT_EQ(boxes.size(), 3u);
    float totalMass = 0.0f, totalVol = 0.0f;
    for (const auto& b : boxes) { totalMass += b.mass; totalVol += boxVolume(b); }
    EXPECT_NEAR(totalMass, 3.0f, 1e-4f);
    EXPECT_NEAR(totalVol, 3.0f, 1e-4f);   // each unit cube contributes volume 1
}

TEST(CoherentFragmentServiceTest, MassConservedRegardlessOfMerging) {
    // A 3x3x1 slab: greedy merging changes box count but not total mass.
    std::vector<KinematicVoxel> vox;
    for (int x = 0; x < 3; ++x)
        for (int y = 0; y < 3; ++y)
            vox.push_back(cube((float)x, (float)y, 0));
    auto boxes = CoherentFragmentService::mergeVoxelsToBoxes(vox, unitMass);
    EXPECT_EQ(boxes.size(), 1u);                 // solid slab -> one box
    float totalMass = 0.0f;
    for (const auto& b : boxes) totalMass += b.mass;
    EXPECT_NEAR(totalMass, 9.0f, 1e-4f);
}

TEST(CoherentFragmentServiceTest, SingleSubcubeIsRepresented) {
    // A lone subcube (scale 1/3) -> one small box of the right size.
    auto boxes = CoherentFragmentService::mergeVoxelsToBoxes({cube(0, 0, 0, 1.0f/3.0f)}, unitMass);
    ASSERT_EQ(boxes.size(), 1u);
    EXPECT_NEAR(boxes[0].halfExtents.x, 1.0f/6.0f, 1e-4f);
    EXPECT_NEAR(boxes[0].mass, 1.0f, 1e-4f);
}

TEST(CoherentFragmentServiceTest, MixedResolutionConservesMass) {
    // A cube plus a distant subcube -> both represented, mass conserved.
    std::vector<KinematicVoxel> vox = {cube(0,0,0, 1.0f), cube(10,0,0, 1.0f/3.0f)};
    auto boxes = CoherentFragmentService::mergeVoxelsToBoxes(vox, unitMass);
    EXPECT_GE(boxes.size(), 2u);
    float totalMass = 0.0f;
    for (const auto& b : boxes) totalMass += b.mass;
    EXPECT_NEAR(totalMass, 2.0f, 1e-4f);
}

TEST(CoherentFragmentServiceTest, FallbackPathIsReachableAndPerVoxel) {
    // A spatially-sparse mixed-resolution set trips the >64 fine-cell fallback: one
    // lone microcube (scale 1/9) forces cellSize=1/9, and 8 unit cubes at x=0..7 span
    // (8.0 * 9) = 72 cells > MAX_GRID_DIM(64). The fallback emits ONE box per voxel
    // (no merging) — so 9 boxes, whereas the merge path would fuse the contiguous run
    // into a SINGLE box (verified by mutation: raising MAX_GRID_DIM makes this 1). Documents
    // that furniture CAN reach the fallback (it is NOT gated out by voxel count), and
    // pins the fallback's per-voxel, caller-mass-model behavior.
    std::vector<KinematicVoxel> vox;
    for (int x = 0; x < 8; ++x) vox.push_back(cube((float)x, 0, 0, 1.0f));  // span 8 units
    vox.push_back(cube(0, 0, 0, 1.0f/9.0f));                                 // forces cellSize=1/9
    auto boxes = CoherentFragmentService::mergeVoxelsToBoxes(vox, unitMass);
    EXPECT_EQ(boxes.size(), vox.size());   // per-voxel => fallback fired (merge would give <9)
    float totalMass = 0.0f;
    for (const auto& b : boxes) totalMass += b.mass;
    EXPECT_NEAR(totalMass, (float)vox.size(), 1e-4f);  // unitMass model => mass == voxel count
}

TEST(CoherentFragmentServiceTest, MassModelIsCallerSupplied) {
    // Volume-weighted model (the "world" model): a cube weighs 8x a subcube of 1/2 scale.
    auto volMass = [](const KinematicVoxel& v) {
        return v.scale.x * v.scale.y * v.scale.z; // density 1
    };
    auto big   = CoherentFragmentService::mergeVoxelsToBoxes({cube(0,0,0, 1.0f)}, volMass);
    auto small = CoherentFragmentService::mergeVoxelsToBoxes({cube(0,0,0, 0.5f)}, volMass);
    ASSERT_EQ(big.size(), 1u);
    ASSERT_EQ(small.size(), 1u);
    EXPECT_NEAR(big[0].mass,   1.0f,   1e-4f);
    EXPECT_NEAR(small[0].mass, 0.125f, 1e-4f);
}

// ============================================================================
// physicalize() — voxel set -> falling compound rigid body (P1.2)
// ============================================================================

namespace {
// A solid 2x2x2 block of unit cubes -> merges to one box, mass 8 under unitMass.
std::vector<KinematicVoxel> block2x2x2() {
    std::vector<KinematicVoxel> v;
    for (int x = 0; x < 2; ++x)
        for (int y = 0; y < 2; ++y)
            for (int z = 0; z < 2; ++z)
                v.push_back(cube((float)x, (float)y, (float)z));
    return v;
}
} // namespace

TEST(CoherentFragmentServiceTest, PhysicalizeCreatesBodyAndRender) {
    Phyxel::Physics::VoxelDynamicsWorld world;
    KinematicVoxelManager kin;   // null physics world is fine (skipCollider path)

    auto pf = CoherentFragmentService::physicalize(
        &world, &kin, "frag", block2x2x2(), glm::mat4(1.0f),
        glm::vec3(0.0f), glm::vec3(0.0f), unitMass);

    ASSERT_TRUE(pf.ok());
    EXPECT_FALSE(pf.kineticObjId.empty());
    EXPECT_EQ(kin.count(), 1u);
    EXPECT_EQ(world.getBodyCount(), 1u);
    // 8 unit cubes merge to ONE box, so the body has a single local box, mass 8.
    EXPECT_EQ(pf.body->getLocalBoxes().size(), 1u);
    EXPECT_NEAR(pf.body->getTotalMass(), 8.0f, 1e-3f);
    // Body sits at the COM (0.5,0.5,0.5) for objectTransform = identity.
    EXPECT_NEAR(pf.body->position.x, 0.5f, 1e-3f);
    EXPECT_NEAR(pf.body->position.y, 0.5f, 1e-3f);
    EXPECT_NEAR(pf.body->position.z, 0.5f, 1e-3f);
}

TEST(CoherentFragmentServiceTest, PhysicalizeAppliesInitialVelocity) {
    Phyxel::Physics::VoxelDynamicsWorld world;
    KinematicVoxelManager kin;
    auto pf = CoherentFragmentService::physicalize(
        &world, &kin, "frag", block2x2x2(), glm::mat4(1.0f),
        glm::vec3(1.0f, -2.0f, 3.0f), glm::vec3(0.0f, 0.5f, 0.0f), unitMass);
    ASSERT_TRUE(pf.ok());
    EXPECT_NEAR(pf.body->linearVelocity.x,  1.0f, 1e-4f);
    EXPECT_NEAR(pf.body->linearVelocity.y, -2.0f, 1e-4f);
    EXPECT_NEAR(pf.body->linearVelocity.z,  3.0f, 1e-4f);
    EXPECT_NEAR(pf.body->angularVelocity.y, 0.5f, 1e-4f);
}

TEST(CoherentFragmentServiceTest, PhysicalizeBodyFallsUnderGravity) {
    Phyxel::Physics::VoxelDynamicsWorld world;
    world.setGravity(glm::vec3(0.0f, -9.81f, 0.0f));
    KinematicVoxelManager kin;

    // Place the block high, no floor -> it should fall (proves it's a live dynamic body).
    glm::mat4 high = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 100.0f, 0.0f));
    auto pf = CoherentFragmentService::physicalize(
        &world, &kin, "frag", block2x2x2(), high,
        glm::vec3(0.0f), glm::vec3(0.0f), unitMass);
    ASSERT_TRUE(pf.ok());
    float startY = pf.body->position.y;
    for (int i = 0; i < 60; ++i) world.stepSimulation(1.0f / 60.0f);  // ~1s
    EXPECT_LT(pf.body->position.y, startY - 1.0f) << "body did not fall under gravity";
}

TEST(CoherentFragmentServiceTest, PhysicalizeFinalizeMassRemap) {
    Phyxel::Physics::VoxelDynamicsWorld world;
    KinematicVoxelManager kin;
    // Remap total mass to a fixed 3.0 regardless of the raw merged mass (8).
    auto pf = CoherentFragmentService::physicalize(
        &world, &kin, "frag", block2x2x2(), glm::mat4(1.0f),
        glm::vec3(0.0f), glm::vec3(0.0f), unitMass,
        [](float) { return 3.0f; });
    ASSERT_TRUE(pf.ok());
    EXPECT_NEAR(pf.body->getTotalMass(), 3.0f, 1e-3f);
}

TEST(CoherentFragmentServiceTest, PhysicalizeRejectsEmptyOrNullWorld) {
    Phyxel::Physics::VoxelDynamicsWorld world;
    KinematicVoxelManager kin;
    EXPECT_FALSE(CoherentFragmentService::physicalize(
        nullptr, &kin, "f", block2x2x2(), glm::mat4(1.0f),
        glm::vec3(0.0f), glm::vec3(0.0f), unitMass).ok());
    EXPECT_FALSE(CoherentFragmentService::physicalize(
        &world, &kin, "f", {}, glm::mat4(1.0f),
        glm::vec3(0.0f), glm::vec3(0.0f), unitMass).ok());
    EXPECT_EQ(kin.count(), 0u);   // nothing registered on failure
}
