/**
 * Unit tests for Math utilities
 * 
 * Tests mathematical helper functions including:
 * - Position/index conversion
 * - Ray-box intersection
 * - Face bitmask operations
 */

#include <gtest/gtest.h>
#include "utils/Math.h"
#include "core/Types.h"
#include <glm/glm.hpp>

using namespace Phyxel;

// ============================================================================
// Position/Index Conversion Tests
// ============================================================================

TEST(MathTest, PositionToIndex_Origin) {
    int index = Math::positionToIndex(0, 0, 0, 32);
    EXPECT_EQ(index, 0);
}

TEST(MathTest, PositionToIndex_XAxis) {
    // First position along X axis
    int index = Math::positionToIndex(1, 0, 0, 32);
    EXPECT_EQ(index, 1);
    
    // Last position along X axis in 32x32x32 grid
    index = Math::positionToIndex(31, 0, 0, 32);
    EXPECT_EQ(index, 31);
}

TEST(MathTest, PositionToIndex_YAxis) {
    // First position along Y axis
    int index = Math::positionToIndex(0, 1, 0, 32);
    EXPECT_EQ(index, 32); // 0 + 1*32 + 0*32*32
    
    // Second position along Y axis
    index = Math::positionToIndex(0, 2, 0, 32);
    EXPECT_EQ(index, 64);
}

TEST(MathTest, PositionToIndex_ZAxis) {
    // First position along Z axis
    int index = Math::positionToIndex(0, 0, 1, 32);
    EXPECT_EQ(index, 1024); // 0 + 0*32 + 1*32*32
}

TEST(MathTest, PositionToIndex_Arbitrary) {
    // Test arbitrary position (5, 10, 3)
    int index = Math::positionToIndex(5, 10, 3, 32);
    // Expected: 5 + 10*32 + 3*32*32 = 5 + 320 + 3072 = 3397
    EXPECT_EQ(index, 3397);
}

TEST(MathTest, PositionToIndex_DifferentGridSize) {
    // Test with 16x16x16 grid
    int index = Math::positionToIndex(5, 5, 5, 16);
    // Expected: 5 + 5*16 + 5*16*16 = 5 + 80 + 1280 = 1365
    EXPECT_EQ(index, 1365);
}

TEST(MathTest, IndexToPosition_Origin) {
    glm::ivec3 pos = Math::indexToPosition(0, 32);
    EXPECT_EQ(pos, glm::ivec3(0, 0, 0));
}

TEST(MathTest, IndexToPosition_XAxis) {
    glm::ivec3 pos = Math::indexToPosition(1, 32);
    EXPECT_EQ(pos, glm::ivec3(1, 0, 0));
    
    pos = Math::indexToPosition(31, 32);
    EXPECT_EQ(pos, glm::ivec3(31, 0, 0));
}

TEST(MathTest, IndexToPosition_YAxis) {
    glm::ivec3 pos = Math::indexToPosition(32, 32);
    EXPECT_EQ(pos, glm::ivec3(0, 1, 0));
    
    pos = Math::indexToPosition(64, 32);
    EXPECT_EQ(pos, glm::ivec3(0, 2, 0));
}

TEST(MathTest, IndexToPosition_ZAxis) {
    glm::ivec3 pos = Math::indexToPosition(1024, 32);
    EXPECT_EQ(pos, glm::ivec3(0, 0, 1));
}

TEST(MathTest, IndexToPosition_Arbitrary) {
    glm::ivec3 pos = Math::indexToPosition(3397, 32);
    EXPECT_EQ(pos, glm::ivec3(5, 10, 3));
}

TEST(MathTest, RoundTrip_PositionIndexConversion) {
    // Test that converting position->index->position gives original position
    for (int x = 0; x < 32; x += 5) {
        for (int y = 0; y < 32; y += 5) {
            for (int z = 0; z < 32; z += 5) {
                glm::ivec3 original(x, y, z);
                int index = Math::positionToIndex(x, y, z, 32);
                glm::ivec3 converted = Math::indexToPosition(index, 32);
                EXPECT_EQ(original, converted) << "Failed for position (" << x << "," << y << "," << z << ")";
            }
        }
    }
}

// ============================================================================
// Face Bitmask Tests
// ============================================================================

TEST(MathTest, FacesToBitmask_NoFaces) {
    CubeFaces faces{false, false, false, false, false, false};
    uint32_t mask = Math::facesToBitmask(faces);
    EXPECT_EQ(mask, 0);
}

TEST(MathTest, FacesToBitmask_AllFaces) {
    CubeFaces faces{true, true, true, true, true, true};
    uint32_t mask = Math::facesToBitmask(faces);
    EXPECT_EQ(mask, 0b111111); // All 6 bits set
}

