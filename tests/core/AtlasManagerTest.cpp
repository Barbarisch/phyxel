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
    // 78 textures, 6 per row = 13 rows, 13*20=260 → next pow2 = 512
    EXPECT_EQ(w, 512);
    EXPECT_EQ(h, 512);
}

TEST_F(AtlasManagerTest, CalcAtlasDimensionsSmall) {
    int w, h;
    AtlasManager::calcAtlasDimensions(6, w, h);
    // 1 row, 120px wide → next pow2 = 128
    EXPECT_EQ(w, 128);
    EXPECT_EQ(h, 128);
}

TEST_F(AtlasManagerTest, BuildAtlasFromSourcePNGs) {
    auto& atlas = AtlasManager::instance();
    atlas.setSourceDirectory("resources/textures/source");
    ASSERT_TRUE(atlas.buildAtlas());

    const auto& info = atlas.getAtlasInfo();
    EXPECT_EQ(info.textureCount, 84);
    EXPECT_EQ(info.atlasWidth, 512);
    EXPECT_EQ(info.atlasHeight, 512);
    EXPECT_EQ(info.pixels.size(), 512u * 512u * 4u);
    EXPECT_EQ(info.uvBounds.size(), 84u);
}

TEST_F(AtlasManagerTest, GetTextureSlotPixels) {
    auto& atlas = AtlasManager::instance();
    atlas.setSourceDirectory("resources/textures/source");
    ASSERT_TRUE(atlas.buildAtlas());

    auto pixels = atlas.getTextureSlotPixels(0);
    EXPECT_EQ(pixels.size(), 18u * 18u * 4u);
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
    std::vector<uint8_t> red(18 * 18 * 4);
    for (int i = 0; i < 18 * 18; i++) {
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
    // Slot 0: col=0, row=0, pixelX=1, pixelY=1
    glm::vec4 uv0 = info.uvBounds[0];
    EXPECT_NEAR(uv0.x, 1.0f / 512.0f, 0.001f);
    EXPECT_NEAR(uv0.y, 1.0f / 512.0f, 0.001f);
    EXPECT_NEAR(uv0.z, 19.0f / 512.0f, 0.001f);
    EXPECT_NEAR(uv0.w, 19.0f / 512.0f, 0.001f);
}
