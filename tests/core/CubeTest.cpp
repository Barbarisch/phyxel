/**
 * Unit tests for Cube bond direction utilities
 * 
 * Tests static utility functions for bond direction conversions.
 */

#include <gtest/gtest.h>
#include "core/Cube.h"
#include <glm/glm.hpp>

using namespace VulkanCube;

// ============================================================================
// Direction Conversion Tests
// ============================================================================

TEST(CubeTest, GetDirectionVector_PositiveX) {
    glm::ivec3 dir = Cube::getDirectionVector(BondDirection::POSITIVE_X);
    EXPECT_EQ(dir, glm::ivec3(1, 0, 0));
}

TEST(CubeTest, GetDirectionVector_NegativeX) {
    glm::ivec3 dir = Cube::getDirectionVector(BondDirection::NEGATIVE_X);
    EXPECT_EQ(dir, glm::ivec3(-1, 0, 0));
}

TEST(CubeTest, GetDirectionVector_PositiveY) {
    glm::ivec3 dir = Cube::getDirectionVector(BondDirection::POSITIVE_Y);
    EXPECT_EQ(dir, glm::ivec3(0, 1, 0));
}

TEST(CubeTest, GetDirectionVector_NegativeY) {
    glm::ivec3 dir = Cube::getDirectionVector(BondDirection::NEGATIVE_Y);
    EXPECT_EQ(dir, glm::ivec3(0, -1, 0));
}

TEST(CubeTest, GetDirectionVector_PositiveZ) {
    glm::ivec3 dir = Cube::getDirectionVector(BondDirection::POSITIVE_Z);
    EXPECT_EQ(dir, glm::ivec3(0, 0, 1));
}

TEST(CubeTest, GetDirectionVector_NegativeZ) {
    glm::ivec3 dir = Cube::getDirectionVector(BondDirection::NEGATIVE_Z);
    EXPECT_EQ(dir, glm::ivec3(0, 0, -1));
}

// ============================================================================
// Opposite Direction Tests
// ============================================================================

TEST(CubeTest, GetOppositeDirection_PositiveX) {
    BondDirection opposite = Cube::getOppositeDirection(BondDirection::POSITIVE_X);
    EXPECT_EQ(opposite, BondDirection::NEGATIVE_X);
}

TEST(CubeTest, GetOppositeDirection_NegativeX) {
    BondDirection opposite = Cube::getOppositeDirection(BondDirection::NEGATIVE_X);
    EXPECT_EQ(opposite, BondDirection::POSITIVE_X);
}

TEST(CubeTest, GetOppositeDirection_PositiveY) {
    BondDirection opposite = Cube::getOppositeDirection(BondDirection::POSITIVE_Y);
    EXPECT_EQ(opposite, BondDirection::NEGATIVE_Y);
}

TEST(CubeTest, GetOppositeDirection_NegativeY) {
    BondDirection opposite = Cube::getOppositeDirection(BondDirection::NEGATIVE_Y);
    EXPECT_EQ(opposite, BondDirection::POSITIVE_Y);
}

TEST(CubeTest, GetOppositeDirection_PositiveZ) {
    BondDirection opposite = Cube::getOppositeDirection(BondDirection::POSITIVE_Z);
    EXPECT_EQ(opposite, BondDirection::NEGATIVE_Z);
}

TEST(CubeTest, GetOppositeDirection_NegativeZ) {
    BondDirection opposite = Cube::getOppositeDirection(BondDirection::NEGATIVE_Z);
    EXPECT_EQ(opposite, BondDirection::POSITIVE_Z);
}

// ============================================================================
// Vector to Direction Tests
// ============================================================================

TEST(CubeTest, VectorToDirection_PositiveX) {
    BondDirection dir = Cube::vectorToDirection(glm::ivec3(1, 0, 0));
    EXPECT_EQ(dir, BondDirection::POSITIVE_X);
}

TEST(CubeTest, VectorToDirection_NegativeX) {
    BondDirection dir = Cube::vectorToDirection(glm::ivec3(-1, 0, 0));
    EXPECT_EQ(dir, BondDirection::NEGATIVE_X);
}

