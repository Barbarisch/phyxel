/**
 * Unit tests for Types utilities
 * 
 * Tests data packing/unpacking functions used for GPU instance data:
 * - Position packing (relative coordinates)
 * - Face mask packing
 * - Instance data packing/unpacking
 * - Round-trip conversions
 */

#include <gtest/gtest.h>
#include "core/Types.h"
#include <glm/glm.hpp>

using namespace Phyxel;
using namespace Phyxel::InstanceDataUtils;

// ============================================================================
// Relative Position Packing Tests
// ============================================================================

TEST(TypesTest, PackRelativePos_Origin) {
    uint16_t packed = packRelativePos(0, 0, 0);
    EXPECT_EQ(packed, 0);
}

TEST(TypesTest, PackRelativePos_MaxValues) {
    // Max values for 5-bit packing (0-31)
    uint16_t packed = packRelativePos(31, 31, 31);
    // Expected: bits 0-4: Z=31, bits 5-9: Y=31, bits 10-14: X=31
    EXPECT_EQ(packed, 0b0111111111111111); // 15 bits set (5 bits * 3)
}

TEST(TypesTest, PackRelativePos_SingleValues) {
    // packRelativePos uses: (x & 0x1F) | ((y & 0x1F) << 5) | ((z & 0x1F) << 10)
    // So: X in bits 0-4, Y in bits 5-9, Z in bits 10-14
    
    // Test X only
    uint16_t packedX = packRelativePos(1, 0, 0);
    EXPECT_EQ(packedX, 1); // X in bits 0-4
    
    // Test Y only
    uint16_t packedY = packRelativePos(0, 1, 0);
    EXPECT_EQ(packedY, 1 << 5); // Y in bits 5-9
    
    // Test Z only
    uint16_t packedZ = packRelativePos(0, 0, 1);
    EXPECT_EQ(packedZ, 1 << 10); // Z in bits 10-14
}

TEST(TypesTest, PackRelativePos_ArbitraryValues) {
    // Test (5, 10, 15)
    uint16_t packed = packRelativePos(5, 10, 15);
    // X=5 (bits 0-4), Y=10 (bits 5-9), Z=15 (bits 10-14)
    uint16_t expected = (5) | (10 << 5) | (15 << 10);
    EXPECT_EQ(packed, expected);
}

// ============================================================================
// Relative Position Unpacking Tests
// ============================================================================

TEST(TypesTest, UnpackRelativePos_Origin) {
    uint32_t x, y, z;
    unpackRelativePos(0, x, y, z);
    EXPECT_EQ(x, 0);
    EXPECT_EQ(y, 0);
    EXPECT_EQ(z, 0);
}

TEST(TypesTest, UnpackRelativePos_MaxValues) {
    uint32_t x, y, z;
    uint32_t packed = packRelativePos(31, 31, 31);
    unpackRelativePos(packed, x, y, z);
    EXPECT_EQ(x, 31);
    EXPECT_EQ(y, 31);
    EXPECT_EQ(z, 31);
}

TEST(TypesTest, UnpackRelativePos_ArbitraryValues) {
    uint32_t x, y, z;
    uint32_t packed = packRelativePos(5, 10, 15);
    unpackRelativePos(packed, x, y, z);
    EXPECT_EQ(x, 5);
    EXPECT_EQ(y, 10);
    EXPECT_EQ(z, 15);
}

TEST(TypesTest, RoundTrip_RelativePositionPacking) {
    // Test all valid values for 5-bit packing (0-31)
    for (uint32_t x = 0; x < 32; ++x) {
        for (uint32_t y = 0; y < 32; y += 7) { // Skip some for speed
            for (uint32_t z = 0; z < 32; z += 7) {
                uint16_t packed = packRelativePos(x, y, z);
                uint32_t ux, uy, uz;
                unpackRelativePos(packed, ux, uy, uz);
                EXPECT_EQ(ux, x) << "Failed for (" << x << "," << y << "," << z << ")";
                EXPECT_EQ(uy, y) << "Failed for (" << x << "," << y << "," << z << ")";
                EXPECT_EQ(uz, z) << "Failed for (" << x << "," << y << "," << z << ")";
            }
        }
    }
}

// ============================================================================
// Face Mask Packing Tests
// ============================================================================

TEST(TypesTest, PackFaceMask_NoFaces) {
    CubeFaces faces{false, false, false, false, false, false};
    uint32_t mask = packFaceMask(faces);
    EXPECT_EQ(mask, 0);
}

