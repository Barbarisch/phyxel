#include "IntegrationTestFixture.h"
#include "utils/Logger.h"
#include <vector>
#include <cstring>

namespace VulkanCube {
namespace Testing {

// ============================================================================
// VulkanTestFixture Implementation
// ============================================================================

void VulkanTestFixture::SetUp() {
    // Create Vulkan instance (minimal setup - no validation layers in tests)
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Phyxel Integration Test";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Phyxel";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;
    instanceInfo.enabledExtensionCount = 0;
    instanceInfo.enabledLayerCount = 0;

    VkResult result = vkCreateInstance(&instanceInfo, nullptr, &instance);
    if (result != VK_SUCCESS) {
        GTEST_SKIP() << "Vulkan not available on this system (vkCreateInstance failed: " << result << ")";
        return;
    }

    // Pick first physical device
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        GTEST_SKIP() << "No Vulkan-capable GPU found";
        return;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    physicalDevice = devices[0];

    // Find queue family with graphics support
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    bool foundQueue = false;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queueFamilyIndex = i;
            foundQueue = true;
            break;
        }
    }

    if (!foundQueue) {
        GTEST_SKIP() << "No graphics queue family found";
        return;
    }

    // Create logical device
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamilyIndex;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.pEnabledFeatures = &deviceFeatures;
    deviceInfo.enabledExtensionCount = 0;
    deviceInfo.enabledLayerCount = 0;

    result = vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device);
    if (result != VK_SUCCESS) {
        GTEST_SKIP() << "Failed to create Vulkan device: " << result;
        return;
    }

    // Get queue handle
    vkGetDeviceQueue(device, queueFamilyIndex, 0, &queue);
}

void VulkanTestFixture::TearDown() {
    if (device != VK_NULL_HANDLE) {
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
}

// ============================================================================
// PhysicsTestFixture Implementation
// ============================================================================

void PhysicsTestFixture::SetUp() {
    physicsWorld = std::make_unique<Physics::PhysicsWorld>();
    
    // Initialize the physics world
    if (!physicsWorld->initialize()) {
        GTEST_SKIP() << "Failed to initialize physics world";
    }
}

void PhysicsTestFixture::TearDown() {
    physicsWorld.reset();
}

// ============================================================================
// VulkanPhysicsTestFixture Implementation
// ============================================================================

void VulkanPhysicsTestFixture::SetUp() {
    // Initialize Vulkan (same as VulkanTestFixture)
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Phyxel Integration Test";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Phyxel";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;
    instanceInfo.enabledExtensionCount = 0;
    instanceInfo.enabledLayerCount = 0;

    VkResult result = vkCreateInstance(&instanceInfo, nullptr, &instance);
    if (result != VK_SUCCESS) {
        GTEST_SKIP() << "Vulkan not available on this system";
        return;
    }

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        GTEST_SKIP() << "No Vulkan-capable GPU found";
        return;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    physicalDevice = devices[0];

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    bool foundQueue = false;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queueFamilyIndex = i;
            foundQueue = true;
            break;
        }
    }

    if (!foundQueue) {
        GTEST_SKIP() << "No graphics queue family found";
        return;
    }

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamilyIndex;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.pEnabledFeatures = &deviceFeatures;
    deviceInfo.enabledExtensionCount = 0;
    deviceInfo.enabledLayerCount = 0;

    result = vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device);
    if (result != VK_SUCCESS) {
        GTEST_SKIP() << "Failed to create Vulkan device";
        return;
    }

    vkGetDeviceQueue(device, queueFamilyIndex, 0, &queue);

    // Initialize Physics World
    physicsWorld = std::make_unique<Physics::PhysicsWorld>();
    if (!physicsWorld->initialize()) {
        GTEST_SKIP() << "Failed to initialize physics world";
        return;
    }
}

void VulkanPhysicsTestFixture::TearDown() {
    physicsWorld.reset();
    
    if (device != VK_NULL_HANDLE) {
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
}

// ============================================================================
// ChunkManagerTestFixture Implementation
// ============================================================================

void ChunkManagerTestFixture::SetUp() {
    VulkanPhysicsTestFixture::SetUp();
    
    if (!isEnvironmentReady()) {
        return; // Test already skipped by parent fixture
    }

    // Create and initialize ChunkManager
    chunkManager = std::make_unique<ChunkManager>();
    chunkManager->initialize(device, physicalDevice);
    chunkManager->setPhysicsWorld(physicsWorld.get());
}

void ChunkManagerTestFixture::TearDown() {
    chunkManager.reset();
    VulkanPhysicsTestFixture::TearDown();
}

} // namespace Testing
} // namespace VulkanCube
