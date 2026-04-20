/**
 * Unit tests for TextureConstants utilities
 *
 * Tests texture index mapping functions for face-based texture selection.
 * Updated for multi-material atlas (78 textures, 13 materials).
 */

#include <gtest/gtest.h>
#include "core/Types.h"
#include <set>

using namespace Phyxel;
using namespace Phyxel::TextureConstants;

// ============================================================================
// Constant Values Tests
// ============================================================================

TEST(TextureConstantsTest, Constants_HaveValidValues) {
    EXPECT_EQ(PLACEHOLDER_TEXTURE_INDEX, 54);
    EXPECT_EQ(INVALID_TEXTURE_INDEX, 0xFFFF);
    EXPECT_EQ(MAX_TEXTURE_INDEX, 0xFFFE);
    EXPECT_EQ(TEXTURE_COUNT, 78);
    EXPECT_EQ(MATERIAL_COUNT, 13);
}

TEST(TextureConstantsTest, Constants_InvalidIsGreaterThanMax) {
    EXPECT_GT(INVALID_TEXTURE_INDEX, MAX_TEXTURE_INDEX);
}

// ============================================================================
// Face Texture Index Tests (Legacy placeholder)
// ============================================================================

TEST(TextureConstantsTest, GetTextureIndexForFace_ValidFaces) {
    EXPECT_EQ(getTextureIndexForFace(0), 0);
    EXPECT_EQ(getTextureIndexForFace(1), 1);
    EXPECT_EQ(getTextureIndexForFace(2), 2);
    EXPECT_EQ(getTextureIndexForFace(3), 3);
    EXPECT_EQ(getTextureIndexForFace(4), 4);
    EXPECT_EQ(getTextureIndexForFace(5), 5);
}

TEST(TextureConstantsTest, GetTextureIndexForFace_InvalidNegative) {
    EXPECT_EQ(getTextureIndexForFace(-1), PLACEHOLDER_TEXTURE_INDEX);
    EXPECT_EQ(getTextureIndexForFace(-100), PLACEHOLDER_TEXTURE_INDEX);
}

TEST(TextureConstantsTest, GetTextureIndexForFace_InvalidTooLarge) {
    EXPECT_EQ(getTextureIndexForFace(6), PLACEHOLDER_TEXTURE_INDEX);
    EXPECT_EQ(getTextureIndexForFace(10), PLACEHOLDER_TEXTURE_INDEX);
}

// ============================================================================
// Material ID Tests
// ============================================================================

TEST(TextureConstantsTest, GetMaterialID_KnownMaterials) {
    EXPECT_EQ(getMaterialID("placeholder"), 0);
    EXPECT_EQ(getMaterialID("grassdirt"), 1);
    EXPECT_EQ(getMaterialID("Cork"), 2);
    EXPECT_EQ(getMaterialID("Default"), 3);
    EXPECT_EQ(getMaterialID("Glass"), 4);
    EXPECT_EQ(getMaterialID("glow"), 5);
    EXPECT_EQ(getMaterialID("hover"), 6);
    EXPECT_EQ(getMaterialID("Ice"), 7);
    EXPECT_EQ(getMaterialID("Metal"), 8);
    EXPECT_EQ(getMaterialID("Rubber"), 9);
    EXPECT_EQ(getMaterialID("Stone"), 10);
    EXPECT_EQ(getMaterialID("Wood"), 11);
    EXPECT_EQ(getMaterialID("Leaf"), 12);
}

TEST(TextureConstantsTest, GetMaterialID_UnknownFallsBackToDefault) {
    EXPECT_EQ(getMaterialID("unknown"), 3);
    EXPECT_EQ(getMaterialID(""), 3);
    EXPECT_EQ(getMaterialID("something_else"), 3);
}

TEST(TextureConstantsTest, GetMaterialID_CaseSensitive) {
    // These should NOT match (wrong case)
    EXPECT_EQ(getMaterialID("stone"), 3);  // Falls back to Default (3)
    EXPECT_EQ(getMaterialID("STONE"), 3);
    EXPECT_EQ(getMaterialID("wood"), 3);
    // But these should match
    EXPECT_EQ(getMaterialID("Stone"), 10);
    EXPECT_EQ(getMaterialID("Wood"), 11);
}

// ============================================================================
// Material Texture Index Tests
// ============================================================================

TEST(TextureConstantsTest, GetTextureIndexForMaterial_Stone) {
    EXPECT_EQ(getTextureIndexForMaterial("Stone", 0), 68);  // side_n
    EXPECT_EQ(getTextureIndexForMaterial("Stone", 1), 69);  // side_s
    EXPECT_EQ(getTextureIndexForMaterial("Stone", 2), 67);  // side_e
    EXPECT_EQ(getTextureIndexForMaterial("Stone", 3), 70);  // side_w
    EXPECT_EQ(getTextureIndexForMaterial("Stone", 4), 71);  // top
    EXPECT_EQ(getTextureIndexForMaterial("Stone", 5), 66);  // bottom
}

TEST(TextureConstantsTest, GetTextureIndexForMaterial_Wood) {
    EXPECT_EQ(getTextureIndexForMaterial("Wood", 0), 74);  // side_n
    EXPECT_EQ(getTextureIndexForMaterial("Wood", 4), 77);  // top
    EXPECT_EQ(getTextureIndexForMaterial("Wood", 5), 72);  // bottom
}

