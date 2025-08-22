#include "core/Chunk.h"
#include "physics/PhysicsWorld.h"
#include <stdexcept>
#include <cstring>
#include <random>
#include <iostream>
#include <iomanip>
#include <unordered_set>

// Bullet Physics includes
#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionShapes/btTriangleMesh.h>
#include <BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h>

namespace VulkanCube {

Chunk::Chunk(const glm::ivec3& origin) 
    : worldOrigin(origin) {
    cubes.reserve(32 * 32 * 32);              // Reserve space for all possible cubes
    staticSubcubes.reserve(1000);             // Reserve reasonable space for static subcubes
    dynamicSubcubes.reserve(100);             // Reserve space for dynamic subcubes
    faces.reserve(32 * 32 * 32 * 6);          // Reserve space for maximum faces (6 per cube)
}

Chunk::~Chunk() {
    // Delete all cube pointers to free memory
    for (Cube* cube : cubes) {
        delete cube;
    }
    cubes.clear();
    
    // Delete all static subcube pointers to free memory
    for (Subcube* subcube : staticSubcubes) {
        delete subcube;
    }
    staticSubcubes.clear();
    
    // Delete all dynamic subcube pointers to free memory
    for (Subcube* subcube : dynamicSubcubes) {
        delete subcube;
    }
    dynamicSubcubes.clear();
    
    cleanupVulkanResources();
    cleanupPhysicsResources();
}

Chunk::Chunk(Chunk&& other) noexcept
    : cubes(std::move(other.cubes))
    , staticSubcubes(std::move(other.staticSubcubes))
    , dynamicSubcubes(std::move(other.dynamicSubcubes))
    , faces(std::move(other.faces))
    , instanceBuffer(other.instanceBuffer)
    , instanceMemory(other.instanceMemory)
    , mappedMemory(other.mappedMemory)
    , numInstances(other.numInstances)
    , worldOrigin(other.worldOrigin)
    , needsUpdate(other.needsUpdate)
    , bufferCapacity(other.bufferCapacity)
    , maxInstancesUsed(other.maxInstancesUsed)
    , device(other.device)
    , physicalDevice(other.physicalDevice) {
    
    // Reset other object's Vulkan handles and capacity tracking
    other.instanceBuffer = VK_NULL_HANDLE;
    other.instanceMemory = VK_NULL_HANDLE;
    other.mappedMemory = nullptr;
    other.bufferCapacity = 0;
    other.maxInstancesUsed = 0;
    other.device = VK_NULL_HANDLE;
    other.physicalDevice = VK_NULL_HANDLE;
}

Chunk& Chunk::operator=(Chunk&& other) noexcept {
    if (this != &other) {
        // Clean up current resources
        cleanupVulkanResources();
        
        // Move data
        cubes = std::move(other.cubes);
        staticSubcubes = std::move(other.staticSubcubes);
        dynamicSubcubes = std::move(other.dynamicSubcubes);
        faces = std::move(other.faces);
        instanceBuffer = other.instanceBuffer;
        instanceMemory = other.instanceMemory;
        mappedMemory = other.mappedMemory;
        numInstances = other.numInstances;
        worldOrigin = other.worldOrigin;
        needsUpdate = other.needsUpdate;
        bufferCapacity = other.bufferCapacity;
        maxInstancesUsed = other.maxInstancesUsed;
        device = other.device;
        physicalDevice = other.physicalDevice;
        
        // Reset other object's Vulkan handles and capacity tracking
        other.instanceBuffer = VK_NULL_HANDLE;
        other.instanceMemory = VK_NULL_HANDLE;
        other.mappedMemory = nullptr;
        other.bufferCapacity = 0;
        other.maxInstancesUsed = 0;
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
    
    // Return the pointer (which could be nullptr for deleted cubes)
    return cubes[index];
}

const Cube* Chunk::getCubeAt(const glm::ivec3& localPos) const {
    if (!isValidLocalPosition(localPos)) return nullptr;
    
    size_t index = localToIndex(localPos);
    if (index >= cubes.size()) return nullptr;
    
    // Return the pointer (which could be nullptr for deleted cubes)
    return cubes[index];
}

Cube* Chunk::getCubeAtIndex(size_t index) {
    if (index >= cubes.size()) return nullptr;
    return cubes[index];
}

const Cube* Chunk::getCubeAtIndex(size_t index) const {
    if (index >= cubes.size()) return nullptr;
    return cubes[index];
}

bool Chunk::setCubeColor(const glm::ivec3& localPos, const glm::vec3& color) {
    Cube* cube = getCubeAt(localPos);
    if (!cube) return false;
    
    cube->setColor(color);
    needsUpdate = true;
    
    // std::cout << "[CHUNK] Set cube color at local pos: (" 
    //           << localPos.x << "," << localPos.y << "," << localPos.z 
    //           << ") to: (" << color.x << "," << color.y << "," << color.z << ")" << std::endl;
    
    return true;
}

bool Chunk::removeCube(const glm::ivec3& localPos) {
    if (!isValidLocalPosition(localPos)) return false;
    
    size_t index = localToIndex(localPos);
    if (index >= cubes.size() || !cubes[index]) return false; // No cube exists at this position
    
    // Actually delete the cube from memory
    delete cubes[index];
    cubes[index] = nullptr; // Mark as deleted
    
    // Immediately rebuild faces to remove the cube from GPU buffer
    rebuildFaces();
    updateVulkanBuffer();
    
    // std::cout << "[CHUNK] Completely removed cube at local pos: (" 
    //           << localPos.x << "," << localPos.y << "," << localPos.z << ")" << std::endl;
    
    return true;
}

bool Chunk::addCube(const glm::ivec3& localPos, const glm::vec3& color) {
    if (!isValidLocalPosition(localPos)) return false;
    
    size_t index = localToIndex(localPos);
    if (index >= cubes.size()) return false;
    
    // If cube already exists, just update its color
    if (cubes[index]) {
        cubes[index]->setColor(color);
        cubes[index]->setOriginalColor(color); // Update original color too
        cubes[index]->setBroken(false);
    } else {
        // Create new cube
        cubes[index] = new Cube(localPos, color);
        cubes[index]->setOriginalColor(color); // Ensure original color is set
    }
    
    needsUpdate = true;
    
    // std::cout << "[CHUNK] Added/restored cube at local pos: (" 
    //           << localPos.x << "," << localPos.y << "," << localPos.z 
    //           << ") with color: (" << color.x << "," << color.y << "," << color.z << ")" << std::endl;
    
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
                Cube* cube = new Cube();
                cube->setPosition(glm::ivec3(x, y, z));  // Local position within chunk (for 5-bit packing efficiency)
                glm::vec3 cubeColor = glm::vec3(
                    colorDist(gen),
                    colorDist(gen),
                    colorDist(gen)
                );
                cube->setColor(cubeColor);
                cube->setOriginalColor(cubeColor); // Store original color
                cubes.push_back(cube);
            }
        }
    }
    
    // std::cout << "[CHUNK] Populated chunk at origin (" 
    //           << worldOrigin.x << "," << worldOrigin.y << "," << worldOrigin.z 
    //           << ") with " << cubes.size() << " cubes" << std::endl;
}