TEST(MathTest, FacesToBitmask_SingleFaces) {
    // Test front face (bit 0)
    CubeFaces frontOnly{true, false, false, false, false, false};
    EXPECT_EQ(Math::facesToBitmask(frontOnly), 0b000001);
    
    // Test back face (bit 1)
    CubeFaces backOnly{false, true, false, false, false, false};
    EXPECT_EQ(Math::facesToBitmask(backOnly), 0b000010);
    
    // Test right face (bit 2)
    CubeFaces rightOnly{false, false, false, true, false, false};
    EXPECT_EQ(Math::facesToBitmask(rightOnly), 0b000100);
    
    // Test left face (bit 3)
    CubeFaces leftOnly{false, false, true, false, false, false};
    EXPECT_EQ(Math::facesToBitmask(leftOnly), 0b001000);
    
    // Test top face (bit 4)
    CubeFaces topOnly{false, false, false, false, true, false};
    EXPECT_EQ(Math::facesToBitmask(topOnly), 0b010000);
    
    // Test bottom face (bit 5)
    CubeFaces bottomOnly{false, false, false, false, false, true};
    EXPECT_EQ(Math::facesToBitmask(bottomOnly), 0b100000);
}

TEST(MathTest, FacesToBitmask_Combinations) {
    // Front and back
    CubeFaces frontBack{true, true, false, false, false, false};
    EXPECT_EQ(Math::facesToBitmask(frontBack), 0b000011);
    
    // All horizontal faces (front, back, left, right)
    CubeFaces horizontal{true, true, true, true, false, false};
    EXPECT_EQ(Math::facesToBitmask(horizontal), 0b001111);
    
    // Top and bottom only
    CubeFaces vertical{false, false, false, false, true, true};
    EXPECT_EQ(Math::facesToBitmask(vertical), 0b110000);
}

// ============================================================================
// Ray-Box Intersection Tests
// ============================================================================

TEST(MathTest, RayBoxIntersection_DirectHit) {
    // Ray shooting directly at box center
    glm::vec3 rayOrigin(-5.0f, 0.0f, 0.0f);
    glm::vec3 rayDir(1.0f, 0.0f, 0.0f);
    glm::vec3 boxMin(-1.0f, -1.0f, -1.0f);
    glm::vec3 boxMax(1.0f, 1.0f, 1.0f);
    
    float tNear, tFar;
    bool hit = Math::rayBoxIntersection(rayOrigin, rayDir, boxMin, boxMax, tNear, tFar);
    
    EXPECT_TRUE(hit);
    EXPECT_FLOAT_EQ(tNear, 4.0f); // Ray enters at x=-1 (distance 4)
    EXPECT_FLOAT_EQ(tFar, 6.0f);  // Ray exits at x=1 (distance 6)
}

TEST(MathTest, RayBoxIntersection_Miss) {
    // Ray pointing away from box
    glm::vec3 rayOrigin(-5.0f, 0.0f, 0.0f);
    glm::vec3 rayDir(-1.0f, 0.0f, 0.0f); // Pointing left, box is right
    glm::vec3 boxMin(-1.0f, -1.0f, -1.0f);
    glm::vec3 boxMax(1.0f, 1.0f, 1.0f);
    
    float tNear, tFar;
    bool hit = Math::rayBoxIntersection(rayOrigin, rayDir, boxMin, boxMax, tNear, tFar);
    
    EXPECT_FALSE(hit);
}

TEST(MathTest, RayBoxIntersection_ParallelMiss) {
    // Ray parallel to box but not intersecting
    glm::vec3 rayOrigin(-5.0f, 5.0f, 0.0f); // Above box
    glm::vec3 rayDir(1.0f, 0.0f, 0.0f);     // Parallel to X axis
    glm::vec3 boxMin(-1.0f, -1.0f, -1.0f);
    glm::vec3 boxMax(1.0f, 1.0f, 1.0f);
    
    float tNear, tFar;
    bool hit = Math::rayBoxIntersection(rayOrigin, rayDir, boxMin, boxMax, tNear, tFar);
    
    EXPECT_FALSE(hit);
}

TEST(MathTest, RayBoxIntersection_OriginInsideBox) {
    // Ray starting inside box
    glm::vec3 rayOrigin(0.0f, 0.0f, 0.0f); // Box center
    glm::vec3 rayDir(1.0f, 0.0f, 0.0f);
    glm::vec3 boxMin(-1.0f, -1.0f, -1.0f);
    glm::vec3 boxMax(1.0f, 1.0f, 1.0f);
    
    float tNear, tFar;
    bool hit = Math::rayBoxIntersection(rayOrigin, rayDir, boxMin, boxMax, tNear, tFar);
    
    EXPECT_TRUE(hit);
    EXPECT_FLOAT_EQ(tNear, 0.0f); // Already inside
    EXPECT_FLOAT_EQ(tFar, 1.0f);  // Exit at x=1
}

