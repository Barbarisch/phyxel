#include "core/Chunk.h"
#include "core/ChunkManager.h"
#include "physics/PhysicsWorld.h"
#include "physics/CollisionSpatialGrid.h"
#include "utils/Logger.h"
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
    , collisionGrid(std::move(other.collisionGrid))
    , collisionNeedsUpdate(other.collisionNeedsUpdate)
    , isInBulkOperation(other.isInBulkOperation) {
    
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
        collisionGrid = std::move(other.collisionGrid);
        collisionNeedsUpdate = other.collisionNeedsUpdate;
        isInBulkOperation = other.isInBulkOperation;
        
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
    removeCollisionEntities(localPos);
    
    // CRITICAL: Update collision shapes of neighboring cubes that might now be exposed
    updateNeighborCollisionShapes(localPos);
    
    // Mark chunk as dirty for smart saving
    setDirty(true);
    LOG_DEBUG_FMT("Chunk", "Removed cube at local pos (" << localPos.x << "," << localPos.y << "," << localPos.z 
              << ") - Chunk now DIRTY for save");
    
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
    addCollisionEntity(localPos);
    
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
    
    // Clear any existing microcubes
    for (Microcube* microcube : staticMicrocubes) {
        delete microcube;
    }
    staticMicrocubes.clear();
    
    // Set bulk operation flag to prevent neighbor collision updates during loading
    isInBulkOperation = true;
    
    LOG_DEBUG_FMT("Chunk", "Initialized chunk at origin (" 
              << worldOrigin.x << "," << worldOrigin.y << "," << worldOrigin.z 
              << ") for database loading");
}

void Chunk::rebuildFaces() {
    // Call the cross-chunk version without a neighbor lookup function
    // This will only do intra-chunk culling
    rebuildFaces(nullptr);
}

