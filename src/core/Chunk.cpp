#include "core/Chunk.h"
#include <stdexcept>
#include <cstring>
#include <random>
#include <iostream>

namespace VulkanCube {

Chunk::Chunk(const glm::ivec3& origin) 
    : worldOrigin(origin) {
    cubes.reserve(32 * 32 * 32);              // Reserve space for all possible cubes
    faces.reserve(32 * 32 * 32 * 6);          // Reserve space for maximum faces (6 per cube)
}

Chunk::~Chunk() {
    cleanupVulkanResources();
}

Chunk::Chunk(Chunk&& other) noexcept
    : cubes(std::move(other.cubes))
    , faces(std::move(other.faces))
    , instanceBuffer(other.instanceBuffer)
    , instanceMemory(other.instanceMemory)
    , mappedMemory(other.mappedMemory)
    , numInstances(other.numInstances)
    , worldOrigin(other.worldOrigin)
    , needsUpdate(other.needsUpdate)
    , device(other.device)
    , physicalDevice(other.physicalDevice) {
    
    // Reset other object's Vulkan handles
    other.instanceBuffer = VK_NULL_HANDLE;
    other.instanceMemory = VK_NULL_HANDLE;
    other.mappedMemory = nullptr;
    other.device = VK_NULL_HANDLE;
    other.physicalDevice = VK_NULL_HANDLE;
}

Chunk& Chunk::operator=(Chunk&& other) noexcept {
    if (this != &other) {
        // Clean up current resources
        cleanupVulkanResources();
        
        // Move data
        cubes = std::move(other.cubes);
        faces = std::move(other.faces);
        instanceBuffer = other.instanceBuffer;
        instanceMemory = other.instanceMemory;
        mappedMemory = other.mappedMemory;
        numInstances = other.numInstances;
        worldOrigin = other.worldOrigin;
        needsUpdate = other.needsUpdate;
        device = other.device;
        physicalDevice = other.physicalDevice;
        
        // Reset other object's Vulkan handles
        other.instanceBuffer = VK_NULL_HANDLE;
        other.instanceMemory = VK_NULL_HANDLE;
        other.mappedMemory = nullptr;
        other.device = VK_NULL_HANDLE;
        other.physicalDevice = VK_NULL_HANDLE;
    }
    return *this;
}

void Chunk::initialize(VkDevice dev, VkPhysicalDevice physDev) {
    device = dev;
    physicalDevice = physDev;
}

Cube* Chunk::getCubeAt(const glm::ivec3& localPos) {
    if (!isValidLocalPosition(localPos)) return nullptr;
    
    size_t index = localToIndex(localPos);
    if (index >= cubes.size()) return nullptr;
    
    return &cubes[index];
}

const Cube* Chunk::getCubeAt(const glm::ivec3& localPos) const {
    if (!isValidLocalPosition(localPos)) return nullptr;
    
    size_t index = localToIndex(localPos);
    if (index >= cubes.size()) return nullptr;
    
    return &cubes[index];
}

Cube* Chunk::getCubeAtIndex(size_t index) {
    if (index >= cubes.size()) return nullptr;
    return &cubes[index];
}

const Cube* Chunk::getCubeAtIndex(size_t index) const {
    if (index >= cubes.size()) return nullptr;
    return &cubes[index];
}

bool Chunk::setCubeColor(const glm::ivec3& localPos, const glm::vec3& color) {
    Cube* cube = getCubeAt(localPos);
    if (!cube) return false;
    
    cube->color = color;
    needsUpdate = true;
    
    std::cout << "[CHUNK] Set cube color at local pos: (" 
              << localPos.x << "," << localPos.y << "," << localPos.z 
              << ") to: (" << color.x << "," << color.y << "," << color.z << ")" << std::endl;
    
    return true;
}

bool Chunk::removeCube(const glm::ivec3& localPos) {
    Cube* cube = getCubeAt(localPos);
    if (!cube) return false;
    
    // Mark cube as removed (using the existing convention)
    cube->color.r = -1.0f;
    needsUpdate = true;
    
    std::cout << "[CHUNK] Removed cube at local pos: (" 
              << localPos.x << "," << localPos.y << "," << localPos.z << ")" << std::endl;
    
    return true;
}

bool Chunk::addCube(const glm::ivec3& localPos, const glm::vec3& color) {
    Cube* cube = getCubeAt(localPos);
    if (!cube) return false;
    
    // Restore cube
    cube->color = color;
    cube->broken = false;
    needsUpdate = true;
    
    std::cout << "[CHUNK] Added/restored cube at local pos: (" 
              << localPos.x << "," << localPos.y << "," << localPos.z 
              << ") with color: (" << color.x << "," << color.y << "," << color.z << ")" << std::endl;
    
    return true;
}

void Chunk::populateWithCubes() {
    cubes.clear();
    faces.clear();
    
    // Random number generator for colors
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> colorDist(0.0f, 1.0f);
    
    // Create logical cubes (32x32x32 grid)
    // CRITICAL: Loop order determines index formula in localToIndex()
    // X-major order (X outermost, Z innermost) requires Z-minor indexing: z + y*32 + x*1024
    for (int x = 0; x < 32; ++x) {
        for (int y = 0; y < 32; ++y) {
            for (int z = 0; z < 32; ++z) {
                Cube cube;
                cube.position = glm::ivec3(x, y, z);  // Relative position within chunk
                cube.color = glm::vec3(
                    colorDist(gen),
                    colorDist(gen),
                    colorDist(gen)
                );
                cubes.push_back(cube);
            }
        }
    }
    
    std::cout << "[CHUNK] Populated chunk at origin (" 
              << worldOrigin.x << "," << worldOrigin.y << "," << worldOrigin.z 
              << ") with " << cubes.size() << " cubes" << std::endl;
}

void Chunk::rebuildFaces() {
    faces.clear();
    
    // Face culling with adjacency checks
    for (size_t cubeIndex = 0; cubeIndex < cubes.size(); ++cubeIndex) {
        const Cube& cube = cubes[cubeIndex];
        
        // Skip removed cubes
        if (cube.color.r < 0.0f) continue;
        
        // Calculate which faces are visible by checking adjacent positions
        bool faceVisible[6] = {true, true, true, true, true, true};
        
        // Face directions: 0=front(+Z), 1=back(-Z), 2=right(+X), 3=left(-X), 4=top(+Y), 5=bottom(-Y)
        glm::ivec3 neighbors[6] = {
            cube.position + glm::ivec3(0, 0, 1),   // front (+Z)
            cube.position + glm::ivec3(0, 0, -1),  // back (-Z)
            cube.position + glm::ivec3(1, 0, 0),   // right (+X)
            cube.position + glm::ivec3(-1, 0, 0),  // left (-X)
            cube.position + glm::ivec3(0, 1, 0),   // top (+Y)
            cube.position + glm::ivec3(0, -1, 0)   // bottom (-Y)
        };
        
        // Check each face for occlusion by adjacent cubes
        for (int faceID = 0; faceID < 6; ++faceID) {
            glm::ivec3 neighborPos = neighbors[faceID];
            
            // Check if neighbor position is within chunk bounds
            if (neighborPos.x >= 0 && neighborPos.x < 32 &&
                neighborPos.y >= 0 && neighborPos.y < 32 &&
                neighborPos.z >= 0 && neighborPos.z < 32) {
                
                // Check if there's a cube at the neighbor position
                const Cube* neighborCube = getCubeAt(neighborPos);
                if (neighborCube && neighborCube->color.r >= 0.0f) {
                    // Neighbor cube exists and is not removed, so this face is occluded
                    faceVisible[faceID] = false;
                }
            }
            // If neighbor is outside chunk bounds, face remains visible (edge of chunk)
        }
        
        // Generate instance data for each visible face
        for (int faceID = 0; faceID < 6; ++faceID) {
            if (faceVisible[faceID]) {
                InstanceData faceInstance;
                
                // Pack cube position (5 bits each) and face ID (3 bits)
                // Bit layout: [0-4]=x, [5-9]=y, [10-14]=z, [15-17]=faceID, [18-31]=future
                faceInstance.packedData = (cube.position.x & 0x1F) | ((cube.position.y & 0x1F) << 5) | 
                                         ((cube.position.z & 0x1F) << 10) | ((faceID & 0x7) << 15);
                
                faceInstance.color = cube.color;
                faces.push_back(faceInstance);
            }
        }
    }
    
    numInstances = static_cast<uint32_t>(faces.size());
    needsUpdate = true;
    
    std::cout << "[CHUNK] Rebuilt faces for chunk at origin (" 
              << worldOrigin.x << "," << worldOrigin.y << "," << worldOrigin.z 
              << "), generated " << numInstances << " visible faces" << std::endl;
}

void Chunk::updateVulkanBuffer() {
    if (!mappedMemory || faces.empty()) return;
    
    VkDeviceSize bufferSize = sizeof(InstanceData) * faces.size();
    memcpy(mappedMemory, faces.data(), bufferSize);
    needsUpdate = false;
    
    std::cout << "[CHUNK] Updated Vulkan buffer for chunk at origin (" 
              << worldOrigin.x << "," << worldOrigin.y << "," << worldOrigin.z 
              << "), uploaded " << faces.size() << " face instances" << std::endl;
}

void Chunk::createVulkanBuffer() {
    if (device == VK_NULL_HANDLE) {
        throw std::runtime_error("Chunk not initialized with Vulkan device!");
    }
    
    if (faces.empty()) {
        std::cout << "[CHUNK] Warning: Creating buffer for chunk with no faces" << std::endl;
        return;
    }
    
    VkDeviceSize bufferSize = sizeof(InstanceData) * faces.size();
    
    // Create buffer
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
    
    // Copy initial data
    memcpy(mappedMemory, faces.data(), bufferSize);
    
    std::cout << "[CHUNK] Created Vulkan buffer for chunk at origin (" 
              << worldOrigin.x << "," << worldOrigin.y << "," << worldOrigin.z 
              << "), buffer size: " << bufferSize << " bytes" << std::endl;
}

void Chunk::cleanupVulkanResources() {
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

size_t Chunk::localToIndex(const glm::ivec3& localPos) {
    // CRITICAL: This must match the loop order in populateWithCubes()
    // X-major order (X outermost, Z innermost): z + y*32 + x*1024
    return localPos.z + localPos.y * 32 + localPos.x * 32 * 32;
}

glm::ivec3 Chunk::indexToLocal(size_t index) {
    // Reverse the localToIndex calculation
    int x = index / (32 * 32);
    int y = (index % (32 * 32)) / 32;
    int z = index % 32;
    return glm::ivec3(x, y, z);
}

uint32_t Chunk::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && 
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    
    throw std::runtime_error("Failed to find suitable memory type!");
}

bool Chunk::isValidLocalPosition(const glm::ivec3& localPos) const {
    return localPos.x >= 0 && localPos.x < 32 &&
           localPos.y >= 0 && localPos.y < 32 &&
           localPos.z >= 0 && localPos.z < 32;
}

} // namespace VulkanCube