void Chunk::rebuildFaces() {
    // std::cout << "[CHUNK] *** STARTING rebuildFaces() *** Current subcubes: " << subcubes.size() << std::endl;
    faces.clear();
    
    // ========================================================================
    // PHASE 1: Process regular cubes (only those that aren't subdivided)
    // ========================================================================
    for (size_t cubeIndex = 0; cubeIndex < cubes.size(); ++cubeIndex) {
        const Cube* cube = cubes[cubeIndex];
        
        // Skip deleted cubes (nullptr) or hidden cubes (subdivided)
        if (!cube || !cube->isVisible()) continue;
        
        // Calculate which faces are visible by checking adjacent positions
        bool faceVisible[6] = {true, true, true, true, true, true};
        
        // Face directions: 0=front(+Z), 1=back(-Z), 2=right(+X), 3=left(-X), 4=top(+Y), 5=bottom(-Y)
        glm::ivec3 cubePos = cube->getPosition();
        glm::ivec3 neighbors[6] = {
            cubePos + glm::ivec3(0, 0, 1),   // front (+Z)
            cubePos + glm::ivec3(0, 0, -1),  // back (-Z)
            cubePos + glm::ivec3(1, 0, 0),   // right (+X)
            cubePos + glm::ivec3(-1, 0, 0),  // left (-X)
            cubePos + glm::ivec3(0, 1, 0),   // top (+Y)
            cubePos + glm::ivec3(0, -1, 0)   // bottom (-Y)
        };
        
        // Check each face for occlusion by adjacent cubes
        for (int faceID = 0; faceID < 6; ++faceID) {
            glm::ivec3 neighborPos = neighbors[faceID];
            
            // Check if neighbor position is within chunk bounds
            if (neighborPos.x >= 0 && neighborPos.x < 32 &&
                neighborPos.y >= 0 && neighborPos.y < 32 &&
                neighborPos.z >= 0 && neighborPos.z < 32) {
                
                // Check if there's a visible cube at the neighbor position
                const Cube* neighborCube = getCubeAt(neighborPos);
                if (neighborCube && neighborCube->isVisible()) {
                    // Neighbor cube exists and is visible, so this face is occluded
                    faceVisible[faceID] = false;
                }
            }
            // If neighbor is outside chunk bounds, face remains visible (edge of chunk)
        }
        
        // Generate instance data for each visible face of the cube
        for (int faceID = 0; faceID < 6; ++faceID) {
            if (faceVisible[faceID]) {
                InstanceData faceInstance;
                
                // Pack cube position (5 bits each) and face ID (3 bits)
                // Bit layout: [0-4]=x, [5-9]=y, [10-14]=z, [15-17]=faceID, [18]=subcube_flag(0), [19-31]=reserved
                const glm::ivec3& cubePos = cube->getPosition();
                uint32_t subcubeData = 0; // Regular cube: subcube_flag=0, rest=0
                faceInstance.packedData = (cubePos.x & 0x1F) | ((cubePos.y & 0x1F) << 5) | 
                                         ((cubePos.z & 0x1F) << 10) | ((faceID & 0x7) << 15) |
                                         (subcubeData << 18);
                
                faceInstance.color = cube->getColor();
                faces.push_back(faceInstance);
            }
        }
    }
    
    // ========================================================================
    // PHASE 2: Process subcubes (from subdivided cubes)
    // ========================================================================
    // PHASE 2: Process static subcubes only (dynamic subcubes use different rendering pipeline)
    // ========================================================================
    // std::cout << "[CHUNK] Processing " << staticSubcubes.size() << " static subcubes in PHASE 2..." << std::endl;
    int subcubeProcessed = 0;
    for (const Subcube* subcube : staticSubcubes) {
        // Skip broken or hidden subcubes (broken subcubes should be in dynamic list)
        if (!subcube || subcube->isBroken() || !subcube->isVisible()) {
            // if (!subcube) {
            //     std::cout << "[CHUNK] Skipping null subcube" << std::endl;
            // } else {
            //     std::cout << "[CHUNK] Skipping subcube - broken: " << subcube->isBroken() << ", visible: " << subcube->isVisible() << std::endl;
            // }
            continue;
        }
        
        // Get subcube properties
        glm::ivec3 parentPos = subcube->getPosition();     // Parent cube's world position
        glm::ivec3 localPos = subcube->getLocalPosition(); // 0-2 for each axis within parent
        
        // std::cout << "[CHUNK] Processing subcube " << subcubeProcessed << " at parent world pos (" 
        //           << parentPos.x << "," << parentPos.y << "," << parentPos.z 
        //           << ") local pos (" << localPos.x << "," << localPos.y << "," << localPos.z 
        //           << ") color (" << subcube->getColor().x << "," << subcube->getColor().y << "," << subcube->getColor().z << ")" << std::endl;
        subcubeProcessed++;
        
        // Convert parent world position to chunk-relative position
        glm::ivec3 parentChunkPos = parentPos - worldOrigin;
        
        // Validate parent position is within chunk bounds
        if (parentChunkPos.x < 0 || parentChunkPos.x >= 32 ||
            parentChunkPos.y < 0 || parentChunkPos.y >= 32 ||
            parentChunkPos.z < 0 || parentChunkPos.z >= 32) {
            continue; // Skip subcubes with invalid parent positions
        }
        
        // Calculate which faces are visible by checking adjacent subcubes/cubes
        bool faceVisible[6] = {true, true, true, true, true, true};
        
        // For subcubes, we need more sophisticated occlusion culling:
        // - Check against other subcubes in the same parent cube
        // - Check against neighboring cubes/subcubes
        // For now, simplified: assume all subcube faces are visible (we can optimize later)
        
        // Generate instance data for each visible face of the subcube
        for (int faceID = 0; faceID < 6; ++faceID) {
            if (faceVisible[faceID]) {
                InstanceData faceInstance;
                
                // Pack parent cube position (5 bits each), face ID (3 bits), and subcube data
                // Bit layout: [0-4]=parent_x, [5-9]=parent_y, [10-14]=parent_z, [15-17]=faceID, 
                //             [18]=subcube_flag(1), [19-20]=local_x, [21-22]=local_y, [23-24]=local_z, [25-31]=reserved
                uint32_t subcubeData = (1 << 0) |                           // subcube_flag = 1
                                      ((localPos.x & 0x3) << 1) |          // local_x (2 bits)
                                      ((localPos.y & 0x3) << 3) |          // local_y (2 bits)
                                      ((localPos.z & 0x3) << 5);           // local_z (2 bits)
                
                faceInstance.packedData = (parentChunkPos.x & 0x1F) | ((parentChunkPos.y & 0x1F) << 5) | 
                                         ((parentChunkPos.z & 0x1F) << 10) | ((faceID & 0x7) << 15) |
                                         (subcubeData << 18);
                
                faceInstance.color = subcube->getColor();
                faces.push_back(faceInstance);
            }
        }
    }
    
    numInstances = static_cast<uint32_t>(faces.size());
    needsUpdate = true;
    
    // std::cout << "[CHUNK] Rebuilt faces for chunk at origin (" 
    //           << worldOrigin.x << "," << worldOrigin.y << "," << worldOrigin.z 
    //           << "), generated " << numInstances << " visible faces (" 
    //           << (faces.size() - subcubes.size() * 6) << " cube faces + " 
    //           << (subcubes.size() * 6) << " subcube faces)" << std::endl;
}