void Chunk::rebuildFaces(const NeighborLookupFunc& getNeighborCube) {
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
                
                // Neighbor within chunk - check directly
                const Cube* neighborCube = getCubeAt(neighborPos);
                if (neighborCube && neighborCube->isVisible()) {
                    faceVisible[faceID] = false;
                }
            } else if (getNeighborCube) {
                // Neighbor outside chunk - use cross-chunk lookup if available
                glm::ivec3 neighborWorldPos = worldOrigin + neighborPos;
                const Cube* neighborCube = getNeighborCube(neighborWorldPos);
                if (neighborCube && neighborCube->isVisible()) {
                    faceVisible[faceID] = false;
                }
            }
            // If no cross-chunk lookup provided, face at boundary remains visible
        }
        
        // Generate instance data for each visible face of the cube
        for (int faceID = 0; faceID < 6; ++faceID) {
            if (faceVisible[faceID]) {
                InstanceData faceInstance;
                
                // Pack cube position and face ID using new layout
                // Scale level 0 = regular cube
                const glm::ivec3& cubePos = cube->getPosition();
                faceInstance.packedData = VulkanCube::InstanceDataUtils::packCubeFaceData(
                    cubePos.x, cubePos.y, cubePos.z, faceID
                );
                
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
                
                // Pack parent cube position, face ID, and subcube local position using new layout
                // Scale level 1 = subcube
                faceInstance.packedData = VulkanCube::InstanceDataUtils::packSubcubeFaceData(
                    parentChunkPos.x, parentChunkPos.y, parentChunkPos.z,
                    faceID,
                    localPos.x, localPos.y, localPos.z
                );
                
                // Assign texture based on face ID
                faceInstance.textureIndex = VulkanCube::TextureConstants::getTextureIndexForFace(faceID);
                faceInstance.reserved = 0;
                faces.push_back(faceInstance);
            }
        }
    }
    
    // ========================================================================
    // PHASE 3: Process microcubes (from subdivided subcubes)
    // ========================================================================
    for (const Microcube* microcube : staticMicrocubes) {
        // Skip broken or hidden microcubes
        if (!microcube || microcube->isBroken() || !microcube->isVisible()) {
            continue;
        }
        
        // Get microcube properties
        glm::ivec3 parentPos = microcube->getParentCubePosition();     // Parent cube's world position
        glm::ivec3 subcubePos = microcube->getSubcubeLocalPosition();  // 0-2 for each axis within parent cube
        glm::ivec3 microcubePos = microcube->getMicrocubeLocalPosition(); // 0-2 for each axis within parent subcube
        
        // Convert parent world position to chunk-relative position
        glm::ivec3 parentChunkPos = parentPos - worldOrigin;
        
        // Validate parent position is within chunk bounds
        if (parentChunkPos.x < 0 || parentChunkPos.x >= 32 ||
            parentChunkPos.y < 0 || parentChunkPos.y >= 32 ||
            parentChunkPos.z < 0 || parentChunkPos.z >= 32) {
            continue; // Skip microcubes with invalid parent positions
        }
        
        // Validate subcube position
        if (subcubePos.x < 0 || subcubePos.x >= 3 ||
            subcubePos.y < 0 || subcubePos.y >= 3 ||
            subcubePos.z < 0 || subcubePos.z >= 3) {
            continue;
        }
        
        // Validate microcube position
        if (microcubePos.x < 0 || microcubePos.x >= 3 ||
            microcubePos.y < 0 || microcubePos.y >= 3 ||
            microcubePos.z < 0 || microcubePos.z >= 3) {
            continue;
        }
        
        // For now, assume all microcube faces are visible (can optimize with culling later)
        bool faceVisible[6] = {true, true, true, true, true, true};
        
        // Generate instance data for each visible face of the microcube
        for (int faceID = 0; faceID < 6; ++faceID) {
            if (faceVisible[faceID]) {
                InstanceData faceInstance;
                
                // Pack parent cube position, face ID, subcube position, and microcube position
                // Scale level 2 = microcube
                faceInstance.packedData = VulkanCube::InstanceDataUtils::packMicrocubeFaceData(
                    parentChunkPos.x, parentChunkPos.y, parentChunkPos.z,
                    faceID,
                    subcubePos.x, subcubePos.y, subcubePos.z,
                    microcubePos.x, microcubePos.y, microcubePos.z
                );
                
                // Use placeholder texture for microcubes
                faceInstance.textureIndex = VulkanCube::TextureConstants::PLACEHOLDER_TEXTURE_INDEX;
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
// Microcube Access Functions
// =============================================================================

Microcube* Chunk::getMicrocubeAt(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos) {
    auto cubeIt = microcubeMap.find(cubePos);
    if (cubeIt != microcubeMap.end()) {
        auto subcubeIt = cubeIt->second.find(subcubePos);
        if (subcubeIt != cubeIt->second.end()) {
            auto microcubeIt = subcubeIt->second.find(microcubePos);
            if (microcubeIt != subcubeIt->second.end()) {
                return microcubeIt->second;
            }
        }
    }
    return nullptr;
}

const Microcube* Chunk::getMicrocubeAt(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos) const {
    auto cubeIt = microcubeMap.find(cubePos);
    if (cubeIt != microcubeMap.end()) {
        auto subcubeIt = cubeIt->second.find(subcubePos);
        if (subcubeIt != cubeIt->second.end()) {
            auto microcubeIt = subcubeIt->second.find(microcubePos);
            if (microcubeIt != subcubeIt->second.end()) {
                return microcubeIt->second;
            }
        }
    }
    return nullptr;
}

std::vector<Microcube*> Chunk::getMicrocubesAt(const glm::ivec3& cubePos, const glm::ivec3& subcubePos) {
    std::vector<Microcube*> result;
    
    // Use hash map lookup instead of linear search - O(1) instead of O(n)
    auto cubeIt = microcubeMap.find(cubePos);
    if (cubeIt != microcubeMap.end()) {
        auto subcubeIt = cubeIt->second.find(subcubePos);
        if (subcubeIt != cubeIt->second.end()) {
            // Found the subcube level, now collect all microcubes (0-27)
            for (const auto& microcubePair : subcubeIt->second) {
                if (microcubePair.second) {
                    result.push_back(microcubePair.second);
                }
            }
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
            LOG_TRACE_FMT("Chunk", "Removed subcubeMap entry at (" << localPos.x << "," << localPos.y << "," << localPos.z << ") - no subcubes remain");
        }
    }
}

void Chunk::addMicrocubeToMaps(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos, Microcube* microcube) {
    if (microcube) {
        microcubeMap[cubePos][subcubePos][microcubePos] = microcube;
        // Note: voxelTypeMap is already set to SUBDIVIDED by the parent subdivision
    }
}

void Chunk::removeMicrocubeFromMaps(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos) {
    auto cubeIt = microcubeMap.find(cubePos);
    if (cubeIt != microcubeMap.end()) {
        auto subcubeIt = cubeIt->second.find(subcubePos);
        if (subcubeIt != cubeIt->second.end()) {
            subcubeIt->second.erase(microcubePos);
            
            if (subcubeIt->second.empty()) {
                cubeIt->second.erase(subcubePos);
                LOG_TRACE_FMT("Chunk", "Removed microcubeMap entry at cube(" << cubePos.x << "," << cubePos.y << "," << cubePos.z 
                          << ") subcube(" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z << ") - no microcubes remain");
            }
            
            if (cubeIt->second.empty()) {
                microcubeMap.erase(cubePos);
            }
        }
    }
}

void Chunk::initializeVoxelMaps() {
    // Clear existing maps
    cubeMap.clear();
    subcubeMap.clear();
    microcubeMap.clear();
    voxelTypeMap.clear();
    
    LOG_DEBUG_FMT("Chunk", "Initializing voxel maps for chunk at origin (" 
              << worldOrigin.x << "," << worldOrigin.y << "," << worldOrigin.z << ")");
    
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
    
    // Build microcube map from existing vector data
    for (Microcube* microcube : staticMicrocubes) {
        if (microcube) {
            glm::ivec3 parentWorldPos = microcube->getParentCubePosition();
            glm::ivec3 localPos = parentWorldPos - worldOrigin;
            glm::ivec3 subcubePos = microcube->getSubcubeLocalPosition();
            glm::ivec3 microcubePos = microcube->getMicrocubeLocalPosition();
            
            // Validate that this microcube belongs to this chunk
            if (isValidLocalPosition(localPos)) {
                microcubeMap[localPos][subcubePos][microcubePos] = microcube;
                voxelTypeMap[localPos] = VoxelLocation::SUBDIVIDED;
            }
        }
    }
    
    LOG_DEBUG_FMT("Chunk", "Voxel maps initialized: " 
              << cubeMap.size() << " cubes, "
              << subcubeMap.size() << " subdivided positions, "
              << microcubeMap.size() << " microcube positions, "
              << voxelTypeMap.size() << " total voxels");
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
        LOG_DEBUG_FMT("Chunk", "Completely removed parent cube at (" 
                  << localPos.x << "," << localPos.y << "," << localPos.z 
                  << ") - replaced by 27 subcubes");
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
    addCollisionEntity(parentPos);
    
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
                LOG_TRACE_FMT("Chunk", "No subcubes remain at parent pos (" 
                          << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                          << ") - removing collision shape");
                removeCollisionEntities(parentPos);
                
                // ARCHITECTURAL CHANGE: Do NOT restore parent cube - leave position empty
                // The parent cube was deleted during subdivision, so position should become empty
                voxelTypeMap.erase(parentPos);
                LOG_DEBUG_FMT("Chunk", "[VOXEL MAP] Position now empty at (" 
                          << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                          << ") - all subcubes removed, no cube to restore");
            } else {
                // Still have subcubes - update collision shape to reflect remaining subcubes
                LOG_DEBUG_FMT("Chunk", "[COLLISION] " << remainingSubcubes.size() 
                          << " subcubes remain at parent pos (" 
                          << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                          << ") - updating collision shape");
                // Remove old shape and add new one to reflect current subcube configuration
                removeCollisionEntities(parentPos);
                addCollisionEntity(parentPos);
                
                // CRITICAL: Ensure voxelTypeMap shows SUBDIVIDED since subcubes remain
                voxelTypeMap[parentPos] = VoxelLocation::SUBDIVIDED;
                LOG_DEBUG_FMT("Chunk", "[VOXEL MAP] Maintained SUBDIVIDED type at (" 
                          << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                          << ") - " << remainingSubcubes.size() << " subcubes remain");
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
        LOG_DEBUG_FMT("Chunk", "[CHUNK] Cleared subdivision at local pos (" << localPos.x << "," << localPos.y << "," << localPos.z 
                  << ") - position now empty");
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
    LOG_DEBUG_FMT("Chunk", "[SUBCUBE BREAKING] Attempting to break subcube at parent (" 
                  << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                  << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z 
                  << ") - searching through " << staticSubcubes.size() << " static subcubes");
    
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
            LOG_DEBUG("Chunk", "[INCREMENTAL] BEFORE: Using proper subcube removal to update all data structures");
            bool removed = removeSubcube(parentPos, subcubePos);
            if (!removed) {
                LOG_ERROR("Chunk", "[ERROR] Failed to remove subcube from data structures");
                return false;
            }
            // NEW: Fast collision update (already done in removeSubcube)
            batchUpdateCollisions();
            LOG_DEBUG("Chunk", "[INCREMENTAL] AFTER: All data structures updated and collision shape updated incrementally");
            
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
                LOG_DEBUG("Chunk", "[SUBCUBE PHYSICS] Created physics body for subcube (no forces applied - gravity only)");
                
                // Enable gravity for natural falling behavior
                if (rigidBody) {
                    rigidBody->setGravity(btVector3(0, -9.81f, 0));
                }
            }
            
            // Transfer the dynamic subcube directly to global system
            if (chunkManager) {
                // The dynamicSubcube is already properly configured, just transfer it
                chunkManager->addGlobalDynamicSubcube(std::move(dynamicSubcube));
                
                LOG_DEBUG("Chunk", "[GLOBAL TRANSFER] Moved broken subcube directly to global dynamic system (safe transfer)");
            } else {
                LOG_ERROR("Chunk", "[ERROR] No ChunkManager provided - cannot transfer to global system");
            }
            
            // Note: No need to delete subcube - it was already properly removed by removeSubcube()
            
            // Rebuild static faces only (no more dynamic faces in chunks)
            rebuildFaces();
            needsUpdate = true;
            
            LOG_DEBUG_FMT("Chunk", "[CHUNK] Broke subcube at parent pos (" 
                      << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                      << ") subcube pos (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z 
                      << ") - transferred to global system safely");
            return true;
        }
        ++it;
    }
    
    LOG_ERROR_FMT("Chunk", "[SUBCUBE BREAKING] Subcube NOT FOUND in static list at parent (" 
                  << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                  << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z << ")");
    
    // Debug: Check if this position has microcubes instead
    auto microcubes = getMicrocubesAt(parentPos, subcubePos);
    if (!microcubes.empty()) {
        LOG_ERROR_FMT("Chunk", "[SUBCUBE BREAKING] DEBUG: Found " << microcubes.size() 
                      << " microcubes at this subcube position - subcube may have been subdivided");
    }
    
    // Debug: Check all subcubes at this parent position
    auto allSubcubes = getSubcubesAt(parentPos);
    LOG_ERROR_FMT("Chunk", "[SUBCUBE BREAKING] DEBUG: Total subcubes at parent position: " << allSubcubes.size());
    for (auto* sc : allSubcubes) {
        if (sc) {
            glm::ivec3 scPos = sc->getLocalPosition();
            LOG_ERROR_FMT("Chunk", "[SUBCUBE BREAKING] DEBUG:   - Subcube at (" 
                          << scPos.x << "," << scPos.y << "," << scPos.z << ")");
        }
    }
    
    return false; // Subcube not found in static list
}

