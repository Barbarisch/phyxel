#pragma once

#include "Types.h"
#include "core/Subcube.h"
#include <vector>
#include <vulkan/vulkan.h>

namespace VulkanCube {

/**
 * Chunk class that manages a 32x32x32 section of cubes
 * Handles cube storage, face generation, and Vulkan buffer management
 */
class Chunk {
    friend class ChunkManager;  // Allow ChunkManager to access private members for cross-chunk culling
    
private:
    std::vector<Cube*> cubes;                      // Pointers to cubes for efficient deletion (32x32x32)
    std::vector<Subcube*> subcubes;                // Pointers to subcubes for voxel subdivision
    std::vector<InstanceData> faces;               // Visible faces only (CPU pre-filtered for rendering)
    VkBuffer instanceBuffer = VK_NULL_HANDLE;     // Vulkan buffer for this chunk's face instance data
    VkDeviceMemory instanceMemory = VK_NULL_HANDLE;
    void* mappedMemory = nullptr;                  // Persistent mapping for updates
    uint32_t numInstances = 0;                    // Variable count based on visible faces
    glm::ivec3 worldOrigin = glm::ivec3(0);       // World-space origin of this chunk
    bool needsUpdate = false;                     // Flag for buffer updates
    
    // Vulkan device handles (set by ChunkManager)
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

public:
    // Constructor
    explicit Chunk(const glm::ivec3& origin = glm::ivec3(0));
    
    // Destructor
    ~Chunk();
    
    // Copy constructor and assignment operator (deleted - chunks should not be copied)
    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;
    
    // Move constructor and assignment operator
    Chunk(Chunk&& other) noexcept;
    Chunk& operator=(Chunk&& other) noexcept;
    
    // Initialization
    void initialize(VkDevice device, VkPhysicalDevice physicalDevice);
    
    // Basic properties
    glm::ivec3 getWorldOrigin() const { return worldOrigin; }
    size_t getCubeCount() const { return cubes.size(); }
    size_t getSubcubeCount() const { return subcubes.size(); }
    uint32_t getNumInstances() const { return numInstances; }
    bool getNeedsUpdate() const { return needsUpdate; }
    void setNeedsUpdate(bool needsUpdate) { this->needsUpdate = needsUpdate; }
    
    // Cube access
    Cube* getCubeAt(const glm::ivec3& localPos);
    const Cube* getCubeAt(const glm::ivec3& localPos) const;
    Cube* getCubeAtIndex(size_t index);
    const Cube* getCubeAtIndex(size_t index) const;
    
    // Subcube access
    Subcube* getSubcubeAt(const glm::ivec3& localPos, const glm::ivec3& subcubePos);
    const Subcube* getSubcubeAt(const glm::ivec3& localPos, const glm::ivec3& subcubePos) const;
    std::vector<Subcube*> getSubcubesAt(const glm::ivec3& localPos);
    
    // Cube manipulation
    bool setCubeColor(const glm::ivec3& localPos, const glm::vec3& color);
    bool removeCube(const glm::ivec3& localPos);
    bool addCube(const glm::ivec3& localPos, const glm::vec3& color);
    
    // Subcube manipulation
    bool subdivideAt(const glm::ivec3& localPos);              // Convert cube to 27 subcubes
    bool addSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos, const glm::vec3& color);
    bool removeSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos);
    bool clearSubdivisionAt(const glm::ivec3& localPos);       // Remove all subcubes and restore cube
    
    // Chunk operations
    void populateWithCubes();                      // Fill chunk with 32x32x32 cubes
    void rebuildFaces();                           // Regenerate face data from cubes
    void updateVulkanBuffer();                     // Update GPU buffer with face data
    
    // Vulkan buffer management
    void createVulkanBuffer();
    void cleanupVulkanResources();
    
    // Utility functions
    static size_t localToIndex(const glm::ivec3& localPos);
    static glm::ivec3 indexToLocal(size_t index);
    static size_t subcubeToIndex(const glm::ivec3& parentPos, const glm::ivec3& subcubePos);
    
    // Access for ChunkManager (friend access or public as needed)
    VkBuffer getInstanceBuffer() const { return instanceBuffer; }
    const std::vector<InstanceData>& getFaces() const { return faces; }
    void* getMappedMemory() const { return mappedMemory; }
    
private:
    // Helper functions
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    bool isValidLocalPosition(const glm::ivec3& localPos) const;
};

} // namespace VulkanCube
