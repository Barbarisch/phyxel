// docs/VoxelFoundationPlan.md step 7 D4 follow-up -- U3.2 "emissive voxels are real lights" on the switched world
// (VoxFound::emissiveLightsOfChunk). Today's Chunk mesher produced these until D4 deleted it; the switched world had
// no feed (a glow block lit nothing). RED before: no function, no light. The model is today's (G-158 / G-157),
// restated on 1/8 m voxels -- the numbers below are that model's, computed by hand.
#include <gtest/gtest.h>

#include <cmath>

#include "core/MaterialRegistry.h"
#include "core/voxfound/EmissiveLights.h"

using namespace Phyxel::Core;
using namespace Phyxel::Core::VoxFound;

namespace {
struct EmissiveRig : ::testing::Test {
    static void SetUpTestSuite() { MaterialRegistry::instance().loadFromJson("resources/materials.json"); }
    std::vector<std::string> palette{"", "Stone", "glow"};
    BlockStore store{glm::ivec3(256)};
};
}  // namespace

TEST_F(EmissiveRig, NoEmitterInThePaletteMeansNoScan) {
    EXPECT_FALSE(anyEmitter(emitterMaterials({"", "Stone", "Grass"})));
    EXPECT_TRUE(anyEmitter(emitterMaterials(palette)));
}

TEST_F(EmissiveRig, AGlowCubeIsOneLightAtItsCentreWithAFullCubesReach) {
    store.fillBlock(glm::ivec3(2, 3, 4), 2);   // a whole 1 m glow cube
    store.fillBlock(glm::ivec3(9, 9, 9), 1);   // Stone: no light (control)
    std::vector<EmissiveLight> out;
    emissiveLightsOfChunk(store, glm::ivec3(1, 0, 0), emitterMaterials(palette), out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_NEAR(out[0].worldPos.x, 32.0f + 2.5f, 1e-5f);   // chunk (1,0,0) starts at x = 32 m
    EXPECT_NEAR(out[0].worldPos.y, 3.5f, 1e-5f);
    EXPECT_NEAR(out[0].worldPos.z, 4.5f, 1e-5f);
    EXPECT_NEAR(out[0].intensity, 1.0f, 1e-6f);          // glow is fully emissive: t = 15 / 15
    EXPECT_NEAR(out[0].radius, 15.0f, 1e-4f);            // volume 1 cube: sizeScale 1
    EXPECT_NEAR(out[0].extent, std::sqrt(3.0f) * 0.5f, 1e-5f);   // excludes exactly its own cube
}

TEST_F(EmissiveRig, OneGlowVoxelIsOneSmallLightAtTheVoxel) {
    store.set(glm::ivec3(5 * 8, 5 * 8, 5 * 8), 2);   // one 1/8 m voxel, the cube's corner
    std::vector<EmissiveLight> out;
    emissiveLightsOfChunk(store, glm::ivec3(0), emitterMaterials(palette), out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_NEAR(out[0].worldPos.x, 5.0f + 1.0f / 16.0f, 1e-5f);   // at the voxel, not the cube centre (G-158)
    const float sizeScale = 1.0f + 0.25f * std::log(1.0f / 512.0f) / std::log(27.0f);   // 0.527
    EXPECT_NEAR(out[0].radius, 15.0f * sizeScale, 1e-3f);
    EXPECT_NEAR(out[0].extent, std::sqrt(3.0f) / 16.0f, 1e-5f);   // its own voxel only
}

TEST_F(EmissiveRig, ManyGlowVoxelsInOneCubeAreOneLightNotASum) {
    for (int i = 0; i < 64; ++i) store.set(glm::ivec3(8 + (i & 3), 8 + ((i >> 2) & 3), 8 + (i >> 4)), 2);
    store.set(glm::ivec3(16, 8, 8), 2);   // the next cube over: its own light
    std::vector<EmissiveLight> out;
    emissiveLightsOfChunk(store, glm::ivec3(0), emitterMaterials(palette), out);
    ASSERT_EQ(out.size(), 2u);
    EXPECT_NEAR(out[0].intensity, 1.0f, 1e-6f) << "the brightest constituent, never the sum of 64";
    EXPECT_NEAR(out[0].worldPos.x, 1.0f + 2.0f / 8.0f, 1e-5f);   // centroid of a 4^3 corner group: 1 + 2/8
}
