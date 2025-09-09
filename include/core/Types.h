#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <array>
#include <cstdint>
#include <optional>
#include <chrono>
#include "core/Cube.h"

// Forward declarations
class btRigidBody;

namespace VulkanCube {

// Forward declarations within namespace
class Chunk;

// Texture system constants
namespace TextureConstants {
    constexpr uint16_t PLACEHOLDER_TEXTURE_INDEX = 5;     // Default fallback texture (placeholder_bottom)
    constexpr uint16_t INVALID_TEXTURE_INDEX = 0xFFFF;    // Invalid/unset texture
    constexpr uint16_t MAX_TEXTURE_INDEX = 0xFFFE;        // Maximum valid texture index
    
    // Helper function to get texture index for a specific face
    // Face IDs now directly correspond to placeholder texture indices (0-5)
    inline uint16_t getTextureIndexForFace(int faceID) {
        if (faceID >= 0 && faceID < 6) {
            return static_cast<uint16_t>(faceID);  // Direct mapping: faceID == textureIndex
        }
        return PLACEHOLDER_TEXTURE_INDEX; // fallback
    }
    
    // Helper function to get grassdirt texture index for a specific face
    inline uint16_t getGrassdirtTextureIndexForFace(int faceID) {
        if (faceID >= 0 && faceID < 6) {
            return static_cast<uint16_t>(faceID + 6);  // Grassdirt textures are indices 6-11
        }
        return PLACEHOLDER_TEXTURE_INDEX; // fallback
    }
}

// Hash function for glm::ivec3 to use in unordered_map
struct IVec3Hash {
    std::size_t operator()(const glm::ivec3& v) const {
        return std::hash<int>()(v.x) ^ (std::hash<int>()(v.y) << 1) ^ (std::hash<int>()(v.z) << 2);
    }
};

// Unified voxel location system for O(1) hover detection
struct VoxelLocation {
    enum Type { EMPTY, CUBE, SUBDIVIDED };
    
    Type type = EMPTY;
    Chunk* chunk = nullptr;                 // VulkanCube::Chunk forward declaration
    glm::ivec3 localPos{-1};        // Local position within chunk
    glm::ivec3 worldPos{-1};        // World position for convenience
    glm::ivec3 subcubePos{-1};      // Only valid if type == SUBDIVIDED
    
    // Face information for cube placement (carried over from raycast)
    int hitFace = -1;               // Which face was hit: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z
    glm::vec3 hitNormal{0};         // Surface normal of hit face
    
    VoxelLocation() = default;
    
    bool isValid() const { return chunk != nullptr && type != EMPTY; }
    bool isSubcube() const { return type == SUBDIVIDED && subcubePos != glm::ivec3(-1); }
    bool isCube() const { return type == CUBE; }
    
    // Equality comparison for hover change detection
    bool operator==(const VoxelLocation& other) const {
        return chunk == other.chunk && 
               localPos == other.localPos && 
               worldPos == other.worldPos &&
               subcubePos == other.subcubePos &&
               type == other.type;
    }
    
    bool operator!=(const VoxelLocation& other) const {
        return !(*this == other);
    }
    
    // Get the world position where a new cube should be placed adjacent to this face
    glm::ivec3 getAdjacentPlacementPosition() const {
        if (hitFace < 0) return worldPos; // No face data available
        
        // Face normals: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z
        glm::ivec3 faceOffset;
        switch (hitFace) {
            case 0: faceOffset = glm::ivec3(1, 0, 0); break;   // +X face
            case 1: faceOffset = glm::ivec3(-1, 0, 0); break;  // -X face
            case 2: faceOffset = glm::ivec3(0, 1, 0); break;   // +Y face
            case 3: faceOffset = glm::ivec3(0, -1, 0); break;  // -Y face
            case 4: faceOffset = glm::ivec3(0, 0, 1); break;   // +Z face
            case 5: faceOffset = glm::ivec3(0, 0, -1); break;  // -Z face
            default: return worldPos;
        }
        return worldPos + faceOffset;
    }
};

// Core vertex structure
struct Vertex {
    uint32_t vertexID;  // Simple vertex ID (0-7)

    static VkVertexInputBindingDescription getBindingDescription();
    static std::array<VkVertexInputAttributeDescription, 1> getAttributeDescriptions();
};

// Instance data for rendering - compressed format with texture support
struct InstanceData {
    uint32_t packedData;      // 15 bits position (5+5+5), 6 bits face mask, 11 bits available for future features
    uint16_t textureIndex;    // Texture atlas index (0-65535)
    uint16_t reserved;        // Reserved for future use (ensures 8-byte alignment)
    // Total: 8 bytes (50% reduction from previous 16 bytes!)
    
    static VkVertexInputBindingDescription getBindingDescription();
    static std::array<VkVertexInputAttributeDescription, 2> getAttributeDescriptions();  // packedData + textureIndex
};

// Instance data for dynamic subcubes with physics
struct DynamicSubcubeInstanceData {
    glm::vec3 worldPosition;  // Full precision world position for physics objects
    uint16_t textureIndex;    // Texture atlas index (0-65535)
    uint16_t reserved1;       // Alignment padding
    uint32_t faceID;          // Face ID (0-5)
    float scale;              // Scale factor (1/3 for subcubes, 1.0 for full cubes)
    glm::vec4 rotation;       // Quaternion rotation (x, y, z, w) for tumbling effect
    glm::ivec3 localPosition; // Original local position in 3x3x3 grid (0-2 for each axis)
    uint32_t reserved2;       // Alignment padding
    // Total: 44 bytes
    
    // Vulkan vertex input description for dynamic subcubes
    static VkVertexInputBindingDescription getBindingDescription();
    static std::array<VkVertexInputAttributeDescription, 6> getAttributeDescriptions();  // Now 6 attributes
};

// Face visibility structure
struct CubeFaces {
    bool front = true;
    bool back = true;
    bool left = true;
    bool right = true;
    bool top = true;
    bool bottom = true;
    
    // Occlusion culling helper
    bool isFullyOccluded() const {
        return !front && !back && !left && !right && !top && !bottom;
    }
    
    // Count visible faces
    int getVisibleFaceCount() const {
        return (front ? 1 : 0) + (back ? 1 : 0) + (left ? 1 : 0) + 
               (right ? 1 : 0) + (top ? 1 : 0) + (bottom ? 1 : 0);
    }
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
    
    // Create instance data from relative position and faces with texture
    inline InstanceData createInstanceData(const glm::ivec3& relativePos, const CubeFaces& faces, 
                                         uint16_t textureIndex, uint32_t futureData = 0) {
        InstanceData instance;
        uint32_t faceMask = packFaceMask(faces);
        instance.packedData = packInstanceData(relativePos.x, relativePos.y, relativePos.z, faceMask, futureData);
        instance.textureIndex = textureIndex;
        instance.reserved = 0;
        return instance;
    }
    
    // Legacy function for backward compatibility during transition
    inline InstanceData createInstanceDataLegacy(const glm::ivec3& relativePos, const CubeFaces& faces, 
                                                const glm::vec3& color, uint32_t futureData = 0) {
        // For now, use placeholder texture index 0 when color is provided
        return createInstanceData(relativePos, faces, 0, futureData);
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
    
    // Occlusion culling details
    int fullyOccludedCubes = 0;          // Cubes with all faces hidden
    int partiallyOccludedCubes = 0;      // Cubes with some faces hidden
    int totalHiddenFaces = 0;            // Total number of hidden faces
    
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
