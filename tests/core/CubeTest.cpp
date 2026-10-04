/**
 * Unit tests for Cube static utilities
 */

#include <gtest/gtest.h>
#include "core/Cube.h"
#include <glm/glm.hpp>

using namespace Phyxel;

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
