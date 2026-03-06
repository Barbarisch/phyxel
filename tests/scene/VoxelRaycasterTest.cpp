/**
 * Unit tests for VoxelRaycaster
 * 
 * Tests raycasting logic using MockChunkManager.
 */

#include <gtest/gtest.h>
#include "scene/VoxelRaycaster.h"
#include "../mocks/MockChunkManager.h"
#include <glm/glm.hpp>

using namespace Phyxel;

class VoxelRaycasterTest : public ::testing::Test {
protected:
    VoxelRaycaster raycaster;
    MockChunkManager mockChunkManager;

    VoxelRaycaster::ChunkManagerAccessFunc getChunkManager = [this]() -> IChunkManager* {
        return &mockChunkManager;
    };
};

TEST_F(VoxelRaycasterTest, RayMiss_EmptyWorld) {
    // Ray from (0,0,0) pointing along X axis
    glm::vec3 origin(0.5f, 0.5f, 0.5f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);
    
    VoxelLocation result = raycaster.pickVoxel(origin, direction, getChunkManager);
    
    EXPECT_FALSE(result.isValid());
}

TEST_F(VoxelRaycasterTest, RayHit_SimpleCube) {
    // Place a cube at (5, 0, 0)
    mockChunkManager.addCube(glm::ivec3(5, 0, 0));
    
    // Ray from (0,0,0) pointing along X axis
    glm::vec3 origin(0.5f, 0.5f, 0.5f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);
    
    VoxelLocation result = raycaster.pickVoxel(origin, direction, getChunkManager);
    
    EXPECT_TRUE(result.isValid());
    EXPECT_EQ(result.type, VoxelLocation::CUBE);
    EXPECT_EQ(result.worldPos, glm::ivec3(5, 0, 0));
    
    // Should hit the left face (Normal: -1, 0, 0)
    EXPECT_EQ(result.hitFace, 0); // 0 = Left (-X)
    EXPECT_EQ(result.hitNormal, glm::vec3(-1.0f, 0.0f, 0.0f));
}

TEST_F(VoxelRaycasterTest, RayHit_Diagonal) {
    // Place a cube at (2, 2, 2)
    mockChunkManager.addCube(glm::ivec3(2, 2, 2));
    
    // Ray from (0,0,0) pointing towards (2,2,2)
    glm::vec3 origin(0.5f, 0.5f, 0.5f);
    glm::vec3 direction = glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f));
    
    VoxelLocation result = raycaster.pickVoxel(origin, direction, getChunkManager);
    
    EXPECT_TRUE(result.isValid());
    EXPECT_EQ(result.worldPos, glm::ivec3(2, 2, 2));
}

TEST_F(VoxelRaycasterTest, RayHit_FaceSelection) {
    // Place a cube at (0, 0, 0)
    mockChunkManager.addCube(glm::ivec3(0, 0, 0));
    
    // Ray from (-2, 0.5, 0.5) pointing Right (+X) -> Should hit Left Face
    {
        glm::vec3 origin(-2.0f, 0.5f, 0.5f);
        glm::vec3 direction(1.0f, 0.0f, 0.0f);
        VoxelLocation result = raycaster.pickVoxel(origin, direction, getChunkManager);
        EXPECT_TRUE(result.isValid());
        EXPECT_EQ(result.hitFace, 0); // Left
    }
    
    // Ray from (2, 0.5, 0.5) pointing Left (-X) -> Should hit Right Face
    {
        glm::vec3 origin(2.0f, 0.5f, 0.5f);
        glm::vec3 direction(-1.0f, 0.0f, 0.0f);
        VoxelLocation result = raycaster.pickVoxel(origin, direction, getChunkManager);
        EXPECT_TRUE(result.isValid());
        EXPECT_EQ(result.hitFace, 1); // Right
    }
    
    // Ray from (0.5, 2, 0.5) pointing Down (-Y) -> Should hit Top Face
    {
        glm::vec3 origin(0.5f, 2.0f, 0.5f);
        glm::vec3 direction(0.0f, -1.0f, 0.0f);
        VoxelLocation result = raycaster.pickVoxel(origin, direction, getChunkManager);
        EXPECT_TRUE(result.isValid());
        EXPECT_EQ(result.hitFace, 3); // Top
    }
}