void Chunk::createChunkPhysicsBody() {
    if (!physicsWorld) {
        LOG_DEBUG("Chunk", "[CHUNK] No physics world available for chunk physics body creation");
        return;
    }

    if (chunkPhysicsBody) {
        LOG_DEBUG("Chunk", "[CHUNK] Chunk physics body already exists, skipping creation");
        return;
    }

    LOG_DEBUG_FMT("Chunk", "[CHUNK] Creating optimized compound collision shape for chunk at (" 
              << worldOrigin.x << "," << worldOrigin.y << "," << worldOrigin.z << ")");

    // Create compound shape with dynamic AABB tree for efficient culling
    btCompoundShape* chunkCompound = new btCompoundShape(true);
    chunkCollisionShape = chunkCompound;
    
    // Clear existing collision tracking (in case of rebuild) - shapes auto-delete with shared_ptr
    collisionGrid.clear();
    
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
    
    LOG_DEBUG_FMT("Chunk", "[CHUNK] Spatial collision system initialized with " << collisionGrid.getTotalEntityCount() 
              << " tracked entities (" << collisionGrid.getCubeEntityCount() << " cubes, " 
              << collisionGrid.getSubcubeEntityCount() << " subcubes)");
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
        
        LOG_DEBUG_FMT("Chunk", "[INCREMENTAL] Spatial collision updates complete with physics sync - maintaining " 
                  << collisionGrid.getTotalEntityCount() << " entities (" << collisionGrid.getCubeEntityCount() 
                  << " cubes, " << collisionGrid.getSubcubeEntityCount() << " subcubes)");
    } else {
        LOG_DEBUG("Chunk", "[INCREMENTAL] No collision updates needed");
    }
}

void Chunk::forcePhysicsRebuild() {
    if (!physicsWorld || !chunkPhysicsBody) return;
    
    LOG_DEBUG("Chunk", "[COMPOUND SHAPE] Force rebuilding compound shape to remove static collision");
    
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
    
    LOG_DEBUG("Chunk", "[COMPOUND SHAPE] Compound shape rebuilt - static collision removed");
}

void Chunk::cleanupPhysicsResources() {
    // Clean up spatial collision grid - entities auto-delete when shared_ptrs are destroyed
    LOG_DEBUG_FMT("Chunk", "[COLLISION CLEANUP] Before cleanup: " << collisionGrid.getTotalEntityCount() << " total entities ("
              << collisionGrid.getCubeEntityCount() << " cubes, " << collisionGrid.getSubcubeEntityCount() << " subcubes)");
    
    // Clear spatial grid - O(1) operation that releases all entity references
    collisionGrid.clear();
    collisionNeedsUpdate = false;
    
    LOG_DEBUG_FMT("Chunk", "[COLLISION CLEANUP] After cleanup: " << collisionGrid.getTotalEntityCount() << " total entities ("
              << collisionGrid.getCubeEntityCount() << " cubes, " << collisionGrid.getSubcubeEntityCount() << " subcubes)");
    
    if (chunkPhysicsBody) {
        LOG_DEBUG("Chunk", "[CHUNK] Cleaning up chunk physics body");
        chunkPhysicsBody = nullptr;
    }
    
    if (chunkCollisionShape) {
        LOG_DEBUG("Chunk", "[CHUNK] Cleaning up chunk collision shape");
        chunkCollisionShape = nullptr;
    }
}

// ============================================================================
// COLLISION SHAPE CREATION HELPERS
// ============================================================================
// These focused helper functions each handle creation of ONE type of collision
// shape. This separation makes the code easier to test, debug, and maintain.
// Each function has a single, clear responsibility with no conditional logic.

void Chunk::createCubeCollisionShape(const glm::ivec3& localPos, btCompoundShape* compound) {
    // Calculate center position in world space
    glm::vec3 shapeCenter = glm::vec3(worldOrigin) + glm::vec3(localPos) + glm::vec3(0.5f);
    
    // Create box shape: full cube is 1.0 units, so half-extents are 0.5
    btBoxShape* boxShape = new btBoxShape(btVector3(0.5f, 0.5f, 0.5f));
    
    // Position the shape in the compound
    btTransform transform;
    transform.setIdentity();
    transform.setOrigin(btVector3(shapeCenter.x, shapeCenter.y, shapeCenter.z));
    compound->addChildShape(transform, boxShape);
    
    // Create collision entity for tracking
    auto entity = std::make_shared<Physics::CollisionSpatialGrid::CollisionEntity>(boxShape, Physics::CollisionSpatialGrid::CollisionEntity::CUBE, shapeCenter);
    entity->isInCompound = true; // Shape is now owned by Bullet compound
    
    // Add to spatial grid for O(1) lookups
    collisionGrid.addEntity(localPos, entity);
    
    LOG_TRACE_FMT("Chunk", "[COLLISION] Created cube shape at (" 
                  << localPos.x << "," << localPos.y << "," << localPos.z << ")");
}