TEST(TypesTest, PackFaceMask_AllFaces) {
    CubeFaces faces{true, true, true, true, true, true};
    uint32_t mask = packFaceMask(faces);
    EXPECT_EQ(mask, 0b111111);
}

TEST(TypesTest, PackFaceMask_SingleFace) {
    // packFaceMask uses: front=bit0, back=bit1, right=bit2, left=bit3, top=bit4, bottom=bit5
    CubeFaces frontOnly{true, false, false, false, false, false};
    EXPECT_EQ(packFaceMask(frontOnly), 0b000001);
    
    CubeFaces backOnly{false, true, false, false, false, false};
    EXPECT_EQ(packFaceMask(backOnly), 0b000010);
    
    CubeFaces leftOnly{false, false, true, false, false, false};
    EXPECT_EQ(packFaceMask(leftOnly), 0b001000);  // left=bit3
    
    CubeFaces rightOnly{false, false, false, true, false, false};
    EXPECT_EQ(packFaceMask(rightOnly), 0b000100);  // right=bit2
    
    CubeFaces topOnly{false, false, false, false, true, false};
    EXPECT_EQ(packFaceMask(topOnly), 0b010000);
    
    CubeFaces bottomOnly{false, false, false, false, false, true};
    EXPECT_EQ(packFaceMask(bottomOnly), 0b100000);
}

// ============================================================================
// Instance Data Packing Tests
// ============================================================================

TEST(TypesTest, PackInstanceData_Origin) {
    uint32_t packed = packInstanceData(0, 0, 0, 0b111111, 0);
    
    // Extract face mask (should be at bits 15-20)
    uint32_t faceMask = getFaceMask(packed);
    EXPECT_EQ(faceMask, 0b111111);
}

TEST(TypesTest, PackInstanceData_MaxRelativePos) {
    uint32_t packed = packInstanceData(31, 31, 31, 0b111111, 0);
    
    uint32_t x, y, z;
    unpackRelativePos(packed, x, y, z);
    EXPECT_EQ(x, 31);
    EXPECT_EQ(y, 31);
    EXPECT_EQ(z, 31);
    
    uint32_t faceMask = getFaceMask(packed);
    EXPECT_EQ(faceMask, 0b111111);
}

TEST(TypesTest, PackInstanceData_ArbitraryValues) {
    uint32_t packed = packInstanceData(10, 15, 20, 0b101010, 0);
    
    uint32_t x, y, z;
    unpackRelativePos(packed, x, y, z);
    EXPECT_EQ(x, 10);
    EXPECT_EQ(y, 15);
    EXPECT_EQ(z, 20);
    
    uint32_t faceMask = getFaceMask(packed);
    EXPECT_EQ(faceMask, 0b101010);
}

TEST(TypesTest, GetFaceMask_ExtractsCorrectly) {
    // Pack data with specific face mask
    uint32_t packed = packInstanceData(5, 5, 5, 0b110011, 0);
    
    uint32_t faceMask = getFaceMask(packed);
    EXPECT_EQ(faceMask, 0b110011);
}

TEST(TypesTest, GetFutureData_ExtractsCorrectly) {
    // Pack with future data
    uint32_t packed = packInstanceData(5, 5, 5, 0b111111, 42);
    
    uint32_t futureData = getFutureData(packed);
    EXPECT_EQ(futureData, 42);
}

// ============================================================================
// Relative Position and Faces Combined Packing
// ============================================================================

TEST(TypesTest, PackRelativePosAndFaces_WithIntegers) {
    uint32_t packed = packRelativePosAndFaces(10, 20, 30, 0b111111);
    
    uint32_t x, y, z;
    unpackRelativePos(packed, x, y, z);
    EXPECT_EQ(x, 10);
    EXPECT_EQ(y, 20);
    EXPECT_EQ(z, 30);
    
    uint32_t faceMask = getFaceMask(packed);
    EXPECT_EQ(faceMask, 0b111111);
}

TEST(TypesTest, PackRelativePosAndFaces_WithStructs) {
    glm::ivec3 pos(5, 10, 15);
    CubeFaces faces{true, false, true, false, true, false};
    
    uint32_t packed = packRelativePosAndFaces(pos, faces);
    
    uint32_t x, y, z;
    unpackRelativePos(packed, x, y, z);
    EXPECT_EQ(x, 5);
    EXPECT_EQ(y, 10);
    EXPECT_EQ(z, 15);
    
    uint32_t faceMask = getFaceMask(packed);
    // front=1, back=0, left=1, right=0, top=1, bottom=0
    // Bit layout: front=bit0, back=bit1, right=bit2, left=bit3, top=bit4, bottom=bit5
    // front=1, back=0, left(3rd param)=1, right(4th param)=0, top=1, bottom=0
    // 1, 0, 1, 0, 1, 0  →  bit0=1, bit1=0, bit2=0, bit3=1, bit4=1, bit5=0 = 0b011001 = 25
    EXPECT_EQ(faceMask, 0b011001);
}