void Chunk::rebuildDynamicSubcubeFaces() {
    dynamicSubcubeFaces.clear();
    
    // Generate face data for dynamic subcubes only
    for (const Subcube* subcube : dynamicSubcubes) {
        if (!subcube || !subcube->isVisible()) {
            continue;
        }
        
        // Get subcube's actual world position (including physics transformation if any)
        glm::vec3 subcubeWorldPos = subcube->getWorldPosition();
        
        // For dynamic subcubes, we render all faces (they can be in arbitrary positions)
        // In the future, we could add collision detection between dynamic subcubes
        for (int faceID = 0; faceID < 6; ++faceID) {
            DynamicSubcubeInstanceData faceInstance;
            faceInstance.worldPosition = subcubeWorldPos;
            faceInstance.color = subcube->getColor();
            faceInstance.faceID = static_cast<uint32_t>(faceID);
            faceInstance.scale = 1.0f / 3.0f;  // Subcubes are 1/3 the size of full cubes
            
            dynamicSubcubeFaces.push_back(faceInstance);
        }
    }
    
    needsUpdate = true;
    
    if (!dynamicSubcubeFaces.empty()) {
        std::cout << "[CHUNK] Generated " << dynamicSubcubeFaces.size() << " dynamic subcube faces" << std::endl;
    }
}

void Chunk::updateVulkanBuffer() {
    if (!mappedMemory || faces.empty()) return;
    
    // Ensure buffer capacity is sufficient, reallocate if necessary
    ensureBufferCapacity(faces.size());
    
    // Track peak usage for analysis
    maxInstancesUsed = std::max(maxInstancesUsed, faces.size());
    
    // Copy data to GPU buffer (only the used portion)
    VkDeviceSize copySize = sizeof(InstanceData) * faces.size();
    memcpy(mappedMemory, faces.data(), copySize);
    needsUpdate = false;
    
    // Periodic utilization logging
    static int updateCount = 0;
    if (++updateCount % 50 == 0) {
        logBufferUtilization();
    }
    
    // std::cout << "[CHUNK] Updated Vulkan buffer for chunk at origin (" 
    //           << worldOrigin.x << "," << worldOrigin.y << "," << worldOrigin.z 
    //           << "), uploaded " << faces.size() << " face instances" << std::endl;
}

void Chunk::updateSingleCubeColor(const glm::ivec3& localPos, const glm::vec3& newColor) {
    if (!isValidLocalPosition(localPos)) return;
    
    // Find the cube and update its color
    Cube* cube = getCubeAt(localPos);
    if (!cube) return;
    
    cube->setColor(newColor);
    
    // Efficiently update only the affected faces in the buffer
    // Instead of rebuilding all faces, find and update just this cube's faces
    if (!mappedMemory) return;
    
    bool updatedAnyFaces = false;
    
    // Find all face instances for this cube and update their colors
    for (size_t i = 0; i < faces.size(); ++i) {
        InstanceData& face = faces[i];
        
        // Extract position from packed data
        int faceX = face.packedData & 0x1F;
        int faceY = (face.packedData >> 5) & 0x1F;
        int faceZ = (face.packedData >> 10) & 0x1F;
        
        // Check if this face belongs to our cube
        if (faceX == localPos.x && faceY == localPos.y && faceZ == localPos.z) {
            // Update the color in the faces vector
            faces[i].color = newColor;
            
            // Update the GPU buffer directly (partial update)
            VkDeviceSize offset = i * sizeof(InstanceData) + offsetof(InstanceData, color);
            memcpy(static_cast<char*>(mappedMemory) + offset, &newColor, sizeof(glm::vec3));
            
            updatedAnyFaces = true;
        }
    }
    
    if (updatedAnyFaces) {
        // Optional: Flush memory if not coherent (most host-visible memory is coherent)
        // VkMappedMemoryRange range{};
        // range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        // range.memory = instanceMemory;
        // range.offset = 0;
        // range.size = VK_WHOLE_SIZE;
        // vkFlushMappedMemoryRanges(device, 1, &range);
        
        // std::cout << "[CHUNK] Updated " << localPos.x << "," << localPos.y << "," << localPos.z 
        //           << " color efficiently (partial buffer update)" << std::endl;
    }
}

