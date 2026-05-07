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

    const auto& info = atlas.getAtlasInfo();
    EXPECT_EQ(info.textureCount, 108);
    EXPECT_EQ(info.atlasWidth, 2048);
    EXPECT_EQ(info.atlasHeight, 2048);
    EXPECT_EQ(info.pixels.size(), 2048u * 2048u * 4u);
    EXPECT_EQ(info.uvBounds.size(), 108u);
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

TEST_F(AtlasManagerTest, UVBoundsCorrectness) {
    auto& atlas = AtlasManager::instance();
    atlas.setSourceDirectory("resources/textures/source");
    ASSERT_TRUE(atlas.buildAtlas());

    const auto& info = atlas.getAtlasInfo();
    // Slot 0: col=0, row=0, pixelX=1, pixelY=1, size=64px on 2048 atlas
    glm::vec4 uv0 = info.uvBounds[0];
    EXPECT_NEAR(uv0.x,  1.0f / 2048.0f, 0.001f);
    EXPECT_NEAR(uv0.y,  1.0f / 2048.0f, 0.001f);
    EXPECT_NEAR(uv0.z, 65.0f / 2048.0f, 0.001f);
    EXPECT_NEAR(uv0.w, 65.0f / 2048.0f, 0.001f);
}
