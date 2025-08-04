#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <array>
#include <cstdint>
#include <optional>

// Forward declarations
class btRigidBody;

namespace VulkanCube {

// Core vertex structure
struct Vertex {
    uint32_t vertexID;  // Simple vertex ID (0-7)

    static VkVertexInputBindingDescription getBindingDescription();
    static std::array<VkVertexInputAttributeDescription, 1> getAttributeDescriptions();
};

// Instance data for rendering
struct InstanceData {
    glm::vec3 offset;
    glm::vec3 color;
    uint32_t faceMask;  // 6 bits for face visibility: bit0=front, bit1=back, bit2=right, bit3=left, bit4=top, bit5=bottom
};

// Face visibility structure
struct CubeFaces {
    bool front = true;
    bool back = true;
    bool left = true;
    bool right = true;
    bool top = true;
    bool bottom = true;
};

// Cube entity
struct Cube {
    glm::ivec3 position;
    glm::vec3 color;
    bool broken = false;
    bool visible = true;
    btRigidBody* rigidBody = nullptr;
    
    Cube() : position(0), color(1.0f) {}
    Cube(glm::ivec3 pos, glm::vec3 col) : position(pos), color(col) {}
};

// Subcube for destruction
struct Subcube {
    btRigidBody* body;
    glm::vec3 color;
    double spawnTime;
};

// Uniform buffer object
struct UniformBufferObject {
    glm::mat4 view;
    glm::mat4 proj;
    alignas(4) uint32_t numInstances;
};

// AABB for frustum culling
struct AABB {
    glm::vec3 min;
    glm::vec3 max;
};

// Performance timing structures
struct FrameTiming {
    double cpuFrameTime = 0.0;
    double gpuFrameTime = 0.0;
    int vertexCount = 0;
    int drawCalls = 0;
    int culledInstances = 0;
    int visibleInstances = 0;
};

struct DetailedFrameTiming {
    double totalFrameTime = 0.0;
    double physicsTime = 0.0;
    double mousePickTime = 0.0;
    double uboFillTime = 0.0;
    double instanceUpdateTime = 0.0;
    double drawCmdUpdateTime = 0.0;
    double uniformUploadTime = 0.0;
    double commandRecordTime = 0.0;
    double gpuSubmitTime = 0.0;
    double presentTime = 0.0;
};

// Queue family indices
struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

// Swap chain support details
struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

// Hash function for glm::ivec3 (used in unordered_map)
struct ivec3_hash {
    std::size_t operator()(const glm::ivec3& v) const {
        return std::hash<int>()(v.x) ^ (std::hash<int>()(v.y) << 1) ^ (std::hash<int>()(v.z) << 2);
    }
};

// Chunk structure for potential chunked rendering
struct Chunk {
    // Implementation depends on requirements
};

// Constants
constexpr uint32_t WIDTH = 1200;
constexpr uint32_t HEIGHT = 720;
constexpr int MAX_FRAMES_IN_FLIGHT = 2;

// Validation layers
const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

} // namespace VulkanCube

// Hash function for glm::ivec3 to use in unordered_map
namespace std {
    template<>
    struct hash<glm::vec<3, int, glm::packed_highp>> {
        std::size_t operator()(const glm::vec<3, int, glm::packed_highp>& v) const {
            std::size_t h1 = std::hash<int>{}(v.x);
            std::size_t h2 = std::hash<int>{}(v.y);
            std::size_t h3 = std::hash<int>{}(v.z);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };
}

namespace VulkanCube {

} // namespace VulkanCube