void Chunk::createSubcubeCollisionShape(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, btCompoundShape* compound) {
    // Calculate subcube center: offset from cube center by subcube position
    // Subcube positions are (0,0,0) to (2,2,2), so we offset by -1 to center them
    constexpr float SUBCUBE_SCALE = 1.0f / 3.0f;
    glm::vec3 subcubeLocalOffset = glm::vec3(subcubePos) - glm::vec3(1.0f);
    glm::vec3 subcubeOffset = subcubeLocalOffset * SUBCUBE_SCALE;
    glm::vec3 subcubeCenter = glm::vec3(worldOrigin) + glm::vec3(cubePos) + glm::vec3(0.5f) + subcubeOffset;
    
    // Create box shape: subcube is 1/3 cube size, so half-extents are 1/6
    btBoxShape* subcubeShape = new btBoxShape(btVector3(1.0f/6.0f, 1.0f/6.0f, 1.0f/6.0f));
    
    // Position the shape in the compound
    btTransform transform;
    transform.setIdentity();
    transform.setOrigin(btVector3(subcubeCenter.x, subcubeCenter.y, subcubeCenter.z));
    compound->addChildShape(transform, subcubeShape);
    
    // Create collision entity with hierarchy tracking
    auto entity = std::make_shared<Physics::CollisionSpatialGrid::CollisionEntity>(subcubeShape, Physics::CollisionSpatialGrid::CollisionEntity::SUBCUBE, subcubeCenter, 1.0f/6.0f);
    entity->isInCompound = true;
    entity->parentChunkPos = cubePos;
    entity->subcubeLocalPos = subcubePos;
    
    // Add to spatial grid
    collisionGrid.addEntity(cubePos, entity);
    
    LOG_TRACE_FMT("Chunk", "[COLLISION] Created subcube shape at cube (" 
                  << cubePos.x << "," << cubePos.y << "," << cubePos.z 
                  << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z << ")");
}

void Chunk::createMicrocubeCollisionShape(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, 
                                         const Microcube* microcube, btCompoundShape* compound) {
    // Calculate microcube center: two-level hierarchy (cube -> subcube -> microcube)
    constexpr float SUBCUBE_SCALE = 1.0f / 3.0f;
    constexpr float MICROCUBE_SCALE = 1.0f / 9.0f;
    
    // First, offset from cube center to subcube center
    glm::vec3 subcubeLocalOffset = glm::vec3(subcubePos) - glm::vec3(1.0f);
    glm::vec3 subcubeOffset = subcubeLocalOffset * SUBCUBE_SCALE;
    
    // Then, offset from subcube center to microcube center
    glm::vec3 microcubeLocalOffset = glm::vec3(microcube->getMicrocubeLocalPosition()) - glm::vec3(1.0f);
    glm::vec3 microcubeOffset = microcubeLocalOffset * MICROCUBE_SCALE;
    
    glm::vec3 microcubeCenter = glm::vec3(worldOrigin) + glm::vec3(cubePos) + glm::vec3(0.5f) + subcubeOffset + microcubeOffset;
    
    // Create box shape: microcube is 1/9 cube size, so half-extents are 1/18
    btBoxShape* microcubeShape = new btBoxShape(btVector3(1.0f/18.0f, 1.0f/18.0f, 1.0f/18.0f));
    microcubeShape->setMargin(0.002f);
    
    // Position the shape in the compound
    btTransform transform;
    transform.setIdentity();
    transform.setOrigin(btVector3(microcubeCenter.x, microcubeCenter.y, microcubeCenter.z));
    compound->addChildShape(transform, microcubeShape);
    
    // Create collision entity with full hierarchy tracking
    auto entity = std::make_shared<Physics::CollisionSpatialGrid::CollisionEntity>(microcubeShape, Physics::CollisionSpatialGrid::CollisionEntity::SUBCUBE, microcubeCenter, 1.0f/18.0f);
    entity->isInCompound = true;
    entity->parentChunkPos = cubePos;
    entity->subcubeLocalPos = subcubePos;
    
    // Add to spatial grid
    collisionGrid.addEntity(cubePos, entity);
    
    LOG_INFO_FMT("Chunk", "[STATIC COLLISION] Created microcube shape at cube (" 
                  << cubePos.x << "," << cubePos.y << "," << cubePos.z 
                  << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z
                  << ") micro (" << microcube->getMicrocubeLocalPosition().x << "," 
                  << microcube->getMicrocubeLocalPosition().y << "," << microcube->getMicrocubeLocalPosition().z << ")");
}

// ============================================================================
// COLLISION ENTITY MANAGEMENT
// ============================================================================

