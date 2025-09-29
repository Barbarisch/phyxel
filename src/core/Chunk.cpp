#include "core/Chunk.h"
#include "core/ChunkManager.h"
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
    cubes.reserve(32 * 32 * 32);              // Reserve space for all possible cubes // chatGPT thinks max number of viewable cubes to be 2977
    staticSubcubes.reserve(1000);             // Reserve reasonable space for static subcubes
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
    
    cleanupVulkanResources();
    cleanupPhysicsResources();
}

Chunk::Chunk(Chunk&& other) noexcept
    : cubes(std::move(other.cubes))
    , staticSubcubes(std::move(other.staticSubcubes))
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
    , physicalDevice(other.physicalDevice)
    , cubeCollisionMap(std::move(other.cubeCollisionMap))
    , subcubeCollisionMap(std::move(other.subcubeCollisionMap))
    , subcubeGroupPositions(std::move(other.subcubeGroupPositions))
    , collisionNeedsUpdate(other.collisionNeedsUpdate)
    , isInBulkOperation(other.isInBulkOperation)
    , debugCollisionShapeCount(other.debugCollisionShapeCount)
    , debugCubeShapeCount(other.debugCubeShapeCount)
    , debugSubcubeShapeCount(other.debugSubcubeShapeCount) {
    
    // Reset other object's Vulkan handles and capacity tracking
    other.instanceBuffer = VK_NULL_HANDLE;
    other.instanceMemory = VK_NULL_HANDLE;
    other.mappedMemory = nullptr;
    other.bufferCapacity = 0;
    other.maxInstancesUsed = 0;
    other.device = VK_NULL_HANDLE;
    other.physicalDevice = VK_NULL_HANDLE;
    
    // Reset other object's collision tracking
    other.collisionNeedsUpdate = false;
    other.isInBulkOperation = false;
    other.debugCollisionShapeCount = 0;
    other.debugCubeShapeCount = 0;
    other.debugSubcubeShapeCount = 0;
}

