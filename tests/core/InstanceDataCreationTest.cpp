#include <gtest/gtest.h>
#include "core/Types.h"
#include <glm/glm.hpp>

using namespace VulkanCube;
using namespace VulkanCube::InstanceDataUtils;

// ============================================================================
// InstanceData Creation Tests
// ============================================================================

// Test createInstanceData with basic parameters
TEST(InstanceDataCreationTest, CreateInstanceData_Origin) {
    glm::ivec3 pos(0, 0, 0);
    CubeFaces faces;
    faces.front = false;
    faces.back = false;
    faces.left = false;
    faces.right = false;
    faces.top = false;
    faces.bottom = false;
    uint16_t textureIndex = 5;
    
    InstanceData instance = createInstanceData(pos, faces, textureIndex);
    
    EXPECT_EQ(instance.textureIndex, 5);
    EXPECT_EQ(instance.reserved, 0);
    
    // Verify position unpacking
    uint32_t x, y, z;
    unpackRelativePos(instance.packedData, x, y, z);
    EXPECT_EQ(x, 0u);
    EXPECT_EQ(y, 0u);
    EXPECT_EQ(z, 0u);
    
    // Verify face mask (all false should be 0)
    uint32_t faceMask = getFaceMask(instance.packedData);
    EXPECT_EQ(faceMask, 0u);
}

TEST(InstanceDataCreationTest, CreateInstanceData_AllFacesVisible) {
    glm::ivec3 pos(10, 15, 20);
    CubeFaces faces; // Default all true
    uint16_t textureIndex = 42;
    
    InstanceData instance = createInstanceData(pos, faces, textureIndex);
    
    EXPECT_EQ(instance.textureIndex, 42);
    
    // Verify position
    uint32_t x, y, z;
    unpackRelativePos(instance.packedData, x, y, z);
    EXPECT_EQ(x, 10u);
    EXPECT_EQ(y, 15u);
    EXPECT_EQ(z, 20u);
    
    // Verify all faces visible (should be 0b111111 = 63)
    uint32_t faceMask = getFaceMask(instance.packedData);
    EXPECT_EQ(faceMask, 0b111111u);
}

TEST(InstanceDataCreationTest, CreateInstanceData_MaxPosition) {
    glm::ivec3 pos(31, 31, 31); // Max for 5 bits
    CubeFaces faces;
    faces.front = true;
    faces.back = false;
    faces.left = false;
    faces.right = false;
    faces.top = false;
    faces.bottom = false;
    uint16_t textureIndex = 100;
    
    InstanceData instance = createInstanceData(pos, faces, textureIndex);
    
    EXPECT_EQ(instance.textureIndex, 100);
    
    // Verify position
    uint32_t x, y, z;
    unpackRelativePos(instance.packedData, x, y, z);
    EXPECT_EQ(x, 31u);
    EXPECT_EQ(y, 31u);
    EXPECT_EQ(z, 31u);
    
    // Verify face mask (only front = 0b000001)
    uint32_t faceMask = getFaceMask(instance.packedData);
    EXPECT_EQ(faceMask, 0b000001u);
}

TEST(InstanceDataCreationTest, CreateInstanceData_WithFutureData) {
    glm::ivec3 pos(5, 10, 15);
    CubeFaces faces;
    faces.front = true;
    faces.back = true;
    faces.left = false;
    faces.right = false;
    faces.top = false;
    faces.bottom = false;
    uint16_t textureIndex = 7;
    uint32_t futureData = 123; // Some arbitrary future data
    
    InstanceData instance = createInstanceData(pos, faces, textureIndex, futureData);
    
    EXPECT_EQ(instance.textureIndex, 7);
    
    // Verify position
    uint32_t x, y, z;
    unpackRelativePos(instance.packedData, x, y, z);
    EXPECT_EQ(x, 5u);
    EXPECT_EQ(y, 10u);
    EXPECT_EQ(z, 15u);
    
    // Verify face mask (front + back = 0b000011)
    uint32_t faceMask = getFaceMask(instance.packedData);
    EXPECT_EQ(faceMask, 0b000011u);
    
    // Verify future data
    uint32_t extractedFutureData = getFutureData(instance.packedData);
    EXPECT_EQ(extractedFutureData, 123u);
}