void Chunk::updateSingleSubcubeColor(const glm::ivec3& parentLocalPos, const glm::ivec3& subcubePos, const glm::vec3& newColor) {
    if (!isValidLocalPosition(parentLocalPos)) return;
    if (subcubePos.x < 0 || subcubePos.x >= 3 || 
        subcubePos.y < 0 || subcubePos.y >= 3 || 
        subcubePos.z < 0 || subcubePos.z >= 3) return;
    
    // Find the subcube and update its color
    Subcube* subcube = getSubcubeAt(parentLocalPos, subcubePos);
    if (!subcube) return;
    
    subcube->setColor(newColor);
    
    // Efficiently update only the affected faces in the buffer
    if (!mappedMemory) return;
    
    bool updatedAnyFaces = false;
    
    // Find all face instances for this subcube and update their colors
    for (size_t i = 0; i < faces.size(); ++i) {
        InstanceData& face = faces[i];
        
        // Extract data from packed format for subcubes
        // Bit layout: [0-4]=parent_x, [5-9]=parent_y, [10-14]=parent_z, [15-17]=faceID, 
        //             [18]=subcube_flag(1), [19-20]=local_x, [21-22]=local_y, [23-24]=local_z
        int parentX = face.packedData & 0x1F;
        int parentY = (face.packedData >> 5) & 0x1F;
        int parentZ = (face.packedData >> 10) & 0x1F;
        uint32_t subcubeData = (face.packedData >> 18);
        bool isSubcubeFace = (subcubeData & 0x1) != 0;
        
        // Check if this is a subcube face belonging to our specific subcube
        if (isSubcubeFace && 
            parentX == parentLocalPos.x && parentY == parentLocalPos.y && parentZ == parentLocalPos.z) {
            
            // Extract subcube local position from packed data
            int localX = (subcubeData >> 1) & 0x3;
            int localY = (subcubeData >> 3) & 0x3;
            int localZ = (subcubeData >> 5) & 0x3;
            
            // Check if this face belongs to our specific subcube
            if (localX == subcubePos.x && localY == subcubePos.y && localZ == subcubePos.z) {
                // Update the color in the faces vector
                faces[i].color = newColor;
                
                // Update the GPU buffer directly (partial update)
                VkDeviceSize offset = i * sizeof(InstanceData) + offsetof(InstanceData, color);
                memcpy(static_cast<char*>(mappedMemory) + offset, &newColor, sizeof(glm::vec3));
                
                updatedAnyFaces = true;
            }
        }
    }
    
    if (updatedAnyFaces) {
        // Successfully updated subcube color
    } else {
        // Failed to update subcube color - no faces found for this subcube
    }
}

