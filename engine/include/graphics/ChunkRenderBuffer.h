#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <cstddef>

namespace Phyxel {

struct InstanceData;  // Forward declaration

namespace Graphics {

/**
 * @class ChunkRenderBuffer
 * @brief Manages Vulkan instance buffer for chunk face rendering
 * 
 * Handles creation, allocation, mapping, and cleanup of Vulkan buffers
 * used for instanced rendering of chunk faces. Provides dynamic reallocation
 * when capacity is exceeded.
 */
class ChunkRenderBuffer {
public:
    static constexpr size_t DEFAULT_BUFFER_CAPACITY = 25000;

    /**
     * @brief Construct render buffer with Vulkan device handles
     * @param device Vulkan logical device
     * @param physicalDevice Vulkan physical device
     */
    ChunkRenderBuffer(VkDevice device, VkPhysicalDevice physicalDevice);
    
    /**
     * @brief Destructor - cleans up Vulkan resources
     */
    ~ChunkRenderBuffer();

    // Delete copy constructor and assignment operator
    ChunkRenderBuffer(const ChunkRenderBuffer&) = delete;
    ChunkRenderBuffer& operator=(const ChunkRenderBuffer&) = delete;

    // Move constructor and assignment operator
    ChunkRenderBuffer(ChunkRenderBuffer&& other) noexcept;
    ChunkRenderBuffer& operator=(ChunkRenderBuffer&& other) noexcept;

    /**
     * @brief Create Vulkan instance buffer with fixed capacity
     * @param initialData Initial face data to copy
     * @param capacity Buffer capacity (defaults to DEFAULT_BUFFER_CAPACITY or initialData size, whichever is larger)
     */
    void createBuffer(const std::vector<InstanceData>& initialData, size_t capacity = 0);

    /**
     * @brief Reallocate buffer with larger capacity
     * @param requiredInstances Minimum required capacity
     * 
     * Creates new buffer with 50% headroom above required capacity.
     * Automatically cleans up old buffer.
     */
    void reallocateBuffer(size_t requiredInstances);

    /**
     * @brief Clean up Vulkan buffer resources
     * 
     * Unmaps memory, destroys buffer, and frees device memory.
     */
    void cleanup();

    /**
     * @brief Get Vulkan buffer handle
     * @return VkBuffer handle
     */
    VkBuffer getBuffer() const { return instanceBuffer; }

    /**
     * @brief Get persistently mapped memory pointer
     * @return Pointer to mapped memory (nullptr if not mapped)
     */
    void* getMappedMemory() const { return mappedMemory; }

    /**
     * @brief Get current buffer capacity
     * @return Number of instances buffer can hold
     */
    size_t getCapacity() const { return bufferCapacity; }

    /**
     * @brief Get peak usage tracking
     * @return Maximum number of instances used
     */
    size_t getMaxInstancesUsed() const { return maxInstancesUsed; }

    /**
     * @brief Update peak usage tracking
     * @param currentUsage Current number of instances
     */
    void updateMaxUsage(size_t currentUsage);

    /**
     * @brief Calculate and log buffer utilization statistics
     * @param currentFaceCount Current number of faces in use
     */
    void logUtilization(size_t currentFaceCount) const;

private:
    /**
     * @brief Find suitable memory type for buffer allocation
     * @param typeFilter Memory type filter bitmask
     * @param properties Required memory properties
     * @return Memory type index
     */
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    VkDevice device;
    VkPhysicalDevice physicalDevice;
    
    VkBuffer instanceBuffer;
    VkDeviceMemory instanceMemory;
    void* mappedMemory;
    
    size_t bufferCapacity;
    size_t maxInstancesUsed;
};

} // namespace Graphics
} // namespace Phyxel
