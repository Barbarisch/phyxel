/**
 * Unit tests for 3x3x3 grid encoding utilities
 * 
 * Tests the encoding/decoding functions used for packing subcube and microcube
 * positions into 6-bit values for GPU instance data.
 */

#include <gtest/gtest.h>
#include "core/Types.h"

using namespace VulkanCube;
using namespace VulkanCube::InstanceDataUtils;

// ============================================================================
// Grid Encoding Tests (3x3x3 → 6 bits)
// ============================================================================

TEST(GridEncodingTest, EncodeGrid3x3x3_Origin) {
    uint32_t encoded = encodeGrid3x3x3(0, 0, 0);
    EXPECT_EQ(encoded, 0);
}

TEST(GridEncodingTest, EncodeGrid3x3x3_MaxValues) {
    // Max valid values for 3x3x3 grid (2, 2, 2)
    uint32_t encoded = encodeGrid3x3x3(2, 2, 2);
    // Formula: x + y*3 + z*9 = 2 + 2*3 + 2*9 = 2 + 6 + 18 = 26
    EXPECT_EQ(encoded, 26);
}

TEST(GridEncodingTest, EncodeGrid3x3x3_XAxis) {
    // Test points along X axis
    EXPECT_EQ(encodeGrid3x3x3(0, 0, 0), 0);
    EXPECT_EQ(encodeGrid3x3x3(1, 0, 0), 1);
    EXPECT_EQ(encodeGrid3x3x3(2, 0, 0), 2);
}

TEST(GridEncodingTest, EncodeGrid3x3x3_YAxis) {
    // Test points along Y axis (y contributes 3 per unit)
    EXPECT_EQ(encodeGrid3x3x3(0, 0, 0), 0);
    EXPECT_EQ(encodeGrid3x3x3(0, 1, 0), 3);
    EXPECT_EQ(encodeGrid3x3x3(0, 2, 0), 6);
}

TEST(GridEncodingTest, EncodeGrid3x3x3_ZAxis) {
    // Test points along Z axis (z contributes 9 per unit)
    EXPECT_EQ(encodeGrid3x3x3(0, 0, 0), 0);
    EXPECT_EQ(encodeGrid3x3x3(0, 0, 1), 9);
    EXPECT_EQ(encodeGrid3x3x3(0, 0, 2), 18);
}

TEST(GridEncodingTest, EncodeGrid3x3x3_ArbitraryPositions) {
    // Test various positions in the 3x3x3 grid
    EXPECT_EQ(encodeGrid3x3x3(1, 1, 1), 1 + 3 + 9);  // 13
    EXPECT_EQ(encodeGrid3x3x3(2, 1, 0), 2 + 3);      // 5
    EXPECT_EQ(encodeGrid3x3x3(1, 2, 1), 1 + 6 + 9);  // 16
}

TEST(GridEncodingTest, EncodeGrid3x3x3_AllPositions) {
    // Verify all 27 positions in 3x3x3 grid produce unique values
    std::set<uint32_t> encoded_values;
    for (uint32_t x = 0; x < 3; ++x) {
        for (uint32_t y = 0; y < 3; ++y) {
            for (uint32_t z = 0; z < 3; ++z) {
                uint32_t encoded = encodeGrid3x3x3(x, y, z);
                EXPECT_LT(encoded, 27) << "Encoded value out of range for (" << x << "," << y << "," << z << ")";
                encoded_values.insert(encoded);
            }
        }
    }
    // All 27 positions should produce unique values
    EXPECT_EQ(encoded_values.size(), 27);
}

// ============================================================================
// Grid Decoding Tests (6 bits → 3x3x3)
// ============================================================================

TEST(GridEncodingTest, DecodeGrid3x3x3_Origin) {
    uint32_t x, y, z;
    decodeGrid3x3x3(0, x, y, z);
    EXPECT_EQ(x, 0);
    EXPECT_EQ(y, 0);
    EXPECT_EQ(z, 0);
}

TEST(GridEncodingTest, DecodeGrid3x3x3_MaxValue) {
    uint32_t x, y, z;
    decodeGrid3x3x3(26, x, y, z);
    EXPECT_EQ(x, 2);
    EXPECT_EQ(y, 2);
    EXPECT_EQ(z, 2);
}

TEST(GridEncodingTest, DecodeGrid3x3x3_XAxis) {
    uint32_t x, y, z;
    
    decodeGrid3x3x3(0, x, y, z);
    EXPECT_EQ(x, 0); EXPECT_EQ(y, 0); EXPECT_EQ(z, 0);
    
    decodeGrid3x3x3(1, x, y, z);
    EXPECT_EQ(x, 1); EXPECT_EQ(y, 0); EXPECT_EQ(z, 0);
    
    decodeGrid3x3x3(2, x, y, z);
    EXPECT_EQ(x, 2); EXPECT_EQ(y, 0); EXPECT_EQ(z, 0);
}

TEST(GridEncodingTest, DecodeGrid3x3x3_YAxis) {
    uint32_t x, y, z;
    
    decodeGrid3x3x3(3, x, y, z);
    EXPECT_EQ(x, 0); EXPECT_EQ(y, 1); EXPECT_EQ(z, 0);
    
    decodeGrid3x3x3(6, x, y, z);
    EXPECT_EQ(x, 0); EXPECT_EQ(y, 2); EXPECT_EQ(z, 0);
}

