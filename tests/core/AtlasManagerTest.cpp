#include <gtest/gtest.h>
#include "core/AtlasManager.h"
#include "core/MaterialRegistry.h"

using namespace Phyxel::Core;

class AtlasManagerTest : public ::testing::Test {
protected:
    // Build the atlas ONCE for the whole fixture.
    //
    // buildAtlas() decodes every source PNG and BC7-compresses 618 texture
    // slots (438 layers @512 + 180 @1024) — measured at ~34 s per call. Each
    // of the four tests below used to call it from SetUp(), rebuilding
    // byte-identical data four times and costing ~140 s of the unit suite's
    // total runtime all by itself (the next-slowest test in the suite is
    // 243 ms). AtlasManager is a singleton, so one build serves them all.
    static void SetUpTestSuite() {
        MaterialRegistry::instance().loadFromJson("resources/materials.json");
        auto& atlas = AtlasManager::instance();
        atlas.setSourceDirectory("resources/textures/source");
        ASSERT_TRUE(atlas.buildAtlas());
    }
};

// NOTE: the legacy packed-2D-atlas dimension tests (calcAtlasDimensions) were removed in
// the texture-array migration — that path no longer packs textures into a 2D grid.

TEST_F(AtlasManagerTest, BuildAtlasFromSourcePNGs) {
    auto& atlas = AtlasManager::instance();   // built in SetUpTestSuite

    // Texture-array layout: one baseSize² RGBA layer per texture, stored layer-major.
    // class 0 = 512px (terrain/standard materials).
    const auto& info = atlas.getAtlasInfo(0);
    const int expectedCount = MaterialRegistry::instance().getTextureCount(0);
    EXPECT_GT(expectedCount, 0);
    EXPECT_EQ(info.textureCount, expectedCount);
    EXPECT_EQ(info.layerCount, expectedCount);
    EXPECT_EQ(info.atlasWidth, AtlasManager::TEXTURE_SIZE);   // per-layer dimensions
    EXPECT_EQ(info.atlasHeight, AtlasManager::TEXTURE_SIZE);
    const size_t layerBytes = static_cast<size_t>(AtlasManager::TEXTURE_SIZE)
                            * AtlasManager::TEXTURE_SIZE * 4u;
    EXPECT_EQ(info.pixels.size(), layerBytes * static_cast<size_t>(expectedCount));
    EXPECT_EQ(info.uvBounds.size(), static_cast<size_t>(expectedCount));
}

TEST_F(AtlasManagerTest, GetTextureSlotPixels) {
    auto& atlas = AtlasManager::instance();   // built in SetUpTestSuite

    auto pixels = atlas.getTextureSlotPixels(0);
    const size_t layerBytes = static_cast<size_t>(AtlasManager::TEXTURE_SIZE)
                            * AtlasManager::TEXTURE_SIZE * 4u;
    EXPECT_EQ(pixels.size(), layerBytes);
    // Should have non-zero pixels (not all transparent)
    bool hasContent = false;
    for (size_t i = 3; i < pixels.size(); i += 4) {
        if (pixels[i] > 0) { hasContent = true; break; }
    }
    EXPECT_TRUE(hasContent);
}

TEST_F(AtlasManagerTest, UpdateTextureSlot) {
    auto& atlas = AtlasManager::instance();   // built in SetUpTestSuite

    // This is the one test that MUTATES the shared atlas. Snapshot slot 0 and
    // put it back at the end — otherwise whether the other tests see real
    // texture data or this red block depends on execution order, which is
    // exactly the kind of hidden dependency that makes a suite flaky.
    const std::vector<uint8_t> original = atlas.getTextureSlotPixels(0);
    ASSERT_FALSE(original.empty());

    // Create red texture sized to one array layer
    const int N = AtlasManager::TEXTURE_SIZE;
    std::vector<uint8_t> red(static_cast<size_t>(N) * N * 4);
    for (int i = 0; i < N * N; i++) {
        red[i * 4 + 0] = 255; // R
        red[i * 4 + 3] = 255; // A
    }

    EXPECT_TRUE(atlas.updateTextureSlot(0, red.data()));

    auto readBack = atlas.getTextureSlotPixels(0);
    EXPECT_EQ(readBack[0], 255); // Red
    EXPECT_EQ(readBack[1], 0);   // Green

    EXPECT_TRUE(atlas.updateTextureSlot(0, original.data()));   // restore
}

TEST_F(AtlasManagerTest, UVBoundsAreFullTilePerLayer) {
    auto& atlas = AtlasManager::instance();   // built in SetUpTestSuite

    // In the texture-array path each layer is a full 0..1 tile; uvBounds carries only
    // SSBO metadata, so every entry is (0,0,1,1).
    const auto& info = atlas.getAtlasInfo();
    glm::vec4 uv0 = info.uvBounds[0];
    EXPECT_NEAR(uv0.x, 0.0f, 0.001f);
    EXPECT_NEAR(uv0.y, 0.0f, 0.001f);
    EXPECT_NEAR(uv0.z, 1.0f, 0.001f);
    EXPECT_NEAR(uv0.w, 1.0f, 0.001f);
}