TEST(MathTest, RayBoxIntersection_DiagonalRay) {
    // Diagonal ray through box
    glm::vec3 rayOrigin(-5.0f, -5.0f, -5.0f);
    glm::vec3 rayDir = glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f));
    glm::vec3 boxMin(-1.0f, -1.0f, -1.0f);
    glm::vec3 boxMax(1.0f, 1.0f, 1.0f);
    
    float tNear, tFar;
    bool hit = Math::rayBoxIntersection(rayOrigin, rayDir, boxMin, boxMax, tNear, tFar);
    
    EXPECT_TRUE(hit);
    EXPECT_GT(tNear, 0.0f);
    EXPECT_GT(tFar, tNear);
}

TEST(MathTest, RayBoxIntersection_GrazingHit) {
    // Ray grazing edge of box
    glm::vec3 rayOrigin(-5.0f, 1.0f, 0.0f); // At top edge height
    glm::vec3 rayDir(1.0f, 0.0f, 0.0f);
    glm::vec3 boxMin(-1.0f, -1.0f, -1.0f);
    glm::vec3 boxMax(1.0f, 1.0f, 1.0f);
    
    float tNear, tFar;
    bool hit = Math::rayBoxIntersection(rayOrigin, rayDir, boxMin, boxMax, tNear, tFar);
    
    EXPECT_TRUE(hit); // Should hit the edge
}

TEST(MathTest, RayBoxIntersection_UnitBox) {
    // Test with unit box at origin (common voxel case)
    glm::vec3 rayOrigin(-2.0f, 0.5f, 0.5f);
    glm::vec3 rayDir(1.0f, 0.0f, 0.0f);
    glm::vec3 boxMin(0.0f, 0.0f, 0.0f);
    glm::vec3 boxMax(1.0f, 1.0f, 1.0f);
    
    float tNear, tFar;
    bool hit = Math::rayBoxIntersection(rayOrigin, rayDir, boxMin, boxMax, tNear, tFar);
    
    EXPECT_TRUE(hit);
    EXPECT_FLOAT_EQ(tNear, 2.0f); // Enter at x=0
    EXPECT_FLOAT_EQ(tFar, 3.0f);  // Exit at x=1
}

// ============================================================================
// Random Float Tests (Statistical)
// ============================================================================

TEST(MathTest, RandomFloat_WithinRange) {
    // Test that random floats are within expected range
    float strength = 2.0f;
    bool hasPositive = false;
    bool hasNegative = false;
    
    for (int i = 0; i < 100; ++i) {
        float value = Math::randomFloat(strength);
        EXPECT_GE(value, -strength);
        EXPECT_LE(value, strength);
        
        if (value > 0) hasPositive = true;
        if (value < 0) hasNegative = true;
    }
    
    // Statistical test: should have both positive and negative values
    EXPECT_TRUE(hasPositive) << "Should generate some positive values";
    EXPECT_TRUE(hasNegative) << "Should generate some negative values";
}

TEST(MathTest, RandomFloat_DifferentStrengths) {
    // Test with different strength values
    float value1 = Math::randomFloat(1.0f);
    EXPECT_GE(value1, -1.0f);
    EXPECT_LE(value1, 1.0f);
    
    float value10 = Math::randomFloat(10.0f);
    EXPECT_GE(value10, -10.0f);
    EXPECT_LE(value10, 10.0f);
}

// ============================================================================
// Screen to World Ray Tests
// ============================================================================

TEST(MathTest, ScreenToWorldRay_CenterOfScreen) {
    // Ray from center of screen should point forward
    int width = 800, height = 600;
    glm::mat4 view = glm::mat4(1.0f); // Identity view
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), 800.0f/600.0f, 0.1f, 100.0f);
    
    glm::vec3 ray = Math::screenToWorldRay(width/2.0, height/2.0, width, height, view, proj);
    
    // Should be normalized
    EXPECT_NEAR(glm::length(ray), 1.0f, 0.001f);
    
    // Should point roughly forward (negative Z in OpenGL)
    EXPECT_LT(ray.z, 0.0f);
}

TEST(MathTest, ScreenToWorldRay_Normalized) {
    // All rays should be normalized
    int width = 1920, height = 1080;
    glm::mat4 view = glm::lookAt(glm::vec3(0,0,5), glm::vec3(0,0,0), glm::vec3(0,1,0));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1920.0f/1080.0f, 0.1f, 1000.0f);
    
    // Test corners and center
    std::vector<std::pair<double, double>> testPoints = {
        {0, 0},           // Top-left
        {width, 0},       // Top-right
        {0, height},      // Bottom-left
        {width, height},  // Bottom-right
        {width/2.0, height/2.0}  // Center
    };
    
    for (const auto& point : testPoints) {
        glm::vec3 ray = Math::screenToWorldRay(point.first, point.second, width, height, view, proj);
        float len = glm::length(ray);
        EXPECT_NEAR(len, 1.0f, 0.001f) << "Ray at (" << point.first << "," << point.second << ") not normalized";
    }
}