// IMPROVED collision system - memory-safe reference-counted shapes with individual subcube tracking
// This method replaces the old system that used nullptr placeholders and geometric distance heuristics
// Now provides proper individual tracking for each collision shape with automatic memory management
void Chunk::addCollisionEntity(const glm::ivec3& localPos) {
    if (!chunkCollisionShape) return;
    
    btCompoundShape* compound = static_cast<btCompoundShape*>(chunkCollisionShape);
    
    // Remove any existing collision entities at this position first
    removeCollisionEntities(localPos);
    
    // Check for regular cube first
    const Cube* cube = getCubeAt(localPos);
    if (cube && cube->isVisible() && hasExposedFaces(localPos)) {
        // Delegate to focused helper function
        createCubeCollisionShape(localPos, compound);
        
        LOG_TRACE_FMT("Chunk", "[COLLISION] Added cube entity at (" 
                  << localPos.x << "," << localPos.y << "," << localPos.z 
                  << ") - Total entities: " << collisionGrid.getTotalEntityCount());
        
    } else {
        // Handle mixed state: process each subcube position individually
        // Some may be subdivided (microcubes), others may be regular subcubes
        
        for (int sx = 0; sx < 3; ++sx) {
            for (int sy = 0; sy < 3; ++sy) {
                for (int sz = 0; sz < 3; ++sz) {
                    glm::ivec3 subcubePos(sx, sy, sz);
                    
                    // Check if this subcube position has microcubes
                    auto microcubes = getMicrocubesAt(localPos, subcubePos);
                    
                    if (!microcubes.empty()) {
                        // This subcube position is subdivided - create microcube collision shapes
                        LOG_TRACE_FMT("Chunk", "[COLLISION] Creating " << microcubes.size() 
                                  << " microcube entities at cube (" << localPos.x << "," << localPos.y << "," << localPos.z 
                                  << ") subcube (" << sx << "," << sy << "," << sz << ")");
                        
                        // Delegate to helper function for each microcube
                        for (const Microcube* microcube : microcubes) {
                            createMicrocubeCollisionShape(localPos, subcubePos, microcube, compound);
                        }
                    } else {
                        // No microcubes at this position - check for regular subcube
                        auto subcubes = getStaticSubcubesAt(localPos);
                        
                        // Find the subcube at this specific subcube position
                        for (const Subcube* subcube : subcubes) {
                            if (subcube->getLocalPosition() == subcubePos) {
                                LOG_TRACE_FMT("Chunk", "[COLLISION] Creating subcube entity at cube (" 
                                          << localPos.x << "," << localPos.y << "," << localPos.z 
                                          << ") subcube (" << sx << "," << sy << "," << sz << ")");
                                
                                // Delegate to focused helper function
                                createSubcubeCollisionShape(localPos, subcubePos, compound);
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
}

void Chunk::removeCollisionEntities(const glm::ivec3& localPos) {
    if (!chunkCollisionShape) return;
    
    btCompoundShape* compound = static_cast<btCompoundShape*>(chunkCollisionShape);
    
    // Get all entities at this position - O(1) operation
    auto& entities = collisionGrid.getEntitiesAt(localPos);
    if (entities.empty()) return;
    
    LOG_TRACE_FMT("Chunk", "[COLLISION] Removing " << entities.size() 
              << " collision entities at (" << localPos.x << "," << localPos.y << "," << localPos.z << ")");
    
    // Remove each entity from the compound shape
    for (auto& entity : entities) {
        btCollisionShape* shapeToRemove = entity->shape;
        bool shapeRemoved = false;
        
        // Remove from compound shape
        for (int i = compound->getNumChildShapes() - 1; i >= 0; i--) {
            if (compound->getChildShape(i) == shapeToRemove) {
                compound->removeChildShapeByIndex(i);
                shapeRemoved = true;
                
                if (entity->isCube()) {
                    LOG_TRACE_FMT("Chunk", "[COLLISION] Removed cube entity at (" 
                              << localPos.x << "," << localPos.y << "," << localPos.z << ")");
                } else {
                    LOG_TRACE_FMT("Chunk", "[COLLISION] Removed subcube entity at local (" 
                              << entity->subcubeLocalPos.x << "," << entity->subcubeLocalPos.y << "," << entity->subcubeLocalPos.z << ")");
                }
                break;
            }
        }
        
        if (shapeRemoved) {
            // Mark shape as no longer in compound so it can be safely deleted
            entity->isInCompound = false;
            // Manually delete the shape now since we removed it from compound
            delete entity->shape;
            entity->shape = nullptr;
        }
    }
    
    // Remove all entities from spatial grid - O(1) operation
    collisionGrid.removeAllAt(localPos);
    
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
    
    LOG_TRACE_FMT("Chunk", "[COLLISION] Total collision entities remaining: " << collisionGrid.getTotalEntityCount() 
              << " (" << collisionGrid.getCubeEntityCount() << " cubes, " << collisionGrid.getSubcubeEntityCount() << " subcubes)");
}

void Chunk::batchUpdateCollisions() {
    if (!collisionNeedsUpdate) return;
    
    // Only rebuild if we don't have any collision shapes yet
    if (collisionGrid.getTotalEntityCount() == 0 && chunkCollisionShape) {
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
    
    // Clear existing spatial grid - shapes auto-delete when entities are destroyed
    collisionGrid.clear();
    
    // Remove all existing children from compound shape
    while (compound->getNumChildShapes() > 0) {
        compound->removeChildShapeByIndex(0);
    }
    
    // Reserve space for expected entities
    size_t expectedEntities = cubes.size() + staticSubcubes.size();
    collisionGrid.reserve(expectedEntities);
    
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
            
            // Create collision entity with spatial tracking
            auto entity = std::make_shared<Physics::CollisionSpatialGrid::CollisionEntity>(boxShape, Physics::CollisionSpatialGrid::CollisionEntity::CUBE, cubeCenter);
            entity->isInCompound = true; // Shape is now owned by Bullet compound
            
            // Add to spatial grid - O(1) operation
            collisionGrid.addEntity(localPos, entity);
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
        
        // Create collision entity with spatial and hierarchy data
        auto entity = std::make_shared<Physics::CollisionSpatialGrid::CollisionEntity>(boxShape, Physics::CollisionSpatialGrid::CollisionEntity::SUBCUBE, subcubeCenter, 1.0f/6.0f);
        entity->isInCompound = true; // Shape is now owned by Bullet compound
        entity->parentChunkPos = parentLocalPos;
        entity->subcubeLocalPos = localPos;
        
        // Add to spatial grid - O(1) operation
        collisionGrid.addEntity(parentLocalPos, entity);
    }
    
    // Build collision shapes for static microcubes with individual tracking
    for (const Microcube* microcube : staticMicrocubes) {
        // Skip broken or hidden microcubes
        if (!microcube || microcube->isBroken() || !microcube->isVisible()) {
            continue;
        }
        
        // Get microcube properties
        glm::ivec3 parentCubePos = microcube->getParentCubePosition();        // Parent cube's world position
        glm::ivec3 subcubePos = microcube->getSubcubeLocalPosition();         // 0-2 for subcube within cube
        glm::ivec3 microcubePos = microcube->getMicrocubeLocalPosition();     // 0-2 for microcube within subcube
        
        // Convert parent world position to chunk-relative position
        glm::ivec3 parentLocalPos = parentCubePos - worldOrigin;
        
        // Validate parent position is within chunk bounds
        if (parentLocalPos.x < 0 || parentLocalPos.x >= 32 ||
            parentLocalPos.y < 0 || parentLocalPos.y >= 32 ||
            parentLocalPos.z < 0 || parentLocalPos.z >= 32) {
            continue; // Skip microcubes with invalid parent positions
        }
        
        // Calculate microcube center with two-level hierarchy (consistent with addCollisionEntity)
        constexpr float SUBCUBE_SCALE = 1.0f / 3.0f;
        constexpr float MICROCUBE_SCALE = 1.0f / 9.0f;
        
        glm::vec3 parentCenter = glm::vec3(worldOrigin) + glm::vec3(parentLocalPos) + glm::vec3(0.5f);
        
        // Convert subcube position from 0-2 range to centered offsets
        glm::vec3 subcubeLocalOffset = glm::vec3(subcubePos) - glm::vec3(1.0f);
        glm::vec3 subcubeOffset = subcubeLocalOffset * SUBCUBE_SCALE;
        
        // Convert microcube position from 0-2 range to centered offsets within subcube
        glm::vec3 microcubeLocalOffset = glm::vec3(microcubePos) - glm::vec3(1.0f);
        glm::vec3 microcubeOffset = microcubeLocalOffset * MICROCUBE_SCALE;
        
        glm::vec3 microcubeCenter = parentCenter + subcubeOffset + microcubeOffset;
        
        // DEBUG: Log initial static microcube collision creation  
        LOG_INFO_FMT("Chunk", "[INITIAL STATIC COLLISION] Microcube at cube (" << parentLocalPos.x << "," << parentLocalPos.y << "," << parentLocalPos.z
                  << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z
                  << ") micro (" << microcubePos.x << "," << microcubePos.y << "," << microcubePos.z << ")");
        LOG_INFO_FMT("Chunk", "[INITIAL STATIC COLLISION] Center: (" << microcubeCenter.x << "," << microcubeCenter.y << "," << microcubeCenter.z << ") Half-extents: " << (1.0f/18.0f));
        
        btBoxShape* boxShape = new btBoxShape(btVector3(1.0f/18.0f, 1.0f/18.0f, 1.0f/18.0f));
        btTransform transform;
        transform.setIdentity();
        transform.setOrigin(btVector3(microcubeCenter.x, microcubeCenter.y, microcubeCenter.z));
        
        compound->addChildShape(transform, boxShape);
        
        // Create collision entity with spatial and hierarchy data
        auto entity = std::make_shared<Physics::CollisionSpatialGrid::CollisionEntity>(boxShape, Physics::CollisionSpatialGrid::CollisionEntity::SUBCUBE, microcubeCenter, 1.0f/18.0f);
        entity->isInCompound = true; // Shape is now owned by Bullet compound
        entity->parentChunkPos = parentLocalPos;
        entity->subcubeLocalPos = subcubePos; // Store parent subcube position
        
        // Add to spatial grid - O(1) operation
        collisionGrid.addEntity(parentLocalPos, entity);
    }
    
    LOG_DEBUG_FMT("Chunk", "[COLLISION] Built " << collisionGrid.getTotalEntityCount() << " initial collision entities ("
              << collisionGrid.getCubeEntityCount() << " cubes, " << collisionGrid.getSubcubeEntityCount() << " subcubes, "
              << staticMicrocubes.size() << " microcubes) with spatial grid optimization");
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
        bool hadCollisionShape = !collisionGrid.getEntitiesAt(neighborPos).empty();
        bool shouldHaveCollisionShape = hasExposedFaces(neighborPos);
        
        if (!hadCollisionShape && shouldHaveCollisionShape) {
            // Neighbor cube is now exposed - add collision shape
            addCollisionEntity(neighborPos);
            LOG_TRACE_FMT("Chunk", "[NEIGHBOR] Added collision shape for newly exposed cube at (" 
                      << neighborPos.x << "," << neighborPos.y << "," << neighborPos.z << ")");
        } else if (hadCollisionShape && !shouldHaveCollisionShape) {
            // Neighbor cube is no longer exposed (shouldn't happen when removing, but handle it)
            removeCollisionEntities(neighborPos);
            LOG_TRACE_FMT("Chunk", "[NEIGHBOR] Removed collision shape for no longer exposed cube at (" 
                      << neighborPos.x << "," << neighborPos.y << "," << neighborPos.z << ")");
        }
        // If hadCollisionShape && shouldHaveCollisionShape, no change needed
        // If !hadCollisionShape && !shouldHaveCollisionShape, no change needed
    }
}

void Chunk::endBulkOperation() {
    if (!isInBulkOperation) return;
    
    LOG_DEBUG("Chunk", "[CHUNK] Ending bulk operation - building complete collision system");
    
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
    LOG_DEBUG("Chunk", "[COLLISION] Building collision shapes only for exposed cubes (huge optimization!)");
    
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
    
    LOG_DEBUG_FMT("Chunk", "[COLLISION] MASSIVE OPTIMIZATION: Generated only " << boxes.size() 
              << " collision boxes (was ~" << cubes.size() << " before)");
    LOG_DEBUG_FMT("Chunk", "[COLLISION] Performance improvement: " 
              << (cubes.size() > 0 ? (100.0f * (cubes.size() - boxes.size()) / cubes.size()) : 0.0f) 
              << "% reduction in collision shapes!");
    
    return boxes;
}

// DEBUG: Collision shape validation and debugging methods
// Validates consistency between Bullet compound shapes and spatial grid tracking
// Helps detect memory management issues and spatial grid inconsistencies
void Chunk::validateCollisionSystem() const {
    if (!chunkCollisionShape) {
        LOG_DEBUG("Chunk", "[COLLISION VALIDATION] No compound collision shape exists");
        return;
    }
    
    btCompoundShape* compound = static_cast<btCompoundShape*>(chunkCollisionShape);
    int actualShapeCount = compound->getNumChildShapes();
    
    LOG_DEBUG_FMT("Chunk", "[COLLISION VALIDATION] Chunk at (" << worldOrigin.x << "," << worldOrigin.y << "," << worldOrigin.z << ")");
    LOG_DEBUG_FMT("Chunk", "  Compound shape has " << actualShapeCount << " child shapes");
    LOG_DEBUG_FMT("Chunk", "  Spatial grid tracking " << collisionGrid.getTotalEntityCount() << " entities");
    LOG_DEBUG_FMT("Chunk", "  Grid breakdown: " << collisionGrid.getCubeEntityCount() << " cubes, " 
              << collisionGrid.getSubcubeEntityCount() << " subcubes");
    LOG_DEBUG_FMT("Chunk", "  Occupied cells: " << collisionGrid.getOccupiedCellCount() << "/" 
              << (Physics::CollisionSpatialGrid::GRID_SIZE * Physics::CollisionSpatialGrid::GRID_SIZE * Physics::CollisionSpatialGrid::GRID_SIZE));
    
    // Validate that tracked count matches compound shape count
    size_t expectedShapeCount = collisionGrid.getTotalEntityCount();
    if (expectedShapeCount != static_cast<size_t>(actualShapeCount)) {
        LOG_ERROR_FMT("Chunk", "[COLLISION ERROR] Shape count mismatch! Expected: " << expectedShapeCount 
                  << ", Actual: " << actualShapeCount);
    }
    
    // Validate spatial grid internal consistency
    if (!collisionGrid.validateGrid()) {
        LOG_ERROR("Chunk", "[COLLISION ERROR] Spatial grid internal validation failed!");
    } else {
        LOG_DEBUG("Chunk", "  Spatial grid validation: PASSED");
    }
}

void Chunk::debugLogSpatialGrid() const {
    LOG_DEBUG("Chunk", "[COLLISION DEBUG] Spatial grid detailed information:");
    LOG_DEBUG_FMT("Chunk", "  Chunk origin: (" << worldOrigin.x << "," << worldOrigin.y << "," << worldOrigin.z << ")");
    
    // Print spatial grid statistics
    collisionGrid.debugPrintStats();
    
    // Log entities by occupied positions
    LOG_DEBUG("Chunk", "  Entity details by position:");
    for (int x = 0; x < Physics::CollisionSpatialGrid::GRID_SIZE; ++x) {
        for (int y = 0; y < Physics::CollisionSpatialGrid::GRID_SIZE; ++y) {
            for (int z = 0; z < Physics::CollisionSpatialGrid::GRID_SIZE; ++z) {
                glm::ivec3 pos(x, y, z);
                const auto& entities = collisionGrid.getEntitiesAt(pos);
                if (!entities.empty()) {
                    LOG_TRACE_FMT("Chunk", "    Position (" << x << "," << y << "," << z << ") has " << entities.size() << " entities:");
                    for (size_t i = 0; i < entities.size(); ++i) {
                        const auto& entity = entities[i];
                        if (entity->isCube()) {
                            LOG_TRACE_FMT("Chunk", "      [" << i << "] Cube - Shape: " << entity->shape 
                                      << ", Center: (" << entity->worldCenter.x << "," << entity->worldCenter.y << "," << entity->worldCenter.z
                                      << "), Refs: " << entity.use_count());
                        } else {
                            LOG_TRACE_FMT("Chunk", "      [" << i << "] Subcube - Shape: " << entity->shape 
                                      << ", Center: (" << entity->worldCenter.x << "," << entity->worldCenter.y << "," << entity->worldCenter.z
                                      << "), Local: (" << entity->subcubeLocalPos.x << "," << entity->subcubeLocalPos.y << "," << entity->subcubeLocalPos.z
                                      << "), Refs: " << entity.use_count());
                        }
                    }
                }
            }
        }
    }
}

size_t Chunk::getCollisionEntityCount() const {
    return collisionGrid.getTotalEntityCount();
}

size_t Chunk::getCubeEntityCount() const {
    return collisionGrid.getCubeEntityCount();
}

size_t Chunk::getSubcubeEntityCount() const {
    return collisionGrid.getSubcubeEntityCount();
}

void Chunk::debugPrintSpatialGridStats() const {
    collisionGrid.debugPrintStats();
}

// =============================================================================
// Microcube Manipulation Functions (Phase 3 - Placeholders for now)
// =============================================================================

bool Chunk::subdivideSubcubeAt(const glm::ivec3& cubePos, const glm::ivec3& subcubePos) {
    // Check if position is valid
    if (!isValidLocalPosition(cubePos)) return false;
    if (subcubePos.x < 0 || subcubePos.x >= 3 || 
        subcubePos.y < 0 || subcubePos.y >= 3 || 
        subcubePos.z < 0 || subcubePos.z >= 3) return false;
    
    // Get the subcube at this position
    Subcube* subcube = getSubcubeAt(cubePos, subcubePos);
    if (!subcube) return false;
    
    // Check if already subdivided into microcubes
    auto existingMicrocubes = getMicrocubesAt(cubePos, subcubePos);
    if (!existingMicrocubes.empty()) return false; // Already subdivided
    
    // Create 27 microcubes (3x3x3) with random colors for clear visual distinction
    glm::ivec3 parentWorldPos = worldOrigin + cubePos;
    
    // Use random colors for microcubes
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> colorDist(0.3f, 1.0f); // Brighter colors for visibility
    
    int colorIndex = 0;
    for (int x = 0; x < 3; ++x) {
        for (int y = 0; y < 3; ++y) {
            for (int z = 0; z < 3; ++z) {
                glm::ivec3 microcubeLocalPos(x, y, z);
                
                // Generate random color for each microcube
                glm::vec3 microcubeColor = glm::vec3(
                    colorDist(gen),
                    colorDist(gen),
                    colorDist(gen)
                );
                
                Microcube* newMicrocube = new Microcube(parentWorldPos, microcubeColor, subcubePos, microcubeLocalPos);
                staticMicrocubes.push_back(newMicrocube);
                
                // Update hash maps for O(1) hover detection
                addMicrocubeToMaps(cubePos, subcubePos, microcubeLocalPos, newMicrocube);
                
                colorIndex++;
            }
        }
    }
    
    // Delete the parent subcube completely
    removeSubcubeFromMaps(cubePos, subcubePos);
    
    // Remove from staticSubcubes vector
    for (auto it = staticSubcubes.begin(); it != staticSubcubes.end(); ++it) {
        if (*it == subcube) {
            delete subcube;
            staticSubcubes.erase(it);
            LOG_DEBUG_FMT("Chunk", "Completely removed parent subcube at cube (" 
                      << cubePos.x << "," << cubePos.y << "," << cubePos.z 
                      << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z
                      << ") - replaced by 27 microcubes");
            break;
        }
    }
    
    // CRITICAL: Update collision shape to create microcube collision entities
    LOG_DEBUG_FMT("Chunk", "[COLLISION] Creating collision shapes for 27 new microcubes at cube (" 
              << cubePos.x << "," << cubePos.y << "," << cubePos.z << ")");
    removeCollisionEntities(cubePos);  // Remove old subcube collision
    addCollisionEntity(cubePos);       // Add new microcube collision
    
    // Mark for update and as dirty for database persistence
    needsUpdate = true;
    setDirty(true);
    
    return true;
}

bool Chunk::addMicrocube(const glm::ivec3& parentCubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos, const glm::vec3& color) {
    // Check if cube position is valid
    if (!isValidLocalPosition(parentCubePos)) return false;
    
    // Check if subcube position is valid (0-2 for each axis)
    if (subcubePos.x < 0 || subcubePos.x >= 3 || 
        subcubePos.y < 0 || subcubePos.y >= 3 || 
        subcubePos.z < 0 || subcubePos.z >= 3) return false;
    
    // Check if microcube position is valid (0-2 for each axis)
    if (microcubePos.x < 0 || microcubePos.x >= 3 || 
        microcubePos.y < 0 || microcubePos.y >= 3 || 
        microcubePos.z < 0 || microcubePos.z >= 3) return false;
    
    // Check if microcube already exists
    if (getMicrocubeAt(parentCubePos, subcubePos, microcubePos)) return false;
    
    // CRITICAL FIX: Check if this is the first microcube at this subcube position
    // If parent subcube exists, we need to remove it (defensive fix for database corruption)
    auto existingMicrocubes = getMicrocubesAt(parentCubePos, subcubePos);
    if (existingMicrocubes.empty()) {
        // This is the first microcube at this subcube position
        // Check if parent subcube exists and remove it if found
        Subcube* parentSubcube = getSubcubeAt(parentCubePos, subcubePos);
        if (parentSubcube) {
            LOG_WARN_FMT("Chunk", "[DATA INTEGRITY] Found parent subcube when adding first microcube at cube (" 
                      << parentCubePos.x << "," << parentCubePos.y << "," << parentCubePos.z 
                      << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z 
                      << ") - removing parent (possible database corruption)");
            
            // Remove from maps and vector
            removeSubcubeFromMaps(parentCubePos, subcubePos);
            for (auto it = staticSubcubes.begin(); it != staticSubcubes.end(); ++it) {
                if (*it == parentSubcube) {
                    delete parentSubcube;
                    staticSubcubes.erase(it);
                    break;
                }
            }
        }
    }
    
    // Create new microcube (constructor: parentCubePos, color, subcubeLocalPos, microcubeLocalPos)
    glm::ivec3 parentWorldPos = worldOrigin + parentCubePos;
    Microcube* newMicrocube = new Microcube(parentWorldPos, color, subcubePos, microcubePos);
    staticMicrocubes.push_back(newMicrocube);
    
    // Update hash maps
    addMicrocubeToMaps(parentCubePos, subcubePos, microcubePos, newMicrocube);
    
    // Mark for update and as dirty for database persistence
    needsUpdate = true;
    setDirty(true);
    
    return true;
}

bool Chunk::removeMicrocube(const glm::ivec3& parentCubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos) {
    LOG_INFO_FMT("Chunk", "[REMOVE MICROCUBE] Called for cube (" << parentCubePos.x << "," << parentCubePos.y << "," << parentCubePos.z 
              << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z
              << ") micro (" << microcubePos.x << "," << microcubePos.y << "," << microcubePos.z << ")");
    
    // Try to find and remove from static microcubes
    for (auto it = staticMicrocubes.begin(); it != staticMicrocubes.end(); ++it) {
        Microcube* microcube = *it;
        if (microcube && 
            microcube->getParentCubePosition() == (worldOrigin + parentCubePos) &&
            microcube->getSubcubeLocalPosition() == subcubePos &&
            microcube->getMicrocubeLocalPosition() == microcubePos) {
            
            LOG_INFO("Chunk", "[REMOVE MICROCUBE] Found microcube to remove");
            
            // Remove from hash maps
            removeMicrocubeFromMaps(parentCubePos, subcubePos, microcubePos);
            
            // Delete and remove from vector
            delete microcube;
            staticMicrocubes.erase(it);
            
            LOG_INFO("Chunk", "[REMOVE MICROCUBE] Checking for remaining microcubes");
            
            // CRITICAL: Update collision shape to reflect microcube removal
            // Check if any microcubes remain at this parent position
            bool hasMicrocubes = false;
            for (int sx = 0; sx < 3; ++sx) {
                for (int sy = 0; sy < 3; ++sy) {
                    for (int sz = 0; sz < 3; ++sz) {
                        glm::ivec3 checkSubcubePos(sx, sy, sz);
                        auto remainingMicros = getMicrocubesAt(parentCubePos, checkSubcubePos);
                        if (!remainingMicros.empty()) {
                            hasMicrocubes = true;
                            break;
                        }
                    }
                    if (hasMicrocubes) break;
                }
                if (hasMicrocubes) break;
            }
            
            if (hasMicrocubes) {
                // Still have microcubes - update collision shape to reflect remaining microcubes
                LOG_INFO_FMT("Chunk", "[COLLISION] Microcubes remain at parent pos (" 
                          << parentCubePos.x << "," << parentCubePos.y << "," << parentCubePos.z 
                          << ") - updating collision shape");
                removeCollisionEntities(parentCubePos);
                addCollisionEntity(parentCubePos);
            } else {
                // No more microcubes - but check if subcubes still exist
                auto remainingSubcubes = getStaticSubcubesAt(parentCubePos);
                if (!remainingSubcubes.empty()) {
                    LOG_INFO_FMT("Chunk", "[VOXEL MAP] No microcubes remain but " << remainingSubcubes.size() 
                              << " subcubes still exist at (" << parentCubePos.x << "," << parentCubePos.y 
                              << "," << parentCubePos.z << ") - keeping SUBDIVIDED state and updating collision");
                    removeCollisionEntities(parentCubePos);
                    addCollisionEntity(parentCubePos);
                    // voxelTypeMap stays as SUBDIVIDED - DO NOT ERASE
                } else {
                    // No microcubes AND no subcubes - completely empty position
                    LOG_INFO_FMT("Chunk", "[COLLISION] No microcubes or subcubes remain at parent pos (" 
                              << parentCubePos.x << "," << parentCubePos.y << "," << parentCubePos.z 
                              << ") - removing collision shape and voxel type entry");
                    removeCollisionEntities(parentCubePos);
                    voxelTypeMap.erase(parentCubePos);
                }
            }
            
            // Mark for update
            needsUpdate = true;
            setDirty(true);
            
            return true;
        }
    }
    
    return false; // Microcube not found
}

bool Chunk::clearMicrocubesAt(const glm::ivec3& cubePos, const glm::ivec3& subcubePos) {
    auto microcubes = getMicrocubesAt(cubePos, subcubePos);
    if (microcubes.empty()) return false; // No microcubes to clear
    
    LOG_INFO_FMT("Chunk", "[CLEAR MICROCUBES] Removing all microcubes at cube (" 
              << cubePos.x << "," << cubePos.y << "," << cubePos.z 
              << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z 
              << ") - leaving empty space (no parent restoration)");
    
    // Remove all microcubes at this subcube position
    for (auto it = staticMicrocubes.begin(); it != staticMicrocubes.end(); ) {
        Microcube* microcube = *it;
        if (microcube && 
            microcube->getParentCubePosition() == (worldOrigin + cubePos) &&
            microcube->getSubcubeLocalPosition() == subcubePos) {
            
            // Remove from hash maps
            removeMicrocubeFromMaps(cubePos, subcubePos, microcube->getMicrocubeLocalPosition());
            
            // Delete and remove from vector
            delete microcube;
            it = staticMicrocubes.erase(it);
        } else {
            ++it;
        }
    }
    
    // Update collision to reflect removal
    // Check if any microcubes remain at the parent cube position
    bool hasMicrocubes = false;
    for (int sx = 0; sx < 3; ++sx) {
        for (int sy = 0; sy < 3; ++sy) {
            for (int sz = 0; sz < 3; ++sz) {
                glm::ivec3 checkSubcubePos(sx, sy, sz);
                auto remainingMicros = getMicrocubesAt(cubePos, checkSubcubePos);
                if (!remainingMicros.empty()) {
                    hasMicrocubes = true;
                    break;
                }
            }
            if (hasMicrocubes) break;
        }
        if (hasMicrocubes) break;
    }
    
    if (hasMicrocubes) {
        // Still have microcubes at other subcube positions - update collision
        removeCollisionEntities(cubePos);
        addCollisionEntity(cubePos);
    } else {
        // No more microcubes - but check if subcubes still exist
        auto remainingSubcubes = getStaticSubcubesAt(cubePos);
        if (!remainingSubcubes.empty()) {
            LOG_INFO_FMT("Chunk", "[VOXEL MAP] No microcubes remain but " << remainingSubcubes.size() 
                      << " subcubes still exist at (" << cubePos.x << "," << cubePos.y 
                      << "," << cubePos.z << ") - keeping SUBDIVIDED state and updating collision");
            removeCollisionEntities(cubePos);
            addCollisionEntity(cubePos);
            // voxelTypeMap stays as SUBDIVIDED - DO NOT ERASE
        } else {
            // No microcubes AND no subcubes - completely empty position
            LOG_INFO_FMT("Chunk", "[CLEAR MICROCUBES] No microcubes or subcubes remain at (" 
                      << cubePos.x << "," << cubePos.y << "," << cubePos.z 
                      << ") - removing collision and voxel type entry");
            removeCollisionEntities(cubePos);
            voxelTypeMap.erase(cubePos);
        }
    }
    
    // Mark for update
    needsUpdate = true;
    setDirty(true);
    
    return true;
}

} // namespace VulkanCube
