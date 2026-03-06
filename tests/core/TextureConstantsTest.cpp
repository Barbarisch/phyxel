/**
 * Unit tests for TextureConstants utilities
 * 
 * Tests texture index mapping functions for face-based texture selection.
 */

#include <gtest/gtest.h>
#include "core/Types.h"

using namespace Phyxel;
using namespace Phyxel::TextureConstants;

// ============================================================================
// Constant Values Tests
// ============================================================================

TEST(TextureConstantsTest, Constants_HaveValidValues) {
    EXPECT_EQ(PLACEHOLDER_TEXTURE_INDEX, 5);
    EXPECT_EQ(HOVER_BASE_TEXTURE_INDEX, 12);
    EXPECT_EQ(INVALID_TEXTURE_INDEX, 0xFFFF);
    EXPECT_EQ(MAX_TEXTURE_INDEX, 0xFFFE);
}

TEST(TextureConstantsTest, Constants_InvalidIsGreaterThanMax) {
    EXPECT_GT(INVALID_TEXTURE_INDEX, MAX_TEXTURE_INDEX);
}

// ============================================================================
// Face Texture Index Tests
// ============================================================================

TEST(TextureConstantsTest, GetTextureIndexForFace_ValidFaces) {
    // Face IDs 0-5 should map directly to texture indices 0-5
    EXPECT_EQ(getTextureIndexForFace(0), 0);
    EXPECT_EQ(getTextureIndexForFace(1), 1);
    EXPECT_EQ(getTextureIndexForFace(2), 2);
    EXPECT_EQ(getTextureIndexForFace(3), 3);
    EXPECT_EQ(getTextureIndexForFace(4), 4);
    EXPECT_EQ(getTextureIndexForFace(5), 5);
}

TEST(TextureConstantsTest, GetTextureIndexForFace_InvalidNegative) {
    // Negative face IDs should return placeholder
    EXPECT_EQ(getTextureIndexForFace(-1), PLACEHOLDER_TEXTURE_INDEX);
    EXPECT_EQ(getTextureIndexForFace(-100), PLACEHOLDER_TEXTURE_INDEX);
}

TEST(TextureConstantsTest, GetTextureIndexForFace_InvalidTooLarge) {
    // Face IDs >= 6 should return placeholder
    EXPECT_EQ(getTextureIndexForFace(6), PLACEHOLDER_TEXTURE_INDEX);
    EXPECT_EQ(getTextureIndexForFace(10), PLACEHOLDER_TEXTURE_INDEX);
    EXPECT_EQ(getTextureIndexForFace(100), PLACEHOLDER_TEXTURE_INDEX);
}

TEST(TextureConstantsTest, GetTextureIndexForFace_BoundaryValues) {
    // Test exact boundaries
    EXPECT_EQ(getTextureIndexForFace(0), 0);  // Min valid
    EXPECT_EQ(getTextureIndexForFace(5), 5);  // Max valid
}

// ============================================================================
// Hover Texture Index Tests
// ============================================================================

TEST(TextureConstantsTest, GetHoverTextureIndexForFace_ValidFaces) {
    // Hover textures are at indices 12-17 (base + faceID)
    EXPECT_EQ(getHoverTextureIndexForFace(0), 12);
    EXPECT_EQ(getHoverTextureIndexForFace(1), 13);
    EXPECT_EQ(getHoverTextureIndexForFace(2), 14);
    EXPECT_EQ(getHoverTextureIndexForFace(3), 15);
    EXPECT_EQ(getHoverTextureIndexForFace(4), 16);
    EXPECT_EQ(getHoverTextureIndexForFace(5), 17);
}

TEST(TextureConstantsTest, GetHoverTextureIndexForFace_InvalidNegative) {
    EXPECT_EQ(getHoverTextureIndexForFace(-1), PLACEHOLDER_TEXTURE_INDEX);
    EXPECT_EQ(getHoverTextureIndexForFace(-50), PLACEHOLDER_TEXTURE_INDEX);
}