void Chunk::createVulkanBuffer() {
    if (device == VK_NULL_HANDLE) {
        throw std::runtime_error("Chunk not initialized with Vulkan device!");
    }
    
    // Use fixed capacity instead of current face count
    size_t capacity = std::max(DEFAULT_BUFFER_CAPACITY, faces.size());
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
    if (!faces.empty()) {
        VkDeviceSize usedSize = sizeof(InstanceData) * faces.size();
        memcpy(mappedMemory, faces.data(), usedSize);
    }
    
    // std::cout << "[CHUNK] Created fixed-capacity Vulkan buffer for chunk at origin (" 
    //           << worldOrigin.x << "," << worldOrigin.y << "," << worldOrigin.z 
    //           << "), capacity: " << bufferCapacity << " instances (" << bufferSize << " bytes)" << std::endl;
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

// =============================================================================
// SUBCUBE MANAGEMENT METHODS
// =============================================================================

Subcube* Chunk::getSubcubeAt(const glm::ivec3& localPos, const glm::ivec3& subcubePos) {
    // Search in static subcubes first
    for (Subcube* subcube : staticSubcubes) {
        if (subcube && 
            subcube->getPosition() == worldOrigin + localPos && 
            subcube->getLocalPosition() == subcubePos) {
            return subcube;
        }
    }
    
    // Search in dynamic subcubes
    for (Subcube* subcube : dynamicSubcubes) {
        if (subcube && 
            subcube->getPosition() == worldOrigin + localPos && 
            subcube->getLocalPosition() == subcubePos) {
            return subcube;
        }
    }
    return nullptr;
}

const Subcube* Chunk::getSubcubeAt(const glm::ivec3& localPos, const glm::ivec3& subcubePos) const {
    // Search in static subcubes first
    for (const Subcube* subcube : staticSubcubes) {
        if (subcube && 
            subcube->getPosition() == worldOrigin + localPos && 
            subcube->getLocalPosition() == subcubePos) {
            return subcube;
        }
    }
    
    // Search in dynamic subcubes
    for (const Subcube* subcube : dynamicSubcubes) {
        if (subcube && 
            subcube->getPosition() == worldOrigin + localPos && 
            subcube->getLocalPosition() == subcubePos) {
            return subcube;
        }
    }
    return nullptr;
}

std::vector<Subcube*> Chunk::getSubcubesAt(const glm::ivec3& localPos) {
    std::vector<Subcube*> result;
    glm::ivec3 parentWorldPos = worldOrigin + localPos;
    
    // Collect from both static and dynamic subcubes
    for (Subcube* subcube : staticSubcubes) {
        if (subcube && subcube->getPosition() == parentWorldPos) {
            result.push_back(subcube);
        }
    }
    
    for (Subcube* subcube : dynamicSubcubes) {
        if (subcube && subcube->getPosition() == parentWorldPos) {
            result.push_back(subcube);
        }
    }
    return result;
}

std::vector<Subcube*> Chunk::getStaticSubcubesAt(const glm::ivec3& localPos) {
    std::vector<Subcube*> result;
    glm::ivec3 parentWorldPos = worldOrigin + localPos;
    
    for (Subcube* subcube : staticSubcubes) {
        if (subcube && subcube->getPosition() == parentWorldPos) {
            result.push_back(subcube);
        }
    }
    return result;
}

bool Chunk::subdivideAt(const glm::ivec3& localPos) {
    // Check if position is valid
    if (!isValidLocalPosition(localPos)) return false;
    
    // Get the cube at this position
    Cube* cube = getCubeAt(localPos);
    if (!cube) return false;
    
    // Check if already subdivided
    auto existingSubcubes = getSubcubesAt(localPos);
    if (!existingSubcubes.empty()) return false; // Already subdivided
    
    // Create 27 subcubes (3x3x3) with random colors for clear visual distinction
    glm::ivec3 parentWorldPos = worldOrigin + localPos;
    
    // Use random colors for subcubes instead of parent-based colors
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> colorDist(0.2f, 1.0f); // Avoid very dark colors
    
    // std::cout << "[CHUNK] Creating 27 subcubes with random colors..." << std::endl;
    
    int colorIndex = 0;
    for (int x = 0; x < 3; ++x) {
        for (int y = 0; y < 3; ++y) {
            for (int z = 0; z < 3; ++z) {
                glm::ivec3 subcubeLocalPos(x, y, z);
                
                // Generate completely random color for each subcube
                glm::vec3 subcubeColor = glm::vec3(
                    colorDist(gen),
                    colorDist(gen),
                    colorDist(gen)
                );
                
                // std::cout << "[CHUNK]   Subcube " << colorIndex << " at (" << x << "," << y << "," << z 
                //           << ") color: (" << subcubeColor.x << "," << subcubeColor.y << "," << subcubeColor.z << ")" << std::endl;
                
                Subcube* newSubcube = new Subcube(parentWorldPos, subcubeColor, subcubeLocalPos);
                staticSubcubes.push_back(newSubcube); // Add to static subcubes list
                cube->addSubcube(newSubcube); // Also add to the parent cube
                colorIndex++;
            }
        }
    }
    
    // Hide the parent cube (don't delete it, just hide it)
    cube->hide();
    
    // Debug: Verify the cube is actually hidden and subdivided
    // std::cout << "[CHUNK] Parent cube hidden. isVisible() = " << cube->isVisible() 
    //           << ", isSubdivided() = " << cube->isSubdivided() << std::endl;
    
    // Mark for update
    needsUpdate = true;
    
    // std::cout << "[CHUNK] Subdivided cube at local pos (" << localPos.x << "," << localPos.y << "," << localPos.z 
    //           << ") into 27 subcubes with " << staticSubcubes.size() << " static subcubes in chunk" << std::endl;
    
    return true;
}

bool Chunk::addSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos, const glm::vec3& color) {
    // Check if position is valid
    if (!isValidLocalPosition(parentPos)) return false;
    if (subcubePos.x < 0 || subcubePos.x >= 3 || 
        subcubePos.y < 0 || subcubePos.y >= 3 || 
        subcubePos.z < 0 || subcubePos.z >= 3) return false;
    
    // Check if subcube already exists
    if (getSubcubeAt(parentPos, subcubePos)) return false;
    
    // Create new subcube (add to static list by default)
    glm::ivec3 parentWorldPos = worldOrigin + parentPos;
    Subcube* newSubcube = new Subcube(parentWorldPos, color, subcubePos);
    staticSubcubes.push_back(newSubcube);
    
    // Mark for update
    needsUpdate = true;
    
    return true;
}

bool Chunk::removeSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos) {
    // Try to find and remove from static subcubes first
    for (auto it = staticSubcubes.begin(); it != staticSubcubes.end(); ++it) {
        Subcube* subcube = *it;
        if (subcube && 
            subcube->getPosition() == worldOrigin + parentPos && 
            subcube->getLocalPosition() == subcubePos) {
            
            // Also remove from parent cube's subcube list
            Cube* parentCube = getCubeAt(parentPos);
            if (parentCube) {
                parentCube->removeSubcube(subcube);
            }
            
            delete subcube;
            staticSubcubes.erase(it);
            needsUpdate = true;
            return true;
        }
    }
    
    // Try to find and remove from dynamic subcubes
    for (auto it = dynamicSubcubes.begin(); it != dynamicSubcubes.end(); ++it) {
        Subcube* subcube = *it;
        if (subcube && 
            subcube->getPosition() == worldOrigin + parentPos && 
            subcube->getLocalPosition() == subcubePos) {
            
            // Also remove from parent cube's subcube list
            Cube* parentCube = getCubeAt(parentPos);
            if (parentCube) {
                parentCube->removeSubcube(subcube);
            }
            
            delete subcube;
            dynamicSubcubes.erase(it);
            needsUpdate = true;
            return true;
        }
    }
    
    return false; // Subcube not found
}

bool Chunk::clearSubdivisionAt(const glm::ivec3& localPos) {
    // Check if position is valid
    if (!isValidLocalPosition(localPos)) return false;
    
    // Remove all static subcubes at this position
    glm::ivec3 parentWorldPos = worldOrigin + localPos;
    auto it = staticSubcubes.begin();
    bool removedAny = false;
    
    while (it != staticSubcubes.end()) {
        Subcube* subcube = *it;
        if (subcube && subcube->getPosition() == parentWorldPos) {
            delete subcube;
            it = staticSubcubes.erase(it);
            removedAny = true;
        } else {
            ++it;
        }
    }
    
    // Also remove all dynamic subcubes at this position
    auto it2 = dynamicSubcubes.begin();
    while (it2 != dynamicSubcubes.end()) {
        Subcube* subcube = *it2;
        if (subcube && subcube->getPosition() == parentWorldPos) {
            delete subcube;
            it2 = dynamicSubcubes.erase(it2);
            removedAny = true;
        } else {
            ++it2;
        }
    }
    
    // Restore the parent cube
    Cube* cube = getCubeAt(localPos);
    if (cube) {
        cube->clearSubcubes(); // Clear subcubes from the parent cube
        cube->show();
    }
    
    if (removedAny) {
        needsUpdate = true;
        // std::cout << "[CHUNK] Cleared subdivision at local pos (" << localPos.x << "," << localPos.y << "," << localPos.z 
        //           << ")" << std::endl;
    }
    
    return removedAny;
}

