#include <gtest/gtest.h>
#include "core/Types.h"

using namespace Phyxel;

// ============================================================================
// CubeFaces Tests - Face Visibility and Occlusion
// ============================================================================

// Test isFullyOccluded() method
TEST(CubeFacesTest, IsFullyOccluded_AllVisible) {
    CubeFaces faces;
    faces.front = true;
    faces.back = true;
    faces.left = true;
    faces.right = true;
    faces.top = true;
    faces.bottom = true;
    EXPECT_FALSE(faces.isFullyOccluded());
}

TEST(CubeFacesTest, IsFullyOccluded_AllHidden) {
    CubeFaces faces;
    faces.front = false;
    faces.back = false;
    faces.left = false;
    faces.right = false;
    faces.top = false;
    faces.bottom = false;
    EXPECT_TRUE(faces.isFullyOccluded());
}

TEST(CubeFacesTest, IsFullyOccluded_OnlyFrontVisible) {
    CubeFaces faces;
    faces.front = true;
    faces.back = false;
    faces.left = false;
    faces.right = false;
    faces.top = false;
    faces.bottom = false;
    EXPECT_FALSE(faces.isFullyOccluded());
}

TEST(CubeFacesTest, IsFullyOccluded_OnlyBackVisible) {
    CubeFaces faces;
    faces.front = false;
    faces.back = true;
    faces.left = false;
    faces.right = false;
    faces.top = false;
    faces.bottom = false;
    EXPECT_FALSE(faces.isFullyOccluded());
}

TEST(CubeFacesTest, IsFullyOccluded_OnlyLeftVisible) {
    CubeFaces faces;
    faces.front = false;
    faces.back = false;
    faces.left = true;
    faces.right = false;
    faces.top = false;
    faces.bottom = false;
    EXPECT_FALSE(faces.isFullyOccluded());
}

TEST(CubeFacesTest, IsFullyOccluded_OnlyRightVisible) {
    CubeFaces faces;
    faces.front = false;
    faces.back = false;
    faces.left = false;
    faces.right = true;
    faces.top = false;
    faces.bottom = false;
    EXPECT_FALSE(faces.isFullyOccluded());
}

TEST(CubeFacesTest, IsFullyOccluded_OnlyTopVisible) {
    CubeFaces faces;
    faces.front = false;
    faces.back = false;
    faces.left = false;
    faces.right = false;
    faces.top = true;
    faces.bottom = false;
    EXPECT_FALSE(faces.isFullyOccluded());
}

TEST(CubeFacesTest, IsFullyOccluded_OnlyBottomVisible) {
    CubeFaces faces;
    faces.front = false;
    faces.back = false;
    faces.left = false;
    faces.right = false;
    faces.top = false;
    faces.bottom = true;
    EXPECT_FALSE(faces.isFullyOccluded());
}

TEST(CubeFacesTest, IsFullyOccluded_MultipleVisible) {
    CubeFaces faces;
    faces.front = true;
    faces.back = false;
    faces.left = true;
    faces.right = false;
    faces.top = true;
    faces.bottom = false;
    EXPECT_FALSE(faces.isFullyOccluded());
}

// Test getVisibleFaceCount() method
TEST(CubeFacesTest, GetVisibleFaceCount_AllVisible) {
    CubeFaces faces;
    faces.front = true;
    faces.back = true;
    faces.left = true;
    faces.right = true;
    faces.top = true;
    faces.bottom = true;
    EXPECT_EQ(faces.getVisibleFaceCount(), 6);
}

TEST(CubeFacesTest, GetVisibleFaceCount_AllHidden) {
    CubeFaces faces;
    faces.front = false;
    faces.back = false;
    faces.left = false;
    faces.right = false;
    faces.top = false;
    faces.bottom = false;
    EXPECT_EQ(faces.getVisibleFaceCount(), 0);
}

TEST(CubeFacesTest, GetVisibleFaceCount_OneFace) {
    CubeFaces faces;
    faces.front = true;
    faces.back = false;
    faces.left = false;
    faces.right = false;
    faces.top = false;
    faces.bottom = false;
    EXPECT_EQ(faces.getVisibleFaceCount(), 1);
}

TEST(CubeFacesTest, GetVisibleFaceCount_TwoFaces) {
    CubeFaces faces;
    faces.front = true;
    faces.back = true;
    faces.left = false;
    faces.right = false;
    faces.top = false;
    faces.bottom = false;
    EXPECT_EQ(faces.getVisibleFaceCount(), 2);
}

TEST(CubeFacesTest, GetVisibleFaceCount_ThreeFaces) {
    CubeFaces faces;
    faces.front = true;
    faces.back = true;
    faces.left = true;
    faces.right = false;
    faces.top = false;
    faces.bottom = false;
    EXPECT_EQ(faces.getVisibleFaceCount(), 3);
}

TEST(CubeFacesTest, GetVisibleFaceCount_FourFaces) {
    CubeFaces faces;
    faces.front = true;
    faces.back = true;
    faces.left = true;
    faces.right = true;
    faces.top = false;
    faces.bottom = false;
    EXPECT_EQ(faces.getVisibleFaceCount(), 4);
}

