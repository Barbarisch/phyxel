#include <gtest/gtest.h>
#include "scene/interaction/PlacementTool.h"
#include <glm/glm.hpp>

using namespace VulkanCube;

class PlacementToolTest : public ::testing::Test {
protected:
    // Helper to check if result matches expected
    void ExpectPlacement(const glm::vec3& hitPoint, const glm::vec3& hitNormal, const glm::ivec3& hitCubePos, 
                        const glm::ivec3& expectedCubePos, const glm::ivec3& expectedSubcubePos) {
        PlacementTool::PlacementResult result = PlacementTool::calculatePlacement(hitPoint, hitNormal, hitCubePos);
        
        EXPECT_EQ(result.cubePos, expectedCubePos) 
            << "Cube position mismatch for hitPoint (" << hitPoint.x << "," << hitPoint.y << "," << hitPoint.z << ")";
        EXPECT_EQ(result.subcubePos, expectedSubcubePos)
            << "Subcube position mismatch for hitPoint (" << hitPoint.x << "," << hitPoint.y << "," << hitPoint.z << ")";
    }
};

TEST_F(PlacementToolTest, PlaceOnTopFace_Center) {
    // Hit top face of cube at (0,0,0)
    glm::vec3 hitPoint(0.5f, 1.0f, 0.5f);
    glm::vec3 hitNormal(0.0f, 1.0f, 0.0f);
    glm::ivec3 hitCubePos(0, 0, 0);
    
    // Should place in (0,1,0) at bottom subcube (1,0,1)
    ExpectPlacement(hitPoint, hitNormal, hitCubePos, glm::ivec3(0, 1, 0), glm::ivec3(1, 0, 1));
}

TEST_F(PlacementToolTest, PlaceOnRightFace_Center) {
    // Hit right face (+X) of cube at (0,0,0)
    glm::vec3 hitPoint(1.0f, 0.5f, 0.5f);
    glm::vec3 hitNormal(1.0f, 0.0f, 0.0f);
    glm::ivec3 hitCubePos(0, 0, 0);
    
    // Should place in (1,0,0) at left subcube (0,1,1)
    ExpectPlacement(hitPoint, hitNormal, hitCubePos, glm::ivec3(1, 0, 0), glm::ivec3(0, 1, 1));
}

TEST_F(PlacementToolTest, PlaceOnTopFace_NearEdge) {
    // Hit top face very close to the edge (X=0.99)
    glm::vec3 hitPoint(0.99f, 1.0f, 0.5f);
    glm::vec3 hitNormal(0.0f, 1.0f, 0.0f);
    glm::ivec3 hitCubePos(0, 0, 0);
    
    // Should still place in (0,1,0)
    // Subcube X index: floor(0.99 * 3) = floor(2.97) = 2
    ExpectPlacement(hitPoint, hitNormal, hitCubePos, glm::ivec3(0, 1, 0), glm::ivec3(2, 0, 1));
}

TEST_F(PlacementToolTest, PlaceOnTopFace_Corner) {
    // Hit top face near corner (0.999, 1.0, 0.999)
    glm::vec3 hitPoint(0.999f, 1.0f, 0.999f);
    glm::vec3 hitNormal(0.0f, 1.0f, 0.0f);
    glm::ivec3 hitCubePos(0, 0, 0);
    
    ExpectPlacement(hitPoint, hitNormal, hitCubePos, glm::ivec3(0, 1, 0), glm::ivec3(2, 0, 2));
}

TEST_F(PlacementToolTest, NegativeCoordinates) {
    // Hit top face of cube at (-1, -1, -1)
    // Top face is at Y=0.
    // Center of top face: (-0.5, 0.0, -0.5)
    glm::vec3 hitPoint(-0.5f, 0.0f, -0.5f);
    glm::vec3 hitNormal(0.0f, 1.0f, 0.0f);
    glm::ivec3 hitCubePos(-1, -1, -1);
    
    // target = (-0.5, 0.001, -0.5)
    // cubePos = (-1, 0, -1)
    // localPoint = (-0.5 - (-1), 0.001 - 0, -0.5 - (-1)) = (0.5, 0.001, 0.5)
    // subcubePos = (1, 0, 1)
    
    ExpectPlacement(hitPoint, hitNormal, hitCubePos, glm::ivec3(-1, 0, -1), glm::ivec3(1, 0, 1));
}