size_t Chunk::subcubeToIndex(const glm::ivec3& parentPos, const glm::ivec3& subcubePos) {
    // Calculate a unique index for subcube identification
    size_t parentIndex = localToIndex(parentPos);
    size_t subcubeOffset = subcubePos.x + subcubePos.y * 3 + subcubePos.z * 9; // 0-26
    return parentIndex * 27 + subcubeOffset;
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

void Chunk::ensureBufferCapacity(size_t requiredInstances) {
    if (requiredInstances <= bufferCapacity) {
        return; // Buffer is large enough
    }
    
    // std::cout << "[CHUNK] WARNING: Buffer reallocation needed! Required: " 
    //           << requiredInstances << ", Current capacity: " << bufferCapacity << std::endl;
    
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
    
    // std::cout << "[CHUNK] Successfully reallocated buffer to capacity: " 
    //           << bufferCapacity << " instances (" << bufferSize << " bytes)" << std::endl;
}

void Chunk::logBufferUtilization() const {
    if (bufferCapacity > 0) {
        float utilization = float(maxInstancesUsed) / float(bufferCapacity) * 100.0f;
        float currentUtilization = float(faces.size()) / float(bufferCapacity) * 100.0f;
        
        // std::cout << "[CHUNK] Buffer utilization - Current: " << currentUtilization 
        //           << "% (" << faces.size() << "/" << bufferCapacity 
        //           << "), Peak: " << utilization 
        //           << "% (" << maxInstancesUsed << "/" << bufferCapacity << ")" << std::endl;
    }
}

// =============================================================================
// PHYSICS-RELATED METHODS
// =============================================================================

bool Chunk::breakSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos, 
                        Physics::PhysicsWorld* physicsWorld, const glm::vec3& impulseForce) {
    // Find the subcube in static list
    auto it = staticSubcubes.begin();
    while (it != staticSubcubes.end()) {
        Subcube* subcube = *it;
        if (subcube && 
            subcube->getPosition() == worldOrigin + parentPos && 
            subcube->getLocalPosition() == subcubePos) {
            
            // Mark as broken and move to dynamic list
            subcube->breakApart();
            
            // Create physics body for dynamic subcube if physics world is available
            if (physicsWorld) {
                glm::vec3 worldPos = subcube->getWorldPosition();
                
                // COORDINATE FIX: Static subcubes use corner-based coordinates, physics uses center-based
                // Convert from subcube corner position to physics center position
                glm::vec3 subcubeCornerPos = worldPos; // This is the corner position (matches static subcubes)
                glm::vec3 subcubeSize(1.0f / 3.0f); // Match visual subcube size
                glm::vec3 physicsCenterPos = subcubeCornerPos + (subcubeSize * 0.5f); // Physics center is at corner + half size per axis
                
                std::cout << "[SUBCUBE POSITION] ===== COMPREHENSIVE SUBCUBE POSITION TRACKING =====" << std::endl;
                std::cout << "[SUBCUBE POSITION] 1. Static subcube world position (corner): (" 
                          << subcubeCornerPos.x << ", " << subcubeCornerPos.y << ", " << subcubeCornerPos.z << ")" << std::endl;
                std::cout << "[SUBCUBE POSITION] 2. Target physics center position (corner + 0.5 * size): (" 
                          << physicsCenterPos.x << ", " << physicsCenterPos.y << ", " << physicsCenterPos.z << ")" << std::endl;
                std::cout << "[SUBCUBE POSITION] 3. Subcube size: " << subcubeSize.x << " (1/3 scale)" << std::endl;
                std::cout << "[SUBCUBE POSITION] 4. Half size offset: " << (subcubeSize.x * 0.5f) << " per axis" << std::endl;
                std::cout << "[SUBCUBE POSITION] 4a. Size calculation debug: 1.0f/3.0f = " << std::setprecision(6) << (1.0f/3.0f) << std::endl;
                std::cout << "[SUBCUBE POSITION] 4b. Half calculation debug: " << std::setprecision(6) << (1.0f/3.0f) << " * 0.5f = " << std::setprecision(6) << ((1.0f/3.0f) * 0.5f) << std::endl;
                
                // Create dynamic physics body at center position with shrunk collision for gap creation
                btRigidBody* rigidBody = physicsWorld->createBreakawaCube(physicsCenterPos, subcubeSize, 0.5f); // 0.5kg mass
                subcube->setRigidBody(rigidBody);
                
                // IMPORTANT: Set initial physics position to match the center position
                subcube->setPhysicsPosition(physicsCenterPos);
                
                // COMPREHENSIVE POSITION VERIFICATION - Check where the physics body actually ended up
                if (rigidBody) {
                    btTransform transform = rigidBody->getWorldTransform();
                    btVector3 physicsPos = transform.getOrigin();
                    std::cout << "[SUBCUBE POSITION] 5. Physics body actual position: (" 
                              << physicsPos.x() << ", " << physicsPos.y() << ", " << physicsPos.z() << ")" << std::endl;
                    std::cout << "[SUBCUBE POSITION] 6. Position difference from intended: (" 
                              << (physicsPos.x() - physicsCenterPos.x) << ", " 
                              << (physicsPos.y() - physicsCenterPos.y) << ", " 
                              << (physicsPos.z() - physicsCenterPos.z) << ")" << std::endl;
                    
                    // Check for any axis-specific offsets
                    float xOffset = physicsPos.x() - physicsCenterPos.x;
                    float yOffset = physicsPos.y() - physicsCenterPos.y;
                    float zOffset = physicsPos.z() - physicsCenterPos.z;
                    
                    if (abs(xOffset) > 0.001f || abs(yOffset) > 0.001f || abs(zOffset) > 0.001f) {
                        std::cout << "[SUBCUBE POSITION] WARNING: Position offset detected!" << std::endl;
                        std::cout << "[SUBCUBE POSITION] X-axis offset: " << xOffset << std::endl;
                        std::cout << "[SUBCUBE POSITION] Y-axis offset: " << yOffset << std::endl;
                        std::cout << "[SUBCUBE POSITION] Z-axis offset: " << zOffset << std::endl;
                    } else {
                        std::cout << "[SUBCUBE POSITION] SUCCESS: Physics body spawned at exact intended position" << std::endl;
                    }
                    
                    // Store physics position for comparison in updates
                    subcube->setPhysicsPosition(glm::vec3(physicsPos.x(), physicsPos.y(), physicsPos.z()));
                }
                
                // Apply initial impulse force to make it "break" away (RE-ENABLED)
                // Apply forces now that collision groups prevent inter-subcube collision
                if (rigidBody && glm::length(impulseForce) > 0.0f) {
                    // Scale down the impulse force for subcubes since they're smaller
                    glm::vec3 scaledImpulse = impulseForce * 0.3f; // Reduced from 0.5f for smaller subcubes
                    btVector3 btImpulse(scaledImpulse.x, scaledImpulse.y, scaledImpulse.z);
                    rigidBody->applyCentralImpulse(btImpulse);
                    
                    std::cout << "[SUBCUBE PHYSICS] Applied scaled impulse force (" 
                              << scaledImpulse.x << "," << scaledImpulse.y << "," << scaledImpulse.z 
                              << ") to broken subcube" << std::endl;
                    
                    // Enable gravity for natural falling behavior
                    rigidBody->setGravity(btVector3(0, -9.81f, 0)); // Standard gravity
                    
                    std::cout << "[SUBCUBE PHYSICS] Enabled gravity and impulse forces for realistic breakage behavior" << std::endl;
                } else {
                    // If no impulse force provided, still enable gravity for natural falling
                    if (rigidBody) {
                        rigidBody->setGravity(btVector3(0, -9.81f, 0)); // Standard gravity
                        std::cout << "[SUBCUBE PHYSICS] No impulse force - enabled gravity only" << std::endl;
                    }
                }
                std::cout << "[SUBCUBE POSITION] ===== END SUBCUBE POSITION TRACKING =====" << std::endl;
            }
            
            // CRITICAL: Force immediate compound shape rebuild to remove static collision BEFORE spawning dynamic cube
            // This must happen BEFORE the physics body interacts with the physics world
            // This prevents the +1.0 X-axis offset caused by collision recovery against the static compound shape
            std::cout << "[COMPOUND SHAPE] BEFORE: Force rebuilding compound shape to remove static collision BEFORE physics body creation" << std::endl;
            staticSubcubes.erase(it);
            forcePhysicsRebuild();
            std::cout << "[COMPOUND SHAPE] AFTER: Compound shape rebuilt - static collision removed before dynamic subcube spawns" << std::endl;
            
            // Now move to global dynamic subcube system after compound shape is rebuilt
            // Note: We need to get the ChunkManager reference to do this properly
            // For now, still add to local dynamic list (this needs to be refactored)
            dynamicSubcubes.push_back(subcube);
            
            // Rebuild both static and dynamic faces
            rebuildFaces();  // Updates static faces
            rebuildDynamicSubcubeFaces();  // Updates dynamic faces
            
            needsUpdate = true;
            std::cout << "[CHUNK] Broke subcube at parent pos (" 
                      << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                      << ") subcube pos (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z 
                      << ") - moved to dynamic list with physics" << std::endl;
            return true;
        }
        ++it;
    }
    
    return false; // Subcube not found in static list
}

