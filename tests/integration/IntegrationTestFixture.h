#pragma once

#include <gtest/gtest.h>
#include <vulkan/vulkan.h>
#include "physics/PhysicsWorld.h"
#include "core/ChunkManager.h"
#include <memory>

namespace VulkanCube {
namespace Testing {

/**
 * @brief Base fixture for integration tests requiring Vulkan initialization
 * 
 * Provides minimal Vulkan environment for testing components that require
 * VkDevice but don't need full swapchain/rendering setup.
 * 
 * USAGE:
 * class MyIntegrationTest : public VulkanTestFixture {
 *   void SetUp() override {
 *     VulkanTestFixture::SetUp();
 *     // Additional setup
 *   }
 * };
 */
class VulkanTestFixture : public ::testing::Test {
protected:
    void SetUp() override;
    void TearDown() override;

    // Vulkan handles (minimal setup)
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queueFamilyIndex = 0;
    VkQueue queue = VK_NULL_HANDLE;

    // Helper: Check if Vulkan is available on this system
    bool isVulkanAvailable() const { return instance != VK_NULL_HANDLE; }
};

/**
 * @brief Base fixture for integration tests requiring Physics World
 * 
 * Provides initialized Bullet Physics world for testing physics interactions.
 */
class PhysicsTestFixture : public ::testing::Test {
protected:
    void SetUp() override;
    void TearDown() override;

    std::unique_ptr<Physics::PhysicsWorld> physicsWorld;
};

/**
 * @brief Combined fixture for tests requiring both Vulkan and Physics
 * 
 * Provides complete environment for testing ChunkManager, voxel modifications,
 * and other systems that need both rendering and physics.
 */
class VulkanPhysicsTestFixture : public ::testing::Test {
protected:
    void SetUp() override;
    void TearDown() override;

    // Vulkan handles
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queueFamilyIndex = 0;
    VkQueue queue = VK_NULL_HANDLE;

    // Physics world
    std::unique_ptr<Physics::PhysicsWorld> physicsWorld;

    // Helper: Check if environment is ready
    bool isEnvironmentReady() const {
        return instance != VK_NULL_HANDLE && physicsWorld != nullptr;
    }
};

/**
 * @brief Fixture for ChunkManager integration tests
 * 
 * Provides fully initialized ChunkManager with Vulkan and Physics.
 * Tests chunk creation, voxel manipulation, and cross-chunk operations.
 */
class ChunkManagerTestFixture : public VulkanPhysicsTestFixture {
protected:
    void SetUp() override;
    void TearDown() override;

    std::unique_ptr<ChunkManager> chunkManager;
};

} // namespace Testing
} // namespace VulkanCube