// ============================================================================
// Face-Specific Packing Tests
// ============================================================================

TEST(TypesTest, PackCubeFaceData_AllFaces) {
    // Test packing for each face ID (0-5)
    for (uint32_t faceID = 0; faceID < 6; ++faceID) {
        uint32_t packed = packCubeFaceData(10, 20, 30, faceID);
        
        // Verify position can be extracted
        uint32_t x, y, z;
        unpackRelativePos(packed, x, y, z);
        EXPECT_EQ(x, 10) << "Failed for face " << faceID;
        EXPECT_EQ(y, 20) << "Failed for face " << faceID;
        EXPECT_EQ(z, 30) << "Failed for face " << faceID;
    }
}

TEST(TypesTest, PackSubcubeFaceData_VariousPositions) {
    // Parent at (5, 10, 15), subcube at (2, 3, 4), face 0
    uint32_t packed = packSubcubeFaceData(5, 10, 15, 0, 2, 3, 4);
    
    // Should contain parent position
    uint32_t x, y, z;
    unpackRelativePos(packed, x, y, z);
    EXPECT_EQ(x, 5);
    EXPECT_EQ(y, 10);
    EXPECT_EQ(z, 15);
}

TEST(TypesTest, PackMicrocubeFaceData_VariousPositions) {
    // Parent at (1, 2, 3), subcube at (4, 5, 6), microcube at (7, 8, 9), face 0
    // Note: Subcube and microcube positions should be 0-2 (3x3x3 grid)
    uint32_t packed = packMicrocubeFaceData(1, 2, 3, 0, 1, 2, 0, 1, 2, 0);
    
    // Should contain parent position
    uint32_t x, y, z;
    unpackRelativePos(packed, x, y, z);
    EXPECT_EQ(x, 1);
    EXPECT_EQ(y, 2);
    EXPECT_EQ(z, 3);
}

// ============================================================================
// Edge Cases and Boundary Tests
// ============================================================================

TEST(TypesTest, PackRelativePos_BoundaryValues) {
    // Test boundary at 31 (max 5-bit value)
    uint16_t packed31 = packRelativePos(31, 0, 0);
    uint32_t x, y, z;
    unpackRelativePos(packed31, x, y, z);
    EXPECT_EQ(x, 31);
    
    // Test boundary at 0
    uint16_t packed0 = packRelativePos(0, 0, 0);
    unpackRelativePos(packed0, x, y, z);
    EXPECT_EQ(x, 0);
}

TEST(TypesTest, FaceMask_AllCombinations) {
    // Test a few specific combinations
    CubeFaces topBottom{false, false, false, false, true, true};
    EXPECT_EQ(packFaceMask(topBottom), 0b110000);
    
    CubeFaces frontBackLeftRight{true, true, true, true, false, false};
    EXPECT_EQ(packFaceMask(frontBackLeftRight), 0b001111);
    
    CubeFaces alternating{true, false, true, false, true, false};
    // front=1, back=0, left=1, right=0, top=1, bottom=0
    // Bit layout: bit0=1, bit1=0, bit2=0, bit3=1, bit4=1, bit5=0 = 0b011001 = 25
    EXPECT_EQ(packFaceMask(alternating), 0b011001);
}

TEST(TypesTest, InstanceData_FutureDataPreserved) {
    // Test that future data bits don't interfere with position/face data
    for (uint32_t futureData = 0; futureData < 10; ++futureData) {
        uint32_t packed = packInstanceData(15, 15, 15, 0b111111, futureData);
        
        uint32_t x, y, z;
        unpackRelativePos(packed, x, y, z);
        EXPECT_EQ(x, 15);
        EXPECT_EQ(y, 15);
        EXPECT_EQ(z, 15);
        
        uint32_t faceMask = getFaceMask(packed);
        EXPECT_EQ(faceMask, 0b111111);
        
        uint32_t extractedFuture = getFutureData(packed);
        EXPECT_EQ(extractedFuture, futureData);
    }
}