TEST(TextureConstantsTest, GetHoverTextureIndexForFace_InvalidTooLarge) {
    EXPECT_EQ(getHoverTextureIndexForFace(6), PLACEHOLDER_TEXTURE_INDEX);
    EXPECT_EQ(getHoverTextureIndexForFace(20), PLACEHOLDER_TEXTURE_INDEX);
}

TEST(TextureConstantsTest, GetHoverTextureIndexForFace_OffsetFromBase) {
    // Verify offset calculation from base
    for (int faceID = 0; faceID < 6; ++faceID) {
        uint16_t expected = HOVER_BASE_TEXTURE_INDEX + faceID;
        EXPECT_EQ(getHoverTextureIndexForFace(faceID), expected)
            << "Failed for face " << faceID;
    }
}

// ============================================================================
// Grassdirt Texture Index Tests
// ============================================================================

TEST(TextureConstantsTest, GetGrassdirtTextureIndexForFace_ValidFaces) {
    // Grassdirt textures are at indices 6-11 (6 + faceID)
    EXPECT_EQ(getGrassdirtTextureIndexForFace(0), 6);
    EXPECT_EQ(getGrassdirtTextureIndexForFace(1), 7);
    EXPECT_EQ(getGrassdirtTextureIndexForFace(2), 8);
    EXPECT_EQ(getGrassdirtTextureIndexForFace(3), 9);
    EXPECT_EQ(getGrassdirtTextureIndexForFace(4), 10);
    EXPECT_EQ(getGrassdirtTextureIndexForFace(5), 11);
}

TEST(TextureConstantsTest, GetGrassdirtTextureIndexForFace_InvalidNegative) {
    EXPECT_EQ(getGrassdirtTextureIndexForFace(-1), PLACEHOLDER_TEXTURE_INDEX);
    EXPECT_EQ(getGrassdirtTextureIndexForFace(-10), PLACEHOLDER_TEXTURE_INDEX);
}

TEST(TextureConstantsTest, GetGrassdirtTextureIndexForFace_InvalidTooLarge) {
    EXPECT_EQ(getGrassdirtTextureIndexForFace(6), PLACEHOLDER_TEXTURE_INDEX);
    EXPECT_EQ(getGrassdirtTextureIndexForFace(100), PLACEHOLDER_TEXTURE_INDEX);
}

TEST(TextureConstantsTest, GetGrassdirtTextureIndexForFace_OffsetCalculation) {
    // Verify offset calculation
    for (int faceID = 0; faceID < 6; ++faceID) {
        uint16_t expected = 6 + faceID;
        EXPECT_EQ(getGrassdirtTextureIndexForFace(faceID), expected)
            << "Failed for face " << faceID;
    }
}

// ============================================================================
// Texture Range Tests
// ============================================================================

TEST(TextureConstantsTest, TextureRanges_NoOverlap) {
    // Verify texture index ranges don't overlap
    // Placeholder: 0-5
    // Grassdirt: 6-11
    // Hover: 12-17
    
    // Check that grassdirt starts after placeholder
    EXPECT_GT(getGrassdirtTextureIndexForFace(0), getTextureIndexForFace(5));
    
    // Check that hover starts after grassdirt
    EXPECT_GT(getHoverTextureIndexForFace(0), getGrassdirtTextureIndexForFace(5));
}

TEST(TextureConstantsTest, TextureRanges_AllValidBelowMax) {
    // All valid texture indices should be below MAX_TEXTURE_INDEX
    for (int faceID = 0; faceID < 6; ++faceID) {
        EXPECT_LT(getTextureIndexForFace(faceID), MAX_TEXTURE_INDEX);
        EXPECT_LT(getGrassdirtTextureIndexForFace(faceID), MAX_TEXTURE_INDEX);
        EXPECT_LT(getHoverTextureIndexForFace(faceID), MAX_TEXTURE_INDEX);
    }
}