bool Chunk::makeSubcubeStatic(Subcube* subcube) {
    if (!subcube) return false;
    
    // Find and remove from dynamic list
    auto it = std::find(dynamicSubcubes.begin(), dynamicSubcubes.end(), subcube);
    if (it != dynamicSubcubes.end()) {
        dynamicSubcubes.erase(it);
        subcube->repair();
        staticSubcubes.push_back(subcube);
        needsUpdate = true;
        return true;
    }
    
    return false; // Not found in dynamic list
}

void Chunk::createChunkPhysicsBody() {
    if (!physicsWorld) {
        std::cout << "[CHUNK] No physics world available for chunk physics body creation" << std::endl;
        return;
    }

    if (chunkPhysicsBody) {
        std::cout << "[CHUNK] Chunk physics body already exists, skipping creation" << std::endl;
        return;
    }

    std::cout << "[CHUNK] Creating optimized compound collision shape for chunk at (" 
              << worldOrigin.x << "," << worldOrigin.y << "," << worldOrigin.z << ")" << std::endl;

    // Create compound shape with dynamic AABB tree for efficient culling
    btCompoundShape* chunkCompound = new btCompoundShape(true);
    chunkCollisionShape = chunkCompound;
    
    // Generate merged collision boxes from visible cube faces
    auto mergedBoxes = generateMergedCollisionBoxes();
    
    if (mergedBoxes.empty()) {
        std::cout << "[CHUNK] No visible geometry - creating minimal collision box" << std::endl;
        // Fallback: small box at chunk origin to prevent total absence
        btBoxShape* fallbackShape = new btBoxShape(btVector3(0.5f, 0.5f, 0.5f));
        btTransform transform;
        transform.setIdentity();
        transform.setOrigin(btVector3(worldOrigin.x, worldOrigin.y, worldOrigin.z));
        chunkCompound->addChildShape(transform, fallbackShape);
    } else {
        for (const auto& box : mergedBoxes) {
            btBoxShape* boxShape = new btBoxShape(btVector3(box.halfExtents.x, box.halfExtents.y, box.halfExtents.z));
            btTransform transform;
            transform.setIdentity();
            transform.setOrigin(btVector3(box.center.x, box.center.y, box.center.z));
            chunkCompound->addChildShape(transform, boxShape);
        }
        std::cout << "[CHUNK] Created " << mergedBoxes.size() << " collision boxes from " 
                  << faces.size() << " visible faces" << std::endl;
    }
    
    // Create static rigid body
    btTransform bodyTransform;
    bodyTransform.setIdentity();
    btDefaultMotionState* motionState = new btDefaultMotionState(bodyTransform);
    btVector3 inertia(0, 0, 0);
    btRigidBody::btRigidBodyConstructionInfo rbInfo(0.0f, motionState, chunkCompound, inertia);
    chunkPhysicsBody = new btRigidBody(rbInfo);
    
    physicsWorld->getWorld()->addRigidBody(chunkPhysicsBody);
    
    std::cout << "[CHUNK] Compound collision shape created successfully" << std::endl;
}