TEST(CubeFacesTest, GetVisibleFaceCount_FiveFaces) {
    CubeFaces faces;
    faces.front = true;
    faces.back = true;
    faces.left = true;
    faces.right = true;
    faces.top = true;
    faces.bottom = false;
    EXPECT_EQ(faces.getVisibleFaceCount(), 5);
}

TEST(CubeFacesTest, GetVisibleFaceCount_DifferentCombinations) {
    // Test various combinations
    CubeFaces faces1;
    faces1.front = false;
    faces1.back = true;
    faces1.left = false;
    faces1.right = true;
    faces1.top = false;
    faces1.bottom = true;
    EXPECT_EQ(faces1.getVisibleFaceCount(), 3);
    
    CubeFaces faces2;
    faces2.front = true;
    faces2.back = false;
    faces2.left = false;
    faces2.right = false;
    faces2.top = true;
    faces2.bottom = false;
    EXPECT_EQ(faces2.getVisibleFaceCount(), 2);
}

// Test correlation between isFullyOccluded and getVisibleFaceCount
TEST(CubeFacesTest, Correlation_FullyOccludedMeansZeroVisible) {
    CubeFaces faces;
    faces.front = false;
    faces.back = false;
    faces.left = false;
    faces.right = false;
    faces.top = false;
    faces.bottom = false;
    
    EXPECT_TRUE(faces.isFullyOccluded());
    EXPECT_EQ(faces.getVisibleFaceCount(), 0);
}

TEST(CubeFacesTest, Correlation_AnyVisibleMeansNotFullyOccluded) {
    CubeFaces faces;
    faces.front = false;
    faces.back = false;
    faces.left = false;
    faces.right = false;
    faces.top = false;
    faces.bottom = true; // Only one visible
    
    EXPECT_FALSE(faces.isFullyOccluded());
    EXPECT_EQ(faces.getVisibleFaceCount(), 1);
}

TEST(CubeFacesTest, Correlation_AllVisibleMeansNotFullyOccluded) {
    CubeFaces faces;
    faces.front = true;
    faces.back = true;
    faces.left = true;
    faces.right = true;
    faces.top = true;
    faces.bottom = true;
    
    EXPECT_FALSE(faces.isFullyOccluded());
    EXPECT_EQ(faces.getVisibleFaceCount(), 6);
}

// Test default construction (all faces visible by default)
TEST(CubeFacesTest, DefaultConstruction_AllVisible) {
    CubeFaces faces;
    
    EXPECT_TRUE(faces.front);
    EXPECT_TRUE(faces.back);
    EXPECT_TRUE(faces.left);
    EXPECT_TRUE(faces.right);
    EXPECT_TRUE(faces.top);
    EXPECT_TRUE(faces.bottom);
    
    EXPECT_FALSE(faces.isFullyOccluded());
    EXPECT_EQ(faces.getVisibleFaceCount(), 6);
}

// Edge case: Test each individual face affects the count correctly
TEST(CubeFacesTest, IndividualFaceContribution_Front) {
    CubeFaces all_hidden;
    all_hidden.front = false;
    all_hidden.back = false;
    all_hidden.left = false;
    all_hidden.right = false;
    all_hidden.top = false;
    all_hidden.bottom = false;
    
    all_hidden.front = true;
    EXPECT_EQ(all_hidden.getVisibleFaceCount(), 1);
}

TEST(CubeFacesTest, IndividualFaceContribution_Back) {
    CubeFaces all_hidden;
    all_hidden.front = false;
    all_hidden.back = false;
    all_hidden.left = false;
    all_hidden.right = false;
    all_hidden.top = false;
    all_hidden.bottom = false;
    
    all_hidden.back = true;
    EXPECT_EQ(all_hidden.getVisibleFaceCount(), 1);
}

TEST(CubeFacesTest, IndividualFaceContribution_Left) {
    CubeFaces all_hidden;
    all_hidden.front = false;
    all_hidden.back = false;
    all_hidden.left = false;
    all_hidden.right = false;
    all_hidden.top = false;
    all_hidden.bottom = false;
    
    all_hidden.left = true;
    EXPECT_EQ(all_hidden.getVisibleFaceCount(), 1);
}

TEST(CubeFacesTest, IndividualFaceContribution_Right) {
    CubeFaces all_hidden;
    all_hidden.front = false;
    all_hidden.back = false;
    all_hidden.left = false;
    all_hidden.right = false;
    all_hidden.top = false;
    all_hidden.bottom = false;
    
    all_hidden.right = true;
    EXPECT_EQ(all_hidden.getVisibleFaceCount(), 1);
}

TEST(CubeFacesTest, IndividualFaceContribution_Top) {
    CubeFaces all_hidden;
    all_hidden.front = false;
    all_hidden.back = false;
    all_hidden.left = false;
    all_hidden.right = false;
    all_hidden.top = false;
    all_hidden.bottom = false;
    
    all_hidden.top = true;
    EXPECT_EQ(all_hidden.getVisibleFaceCount(), 1);
}

TEST(CubeFacesTest, IndividualFaceContribution_Bottom) {
    CubeFaces all_hidden;
    all_hidden.front = false;
    all_hidden.back = false;
    all_hidden.left = false;
    all_hidden.right = false;
    all_hidden.top = false;
    all_hidden.bottom = false;
    
    all_hidden.bottom = true;
    EXPECT_EQ(all_hidden.getVisibleFaceCount(), 1);
}