TEST(InstanceDataCreationTest, CreateInstanceData_VariousFaceCombinations) {
    glm::ivec3 pos(7, 8, 9);
    uint16_t textureIndex = 15;
    
    // Test: front + left + top
    // front=bit0, left=bit3, top=bit4 → binary 011001 = 25
    CubeFaces faces1;
    faces1.front = true;
    faces1.back = false;
    faces1.left = true;
    faces1.right = false;
    faces1.top = true;
    faces1.bottom = false;
    
    InstanceData instance1 = createInstanceData(pos, faces1, textureIndex);
    uint32_t faceMask1 = getFaceMask(instance1.packedData);
    EXPECT_EQ(faceMask1, 0b011001u); // front(bit0) + left(bit3) + top(bit4)
    
    // Test: back + right + bottom
    // back=bit1, right=bit2, bottom=bit5 → binary 100110 = 38
    CubeFaces faces2;
    faces2.front = false;
    faces2.back = true;
    faces2.left = false;
    faces2.right = true;
    faces2.top = false;
    faces2.bottom = true;
    
    InstanceData instance2 = createInstanceData(pos, faces2, textureIndex);
    uint32_t faceMask2 = getFaceMask(instance2.packedData);
    EXPECT_EQ(faceMask2, 0b100110u); // back(bit1) + right(bit2) + bottom(bit5)
}

TEST(InstanceDataCreationTest, CreateInstanceData_MaxTextureIndex) {
    glm::ivec3 pos(1, 2, 3);
    CubeFaces faces; // All visible
    uint16_t textureIndex = 65535; // Max for uint16_t
    
    InstanceData instance = createInstanceData(pos, faces, textureIndex);
    
    EXPECT_EQ(instance.textureIndex, 65535);
}

TEST(InstanceDataCreationTest, CreateInstanceData_ZeroTextureIndex) {
    glm::ivec3 pos(1, 2, 3);
    CubeFaces faces;
    uint16_t textureIndex = 0;
    
    InstanceData instance = createInstanceData(pos, faces, textureIndex);
    
    EXPECT_EQ(instance.textureIndex, 0);
}

// ============================================================================
// Legacy createInstanceDataLegacy Tests
// ============================================================================

TEST(InstanceDataCreationTest, CreateInstanceDataLegacy_BasicUsage) {
    glm::ivec3 pos(5, 10, 15);
    CubeFaces faces;
    faces.front = true;
    faces.back = true;
    faces.left = false;
    faces.right = false;
    faces.top = false;
    faces.bottom = false;
    glm::vec3 color(1.0f, 0.5f, 0.25f); // Color is ignored, using placeholder
    
    InstanceData instance = createInstanceDataLegacy(pos, faces, color);
    
    // Should use placeholder texture index 0
    EXPECT_EQ(instance.textureIndex, 0);
    
    // Verify position
    uint32_t x, y, z;
    unpackRelativePos(instance.packedData, x, y, z);
    EXPECT_EQ(x, 5u);
    EXPECT_EQ(y, 10u);
    EXPECT_EQ(z, 15u);
    
    // Verify face mask
    uint32_t faceMask = getFaceMask(instance.packedData);
    EXPECT_EQ(faceMask, 0b000011u);
}

TEST(InstanceDataCreationTest, CreateInstanceDataLegacy_WithFutureData) {
    glm::ivec3 pos(20, 21, 22);
    CubeFaces faces;
    glm::vec3 color(0.0f, 0.0f, 0.0f);
    uint32_t futureData = 456;
    
    InstanceData instance = createInstanceDataLegacy(pos, faces, color, futureData);
    
    // Should use placeholder texture index 0
    EXPECT_EQ(instance.textureIndex, 0);
    
    // Verify future data
    uint32_t extractedFutureData = getFutureData(instance.packedData);
    EXPECT_EQ(extractedFutureData, 456u);
}

