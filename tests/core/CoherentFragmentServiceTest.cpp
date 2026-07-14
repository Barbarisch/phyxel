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