TEST(TextureConstantsTest, GetTextureIndexForMaterial_Leaf) {
    EXPECT_EQ(getTextureIndexForMaterial("Leaf", 0), 44);  // side_n
    EXPECT_EQ(getTextureIndexForMaterial("Leaf", 1), 45);  // side_s
    EXPECT_EQ(getTextureIndexForMaterial("Leaf", 2), 43);  // side_e
    EXPECT_EQ(getTextureIndexForMaterial("Leaf", 3), 46);  // side_w
    EXPECT_EQ(getTextureIndexForMaterial("Leaf", 4), 47);  // top
    EXPECT_EQ(getTextureIndexForMaterial("Leaf", 5), 42);  // bottom
}

TEST(TextureConstantsTest, GetTextureIndexForMaterial_InvalidFace) {
    EXPECT_EQ(getTextureIndexForMaterial("Stone", -1), PLACEHOLDER_TEXTURE_INDEX);
    EXPECT_EQ(getTextureIndexForMaterial("Stone", 6), PLACEHOLDER_TEXTURE_INDEX);
}

TEST(TextureConstantsTest, GetTextureIndexForMaterial_Placeholder) {
    // placeholder: {side_n=56, side_s=57, side_e=55, side_w=58, top=59, bottom=54}
    EXPECT_EQ(getTextureIndexForMaterial("placeholder", 0), 56);  // side_n
    EXPECT_EQ(getTextureIndexForMaterial("placeholder", 1), 57);  // side_s
    EXPECT_EQ(getTextureIndexForMaterial("placeholder", 2), 55);  // side_e
    EXPECT_EQ(getTextureIndexForMaterial("placeholder", 3), 58);  // side_w
    EXPECT_EQ(getTextureIndexForMaterial("placeholder", 4), 59);  // top
    EXPECT_EQ(getTextureIndexForMaterial("placeholder", 5), 54);  // bottom
}

// ============================================================================
// Hover Texture Index Tests (updated for new atlas)
// ============================================================================

TEST(TextureConstantsTest, GetHoverTextureIndexForFace_ValidFaces) {
    // hover: {side_n=32, side_s=33, side_e=31, side_w=34, top=35, bottom=30}
    EXPECT_EQ(getHoverTextureIndexForFace(0), 32);  // hover_side_n
    EXPECT_EQ(getHoverTextureIndexForFace(1), 33);  // hover_side_s
    EXPECT_EQ(getHoverTextureIndexForFace(2), 31);  // hover_side_e
    EXPECT_EQ(getHoverTextureIndexForFace(3), 34);  // hover_side_w
    EXPECT_EQ(getHoverTextureIndexForFace(4), 35);  // hover_top
    EXPECT_EQ(getHoverTextureIndexForFace(5), 30);  // hover_bottom
}

TEST(TextureConstantsTest, GetHoverTextureIndexForFace_InvalidFaces) {
    EXPECT_EQ(getHoverTextureIndexForFace(-1), PLACEHOLDER_TEXTURE_INDEX);
    EXPECT_EQ(getHoverTextureIndexForFace(6), PLACEHOLDER_TEXTURE_INDEX);
}

// ============================================================================
// Grassdirt Texture Index Tests
// ============================================================================

TEST(TextureConstantsTest, GetGrassdirtTextureIndexForFace_ValidFaces) {
    // grassdirt: {side_n=26, side_s=27, side_e=25, side_w=28, top=29, bottom=24}
    EXPECT_EQ(getGrassdirtTextureIndexForFace(0), 26);
    EXPECT_EQ(getGrassdirtTextureIndexForFace(1), 27);
    EXPECT_EQ(getGrassdirtTextureIndexForFace(2), 25);
    EXPECT_EQ(getGrassdirtTextureIndexForFace(3), 28);
    EXPECT_EQ(getGrassdirtTextureIndexForFace(4), 29);
    EXPECT_EQ(getGrassdirtTextureIndexForFace(5), 24);
}

TEST(TextureConstantsTest, GetGrassdirtTextureIndexForFace_InvalidFaces) {
    EXPECT_EQ(getGrassdirtTextureIndexForFace(-1), PLACEHOLDER_TEXTURE_INDEX);
    EXPECT_EQ(getGrassdirtTextureIndexForFace(6), PLACEHOLDER_TEXTURE_INDEX);
}

// ============================================================================
// Texture Range Tests
// ============================================================================

TEST(TextureConstantsTest, AllMaterialIndices_BelowTextureCount) {
    for (int mat = 0; mat < MATERIAL_COUNT; ++mat) {
        for (int face = 0; face < 6; ++face) {
            EXPECT_LT(MATERIAL_FACE_INDEX[mat][face], TEXTURE_COUNT)
                << "Material " << mat << " face " << face << " out of range";
        }
    }
}

TEST(TextureConstantsTest, AllMaterialIndices_Unique) {
    std::set<uint16_t> allIndices;
    for (int mat = 0; mat < MATERIAL_COUNT; ++mat) {
        for (int face = 0; face < 6; ++face) {
            allIndices.insert(MATERIAL_FACE_INDEX[mat][face]);
        }
    }
    // Each material x face should produce a unique index
    EXPECT_EQ(allIndices.size(), MATERIAL_COUNT * 6);
}

TEST(TextureConstantsTest, AllFunctions_SameFallback) {
    int invalidFaceID = -1;
    EXPECT_EQ(getTextureIndexForFace(invalidFaceID), 
              getHoverTextureIndexForFace(invalidFaceID));
    EXPECT_EQ(getTextureIndexForFace(invalidFaceID), 
              getGrassdirtTextureIndexForFace(invalidFaceID));
    EXPECT_EQ(getTextureIndexForFace(invalidFaceID),
              getTextureIndexForMaterial("Stone", invalidFaceID));
}