TEST(CubeTest, VectorToDirection_PositiveY) {
    BondDirection dir = Cube::vectorToDirection(glm::ivec3(0, 1, 0));
    EXPECT_EQ(dir, BondDirection::POSITIVE_Y);
}

TEST(CubeTest, VectorToDirection_NegativeY) {
    BondDirection dir = Cube::vectorToDirection(glm::ivec3(0, -1, 0));
    EXPECT_EQ(dir, BondDirection::NEGATIVE_Y);
}

TEST(CubeTest, VectorToDirection_PositiveZ) {
    BondDirection dir = Cube::vectorToDirection(glm::ivec3(0, 0, 1));
    EXPECT_EQ(dir, BondDirection::POSITIVE_Z);
}

TEST(CubeTest, VectorToDirection_NegativeZ) {
    BondDirection dir = Cube::vectorToDirection(glm::ivec3(0, 0, -1));
    EXPECT_EQ(dir, BondDirection::NEGATIVE_Z);
}

// ============================================================================
// Round-Trip Conversion Tests
// ============================================================================

TEST(CubeTest, RoundTrip_DirectionToVectorToDirection) {
    // Test all 6 bond directions
    BondDirection directions[] = {
        BondDirection::POSITIVE_X,
        BondDirection::NEGATIVE_X,
        BondDirection::POSITIVE_Y,
        BondDirection::NEGATIVE_Y,
        BondDirection::POSITIVE_Z,
        BondDirection::NEGATIVE_Z
    };
    
    for (BondDirection dir : directions) {
        glm::ivec3 vec = Cube::getDirectionVector(dir);
        BondDirection result = Cube::vectorToDirection(vec);
        EXPECT_EQ(result, dir) << "Failed for direction " << static_cast<int>(dir);
    }
}

TEST(CubeTest, RoundTrip_OppositeDirection) {
    // Applying opposite twice should return original direction
    BondDirection directions[] = {
        BondDirection::POSITIVE_X,
        BondDirection::NEGATIVE_X,
        BondDirection::POSITIVE_Y,
        BondDirection::NEGATIVE_Y,
        BondDirection::POSITIVE_Z,
        BondDirection::NEGATIVE_Z
    };
    
    for (BondDirection dir : directions) {
        BondDirection opposite = Cube::getOppositeDirection(dir);
        BondDirection backToOriginal = Cube::getOppositeDirection(opposite);
        EXPECT_EQ(backToOriginal, dir) << "Failed for direction " << static_cast<int>(dir);
    }
}

// ============================================================================
// Direction Vector Properties Tests
// ============================================================================

TEST(CubeTest, DirectionVectors_AreUnitVectors) {
    // All direction vectors should have length 1
    BondDirection directions[] = {
        BondDirection::POSITIVE_X,
        BondDirection::NEGATIVE_X,
        BondDirection::POSITIVE_Y,
        BondDirection::NEGATIVE_Y,
        BondDirection::POSITIVE_Z,
        BondDirection::NEGATIVE_Z
    };
    
    for (BondDirection dir : directions) {
        glm::ivec3 vec = Cube::getDirectionVector(dir);
        int length = abs(vec.x) + abs(vec.y) + abs(vec.z);
        EXPECT_EQ(length, 1) << "Direction vector not unit length for " << static_cast<int>(dir);
    }
}

TEST(CubeTest, DirectionVectors_AreOrthogonal) {
    // Direction vectors along different axes should be orthogonal
    glm::ivec3 xDir = Cube::getDirectionVector(BondDirection::POSITIVE_X);
    glm::ivec3 yDir = Cube::getDirectionVector(BondDirection::POSITIVE_Y);
    glm::ivec3 zDir = Cube::getDirectionVector(BondDirection::POSITIVE_Z);
    
    // Manual dot products for integer vectors (x.x*y.x + x.y*y.y + x.z*y.z)
    EXPECT_EQ(xDir.x * yDir.x + xDir.y * yDir.y + xDir.z * yDir.z, 0);
    EXPECT_EQ(xDir.x * zDir.x + xDir.y * zDir.y + xDir.z * zDir.z, 0);
    EXPECT_EQ(yDir.x * zDir.x + yDir.y * zDir.y + yDir.z * zDir.z, 0);
}

