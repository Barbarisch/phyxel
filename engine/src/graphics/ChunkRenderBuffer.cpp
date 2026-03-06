#include "graphics/ChunkRenderBuffer.h"
#include "core/Types.h"
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <iostream>

namespace Phyxel {
namespace Graphics {

ChunkRenderBuffer::ChunkRenderBuffer(VkDevice device, VkPhysicalDevice physicalDevice)
    : device(device)
    , physicalDevice(physicalDevice)
    , instanceBuffer(VK_NULL_HANDLE)
    , instanceMemory(VK_NULL_HANDLE)
    , mappedMemory(nullptr)
    , bufferCapacity(0)
    , maxInstancesUsed(0)
{
}

ChunkRenderBuffer::~ChunkRenderBuffer() {
    cleanup();
}

ChunkRenderBuffer::ChunkRenderBuffer(ChunkRenderBuffer&& other) noexcept
    : device(other.device)
    , physicalDevice(other.physicalDevice)
    , instanceBuffer(other.instanceBuffer)
    , instanceMemory(other.instanceMemory)
    , mappedMemory(other.mappedMemory)
    , bufferCapacity(other.bufferCapacity)
    , maxInstancesUsed(other.maxInstancesUsed)
{
    other.instanceBuffer = VK_NULL_HANDLE;
    other.instanceMemory = VK_NULL_HANDLE;
    other.mappedMemory = nullptr;
    other.bufferCapacity = 0;
    other.maxInstancesUsed = 0;
}

ChunkRenderBuffer& ChunkRenderBuffer::operator=(ChunkRenderBuffer&& other) noexcept {
    if (this != &other) {
        cleanup();
        
        device = other.device;
        physicalDevice = other.physicalDevice;
        instanceBuffer = other.instanceBuffer;
        instanceMemory = other.instanceMemory;
        mappedMemory = other.mappedMemory;
        bufferCapacity = other.bufferCapacity;
        maxInstancesUsed = other.maxInstancesUsed;
        
        other.instanceBuffer = VK_NULL_HANDLE;
        other.instanceMemory = VK_NULL_HANDLE;
        other.mappedMemory = nullptr;
        other.bufferCapacity = 0;
        other.maxInstancesUsed = 0;
    }
    return *this;
}

void ChunkRenderBuffer::createBuffer(const std::vector<InstanceData>& initialData, size_t capacity) {
    if (device == VK_NULL_HANDLE) {
        throw std::runtime_error("ChunkRenderBuffer not initialized with valid Vulkan device!");
    }
    
    // Use provided capacity, or calculate based on initial data
    if (capacity == 0) {
        capacity = std::max(DEFAULT_BUFFER_CAPACITY, initialData.size());
    }
    
    VkDeviceSize bufferSize = sizeof(InstanceData) * capacity;
    bufferCapacity = capacity;
    
    // Create buffer with fixed capacity
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &instanceBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create chunk instance buffer!");
    }
    
    // Allocate memory
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, instanceBuffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &instanceMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate chunk instance buffer memory!");
    }
    
    vkBindBufferMemory(device, instanceBuffer, instanceMemory, 0);
    
    // Map memory persistently for easy updates
    vkMapMemory(device, instanceMemory, 0, bufferSize, 0, &mappedMemory);
    
    // Copy initial data (only the used portion)
    if (!initialData.empty()) {
        VkDeviceSize usedSize = sizeof(InstanceData) * initialData.size();
        memcpy(mappedMemory, initialData.data(), usedSize);
    }
}

void ChunkRenderBuffer::reallocateBuffer(size_t requiredInstances) {
    // Calculate new capacity with headroom (50% extra)
    size_t newCapacity = static_cast<size_t>(requiredInstances * 1.5f);
    
    // Clean up existing buffer
    if (mappedMemory) {
        vkUnmapMemory(device, instanceMemory);
        mappedMemory = nullptr;
    }
    if (instanceBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, instanceBuffer, nullptr);
        instanceBuffer = VK_NULL_HANDLE;
    }
    if (instanceMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, instanceMemory, nullptr);
        instanceMemory = VK_NULL_HANDLE;
    }
    
    // Create new larger buffer
    VkDeviceSize bufferSize = sizeof(InstanceData) * newCapacity;
    bufferCapacity = newCapacity;
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &instanceBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to reallocate chunk instance buffer!");
    }
    
    // Allocate memory
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, instanceBuffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &instanceMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate reallocated chunk instance buffer memory!");
    }
    
    vkBindBufferMemory(device, instanceBuffer, instanceMemory, 0);
    
    // Map memory persistently
    vkMapMemory(device, instanceMemory, 0, bufferSize, 0, &mappedMemory);
}

void ChunkRenderBuffer::cleanup() {
    if (device != VK_NULL_HANDLE) {
        if (mappedMemory) {
            vkUnmapMemory(device, instanceMemory);
            mappedMemory = nullptr;
        }
        if (instanceBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, instanceBuffer, nullptr);
            instanceBuffer = VK_NULL_HANDLE;
        }
        if (instanceMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, instanceMemory, nullptr);
            instanceMemory = VK_NULL_HANDLE;
        }
    }
}

void ChunkRenderBuffer::updateMaxUsage(size_t currentUsage) {
    if (currentUsage > maxInstancesUsed) {
        maxInstancesUsed = currentUsage;
    }
}

void ChunkRenderBuffer::logUtilization(size_t currentFaceCount) const {
    if (bufferCapacity > 0) {
        float utilization = float(maxInstancesUsed) / float(bufferCapacity) * 100.0f;
        float currentUtilization = float(currentFaceCount) / float(bufferCapacity) * 100.0f;
        
        // std::cout << "[CHUNK] Buffer utilization - Current: " << currentUtilization 
        //           << "% (" << currentFaceCount << "/" << bufferCapacity 
        //           << "), Peak: " << utilization 
        //           << "% (" << maxInstancesUsed << "/" << bufferCapacity << ")" << std::endl;
    }
}

uint32_t ChunkRenderBuffer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    
    throw std::runtime_error("Failed to find suitable memory type!");
}

} // namespace Graphics
} // namespace Phyxel
