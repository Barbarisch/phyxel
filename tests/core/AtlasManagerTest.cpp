#include <gtest/gtest.h>
#include "core/AtlasManager.h"
#include "core/MaterialRegistry.h"

using namespace Phyxel::Core;

class AtlasManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto& reg = MaterialRegistry::instance();
        reg.loadFromJson("resources/materials.json");
    }
};

TEST_F(AtlasManagerTest, CalcAtlasDimensionsBasic) {
    int w, h;
    AtlasManager::calcAtlasDimensions(78, w, h);
    // 78 textures, 6 per row = 13 rows, 13*66=858 → next pow2 = 1024
    EXPECT_EQ(w, 1024);
    EXPECT_EQ(h, 1024);
}

TEST_F(AtlasManagerTest, CalcAtlasDimensionsSmall) {
    int w, h;
    AtlasManager::calcAtlasDimensions(6, w, h);
    // 1 row, 6*66=396px wide → next pow2 = 512
    EXPECT_EQ(w, 512);
    EXPECT_EQ(h, 512);
}

TEST_F(AtlasManagerTest, BuildAtlasFromSourcePNGs) {
    auto& atlas = AtlasManager::instance();
    atlas.setSourceDirectory("resources/textures/source");
    ASSERT_TRUE(atlas.buildAtlas());

    // Texture-array layout: one TEXTURE_SIZE² RGBA layer per texture, stored layer-major.
    const auto& info = atlas.getAtlasInfo();
    const int expectedCount = MaterialRegistry::instance().getTextureCount();
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
    auto& atlas = AtlasManager::instance();
    atlas.setSourceDirectory("resources/textures/source");
    ASSERT_TRUE(atlas.buildAtlas());

    auto pixels = atlas.getTextureSlotPixels(0);
    EXPECT_EQ(pixels.size(), 64u * 64u * 4u);
    // Should have non-zero pixels (not all transparent)
    bool hasContent = false;
    for (size_t i = 3; i < pixels.size(); i += 4) {
        if (pixels[i] > 0) { hasContent = true; break; }
    }
    EXPECT_TRUE(hasContent);
}

TEST_F(AtlasManagerTest, UpdateTextureSlot) {
    auto& atlas = AtlasManager::instance();
    atlas.setSourceDirectory("resources/textures/source");
    ASSERT_TRUE(atlas.buildAtlas());

    // Create red texture
    std::vector<uint8_t> red(64 * 64 * 4);
    for (int i = 0; i < 64 * 64; i++) {
        red[i * 4 + 0] = 255; // R
        red[i * 4 + 3] = 255; // A
    }

    EXPECT_TRUE(atlas.updateTextureSlot(0, red.data()));

    auto readBack = atlas.getTextureSlotPixels(0);
    EXPECT_EQ(readBack[0], 255); // Red
    EXPECT_EQ(readBack[1], 0);   // Green
}

TEST_F(AtlasManagerTest, UVBoundsAreFullTilePerLayer) {
    auto& atlas = AtlasManager::instance();
    atlas.setSourceDirectory("resources/textures/source");
    ASSERT_TRUE(atlas.buildAtlas());

    // In the texture-array path each layer is a full 0..1 tile; uvBounds carries only
    // SSBO metadata, so every entry is (0,0,1,1).
    const auto& info = atlas.getAtlasInfo();
    glm::vec4 uv0 = info.uvBounds[0];
    EXPECT_NEAR(uv0.x, 0.0f, 0.001f);
    EXPECT_NEAR(uv0.y, 0.0f, 0.001f);
    EXPECT_NEAR(uv0.z, 1.0f, 0.001f);
    EXPECT_NEAR(uv0.w, 1.0f, 0.001f);
}