void Chunk::updateChunkPhysicsBody() {
    if (!physicsWorld || !chunkPhysicsBody) return;
    
    // For performance optimization, we'll use a more intelligent update strategy
    // Instead of fully rebuilding every time, check if rebuild is actually necessary
    static int updateCounter = 0;
    updateCounter++;
    
    // Only do full rebuild every few updates, or when we have major changes
    bool needsFullRebuild = (updateCounter % 3 == 0) || (faces.size() < 50); // Full rebuild less frequently
    
    if (needsFullRebuild) {
        std::cout << "[CHUNK PERF] Full physics rebuild (#" << updateCounter << ") - faces: " << faces.size() << std::endl;
        
        // Remove existing body from world
        physicsWorld->getWorld()->removeRigidBody(chunkPhysicsBody);
        
        // Clean up existing resources
        if (chunkCollisionShape) {
            delete chunkCollisionShape;
            chunkCollisionShape = nullptr;
        }
        chunkPhysicsBody = nullptr;
        
        // Recreate with updated geometry
        createChunkPhysicsBody();
    } else {
        // Quick update: just adjust the existing collision shape properties
        std::cout << "[CHUNK PERF] Quick physics update (#" << updateCounter << ") - skipping full rebuild for performance" << std::endl;
        
        // Ensure the body is still active and responsive
        if (chunkPhysicsBody) {
            chunkPhysicsBody->activate(true);
            // Update collision flags to ensure proper interaction
            chunkPhysicsBody->setCollisionFlags(chunkPhysicsBody->getCollisionFlags() | btCollisionObject::CF_STATIC_OBJECT);
        }
    }
}

void Chunk::forcePhysicsRebuild() {
    if (!physicsWorld || !chunkPhysicsBody) return;
    
    std::cout << "[COMPOUND SHAPE] Force rebuilding compound shape to remove static collision" << std::endl;
    
    // Remove existing body from world
    physicsWorld->getWorld()->removeRigidBody(chunkPhysicsBody);
    
    // Clean up existing resources
    if (chunkCollisionShape) {
        delete chunkCollisionShape;
        chunkCollisionShape = nullptr;
    }
    chunkPhysicsBody = nullptr;
    
    // Recreate with updated geometry (this will exclude the broken subcube)
    createChunkPhysicsBody();
    
    std::cout << "[COMPOUND SHAPE] Compound shape rebuilt - static collision removed" << std::endl;
}

void Chunk::cleanupPhysicsResources() {
    // TODO: Implement physics cleanup
    if (chunkPhysicsBody) {
        std::cout << "[CHUNK] Cleaning up chunk physics body" << std::endl;
        // Delete physics body and collision shape
        chunkPhysicsBody = nullptr;
    }
    
    if (chunkCollisionShape) {
        std::cout << "[CHUNK] Cleaning up chunk collision shape" << std::endl;
        chunkCollisionShape = nullptr;
    }
}

std::vector<Chunk::CollisionBox> Chunk::generateMergedCollisionBoxes() {
    std::vector<CollisionBox> boxes;
    
    // NEW APPROACH: Build collision boxes directly from cubes, not faces
    // This eliminates the 6× duplication problem and sorting overhead
    std::cout << "[COLLISION] Building collision shapes from cubes directly (not faces)" << std::endl;
    
    // =========================================================================
    // PHASE 1: Process regular cubes (those that aren't subdivided)
    // =========================================================================
    for (size_t i = 0; i < cubes.size(); ++i) {
        const Cube* cube = cubes[i];
        
        // Skip deleted cubes (nullptr) or hidden cubes (subdivided)
        if (!cube || !cube->isVisible()) {
            continue;
        }
        
        // Get cube's local position within chunk
        glm::ivec3 localPos = indexToLocal(i);
        
        // Calculate world center position for collision box
        glm::vec3 cubeCenter = glm::vec3(worldOrigin) + glm::vec3(localPos) + glm::vec3(0.5f);
        glm::vec3 cubeHalfExtents(0.5f);
        
        boxes.emplace_back(cubeCenter, cubeHalfExtents);
    }
    
    // =========================================================================
    // PHASE 2: Process static subcubes (from subdivided cubes)
    // =========================================================================
    for (const Subcube* subcube : staticSubcubes) {
        // Skip broken or hidden subcubes
        if (!subcube || subcube->isBroken() || !subcube->isVisible()) {
            continue;
        }
        
        // Get subcube properties
        glm::ivec3 parentPos = subcube->getPosition();     // Parent cube's world position
        glm::ivec3 localPos = subcube->getLocalPosition(); // 0-2 for each axis within parent
        
        // Convert parent world position to chunk-relative position
        glm::ivec3 parentLocalPos = parentPos - worldOrigin;
        
        // Validate parent position is within chunk bounds
        if (parentLocalPos.x < 0 || parentLocalPos.x >= 32 ||
            parentLocalPos.y < 0 || parentLocalPos.y >= 32 ||
            parentLocalPos.z < 0 || parentLocalPos.z >= 32) {
            continue; // Skip subcubes with invalid parent positions
        }
        
        // Calculate subcube world center position
        glm::vec3 parentCenter = glm::vec3(worldOrigin) + glm::vec3(parentLocalPos) + glm::vec3(0.5f);
        glm::vec3 subcubeOffset = (glm::vec3(localPos) - glm::vec3(1.0f)) * (1.0f/3.0f);
        glm::vec3 subcubeCenter = parentCenter + subcubeOffset;
        glm::vec3 subcubeHalfExtents(1.0f/6.0f); // 1/3 cube size -> 1/6 half-extents
        
        boxes.emplace_back(subcubeCenter, subcubeHalfExtents);
    }
    
    std::cout << "[COLLISION] Generated " << boxes.size() << " collision boxes: " 
              << (cubes.size() - boxes.size() + staticSubcubes.size()) << " regular cubes + " 
              << staticSubcubes.size() << " subcubes" << std::endl;
    
    // No sorting or deduplication needed - each cube/subcube generates exactly one collision box!
    return boxes;
}

} // namespace VulkanCube