TEST(InstanceDataCreationTest, CreateInstanceDataLegacy_AllFaces) {
    glm::ivec3 pos(0, 0, 0);
    CubeFaces faces; // All true by default
    glm::vec3 color(1.0f, 1.0f, 1.0f);
    
    InstanceData instance = createInstanceDataLegacy(pos, faces, color);
    
    // Verify all faces visible
    uint32_t faceMask = getFaceMask(instance.packedData);
    EXPECT_EQ(faceMask, 0b111111u);
}

// ============================================================================
// Round-trip and Integration Tests
// ============================================================================

TEST(InstanceDataCreationTest, RoundTrip_CreateAndExtract) {
    // Create instance data with known values
    glm::ivec3 originalPos(12, 18, 24);
    CubeFaces originalFaces;
    originalFaces.front = true;
    originalFaces.back = false;
    originalFaces.left = true;
    originalFaces.right = false;
    originalFaces.top = true;
    originalFaces.bottom = false;
    uint16_t originalTexture = 789;
    uint32_t originalFuture = 321;
    
    // Create instance
    InstanceData instance = createInstanceData(originalPos, originalFaces, originalTexture, originalFuture);
    
    // Extract and verify all components
    uint32_t x, y, z;
    unpackRelativePos(instance.packedData, x, y, z);
    EXPECT_EQ(x, 12u);
    EXPECT_EQ(y, 18u);
    EXPECT_EQ(z, 24u);
    
    // front=bit0, left=bit3, top=bit4 → binary 011001 = 25
    uint32_t faceMask = getFaceMask(instance.packedData);
    EXPECT_EQ(faceMask, 0b011001u); // front + left + top
    
    uint32_t futureData = getFutureData(instance.packedData);
    EXPECT_EQ(futureData, 321u);
    
    EXPECT_EQ(instance.textureIndex, 789);
}

TEST(InstanceDataCreationTest, CompareNewAndLegacy) {
    glm::ivec3 pos(7, 14, 21);
    CubeFaces faces;
    faces.front = true;
    faces.back = true;
    faces.left = true;
    faces.right = true;
    faces.top = false;
    faces.bottom = false;
    glm::vec3 color(0.5f, 0.5f, 0.5f);
    
    // Create with both methods
    InstanceData newStyle = createInstanceData(pos, faces, 0);
    InstanceData legacyStyle = createInstanceDataLegacy(pos, faces, color);
    
    // Should produce identical packed data (both use texture index 0)
    EXPECT_EQ(newStyle.packedData, legacyStyle.packedData);
    EXPECT_EQ(newStyle.textureIndex, legacyStyle.textureIndex);
}

TEST(InstanceDataCreationTest, MultipleInstances_DifferentTextures) {
    glm::ivec3 pos(5, 5, 5);
    CubeFaces faces; // All visible
    
    // Create multiple instances with different textures
    InstanceData instance1 = createInstanceData(pos, faces, 10);
    InstanceData instance2 = createInstanceData(pos, faces, 20);
    InstanceData instance3 = createInstanceData(pos, faces, 30);
    
    // Same packed data (position and faces identical)
    EXPECT_EQ(instance1.packedData, instance2.packedData);
    EXPECT_EQ(instance2.packedData, instance3.packedData);
    
    // Different texture indices
    EXPECT_EQ(instance1.textureIndex, 10);
    EXPECT_EQ(instance2.textureIndex, 20);
    EXPECT_EQ(instance3.textureIndex, 30);
}

TEST(InstanceDataCreationTest, ReservedField_AlwaysZero) {
    // Test that reserved field is always initialized to 0
    glm::ivec3 pos(15, 16, 17);
    CubeFaces faces;
    
    InstanceData instance1 = createInstanceData(pos, faces, 100);
    InstanceData instance2 = createInstanceData(pos, faces, 200, 500);
    InstanceData instance3 = createInstanceDataLegacy(pos, faces, glm::vec3(1.0f));
    
    EXPECT_EQ(instance1.reserved, 0);
    EXPECT_EQ(instance2.reserved, 0);
    EXPECT_EQ(instance3.reserved, 0);
}