TEST(GridEncodingTest, DecodeGrid3x3x3_ZAxis) {
    uint32_t x, y, z;
    
    decodeGrid3x3x3(9, x, y, z);
    EXPECT_EQ(x, 0); EXPECT_EQ(y, 0); EXPECT_EQ(z, 1);
    
    decodeGrid3x3x3(18, x, y, z);
    EXPECT_EQ(x, 0); EXPECT_EQ(y, 0); EXPECT_EQ(z, 2);
}

TEST(GridEncodingTest, DecodeGrid3x3x3_ArbitraryValues) {
    uint32_t x, y, z;
    
    decodeGrid3x3x3(13, x, y, z);  // 1 + 3 + 9
    EXPECT_EQ(x, 1); EXPECT_EQ(y, 1); EXPECT_EQ(z, 1);
    
    decodeGrid3x3x3(5, x, y, z);   // 2 + 3
    EXPECT_EQ(x, 2); EXPECT_EQ(y, 1); EXPECT_EQ(z, 0);
    
    decodeGrid3x3x3(16, x, y, z);  // 1 + 6 + 9
    EXPECT_EQ(x, 1); EXPECT_EQ(y, 2); EXPECT_EQ(z, 1);
}

// ============================================================================
// Round-Trip Tests
// ============================================================================

TEST(GridEncodingTest, RoundTrip_EncodeDecodeGrid) {
    // Test all valid positions in 3x3x3 grid
    for (uint32_t x = 0; x < 3; ++x) {
        for (uint32_t y = 0; y < 3; ++y) {
            for (uint32_t z = 0; z < 3; ++z) {
                uint32_t encoded = encodeGrid3x3x3(x, y, z);
                
                uint32_t dx, dy, dz;
                decodeGrid3x3x3(encoded, dx, dy, dz);
                
                EXPECT_EQ(dx, x) << "Failed for (" << x << "," << y << "," << z << ")";
                EXPECT_EQ(dy, y) << "Failed for (" << x << "," << y << "," << z << ")";
                EXPECT_EQ(dz, z) << "Failed for (" << x << "," << y << "," << z << ")";
            }
        }
    }
}

TEST(GridEncodingTest, RoundTrip_DecodeEncodeGrid) {
    // Test all valid encoded values (0-26)
    for (uint32_t encoded = 0; encoded < 27; ++encoded) {
        uint32_t x, y, z;
        decodeGrid3x3x3(encoded, x, y, z);
        
        uint32_t reencoded = encodeGrid3x3x3(x, y, z);
        
        EXPECT_EQ(reencoded, encoded) << "Failed for encoded value " << encoded;
    }
}

// ============================================================================
// Edge Cases and Boundary Tests
// ============================================================================

TEST(GridEncodingTest, EncodingBoundaries_ValidRange) {
    // All valid 3x3x3 positions should encode to 0-26
    for (uint32_t x = 0; x < 3; ++x) {
        for (uint32_t y = 0; y < 3; ++y) {
            for (uint32_t z = 0; z < 3; ++z) {
                uint32_t encoded = encodeGrid3x3x3(x, y, z);
                EXPECT_GE(encoded, 0);
                EXPECT_LE(encoded, 26);
            }
        }
    }
}

TEST(GridEncodingTest, DecodingBoundaries_ValidRange) {
    // All valid encoded values should decode to coordinates 0-2
    for (uint32_t encoded = 0; encoded < 27; ++encoded) {
        uint32_t x, y, z;
        decodeGrid3x3x3(encoded, x, y, z);
        
        EXPECT_GE(x, 0); EXPECT_LE(x, 2);
        EXPECT_GE(y, 0); EXPECT_LE(y, 2);
        EXPECT_GE(z, 0); EXPECT_LE(z, 2);
    }
}

TEST(GridEncodingTest, CornerPositions) {
    // Test all 8 corners of the 3x3x3 grid
    struct Corner { uint32_t x, y, z, expected; };
    Corner corners[] = {
        {0, 0, 0, 0},   // Origin corner
        {2, 0, 0, 2},   // +X corner
        {0, 2, 0, 6},   // +Y corner
        {2, 2, 0, 8},   // +X+Y corner
        {0, 0, 2, 18},  // +Z corner
        {2, 0, 2, 20},  // +X+Z corner
        {0, 2, 2, 24},  // +Y+Z corner
        {2, 2, 2, 26},  // Opposite corner (max)
    };
    
    for (const auto& corner : corners) {
        uint32_t encoded = encodeGrid3x3x3(corner.x, corner.y, corner.z);
        EXPECT_EQ(encoded, corner.expected) 
            << "Corner (" << corner.x << "," << corner.y << "," << corner.z << ")";
        
        // Verify decoding
        uint32_t x, y, z;
        decodeGrid3x3x3(encoded, x, y, z);
        EXPECT_EQ(x, corner.x);
        EXPECT_EQ(y, corner.y);
        EXPECT_EQ(z, corner.z);
    }
}

TEST(GridEncodingTest, CenterPosition) {
    // Test the center of the 3x3x3 grid
    uint32_t encoded = encodeGrid3x3x3(1, 1, 1);
    EXPECT_EQ(encoded, 13);  // 1 + 1*3 + 1*9 = 13
    
    uint32_t x, y, z;
    decodeGrid3x3x3(13, x, y, z);
    EXPECT_EQ(x, 1);
    EXPECT_EQ(y, 1);
    EXPECT_EQ(z, 1);
}