TEST(CubeTest, OppositeDirections_AreNegatives) {
    // Opposite direction vectors should sum to zero
    BondDirection directions[] = {
        BondDirection::POSITIVE_X,
        BondDirection::POSITIVE_Y,
        BondDirection::POSITIVE_Z
    };
    
    for (BondDirection dir : directions) {
        glm::ivec3 vec = Cube::getDirectionVector(dir);
        BondDirection oppositeDir = Cube::getOppositeDirection(dir);
        glm::ivec3 oppositeVec = Cube::getDirectionVector(oppositeDir);
        
        glm::ivec3 sum = vec + oppositeVec;
        EXPECT_EQ(sum, glm::ivec3(0, 0, 0)) 
            << "Opposite vectors don't sum to zero for " << static_cast<int>(dir);
    }
}

// ============================================================================
// Scale Tests
// ============================================================================

TEST(CubeTest, GetScale_ReturnsPositive) {
    float scale = Cube::getScale();
    EXPECT_GT(scale, 0.0f);
}

TEST(CubeTest, GetScale_IsConstant) {
    float scale1 = Cube::getScale();
    float scale2 = Cube::getScale();
    EXPECT_EQ(scale1, scale2);
}

// ============================================================================
// Bond Structure Tests
// ============================================================================

TEST(CubeTest, Bond_DefaultConstruction) {
    Bond bond;
    EXPECT_EQ(bond.strength, 100.0f);
    EXPECT_EQ(bond.accumulatedForce, 0.0f);
    EXPECT_FALSE(bond.isBroken);
}

TEST(CubeTest, Bond_ConstructionWithStrength) {
    Bond bond(50.0f);
    EXPECT_EQ(bond.strength, 50.0f);
    EXPECT_EQ(bond.accumulatedForce, 0.0f);
    EXPECT_FALSE(bond.isBroken);
}

TEST(CubeTest, Bond_AddForce) {
    Bond bond(100.0f);
    bond.addForce(25.0f);
    EXPECT_EQ(bond.accumulatedForce, 25.0f);
    
    bond.addForce(30.0f);
    EXPECT_EQ(bond.accumulatedForce, 55.0f);
}

TEST(CubeTest, Bond_ResetForce) {
    Bond bond(100.0f);
    bond.addForce(50.0f);
    bond.resetForce();
    EXPECT_EQ(bond.accumulatedForce, 0.0f);
}

TEST(CubeTest, Bond_ShouldBreak_BelowThreshold) {
    Bond bond(100.0f);
    bond.addForce(50.0f);
    EXPECT_FALSE(bond.shouldBreak());
}

TEST(CubeTest, Bond_ShouldBreak_AtThreshold) {
    Bond bond(100.0f);
    bond.addForce(100.0f);
    EXPECT_TRUE(bond.shouldBreak());
}

TEST(CubeTest, Bond_ShouldBreak_AboveThreshold) {
    Bond bond(100.0f);
    bond.addForce(150.0f);
    EXPECT_TRUE(bond.shouldBreak());
}

TEST(CubeTest, Bond_BreakBond) {
    Bond bond(100.0f);
    bond.breakBond();
    EXPECT_TRUE(bond.isBroken);
}

TEST(CubeTest, Bond_ShouldBreak_AlreadyBroken) {
    Bond bond(100.0f);
    bond.addForce(150.0f);
    bond.breakBond();
    EXPECT_FALSE(bond.shouldBreak()); // Already broken, shouldn't break again
}

TEST(CubeTest, Bond_Repair) {
    Bond bond(100.0f);
    bond.addForce(50.0f);
    bond.breakBond();
    
    bond.repair();
    EXPECT_FALSE(bond.isBroken);
    EXPECT_EQ(bond.accumulatedForce, 0.0f);
}
