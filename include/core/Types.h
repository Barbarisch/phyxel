#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <array>
#include <cstdint>
#include <optional>
#include <chrono>

// Forward declarations
class btRigidBody;

namespace VulkanCube {

// Core vertex structure
struct Vertex {
    uint32_t vertexID;  // Simple vertex ID (0-7)

    static VkVertexInputBindingDescription getBindingDescription();
    static std::array<VkVertexInputAttributeDescription, 1> getAttributeDescriptions();
};

// Instance data for rendering - compressed format with future expansion capability
struct InstanceData {
    uint32_t packedData;   // 15 bits position (5+5+5), 6 bits face mask, 11 bits available for future features
    glm::vec3 color;       // RGB color (12 bytes)
    // Total: 16 bytes (was 28 bytes - 43% reduction!)
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

// Utility functions for packing/unpacking instance data
namespace InstanceDataUtils {
    // Bit layout in packedData:
    // Bits 0-4:   X position (5 bits)
    // Bits 5-9:   Y position (5 bits) 
    // Bits 10-14: Z position (5 bits)
    // Bits 15-20: Face mask (6 bits)
    // Bits 21-31: Available for future features (11 bits) - subcube scaling, material ID, etc.
    
    // Pack all data into uint32_t
    inline uint32_t packInstanceData(uint32_t x, uint32_t y, uint32_t z, uint32_t faceMask, uint32_t futureData = 0) {
        return (x & 0x1F) |
               ((y & 0x1F) << 5) |
               ((z & 0x1F) << 10) |
               ((faceMask & 0x3F) << 15) |
               ((futureData & 0x7FF) << 21);
    }
    
    // Pack face mask into 6-bit value
    inline uint32_t packFaceMask(const CubeFaces& faces) {
        uint32_t faceMask = 0;
        if (faces.front)  faceMask |= (1 << 0);
        if (faces.back)   faceMask |= (1 << 1);
        if (faces.right)  faceMask |= (1 << 2);
        if (faces.left)   faceMask |= (1 << 3);
        if (faces.top)    faceMask |= (1 << 4);
        if (faces.bottom) faceMask |= (1 << 5);
        return faceMask;
    }
    
    // Create instance data from relative position and faces
    inline InstanceData createInstanceData(const glm::ivec3& relativePos, const CubeFaces& faces, 
                                         const glm::vec3& color, uint32_t futureData = 0) {
        InstanceData instance;
        uint32_t faceMask = packFaceMask(faces);
        instance.packedData = packInstanceData(relativePos.x, relativePos.y, relativePos.z, faceMask, futureData);
        instance.color = color;
        return instance;
    }
    
    // Unpack relative position
    inline void unpackRelativePos(uint32_t packed, uint32_t& x, uint32_t& y, uint32_t& z) {
        x = packed & 0x1F;
        y = (packed >> 5) & 0x1F;
        z = (packed >> 10) & 0x1F;
    }
    
    // Unpack face mask
    inline uint32_t getFaceMask(uint32_t packed) {
        return (packed >> 15) & 0x3F;
    }
    
    // Get future data bits (for subcube scaling, etc.)
    inline uint32_t getFutureData(uint32_t packed) {
        return (packed >> 21) & 0x7FF;
    }
    
    // Legacy functions for backward compatibility
    inline uint16_t packRelativePos(uint32_t x, uint32_t y, uint32_t z) {
        return (x & 0x1F) | 
               ((y & 0x1F) << 5) | 
               ((z & 0x1F) << 10);
    }
    
    inline uint32_t packRelativePosAndFaces(uint32_t x, uint32_t y, uint32_t z, uint32_t faceMask) {
        return packInstanceData(x, y, z, faceMask, 0);
    }
    
    inline uint32_t packRelativePosAndFaces(const glm::ivec3& relativePos, const CubeFaces& faces) {
        uint32_t faceMask = packFaceMask(faces);
        return packInstanceData(relativePos.x, relativePos.y, relativePos.z, faceMask, 0);
    }
}

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
    
    // Enhanced culling metrics
    int frustumCulledInstances = 0;
    int occlusionCulledInstances = 0;
    int faceCulledFaces = 0;
    
    // Memory bandwidth metrics
    double memoryBandwidthMBps = 0.0;
    size_t vertexBufferBytes = 0;
    size_t indexBufferBytes = 0;
    size_t instanceBufferBytes = 0;
    size_t uniformBufferBytes = 0;
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
    
    // Enhanced timing metrics
    double frustumCullingTime = 0.0;
    double occlusionCullingTime = 0.0;
    double faceCullingTime = 0.0;
    double memoryTransferTime = 0.0;
    double bufferUpdateTime = 0.0;
};

// Memory bandwidth tracker
struct MemoryBandwidthTracker {
    std::chrono::high_resolution_clock::time_point frameStartTime;
    size_t totalBytesTransferred = 0;
    double averageBandwidthMBps = 0.0;
    std::vector<double> recentBandwidthSamples;
    static constexpr size_t MAX_SAMPLES = 60; // 1 second at 60fps
    
    void startFrame() {
        frameStartTime = std::chrono::high_resolution_clock::now();
        totalBytesTransferred = 0;
    }
    
    void recordTransfer(size_t bytes) {
        totalBytesTransferred += bytes;
    }
    
    void endFrame() {
        auto frameEndTime = std::chrono::high_resolution_clock::now();
        auto frameTime = std::chrono::duration<double>(frameEndTime - frameStartTime).count();
        
        if (frameTime > 0.0) {
            double bandwidthMBps = (totalBytesTransferred / (1024.0 * 1024.0)) / frameTime;
            recentBandwidthSamples.push_back(bandwidthMBps);
            
            if (recentBandwidthSamples.size() > MAX_SAMPLES) {
                recentBandwidthSamples.erase(recentBandwidthSamples.begin());
            }
            
            // Calculate rolling average
            double sum = 0.0;
            for (double sample : recentBandwidthSamples) {
                sum += sample;
            }
            averageBandwidthMBps = sum / recentBandwidthSamples.size();
        }
    }
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
