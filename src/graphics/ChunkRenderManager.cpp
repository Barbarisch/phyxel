#include "graphics/ChunkRenderManager.h"
#include "core/Cube.h"
#include "core/Subcube.h"
#include "core/Microcube.h"
#include "core/Types.h"
#include "utils/Logger.h"
#include <cstring>
#include <iostream>

namespace VulkanCube {
namespace Graphics {

ChunkRenderManager::ChunkRenderManager()
    : numInstances(0)
    , needsUpdate(false)
    , renderBuffer(VK_NULL_HANDLE, VK_NULL_HANDLE)
    , device(VK_NULL_HANDLE)
    , physicalDevice(VK_NULL_HANDLE)
{
    faces.reserve(32 * 32 * 32 * 6); // Reserve for maximum faces
}

ChunkRenderManager::~ChunkRenderManager() {
    cleanupVulkanResources();
}

ChunkRenderManager::ChunkRenderManager(ChunkRenderManager&& other) noexcept
    : faces(std::move(other.faces))
    , numInstances(other.numInstances)
    , needsUpdate(other.needsUpdate)
    , renderBuffer(std::move(other.renderBuffer))
    , device(other.device)
    , physicalDevice(other.physicalDevice)
{
    other.numInstances = 0;
    other.needsUpdate = false;
    other.device = VK_NULL_HANDLE;
    other.physicalDevice = VK_NULL_HANDLE;
}

ChunkRenderManager& ChunkRenderManager::operator=(ChunkRenderManager&& other) noexcept {
    if (this != &other) {
        cleanupVulkanResources();
        
        faces = std::move(other.faces);
        numInstances = other.numInstances;
        needsUpdate = other.needsUpdate;
        renderBuffer = std::move(other.renderBuffer);
        device = other.device;
        physicalDevice = other.physicalDevice;
        
        other.numInstances = 0;
        other.needsUpdate = false;
        other.device = VK_NULL_HANDLE;
        other.physicalDevice = VK_NULL_HANDLE;
    }
    return *this;
}

void ChunkRenderManager::initialize(VkDevice dev, VkPhysicalDevice physDev) {
    device = dev;
    physicalDevice = physDev;
    renderBuffer = ChunkRenderBuffer(device, physicalDevice);
}

void ChunkRenderManager::rebuildAllFaces(
    const std::vector<Cube*>& cubes,
    const std::vector<Subcube*>& subcubes,
    const std::vector<Microcube*>& microcubes,
    const glm::ivec3& worldOrigin,
    const NeighborLookupFunc& getNeighborCube)
{
    faces.clear();
    
    // Rebuild faces for each voxel type
    rebuildCubeFaces(cubes, worldOrigin, getNeighborCube);
    rebuildSubcubeFaces(subcubes, worldOrigin);
    rebuildMicrocubeFaces(microcubes, worldOrigin);
    
    numInstances = static_cast<uint32_t>(faces.size());
    needsUpdate = true;
}

void ChunkRenderManager::rebuildCubeFaces(
    const std::vector<Cube*>& cubes,
    const glm::ivec3& worldOrigin,
    const NeighborLookupFunc& getNeighborCube)
{
    // Process regular cubes (only those that aren't subdivided)
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
                const Cube* neighborCube = getCubeAtPosition(neighborPos, cubes);
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
}

void ChunkRenderManager::rebuildSubcubeFaces(
    const std::vector<Subcube*>& subcubes,
    const glm::ivec3& worldOrigin)
{
    // Process subcubes (from subdivided cubes)
    for (const Subcube* subcube : subcubes) {
        // Skip broken or hidden subcubes
        if (!subcube || subcube->isBroken() || !subcube->isVisible()) {
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
        
        // For now, assume all subcube faces are visible (can optimize with culling later)
        bool faceVisible[6] = {true, true, true, true, true, true};
        
        // Generate instance data for each visible face of the subcube
        for (int faceID = 0; faceID < 6; ++faceID) {
            if (faceVisible[faceID]) {
                InstanceData faceInstance;
                
                // Pack parent cube position, face ID, and subcube local position
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
}

void ChunkRenderManager::rebuildMicrocubeFaces(
    const std::vector<Microcube*>& microcubes,
    const glm::ivec3& worldOrigin)
{
    // Process microcubes (from subdivided subcubes)
    for (const Microcube* microcube : microcubes) {
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
            continue;
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
}

void ChunkRenderManager::updateVulkanBuffer() {
    void* mappedMem = renderBuffer.getMappedMemory();
    if (!mappedMem || faces.empty()) return;
    
    // Ensure buffer capacity is sufficient, reallocate if necessary
    ensureBufferCapacity(faces.size());
    
    // Track peak usage for analysis
    renderBuffer.updateMaxUsage(faces.size());
    
    // Copy data to GPU buffer (only the used portion)
    VkDeviceSize copySize = sizeof(InstanceData) * faces.size();
    memcpy(mappedMem, faces.data(), copySize);
    needsUpdate = false;
    
    // Periodic utilization logging
    static int updateCount = 0;
    if (++updateCount % 50 == 0) {
        logBufferUtilization();
    }
}

void ChunkRenderManager::updateSingleCubeTexture(
    const glm::ivec3& localPos,
    uint16_t textureIndex,
    const std::vector<Cube*>& cubes)
{
    // Find the cube
    const Cube* cube = getCubeAtPosition(localPos, cubes);
    if (!cube) return;
    
    // Efficiently update only the affected faces in the buffer
    if (!renderBuffer.getMappedMemory()) return;
    
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
            memcpy(static_cast<char*>(renderBuffer.getMappedMemory()) + offset, &textureIndex, sizeof(uint16_t));
            
            updatedAnyFaces = true;
        }
    }
}

void ChunkRenderManager::updateSingleSubcubeTexture(
    const glm::ivec3& parentLocalPos,
    const glm::ivec3& subcubePos,
    uint16_t textureIndex,
    const std::vector<Subcube*>& subcubes,
    const glm::ivec3& worldOrigin)
{
    // Validate positions
    if (subcubePos.x < 0 || subcubePos.x >= 3 || 
        subcubePos.y < 0 || subcubePos.y >= 3 || 
        subcubePos.z < 0 || subcubePos.z >= 3) return;
    
    // Efficiently update only the affected faces in the buffer
    if (!renderBuffer.getMappedMemory()) return;
    
    bool updatedAnyFaces = false;
    
    // Find all face instances for this subcube and update their texture indices
    for (size_t i = 0; i < faces.size(); ++i) {
        InstanceData& face = faces[i];
        
        // Extract data from packed format for subcubes
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
                memcpy(static_cast<char*>(renderBuffer.getMappedMemory()) + offset, &textureIndex, sizeof(uint16_t));
                
                updatedAnyFaces = true;
            }
        }
    }
}

void ChunkRenderManager::updateSingleCubeColor(
    const glm::ivec3& localPos,
    const glm::vec3& newColor,
    const std::vector<Cube*>& cubes)
{
    // Color updates would require rebuilding faces since colors are baked into vertex data
    // For now, this is a placeholder - actual implementation depends on rendering architecture
    // TODO: Implement color updates if needed
}

void ChunkRenderManager::updateSingleSubcubeColor(
    const glm::ivec3& localPos,
    const glm::ivec3& subcubePos,
    const glm::vec3& newColor,
    const std::vector<Subcube*>& subcubes,
    const glm::ivec3& worldOrigin)
{
    // Color updates would require rebuilding faces since colors are baked into vertex data
    // For now, this is a placeholder - actual implementation depends on rendering architecture
    // TODO: Implement color updates if needed
}

void ChunkRenderManager::createVulkanBuffer() {
    if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("ChunkRenderManager::createVulkanBuffer() called before initialize()!");
    }
    renderBuffer.createBuffer(faces);
}

void ChunkRenderManager::cleanupVulkanResources() {
    renderBuffer.cleanup();
}

void ChunkRenderManager::ensureBufferCapacity(size_t requiredInstances) {
    if (requiredInstances > renderBuffer.getCapacity()) {
        renderBuffer.reallocateBuffer(requiredInstances);
    }
}

void ChunkRenderManager::logBufferUtilization() const {
    renderBuffer.logUtilization(faces.size());
}

// Helper methods

bool ChunkRenderManager::isCubeFaceVisible(
    const glm::ivec3& cubePos,
    int faceID,
    const std::vector<Cube*>& cubes,
    const glm::ivec3& worldOrigin,
    const NeighborLookupFunc& getNeighborCube) const
{
    // This is a helper that could be used for more sophisticated culling
    // For now, it's not used, but kept for potential future optimization
    return true;
}

const Cube* ChunkRenderManager::getCubeAtPosition(
    const glm::ivec3& localPos,
    const std::vector<Cube*>& cubes) const
{
    // Use indexed lookup - cubes vector is arranged in X-major order
    // Index formula: z + y*32 + x*32*32 (must match Chunk::localToIndex)
    if (localPos.x < 0 || localPos.x >= 32 ||
        localPos.y < 0 || localPos.y >= 32 ||
        localPos.z < 0 || localPos.z >= 32) {
        return nullptr;
    }
    
    size_t index = localPos.z + localPos.y * 32 + localPos.x * 32 * 32;
    if (index >= cubes.size()) {
        return nullptr;
    }
    
    return cubes[index];  // Could be nullptr for deleted cubes
}

} // namespace Graphics
} // namespace VulkanCube