TEST(TextureConstantsTest, TextureRanges_ConsecutiveIndices) {
    // Verify each range has consecutive indices
    for (int faceID = 0; faceID < 5; ++faceID) {
        // Placeholder range
        EXPECT_EQ(getTextureIndexForFace(faceID + 1), 
                  getTextureIndexForFace(faceID) + 1);
        
        // Grassdirt range
        EXPECT_EQ(getGrassdirtTextureIndexForFace(faceID + 1), 
                  getGrassdirtTextureIndexForFace(faceID) + 1);
        
        // Hover range
        EXPECT_EQ(getHoverTextureIndexForFace(faceID + 1), 
                  getHoverTextureIndexForFace(faceID) + 1);
    }
}

// ============================================================================
// Edge Cases and Boundary Tests
// ============================================================================

TEST(TextureConstantsTest, AllFunctions_SameFallback) {
    // All functions should use the same fallback value for invalid input
    int invalidFaceID = -1;
    EXPECT_EQ(getTextureIndexForFace(invalidFaceID), 
              getHoverTextureIndexForFace(invalidFaceID));
    EXPECT_EQ(getTextureIndexForFace(invalidFaceID), 
              getGrassdirtTextureIndexForFace(invalidFaceID));
}

TEST(TextureConstantsTest, FaceID_ZeroIsValid) {
    // Face ID 0 should be valid for all functions
    EXPECT_NE(getTextureIndexForFace(0), PLACEHOLDER_TEXTURE_INDEX);
    EXPECT_NE(getHoverTextureIndexForFace(0), PLACEHOLDER_TEXTURE_INDEX);
    EXPECT_NE(getGrassdirtTextureIndexForFace(0), PLACEHOLDER_TEXTURE_INDEX);
}

TEST(TextureConstantsTest, FaceID_FiveIsValid) {
    // Face ID 5 should be valid (last valid face)
    // For placeholder textures, face 5 returns index 5, which equals PLACEHOLDER_TEXTURE_INDEX
    // So we check if it's in the valid range instead
    EXPECT_EQ(getTextureIndexForFace(5), 5);
    EXPECT_EQ(getHoverTextureIndexForFace(5), 17);  // 12 + 5
    EXPECT_EQ(getGrassdirtTextureIndexForFace(5), 11);  // 6 + 5
}

TEST(TextureConstantsTest, FaceID_SixIsInvalid) {
    // Face ID 6 should be invalid (first invalid face)
    EXPECT_EQ(getTextureIndexForFace(6), PLACEHOLDER_TEXTURE_INDEX);
    EXPECT_EQ(getHoverTextureIndexForFace(6), PLACEHOLDER_TEXTURE_INDEX);
    EXPECT_EQ(getGrassdirtTextureIndexForFace(6), PLACEHOLDER_TEXTURE_INDEX);
}

// ============================================================================
// Specific Texture Layout Tests
// ============================================================================

TEST(TextureConstantsTest, PlaceholderRange_Covers0To5) {
    // Placeholder textures occupy indices 0-5
    std::set<uint16_t> indices;
    for (int faceID = 0; faceID < 6; ++faceID) {
        indices.insert(getTextureIndexForFace(faceID));
    }
    EXPECT_EQ(indices.size(), 6);
    EXPECT_EQ(*indices.begin(), 0);
    EXPECT_EQ(*indices.rbegin(), 5);
}

TEST(TextureConstantsTest, GrassdirtRange_Covers6To11) {
    // Grassdirt textures occupy indices 6-11
    std::set<uint16_t> indices;
    for (int faceID = 0; faceID < 6; ++faceID) {
        indices.insert(getGrassdirtTextureIndexForFace(faceID));
    }
    EXPECT_EQ(indices.size(), 6);
    EXPECT_EQ(*indices.begin(), 6);
    EXPECT_EQ(*indices.rbegin(), 11);
}

TEST(TextureConstantsTest, HoverRange_Covers12To17) {
    // Hover textures occupy indices 12-17
    std::set<uint16_t> indices;
    for (int faceID = 0; faceID < 6; ++faceID) {
        indices.insert(getHoverTextureIndexForFace(faceID));
    }
    EXPECT_EQ(indices.size(), 6);
    EXPECT_EQ(*indices.begin(), 12);
    EXPECT_EQ(*indices.rbegin(), 17);
}