Chunk& Chunk::operator=(Chunk&& other) noexcept {
    if (this != &other) {
        // Clean up current resources
        cleanupVulkanResources();
        cleanupPhysicsResources();
        
        // Move data
        cubes = std::move(other.cubes);
        staticSubcubes = std::move(other.staticSubcubes);
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
        
        // Move collision tracking data
        cubeCollisionMap = std::move(other.cubeCollisionMap);
        subcubeCollisionMap = std::move(other.subcubeCollisionMap);
        subcubeGroupPositions = std::move(other.subcubeGroupPositions);
        collisionNeedsUpdate = other.collisionNeedsUpdate;
        isInBulkOperation = other.isInBulkOperation;
        debugCollisionShapeCount = other.debugCollisionShapeCount;
        debugCubeShapeCount = other.debugCubeShapeCount;
        debugSubcubeShapeCount = other.debugSubcubeShapeCount;
        
        // Reset other object's Vulkan handles and capacity tracking
        other.instanceBuffer = VK_NULL_HANDLE;
        other.instanceMemory = VK_NULL_HANDLE;
        other.mappedMemory = nullptr;
        other.bufferCapacity = 0;
        other.maxInstancesUsed = 0;
        other.device = VK_NULL_HANDLE;
        other.physicalDevice = VK_NULL_HANDLE;
        
        // Reset other object's collision tracking
        other.collisionNeedsUpdate = false;
        other.isInBulkOperation = false;
        other.debugCollisionShapeCount = 0;
        other.debugCubeShapeCount = 0;
        other.debugSubcubeShapeCount = 0;
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
    
    // Update hash maps to reflect removal
    removeFromVoxelMaps(localPos);
    
    // IMPROVED: Remove collision shape with proper memory management
    fastRemoveCollisionAt(localPos);
    
    // CRITICAL: Update collision shapes of neighboring cubes that might now be exposed
    updateNeighborCollisionShapes(localPos);
    
    // Mark chunk as dirty for smart saving
    setDirty(true);
    std::cout << "[CHUNK] Removed cube at local pos (" << localPos.x << "," << localPos.y << "," << localPos.z 
              << ") - Chunk now DIRTY for save" << std::endl;
    
    // Immediately rebuild faces to remove the cube from GPU buffer
    rebuildFaces();
    updateVulkanBuffer();
    
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
        // Update hash maps for existing cube
        addToVoxelMaps(localPos, cubes[index]);
    } else {
        // Create new cube
        cubes[index] = new Cube(localPos, color);
        cubes[index]->setOriginalColor(color); // Ensure original color is set
        // Update hash maps for new cube
        addToVoxelMaps(localPos, cubes[index]);
    }
    
    // Mark chunk as dirty for smart saving
    setDirty(true);
    
    // IMPROVED: Add collision shape with reference counting
    fastAddCollisionAt(localPos);
    
    // CRITICAL: Only update neighbors during individual operations, not bulk loading
    if (!isInBulkOperation) {
        updateNeighborCollisionShapes(localPos);
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
    
    // Mark chunk as dirty since it has new content
    setDirty(true);
    
    // Initialize hash maps for O(1) lookups
    initializeVoxelMaps();
    
    // std::cout << "[CHUNK] Populated chunk at origin (" 
    //           << worldOrigin.x << "," << worldOrigin.y << "," << worldOrigin.z 
    //           << ") with " << cubes.size() << " cubes" << std::endl;
}

void Chunk::initializeForLoading() {
    cubes.clear();
    faces.clear();
    
    // Initialize sparse cube storage (32x32x32 array with nullptr entries)
    cubes.resize(32 * 32 * 32, nullptr);
    
    // Clear any existing subcubes
    for (Subcube* subcube : staticSubcubes) {
        delete subcube;
    }
    staticSubcubes.clear();
    
    // Set bulk operation flag to prevent neighbor collision updates during loading
    isInBulkOperation = true;
    
    std::cout << "[CHUNK] Initialized chunk at origin (" 
              << worldOrigin.x << "," << worldOrigin.y << "," << worldOrigin.z 
              << ") for database loading" << std::endl;
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
                
                // Assign texture based on face ID
                faceInstance.textureIndex = VulkanCube::TextureConstants::getTextureIndexForFace(faceID);
                faceInstance.reserved = 0;
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
                
                // Assign texture based on face ID
                faceInstance.textureIndex = VulkanCube::TextureConstants::getTextureIndexForFace(faceID);
                faceInstance.reserved = 0;
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

void Chunk::updateSingleCubeTexture(const glm::ivec3& localPos, uint16_t textureIndex) {
    if (!isValidLocalPosition(localPos)) return;
    
    // Find the cube - we don't store texture info in cube objects yet, just update the faces
    Cube* cube = getCubeAt(localPos);
    if (!cube) return;
    
    // Efficiently update only the affected faces in the buffer
    // Instead of rebuilding all faces, find and update just this cube's faces
    if (!mappedMemory) return;
    
    bool updatedAnyFaces = false;
    
    // Find all face instances for this cube and update their texture indices
    for (size_t i = 0; i < faces.size(); ++i) {
        InstanceData& face = faces[i];
        
        // Extract position from packed data
        int faceX = face.packedData & 0x1F;
        int faceY = (face.packedData >> 5) & 0x1F;
        int faceZ = (face.packedData >> 10) & 0x1F;
        
        // Check if this face belongs to our cube
        if (faceX == localPos.x && faceY == localPos.y && faceZ == localPos.z) {
            // Update the texture index in the faces vector
            faces[i].textureIndex = textureIndex;
            
            // Update the GPU buffer directly (partial update)
            VkDeviceSize offset = i * sizeof(InstanceData) + offsetof(InstanceData, textureIndex);
            memcpy(static_cast<char*>(mappedMemory) + offset, &textureIndex, sizeof(uint16_t));
            
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

void Chunk::updateSingleSubcubeTexture(const glm::ivec3& parentLocalPos, const glm::ivec3& subcubePos, uint16_t textureIndex) {
    if (!isValidLocalPosition(parentLocalPos)) return;
    if (subcubePos.x < 0 || subcubePos.x >= 3 || 
        subcubePos.y < 0 || subcubePos.y >= 3 || 
        subcubePos.z < 0 || subcubePos.z >= 3) return;
    
    // Find the subcube - we don't store texture info in subcube objects yet, just update the faces
    Subcube* subcube = getSubcubeAt(parentLocalPos, subcubePos);
    if (!subcube) return;
    
    // Efficiently update only the affected faces in the buffer
    if (!mappedMemory) return;
    
    bool updatedAnyFaces = false;
    
    // Find all face instances for this subcube and update their texture indices
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
                // Update the texture index in the faces vector
                faces[i].textureIndex = textureIndex;
                
                // Update the GPU buffer directly (partial update)
                VkDeviceSize offset = i * sizeof(InstanceData) + offsetof(InstanceData, textureIndex);
                memcpy(static_cast<char*>(mappedMemory) + offset, &textureIndex, sizeof(uint16_t));
                
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

glm::vec3 Chunk::getMinBounds() const {
    // Convert world origin (ivec3) to world position (vec3) for bounding box
    return glm::vec3(worldOrigin);
}

glm::vec3 Chunk::getMaxBounds() const {
    // Chunk spans 32x32x32 units from worldOrigin to worldOrigin + (31,31,31)
    // For bounding box calculations, we want worldOrigin + (32,32,32) as max bounds
    return glm::vec3(worldOrigin) + glm::vec3(32.0f, 32.0f, 32.0f);
}

// =============================================================================
// SUBCUBE MANAGEMENT METHODS
// =============================================================================

Subcube* Chunk::getSubcubeAt(const glm::ivec3& localPos, const glm::ivec3& subcubePos) {
    // Search in static subcubes only
    for (Subcube* subcube : staticSubcubes) {
        if (subcube && 
            subcube->getPosition() == worldOrigin + localPos && 
            subcube->getLocalPosition() == subcubePos) {
            return subcube;
        }
    }
    return nullptr;
}

const Subcube* Chunk::getSubcubeAt(const glm::ivec3& localPos, const glm::ivec3& subcubePos) const {
    // Search in static subcubes only
    for (const Subcube* subcube : staticSubcubes) {
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
    
    // Collect from static subcubes only
    for (Subcube* subcube : staticSubcubes) {
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

// =============================================================================
// NEW: O(1) VoxelLocation resolution system for optimized hover detection
// =============================================================================

VoxelLocation Chunk::resolveLocalPosition(const glm::ivec3& localPos) const {
    // Quick bounds check
    if (!isValidLocalPosition(localPos)) {
        return VoxelLocation();
    }
    
    // O(1) voxel type lookup
    auto typeIt = voxelTypeMap.find(localPos);
    if (typeIt == voxelTypeMap.end()) {
        return VoxelLocation(); // No voxel at this position
    }
    
    glm::ivec3 worldPos = worldOrigin + localPos;
    VoxelLocation::Type type = typeIt->second;
    
    if (type == VoxelLocation::SUBDIVIDED) {
        VoxelLocation location;
        location.type = VoxelLocation::SUBDIVIDED;
        location.chunk = const_cast<Chunk*>(this);
        location.localPos = localPos;
        location.worldPos = worldPos;
        location.subcubePos = glm::ivec3(-1);
        return location;
    } else if (type == VoxelLocation::CUBE) {
        VoxelLocation location;
        location.type = VoxelLocation::CUBE;
        location.chunk = const_cast<Chunk*>(this);
        location.localPos = localPos;
        location.worldPos = worldPos;
        location.subcubePos = glm::ivec3(-1);
        return location;
    }
    
    return VoxelLocation();
}

bool Chunk::hasVoxelAt(const glm::ivec3& localPos) const {
    if (!isValidLocalPosition(localPos)) return false;
    return voxelTypeMap.find(localPos) != voxelTypeMap.end();
}

bool Chunk::hasSubcubeAt(const glm::ivec3& localPos, const glm::ivec3& subcubePos) const {
    auto parentIt = subcubeMap.find(localPos);
    if (parentIt == subcubeMap.end()) return false;
    
    return parentIt->second.find(subcubePos) != parentIt->second.end();
}

VoxelLocation::Type Chunk::getVoxelType(const glm::ivec3& localPos) const {
    auto it = voxelTypeMap.find(localPos);
    return (it != voxelTypeMap.end()) ? it->second : VoxelLocation::EMPTY;
}

// O(1) optimized lookups (replace linear searches)
Cube* Chunk::getCubeAtFast(const glm::ivec3& localPos) {
    auto it = cubeMap.find(localPos);
    return (it != cubeMap.end()) ? it->second : nullptr;
}

const Cube* Chunk::getCubeAtFast(const glm::ivec3& localPos) const {
    auto it = cubeMap.find(localPos);
    return (it != cubeMap.end()) ? it->second : nullptr;
}

Subcube* Chunk::getSubcubeAtFast(const glm::ivec3& localPos, const glm::ivec3& subcubePos) {
    auto parentIt = subcubeMap.find(localPos);
    if (parentIt == subcubeMap.end()) return nullptr;
    
    auto subcubeIt = parentIt->second.find(subcubePos);
    return (subcubeIt != parentIt->second.end()) ? subcubeIt->second : nullptr;
}

const Subcube* Chunk::getSubcubeAtFast(const glm::ivec3& localPos, const glm::ivec3& subcubePos) const {
    auto parentIt = subcubeMap.find(localPos);
    if (parentIt == subcubeMap.end()) return nullptr;
    
    auto subcubeIt = parentIt->second.find(subcubePos);
    return (subcubeIt != parentIt->second.end()) ? subcubeIt->second : nullptr;
}

std::vector<Subcube*> Chunk::getSubcubesAtFast(const glm::ivec3& localPos) {
    std::vector<Subcube*> result;
    auto parentIt = subcubeMap.find(localPos);
    if (parentIt != subcubeMap.end()) {
        result.reserve(parentIt->second.size());
        for (auto& pair : parentIt->second) {
            result.push_back(pair.second);
        }
    }
    return result;
}

// Internal: Maintain hash map consistency
void Chunk::updateVoxelMaps(const glm::ivec3& localPos) {
    // Check what exists at this position using legacy arrays
    Cube* cube = getCubeAt(localPos); // Use legacy method
    auto subcubes = getSubcubesAt(localPos); // Use legacy method
    
    if (!subcubes.empty()) {
        // Position has subcubes - mark as subdivided
        voxelTypeMap[localPos] = VoxelLocation::SUBDIVIDED;
        
        // Update subcube map
        subcubeMap[localPos].clear();
        for (Subcube* subcube : subcubes) {
            if (subcube) {
                subcubeMap[localPos][subcube->getLocalPosition()] = subcube;
            }
        }
        
        // Update cube map
        if (cube) {
            cubeMap[localPos] = cube;
        } else {
            cubeMap.erase(localPos);
        }
    } else if (cube) {
        // Position has cube but no subcubes
        voxelTypeMap[localPos] = VoxelLocation::CUBE;
        cubeMap[localPos] = cube;
        subcubeMap.erase(localPos); // Remove any leftover subcube data
    } else {
        // Position is empty
        voxelTypeMap.erase(localPos);
        cubeMap.erase(localPos);
        subcubeMap.erase(localPos);
    }
}

void Chunk::addToVoxelMaps(const glm::ivec3& localPos, Cube* cube) {
    if (cube) {
        cubeMap[localPos] = cube;
        
        // Check if we have subcubes at this position
        auto subcubes = getSubcubesAt(localPos);
        if (!subcubes.empty()) {
            voxelTypeMap[localPos] = VoxelLocation::SUBDIVIDED;
        } else {
            voxelTypeMap[localPos] = VoxelLocation::CUBE;
        }
    }
}

void Chunk::removeFromVoxelMaps(const glm::ivec3& localPos) {
    cubeMap.erase(localPos);
    
    // Check if we still have subcubes at this position
    auto subcubes = getSubcubesAt(localPos);
    if (!subcubes.empty()) {
        voxelTypeMap[localPos] = VoxelLocation::SUBDIVIDED;
    } else {
        voxelTypeMap.erase(localPos);
        subcubeMap.erase(localPos);
    }
}

void Chunk::addSubcubeToMaps(const glm::ivec3& localPos, const glm::ivec3& subcubePos, Subcube* subcube) {
    if (subcube) {
        subcubeMap[localPos][subcubePos] = subcube;
        voxelTypeMap[localPos] = VoxelLocation::SUBDIVIDED;
    }
}

void Chunk::removeSubcubeFromMaps(const glm::ivec3& localPos, const glm::ivec3& subcubePos) {
    auto parentIt = subcubeMap.find(localPos);
    if (parentIt != subcubeMap.end()) {
        parentIt->second.erase(subcubePos);
        
        // If no subcubes left at this position, just remove the subcubeMap entry
        // The caller (removeSubcube) will handle voxelTypeMap updates after cube restoration
        if (parentIt->second.empty()) {
            subcubeMap.erase(localPos);
            std::cout << "[VOXEL MAP] Removed subcubeMap entry at (" << localPos.x << "," << localPos.y << "," << localPos.z << ") - no subcubes remain" << std::endl;
        }
    }
}

void Chunk::initializeVoxelMaps() {
    // Clear existing maps
    cubeMap.clear();
    subcubeMap.clear();
    voxelTypeMap.clear();
    
    std::cout << "[CHUNK] Initializing voxel maps for chunk at origin (" 
              << worldOrigin.x << "," << worldOrigin.y << "," << worldOrigin.z << ")" << std::endl;
    
    // Build cube map from existing vector data
    for (size_t i = 0; i < cubes.size(); ++i) {
        if (cubes[i]) {
            glm::ivec3 localPos = indexToLocal(i);
            cubeMap[localPos] = cubes[i];
            voxelTypeMap[localPos] = VoxelLocation::CUBE;
        }
    }
    
    // Build subcube map from existing vector data
    for (Subcube* subcube : staticSubcubes) {
        if (subcube) {
            glm::ivec3 parentWorldPos = subcube->getPosition();
            glm::ivec3 localPos = parentWorldPos - worldOrigin;
            glm::ivec3 subcubePos = subcube->getLocalPosition();
            
            // Validate that this subcube belongs to this chunk
            if (isValidLocalPosition(localPos)) {
                subcubeMap[localPos][subcubePos] = subcube;
                voxelTypeMap[localPos] = VoxelLocation::SUBDIVIDED;
            }
        }
    }
    
    std::cout << "[CHUNK] Voxel maps initialized: " 
              << cubeMap.size() << " cubes, "
              << subcubeMap.size() << " subdivided positions, "
              << voxelTypeMap.size() << " total voxels" << std::endl;
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
                
                // CRITICAL: Update hash maps for each subcube for O(1) hover detection
                addSubcubeToMaps(localPos, subcubeLocalPos, newSubcube);
                
                colorIndex++;
            }
        }
    }
    
    // CRITICAL ARCHITECTURAL CHANGE: Delete the parent cube completely (don't just hide it)
    // Remove from cubeMap and cubes vector
    cubeMap.erase(localPos);
    size_t cubeIndex = localToIndex(localPos);
    if (cubeIndex < cubes.size() && cubes[cubeIndex] == cube) {
        delete cube;  // Delete the cube object
        cubes[cubeIndex] = nullptr;  // Clear the vector entry
        std::cout << "[CUBE DELETION] Completely removed parent cube at (" 
                  << localPos.x << "," << localPos.y << "," << localPos.z 
                  << ") - replaced by 27 subcubes" << std::endl;
    }
    
    // CRITICAL: Update voxelTypeMap to mark position as subdivided for proper hover detection
    voxelTypeMap[localPos] = VoxelLocation::SUBDIVIDED;
    
    // Debug: Verify the cube is actually hidden and subdivided
    // std::cout << "[CHUNK] Parent cube hidden. isVisible() = " << cube->isVisible() 
    //           << ", isSubdivided() = " << cube->isSubdivided() << std::endl;
    
    // Mark for update and as dirty for database persistence
    needsUpdate = true;
    setDirty(true);
    
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
    
    // Update hash maps
    addSubcubeToMaps(parentPos, subcubePos, newSubcube);
    
    // IMPROVED: Update collision shape with memory safety
    fastAddCollisionAt(parentPos);
    
    // Mark for update and as dirty for database persistence
    needsUpdate = true;
    setDirty(true);
    
    return true;
}

bool Chunk::removeSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos) {
    // Try to find and remove from static subcubes
    for (auto it = staticSubcubes.begin(); it != staticSubcubes.end(); ++it) {
        Subcube* subcube = *it;
        if (subcube && 
            subcube->getPosition() == worldOrigin + parentPos && 
            subcube->getLocalPosition() == subcubePos) {
            
            delete subcube;
            staticSubcubes.erase(it);
            
            // Update hash maps BEFORE checking remaining subcubes
            removeSubcubeFromMaps(parentPos, subcubePos);
            
            // CRITICAL FIX: Check if any subcubes remain at this parent position
            std::vector<Subcube*> remainingSubcubes = getStaticSubcubesAt(parentPos);
            
            if (remainingSubcubes.empty()) {
                // No more subcubes at this position - remove collision shape entirely
                std::cout << "[COLLISION] No subcubes remain at parent pos (" 
                          << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                          << ") - removing collision shape" << std::endl;
                fastRemoveCollisionAt(parentPos);
                
                // ARCHITECTURAL CHANGE: Do NOT restore parent cube - leave position empty
                // The parent cube was deleted during subdivision, so position should become empty
                voxelTypeMap.erase(parentPos);
                std::cout << "[VOXEL MAP] Position now empty at (" 
                          << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                          << ") - all subcubes removed, no cube to restore" << std::endl;
            } else {
                // Still have subcubes - update collision shape to reflect remaining subcubes
                std::cout << "[COLLISION] " << remainingSubcubes.size() 
                          << " subcubes remain at parent pos (" 
                          << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                          << ") - updating collision shape" << std::endl;
                // Remove old shape and add new one to reflect current subcube configuration
                fastRemoveCollisionAt(parentPos);
                fastAddCollisionAt(parentPos);
                
                // CRITICAL: Ensure voxelTypeMap shows SUBDIVIDED since subcubes remain
                voxelTypeMap[parentPos] = VoxelLocation::SUBDIVIDED;
                std::cout << "[VOXEL MAP] Maintained SUBDIVIDED type at (" 
                          << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                          << ") - " << remainingSubcubes.size() << " subcubes remain" << std::endl;
            }
            
            needsUpdate = true;
            setDirty(true);
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
    
    // ARCHITECTURAL CHANGE: Don't try to restore parent cube - it was deleted during subdivision
    // Clear the subdivision state from data structures
    subcubeMap.erase(localPos);
    voxelTypeMap.erase(localPos);
    
    if (removedAny) {
        std::cout << "[CHUNK] Cleared subdivision at local pos (" << localPos.x << "," << localPos.y << "," << localPos.z 
                  << ") - position now empty" << std::endl;
        needsUpdate = true;
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
                        Physics::PhysicsWorld* physicsWorld, ChunkManager* chunkManager, const glm::vec3& impulseForce) {
    // Find the subcube in static list
    auto it = staticSubcubes.begin();
    while (it != staticSubcubes.end()) {
        Subcube* subcube = *it;
        if (subcube && 
            subcube->getPosition() == worldOrigin + parentPos && 
            subcube->getLocalPosition() == subcubePos) {
            
            // Store subcube data before removal
            glm::vec3 worldPos = subcube->getWorldPosition();
            glm::vec3 originalColor = subcube->getOriginalColor();
            bool isVisible = subcube->isVisible();
            float lifetime = subcube->getLifetime();
            
            // CRITICAL: Use proper removeSubcube to update all data structures (voxelTypeMap, subcubeMap, etc.)
            std::cout << "[INCREMENTAL] BEFORE: Using proper subcube removal to update all data structures" << std::endl;
            bool removed = removeSubcube(parentPos, subcubePos);
            if (!removed) {
                std::cout << "[ERROR] Failed to remove subcube from data structures" << std::endl;
                return false;
            }
            // NEW: Fast collision update (already done in removeSubcube)
            batchUpdateCollisions();
            std::cout << "[INCREMENTAL] AFTER: All data structures updated and collision shape updated incrementally" << std::endl;
            
            // Create new dynamic subcube for physics (since original was removed)
            auto dynamicSubcube = std::make_unique<Subcube>(
                worldOrigin + parentPos, 
                originalColor, 
                subcubePos
            );
            
            // Set properties from stored data
            dynamicSubcube->setOriginalColor(originalColor);
            dynamicSubcube->setVisible(isVisible);
            dynamicSubcube->setLifetime(lifetime);
            dynamicSubcube->breakApart(); // Mark as broken
            
            // Create physics body for dynamic subcube if physics world is available
            if (physicsWorld) {
                // COORDINATE FIX: Static subcubes use corner-based coordinates, physics uses center-based
                glm::vec3 subcubeCornerPos = worldPos; // Corner position (matches static subcubes)
                glm::vec3 subcubeSize(1.0f / 3.0f); // Match visual subcube size
                glm::vec3 physicsCenterPos = subcubeCornerPos + (subcubeSize * 0.5f); // Physics center position
                
                // Create dynamic physics body at center position
                btRigidBody* rigidBody = physicsWorld->createBreakawaCube(physicsCenterPos, subcubeSize, 0.5f); // 0.5kg mass
                dynamicSubcube->setRigidBody(rigidBody);
                dynamicSubcube->setPhysicsPosition(physicsCenterPos);
                
                // NO FORCES APPLIED - subcubes break gently with gravity only
                std::cout << "[SUBCUBE PHYSICS] Created physics body for subcube (no forces applied - gravity only)" << std::endl;
                
                // Enable gravity for natural falling behavior
                if (rigidBody) {
                    rigidBody->setGravity(btVector3(0, -9.81f, 0));
                }
            }
            
            // Transfer the dynamic subcube directly to global system
            if (chunkManager) {
                // The dynamicSubcube is already properly configured, just transfer it
                chunkManager->addGlobalDynamicSubcube(std::move(dynamicSubcube));
                
                std::cout << "[GLOBAL TRANSFER] Moved broken subcube directly to global dynamic system (safe transfer)" << std::endl;
            } else {
                std::cout << "[ERROR] No ChunkManager provided - cannot transfer to global system" << std::endl;
            }
            
            // Note: No need to delete subcube - it was already properly removed by removeSubcube()
            
            // Rebuild static faces only (no more dynamic faces in chunks)
            rebuildFaces();
            needsUpdate = true;
            
            std::cout << "[CHUNK] Broke subcube at parent pos (" 
                      << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                      << ") subcube pos (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z 
                      << ") - transferred to global system safely" << std::endl;
            return true;
        }
        ++it;
    }
    
    return false; // Subcube not found in static list
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
    
    // Clear existing collision tracking (in case of rebuild) - shapes auto-delete with shared_ptr
    cubeCollisionMap.clear();
    subcubeCollisionMap.clear();
    subcubeGroupPositions.clear();
    debugCollisionShapeCount = 0;
    debugCubeShapeCount = 0;
    debugSubcubeShapeCount = 0;
    
    // IMPROVED: Build collision shapes with reference-counted tracking
    buildInitialCollisionShapes();
    
    // Create static rigid body
    btTransform bodyTransform;
    bodyTransform.setIdentity();
    btDefaultMotionState* motionState = new btDefaultMotionState(bodyTransform);
    btVector3 inertia(0, 0, 0);
    btRigidBody::btRigidBodyConstructionInfo rbInfo(0.0f, motionState, chunkCompound, inertia);
    chunkPhysicsBody = new btRigidBody(rbInfo);
    
    physicsWorld->getWorld()->addRigidBody(chunkPhysicsBody);
    
    std::cout << "[CHUNK] Improved compound collision shape created with " << debugCollisionShapeCount 
              << " tracked collision shapes (" << debugCubeShapeCount << " cubes, " 
              << debugSubcubeShapeCount << " subcubes)" << std::endl;
}

void Chunk::updateChunkPhysicsBody() {
    if (!physicsWorld || !chunkPhysicsBody) return;
    
    // IMPROVED: Batch any remaining collision updates efficiently
    batchUpdateCollisions();
    
    // CRITICAL: Force physics world to recognize collision shape changes
    if (chunkPhysicsBody) {
        // Activate the physics body to ensure collision detection updates
        chunkPhysicsBody->activate(true);
        
        // IMPORTANT: Recalculate AABB for the compound shape after collision changes
        // This ensures the physics world immediately recognizes the new collision geometry
        btCompoundShape* compound = static_cast<btCompoundShape*>(chunkPhysicsBody->getCollisionShape());
        if (compound) {
            compound->recalculateLocalAabb();
        }
        
        // Force the physics world to update the broadphase AABB for this body
        // This prevents the brief moment where old collision data might be cached
        auto* dynamicsWorld = physicsWorld->getWorld();
        if (dynamicsWorld) {
            dynamicsWorld->updateSingleAabb(chunkPhysicsBody);
        }
        
        std::cout << "[INCREMENTAL] Improved collision updates complete with physics sync - maintaining " 
                  << debugCollisionShapeCount << " collision shapes (" << debugCubeShapeCount 
                  << " cubes, " << debugSubcubeShapeCount << " subcubes)" << std::endl;
    } else {
        std::cout << "[INCREMENTAL] No collision updates needed" << std::endl;
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
    // Clean up improved collision tracking system - shapes auto-delete when reference count reaches zero
    std::cout << "[COLLISION CLEANUP] Before cleanup: " << debugCollisionShapeCount << " total shapes ("
              << debugCubeShapeCount << " cubes, " << debugSubcubeShapeCount << " subcubes)" << std::endl;
    
    cubeCollisionMap.clear();
    subcubeCollisionMap.clear();
    subcubeGroupPositions.clear();
    collisionNeedsUpdate = false;
    
    // Reset debug counters
    debugCollisionShapeCount = 0;
    debugCubeShapeCount = 0;
    debugSubcubeShapeCount = 0;
    
    if (chunkPhysicsBody) {
        std::cout << "[CHUNK] Cleaning up chunk physics body" << std::endl;
        chunkPhysicsBody = nullptr;
    }
    
    if (chunkCollisionShape) {
        std::cout << "[CHUNK] Cleaning up chunk collision shape" << std::endl;
        chunkCollisionShape = nullptr;
    }
}

// IMPROVED collision system - memory-safe reference-counted shapes with individual subcube tracking
// This method replaces the old system that used nullptr placeholders and geometric distance heuristics
// Now provides proper individual tracking for each collision shape with automatic memory management
void Chunk::fastAddCollisionAt(const glm::ivec3& localPos) {
    if (!chunkCollisionShape) return;
    
    btCompoundShape* compound = static_cast<btCompoundShape*>(chunkCollisionShape);
    
    // Remove any existing collision shapes at this position first
    fastRemoveCollisionAt(localPos);
    
    // Check for regular cube first
    const Cube* cube = getCubeAt(localPos);
    if (cube && cube->isVisible() && hasExposedFaces(localPos)) {
        // Create full cube collision shape
        glm::vec3 shapeCenter = glm::vec3(worldOrigin) + glm::vec3(localPos) + glm::vec3(0.5f);
        btBoxShape* boxShape = new btBoxShape(btVector3(0.5f, 0.5f, 0.5f));
        
        btTransform transform;
        transform.setIdentity();
        transform.setOrigin(btVector3(shapeCenter.x, shapeCenter.y, shapeCenter.z));
        compound->addChildShape(transform, boxShape);
        
        // Track with reference counting - shape is now managed by compound
        auto shapeInfo = std::make_shared<CollisionShapeInfo>(boxShape, CollisionShapeInfo::CUBE);
        shapeInfo->isInCompound = true; // Shape is now owned by Bullet compound
        cubeCollisionMap[localPos] = shapeInfo;
        
        // Update debug counters
        debugCollisionShapeCount++;
        debugCubeShapeCount++;
        
        std::cout << "[COLLISION] Added cube collision at (" 
                  << localPos.x << "," << localPos.y << "," << localPos.z 
                  << ") - Total shapes: " << debugCollisionShapeCount << std::endl;
        
    } else {
        // Check for subcubes at this position
        auto subcubes = getStaticSubcubesAt(localPos);
        if (!subcubes.empty()) {
            std::cout << "[COLLISION] Creating " << subcubes.size() 
                      << " subcube collision shapes at (" << localPos.x << "," << localPos.y << "," << localPos.z << ")" << std::endl;
            
            // Create collision shape for EACH subcube with individual tracking
            for (const Subcube* subcube : subcubes) {
                // Calculate subcube center position
                glm::vec3 subcubeLocalOffset = glm::vec3(subcube->getLocalPosition()) - glm::vec3(1.0f); // Convert from 0-2 to -1 to +1
                glm::vec3 subcubeOffset = subcubeLocalOffset * (1.0f/3.0f); // Scale to subcube size
                glm::vec3 subcubeCenter = glm::vec3(worldOrigin) + glm::vec3(localPos) + glm::vec3(0.5f) + subcubeOffset;
                
                // Create collision shape
                btBoxShape* subcubeShape = new btBoxShape(btVector3(1.0f/6.0f, 1.0f/6.0f, 1.0f/6.0f));
                
                btTransform transform;
                transform.setIdentity();
                transform.setOrigin(btVector3(subcubeCenter.x, subcubeCenter.y, subcubeCenter.z));
                compound->addChildShape(transform, subcubeShape);
                
                // Track individual subcube with reference counting - shape is now managed by compound
                SubcubeCollisionKey key{localPos, subcube->getLocalPosition()};
                auto shapeInfo = std::make_shared<CollisionShapeInfo>(subcubeShape, CollisionShapeInfo::SUBCUBE);
                shapeInfo->isInCompound = true; // Shape is now owned by Bullet compound
                subcubeCollisionMap[key] = shapeInfo;
                
                // Update debug counters
                debugCollisionShapeCount++;
                debugSubcubeShapeCount++;
                
                std::cout << "[COLLISION] Added subcube collision at local (" 
                          << subcube->getLocalPosition().x << "," << subcube->getLocalPosition().y << "," << subcube->getLocalPosition().z 
                          << ") center (" << subcubeCenter.x << "," << subcubeCenter.y << "," << subcubeCenter.z << ")" << std::endl;
            }
            
            // Mark this position as having subcube groups (performance optimization)
            subcubeGroupPositions.insert(localPos);
        }
    }
}

void Chunk::fastRemoveCollisionAt(const glm::ivec3& localPos) {
    if (!chunkCollisionShape) return;
    
    btCompoundShape* compound = static_cast<btCompoundShape*>(chunkCollisionShape);
    
    // Check for cube collision shape first
    auto cubeIt = cubeCollisionMap.find(localPos);
    if (cubeIt != cubeCollisionMap.end()) {
        // Remove cube collision shape from compound
        btCollisionShape* shapeToRemove = cubeIt->second->shape;
        bool shapeRemoved = false;
        
        for (int i = compound->getNumChildShapes() - 1; i >= 0; i--) {
            if (compound->getChildShape(i) == shapeToRemove) {
                compound->removeChildShapeByIndex(i);
                shapeRemoved = true;
                std::cout << "[COLLISION] Removed cube collision shape at (" 
                          << localPos.x << "," << localPos.y << "," << localPos.z << ")" << std::endl;
                break;
            }
        }
        
        if (shapeRemoved) {
            // Mark shape as no longer in compound so it can be safely deleted
            cubeIt->second->isInCompound = false;
            // Manually delete the shape now since we removed it from compound
            delete cubeIt->second->shape;
            cubeIt->second->shape = nullptr;
        }
        
        // Remove from tracking
        cubeCollisionMap.erase(cubeIt);
        debugCollisionShapeCount--;
        debugCubeShapeCount--;
    }
    
    // Check for subcube collision shapes at this position
    if (subcubeGroupPositions.count(localPos)) {
        std::vector<SubcubeCollisionKey> subcubesToRemove;
        
        // Find all subcubes at this parent position
        for (auto it = subcubeCollisionMap.begin(); it != subcubeCollisionMap.end(); ++it) {
            if (it->first.parentPos == localPos) {
                subcubesToRemove.push_back(it->first);
            }
        }
        
        std::cout << "[COLLISION] Removing " << subcubesToRemove.size() 
                  << " subcube collision shapes at (" << localPos.x << "," << localPos.y << "," << localPos.z << ")" << std::endl;
        
        // Remove each subcube collision shape
        for (const auto& key : subcubesToRemove) {
            auto subcubeIt = subcubeCollisionMap.find(key);
            if (subcubeIt != subcubeCollisionMap.end()) {
                btCollisionShape* shapeToRemove = subcubeIt->second->shape;
                bool shapeRemoved = false;
                
                // Remove from compound shape
                for (int i = compound->getNumChildShapes() - 1; i >= 0; i--) {
                    if (compound->getChildShape(i) == shapeToRemove) {
                        compound->removeChildShapeByIndex(i);
                        shapeRemoved = true;
                        std::cout << "[COLLISION] Removed subcube collision shape at local (" 
                                  << key.subcubePos.x << "," << key.subcubePos.y << "," << key.subcubePos.z << ")" << std::endl;
                        break;
                    }
                }
                
                if (shapeRemoved) {
                    // Mark shape as no longer in compound so it can be safely deleted
                    subcubeIt->second->isInCompound = false;
                    // Manually delete the shape now since we removed it from compound
                    delete subcubeIt->second->shape;
                    subcubeIt->second->shape = nullptr;
                }
                
                // Remove from tracking
                subcubeCollisionMap.erase(subcubeIt);
                debugCollisionShapeCount--;
                debugSubcubeShapeCount--;
            }
        }
        
        // Remove from subcube group tracking if no more subcubes at this position
        if (subcubesToRemove.size() > 0) {
            subcubeGroupPositions.erase(localPos);
        }
    }
    
    // CRITICAL: Immediately update collision geometry after removal
    if (chunkPhysicsBody) {
        // Recalculate the compound shape's AABB after child removal
        compound->recalculateLocalAabb();
        
        // Force immediate physics world synchronization
        if (physicsWorld && physicsWorld->getWorld()) {
            chunkPhysicsBody->activate(true);
            physicsWorld->getWorld()->updateSingleAabb(chunkPhysicsBody);
        }
    }
    
    std::cout << "[COLLISION] Total collision shapes remaining: " << debugCollisionShapeCount 
              << " (" << debugCubeShapeCount << " cubes, " << debugSubcubeShapeCount << " subcubes)" << std::endl;
}

void Chunk::batchUpdateCollisions() {
    if (!collisionNeedsUpdate) return;
    
    // Only rebuild if we don't have any collision shapes yet
    if (cubeCollisionMap.empty() && subcubeCollisionMap.empty() && chunkCollisionShape) {
        buildInitialCollisionShapes();
    }
    
    collisionNeedsUpdate = false;
}

// Helper method to check if a cube has exposed faces (for collision optimization)
bool Chunk::hasExposedFaces(const glm::ivec3& localPos) const {
    // Same logic as in generateMergedCollisionBoxes()
    glm::ivec3 neighbors[6] = {
        localPos + glm::ivec3(0, 0, 1),   // front (+Z)
        localPos + glm::ivec3(0, 0, -1),  // back (-Z)
        localPos + glm::ivec3(1, 0, 0),   // right (+X)
        localPos + glm::ivec3(-1, 0, 0),  // left (-X)
        localPos + glm::ivec3(0, 1, 0),   // top (+Y)
        localPos + glm::ivec3(0, -1, 0)   // bottom (-Y)
    };
    
    for (int faceID = 0; faceID < 6; ++faceID) {
        glm::ivec3 neighborPos = neighbors[faceID];
        
        // Face is exposed if neighbor is outside chunk bounds OR if no visible cube at neighbor position
        if (neighborPos.x < 0 || neighborPos.x >= 32 ||
            neighborPos.y < 0 || neighborPos.y >= 32 ||
            neighborPos.z < 0 || neighborPos.z >= 32) {
            return true; // Edge of chunk - exposed
        } else {
            const Cube* neighborCube = getCubeAt(neighborPos);
            if (!neighborCube || !neighborCube->isVisible()) {
                return true; // No occluding neighbor - exposed
            }
        }
    }
    
    return false; // All faces are occluded
}

void Chunk::buildInitialCollisionShapes() {
    btCompoundShape* compound = static_cast<btCompoundShape*>(chunkCollisionShape);
    if (!compound) return;
    
    // Clear existing tracking - shapes auto-delete when shared_ptrs are destroyed
    cubeCollisionMap.clear();
    subcubeCollisionMap.clear();
    subcubeGroupPositions.clear();
    
    // Reset debug counters
    debugCollisionShapeCount = 0;
    debugCubeShapeCount = 0;
    debugSubcubeShapeCount = 0;
    
    // Remove all existing children from compound shape
    while (compound->getNumChildShapes() > 0) {
        compound->removeChildShapeByIndex(0);
    }
    
    // Build collision shapes for visible cubes that have exposed faces
    for (size_t i = 0; i < cubes.size(); ++i) {
        const Cube* cube = cubes[i];
        
        // Skip deleted cubes (nullptr) or hidden cubes (subdivided)
        if (!cube || !cube->isVisible()) {
            continue;
        }
        
        // Get cube's local position within chunk
        glm::ivec3 localPos = indexToLocal(i);
        
        // Only create collision shape if cube has exposed faces (performance optimization)
        if (hasExposedFaces(localPos)) {
            glm::vec3 cubeCenter = glm::vec3(worldOrigin) + glm::vec3(localPos) + glm::vec3(0.5f);
            
            btBoxShape* boxShape = new btBoxShape(btVector3(0.5f, 0.5f, 0.5f));
            btTransform transform;
            transform.setIdentity();
            transform.setOrigin(btVector3(cubeCenter.x, cubeCenter.y, cubeCenter.z));
            
            compound->addChildShape(transform, boxShape);
            
            // Track with reference counting - shape is now managed by compound
            auto shapeInfo = std::make_shared<CollisionShapeInfo>(boxShape, CollisionShapeInfo::CUBE);
            shapeInfo->isInCompound = true; // Shape is now owned by Bullet compound
            cubeCollisionMap[localPos] = shapeInfo;
            
            debugCollisionShapeCount++;
            debugCubeShapeCount++;
        }
    }
    
    // Build collision shapes for static subcubes with individual tracking
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
        
        // Calculate subcube center
        glm::vec3 parentCenter = glm::vec3(worldOrigin) + glm::vec3(parentLocalPos) + glm::vec3(0.5f);
        glm::vec3 subcubeOffset = (glm::vec3(localPos) - glm::vec3(1.0f)) * (1.0f/3.0f);
        glm::vec3 subcubeCenter = parentCenter + subcubeOffset;
        
        btBoxShape* boxShape = new btBoxShape(btVector3(1.0f/6.0f, 1.0f/6.0f, 1.0f/6.0f));
        btTransform transform;
        transform.setIdentity();
        transform.setOrigin(btVector3(subcubeCenter.x, subcubeCenter.y, subcubeCenter.z));
        
        compound->addChildShape(transform, boxShape);
        
        // Track individual subcube with reference counting - shape is now managed by compound
        SubcubeCollisionKey key{parentLocalPos, localPos};
        auto shapeInfo = std::make_shared<CollisionShapeInfo>(boxShape, CollisionShapeInfo::SUBCUBE);
        shapeInfo->isInCompound = true; // Shape is now owned by Bullet compound
        subcubeCollisionMap[key] = shapeInfo;
        
        // Mark position as having subcube group
        subcubeGroupPositions.insert(parentLocalPos);
        
        debugCollisionShapeCount++;
        debugSubcubeShapeCount++;
    }
    
    std::cout << "[COLLISION] Built " << debugCollisionShapeCount << " initial collision shapes ("
              << debugCubeShapeCount << " cubes, " << debugSubcubeShapeCount << " subcubes) with improved memory management" << std::endl;
}

void Chunk::updateNeighborCollisionShapes(const glm::ivec3& localPos) {
    // Check all 6 neighboring positions that might now be exposed
    glm::ivec3 neighbors[6] = {
        localPos + glm::ivec3(0, 0, 1),   // front (+Z)
        localPos + glm::ivec3(0, 0, -1),  // back (-Z)
        localPos + glm::ivec3(1, 0, 0),   // right (+X)
        localPos + glm::ivec3(-1, 0, 0),  // left (-X)
        localPos + glm::ivec3(0, 1, 0),   // top (+Y)
        localPos + glm::ivec3(0, -1, 0)   // bottom (-Y)
    };
    
    for (int i = 0; i < 6; ++i) {
        glm::ivec3 neighborPos = neighbors[i];
        
        // Skip neighbors outside chunk bounds
        if (neighborPos.x < 0 || neighborPos.x >= 32 ||
            neighborPos.y < 0 || neighborPos.y >= 32 ||
            neighborPos.z < 0 || neighborPos.z >= 32) {
            continue;
        }
        
        // Check if neighbor cube exists and is visible
        const Cube* neighborCube = getCubeAt(neighborPos);
        if (!neighborCube || !neighborCube->isVisible()) {
            continue; // No cube to update
        }
        
        // Check if this neighbor now has exposed faces (due to the removal)
        bool hadCollisionShape = (cubeCollisionMap.find(neighborPos) != cubeCollisionMap.end()) || 
                                 (subcubeGroupPositions.count(neighborPos) > 0);
        bool shouldHaveCollisionShape = hasExposedFaces(neighborPos);
        
        if (!hadCollisionShape && shouldHaveCollisionShape) {
            // Neighbor cube is now exposed - add collision shape
            fastAddCollisionAt(neighborPos);
            std::cout << "[NEIGHBOR] Added collision shape for newly exposed cube at (" 
                      << neighborPos.x << "," << neighborPos.y << "," << neighborPos.z << ")" << std::endl;
        } else if (hadCollisionShape && !shouldHaveCollisionShape) {
            // Neighbor cube is no longer exposed (shouldn't happen when removing, but handle it)
            fastRemoveCollisionAt(neighborPos);
            std::cout << "[NEIGHBOR] Removed collision shape for no longer exposed cube at (" 
                      << neighborPos.x << "," << neighborPos.y << "," << neighborPos.z << ")" << std::endl;
        }
        // If hadCollisionShape && shouldHaveCollisionShape, no change needed
        // If !hadCollisionShape && !shouldHaveCollisionShape, no change needed
    }
}

void Chunk::endBulkOperation() {
    if (!isInBulkOperation) return;
    
    std::cout << "[CHUNK] Ending bulk operation - building complete collision system" << std::endl;
    
    // Turn off bulk operation flag
    isInBulkOperation = false;
    
    // Now rebuild the entire collision system properly
    if (chunkCollisionShape) {
        buildInitialCollisionShapes();
    }
}

std::vector<Chunk::CollisionBox> Chunk::generateMergedCollisionBoxes() {
    std::vector<CollisionBox> boxes;
    
    // OPTIMIZED APPROACH: Only create collision shapes for cubes with exposed faces
    // This dramatically reduces collision complexity from ~32K to typically <1K collision boxes
    std::cout << "[COLLISION] Building collision shapes only for exposed cubes (huge optimization!)" << std::endl;
    
    // =========================================================================
    // PHASE 1: Process regular cubes (only those with exposed faces)
    // =========================================================================
    for (size_t i = 0; i < cubes.size(); ++i) {
        const Cube* cube = cubes[i];
        
        // Skip deleted cubes (nullptr) or hidden cubes (subdivided)
        if (!cube || !cube->isVisible()) {
            continue;
        }
        
        // Get cube's local position within chunk
        glm::ivec3 localPos = indexToLocal(i);
        
        // CRITICAL OPTIMIZATION: Only create collision shape if cube has exposed faces
        bool hasExposedFace = false;
        
        // Check all 6 faces for exposure (same logic as rebuildFaces)
        glm::ivec3 neighbors[6] = {
            localPos + glm::ivec3(0, 0, 1),   // front (+Z)
            localPos + glm::ivec3(0, 0, -1),  // back (-Z)
            localPos + glm::ivec3(1, 0, 0),   // right (+X)
            localPos + glm::ivec3(-1, 0, 0),  // left (-X)
            localPos + glm::ivec3(0, 1, 0),   // top (+Y)
            localPos + glm::ivec3(0, -1, 0)   // bottom (-Y)
        };
        
        for (int faceID = 0; faceID < 6; ++faceID) {
            glm::ivec3 neighborPos = neighbors[faceID];
            
            // Face is exposed if neighbor is outside chunk bounds OR if no visible cube at neighbor position
            if (neighborPos.x < 0 || neighborPos.x >= 32 ||
                neighborPos.y < 0 || neighborPos.y >= 32 ||
                neighborPos.z < 0 || neighborPos.z >= 32) {
                hasExposedFace = true; // Edge of chunk
                break;
            } else {
                const Cube* neighborCube = getCubeAt(neighborPos);
                if (!neighborCube || !neighborCube->isVisible()) {
                    hasExposedFace = true; // No occluding neighbor
                    break;
                }
            }
        }
        
        // Only create collision box for cubes with at least one exposed face
        if (hasExposedFace) {
            glm::vec3 cubeCenter = glm::vec3(worldOrigin) + glm::vec3(localPos) + glm::vec3(0.5f);
            glm::vec3 cubeHalfExtents(0.5f);
            boxes.emplace_back(cubeCenter, cubeHalfExtents);
        }
    }
    
    // =========================================================================
    // PHASE 2: Process static subcubes (only those with exposed faces)
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
        
        // OPTIMIZATION: For subdivided cubes, we could also check for exposed faces,
        // but since subcubes are typically created when a cube is broken/interacted with,
        // they're more likely to need collision detection. Keep all subcubes for now.
        
        // Calculate subcube world center position
        glm::vec3 parentCenter = glm::vec3(worldOrigin) + glm::vec3(parentLocalPos) + glm::vec3(0.5f);
        glm::vec3 subcubeOffset = (glm::vec3(localPos) - glm::vec3(1.0f)) * (1.0f/3.0f);
        glm::vec3 subcubeCenter = parentCenter + subcubeOffset;
        glm::vec3 subcubeHalfExtents(1.0f/6.0f); // 1/3 cube size -> 1/6 half-extents
        
        boxes.emplace_back(subcubeCenter, subcubeHalfExtents);
    }
    
    std::cout << "[COLLISION] MASSIVE OPTIMIZATION: Generated only " << boxes.size() 
              << " collision boxes (was ~" << cubes.size() << " before)" << std::endl;
    std::cout << "[COLLISION] Performance improvement: " 
              << (cubes.size() > 0 ? (100.0f * (cubes.size() - boxes.size()) / cubes.size()) : 0.0f) 
              << "% reduction in collision shapes!" << std::endl;
    
    return boxes;
}

// DEBUG: Collision shape validation and debugging methods
// Validates consistency between Bullet compound shapes and our tracking data structures
// Helps detect memory management issues and tracking inconsistencies
void Chunk::validateCollisionShapes() const {
    if (!chunkCollisionShape) {
        std::cout << "[COLLISION VALIDATION] No compound collision shape exists" << std::endl;
        return;
    }
    
    btCompoundShape* compound = static_cast<btCompoundShape*>(chunkCollisionShape);
    int actualShapeCount = compound->getNumChildShapes();
    
    std::cout << "[COLLISION VALIDATION] Chunk at (" << worldOrigin.x << "," << worldOrigin.y << "," << worldOrigin.z << ")" << std::endl;
    std::cout << "  Compound shape has " << actualShapeCount << " child shapes" << std::endl;
    std::cout << "  Tracking " << cubeCollisionMap.size() << " cube shapes" << std::endl;
    std::cout << "  Tracking " << subcubeCollisionMap.size() << " subcube shapes" << std::endl;
    std::cout << "  Debug counters: " << debugCollisionShapeCount << " total ("
              << debugCubeShapeCount << " cubes, " << debugSubcubeShapeCount << " subcubes)" << std::endl;
    
    // Validate that tracked count matches compound shape count
    size_t expectedShapeCount = cubeCollisionMap.size() + subcubeCollisionMap.size();
    if (expectedShapeCount != static_cast<size_t>(actualShapeCount)) {
        std::cout << "[COLLISION ERROR] Shape count mismatch! Expected: " << expectedShapeCount 
                  << ", Actual: " << actualShapeCount << std::endl;
    }
    
    if (debugCollisionShapeCount != expectedShapeCount) {
        std::cout << "[COLLISION ERROR] Debug counter mismatch! Counter: " << debugCollisionShapeCount 
                  << ", Tracked: " << expectedShapeCount << std::endl;
    }
    
    // Validate subcube group positions
    std::cout << "  Subcube group positions: " << subcubeGroupPositions.size() << std::endl;
    for (const auto& pos : subcubeGroupPositions) {
        int subcubeCount = 0;
        for (const auto& pair : subcubeCollisionMap) {
            if (pair.first.parentPos == pos) {
                subcubeCount++;
            }
        }
        std::cout << "    Position (" << pos.x << "," << pos.y << "," << pos.z << ") has " << subcubeCount << " subcubes" << std::endl;
    }
}

void Chunk::debugLogCollisionShapes() const {
    std::cout << "[COLLISION DEBUG] Detailed collision shape information:" << std::endl;
    std::cout << "  Chunk origin: (" << worldOrigin.x << "," << worldOrigin.y << "," << worldOrigin.z << ")" << std::endl;
    
    // Log cube collision shapes
    std::cout << "  Cube collision shapes (" << cubeCollisionMap.size() << "):" << std::endl;
    for (const auto& pair : cubeCollisionMap) {
        const glm::ivec3& pos = pair.first;
        const auto& shapeInfo = pair.second;
        std::cout << "    Cube at (" << pos.x << "," << pos.y << "," << pos.z 
                  << ") - Shape: " << shapeInfo->shape 
                  << ", Refs: " << shapeInfo.use_count() << std::endl;
    }
    
    // Log subcube collision shapes
    std::cout << "  Subcube collision shapes (" << subcubeCollisionMap.size() << "):" << std::endl;
    for (const auto& pair : subcubeCollisionMap) {
        const SubcubeCollisionKey& key = pair.first;
        const auto& shapeInfo = pair.second;
        std::cout << "    Subcube at parent (" << key.parentPos.x << "," << key.parentPos.y << "," << key.parentPos.z 
                  << ") local (" << key.subcubePos.x << "," << key.subcubePos.y << "," << key.subcubePos.z 
                  << ") - Shape: " << shapeInfo->shape 
                  << ", Refs: " << shapeInfo.use_count() << std::endl;
    }
}

} // namespace VulkanCube
