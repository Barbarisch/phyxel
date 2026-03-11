#include "core/FaceUpdateCoordinator.h"
#include "core/Chunk.h"
#include "core/Subcube.h"
#include "core/Cube.h"
#include "core/Microcube.h"
#include "core/Types.h"
#include "utils/Logger.h"
#include <set>

namespace Phyxel {

void FaceUpdateCoordinator::setCallbacks(
    DynamicSubcubeVectorAccessFunc getSubcubesFunc,
    DynamicCubeVectorAccessFunc getCubesFunc,
    DynamicMicrocubeVectorAccessFunc getMicrocubesFunc,
    FaceDataAccessFunc getFaceDataFunc,
    ChunkLookupFunc getChunkAtFunc,
    MarkChunkDirtyFunc markChunkDirtyFunc
) {
    m_getSubcubes = getSubcubesFunc;
    m_getCubes = getCubesFunc;
    m_getMicrocubes = getMicrocubesFunc;
    m_getFaceData = getFaceDataFunc;
    m_getChunkAt = getChunkAtFunc;
    m_markChunkDirty = markChunkDirtyFunc;
}

void FaceUpdateCoordinator::rebuildGlobalDynamicFaces() {
    auto& globalDynamicSubcubeFaces = m_getFaceData();
    globalDynamicSubcubeFaces.clear();
    
    static constexpr float SUBCUBE_SCALE = 1.0f / 3.0f;   // Subcubes are 1/3 the size
    static constexpr float CUBE_SCALE = 1.0f;             // Full cubes are full size
    static constexpr float MICROCUBE_SCALE = 1.0f / 9.0f; // Microcubes are 1/9 the size
    
    // Generate faces for all global dynamic subcubes
    auto& subcubes = m_getSubcubes();
    for (const auto& subcube : subcubes) {
        if (!subcube->isVisible()) continue;
        
        // For dynamic subcubes, we render all faces (they can be in arbitrary positions)
        for (int faceID = 0; faceID < 6; ++faceID) {
            DynamicSubcubeInstanceData faceInstance;
            
            // Use smooth physics position for dynamic subcubes, fallback to grid position for static
            if (subcube->isDynamic()) {
                faceInstance.worldPosition = subcube->getPhysicsPosition();
                faceInstance.rotation = subcube->getPhysicsRotation(); // Get rotation from physics
                faceInstance.scale = glm::vec3(subcube->getScale()); // Uniform scale for subcubes
            } else {
                faceInstance.worldPosition = glm::vec3(subcube->getPosition()) + glm::vec3(subcube->getLocalPosition()) * SUBCUBE_SCALE;
                faceInstance.rotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); // Identity quaternion for static subcubes
                faceInstance.scale = glm::vec3(subcube->getScale());
            }
            
            faceInstance.textureIndex = TextureConstants::getTextureIndexForMaterial(subcube->getMaterialName(), faceID);
            faceInstance.faceID = faceID;
            faceInstance.localPosition = subcube->getLocalPosition(); // Preserve original grid position
            
            globalDynamicSubcubeFaces.push_back(faceInstance);
        }
    }
    
    // Generate faces for all global dynamic cubes
    auto& cubes = m_getCubes();
    for (const auto& cube : cubes) {
        if (!cube->isVisible()) continue;
        
        // For dynamic cubes, we render all faces (they can be in arbitrary positions)
        for (int faceID = 0; faceID < 6; ++faceID) {
            DynamicSubcubeInstanceData faceInstance; // Using same data structure as subcubes
            
            // Dynamic cubes always use physics position and rotation
            faceInstance.worldPosition = cube->getPhysicsPosition();
            faceInstance.rotation = cube->getPhysicsRotation();
            faceInstance.textureIndex = TextureConstants::getTextureIndexForMaterial(
                cube->getMaterialName(), faceID);
            faceInstance.faceID = faceID;
            faceInstance.scale = cube->getDynamicScale(); // Use dynamic scale (vec3)
            faceInstance.localPosition = glm::ivec3(1, 1, 1); // Center position for full cubes
            
            globalDynamicSubcubeFaces.push_back(faceInstance);
        }
    }
    
    // Generate faces for all global dynamic microcubes
    auto& microcubes = m_getMicrocubes();
    for (const auto& microcube : microcubes) {
        if (!microcube->isVisible()) continue;
        
        // For dynamic microcubes, we render all faces (they can be in arbitrary positions)
        for (int faceID = 0; faceID < 6; ++faceID) {
            DynamicSubcubeInstanceData faceInstance; // Using same data structure
            
            // Dynamic microcubes always use physics position and rotation
            faceInstance.worldPosition = microcube->getPhysicsPosition();
            faceInstance.rotation = microcube->getPhysicsRotation();
            faceInstance.textureIndex = TextureConstants::getTextureIndexForMaterial(microcube->getMaterialName(), faceID);
            faceInstance.faceID = faceID;
            faceInstance.scale = glm::vec3(microcube->getScale()); // Uniform scale for microcubes
            
            // Pack both subcube and microcube positions into localPosition for texture coordinate calculation
            // Bits 0-1: subcube X, Bits 2-3: subcube Y, Bits 4-5: subcube Z
            // Bits 6-7: microcube X, Bits 8-9: microcube Y, Bits 10-11: microcube Z
            glm::ivec3 subcubePos = microcube->getSubcubeLocalPosition();
            glm::ivec3 microcubePos = microcube->getMicrocubeLocalPosition();
            int packed = (subcubePos.x & 0x3) | ((subcubePos.y & 0x3) << 2) | ((subcubePos.z & 0x3) << 4) |
                        ((microcubePos.x & 0x3) << 6) | ((microcubePos.y & 0x3) << 8) | ((microcubePos.z & 0x3) << 10);
            faceInstance.localPosition = glm::ivec3(packed, 0, 0); // Store packed data in X component
            
            globalDynamicSubcubeFaces.push_back(faceInstance);
        }
    }
    
    if (!globalDynamicSubcubeFaces.empty()) {
        //std::cout << "[CHUNK MANAGER] Generated " << globalDynamicSubcubeFaces.size() << " global dynamic faces (subcubes + cubes + microcubes)" << std::endl;
    }
}

void FaceUpdateCoordinator::updateAfterCubeBreak(const glm::ivec3& worldPos) {
    // When a cube is broken (removed), we need to:
    // 1. Remove faces of the broken cube (already done by removeCube)
    // 2. Update faces of neighboring cubes that may now be exposed
    
    LOG_DEBUG_FMT("FaceUpdate", "[SELECTIVE UPDATE] Cube broken at world pos (" << worldPos.x << "," << worldPos.y << "," << worldPos.z << ")");
    
    // Get the chunk containing the broken cube
    Chunk* primaryChunk = m_getChunkAt(worldPos);
    if (primaryChunk) {
        m_markChunkDirty(primaryChunk);
    }
    
    // Check if any neighbors are in different chunks and mark those dirty too
    std::vector<glm::ivec3> neighborPositions = getAffectedNeighborPositions(worldPos);
    std::set<Chunk*> affectedChunks;
    
    for (const glm::ivec3& neighborPos : neighborPositions) {
        Chunk* neighborChunk = m_getChunkAt(neighborPos);
        if (neighborChunk && neighborChunk != primaryChunk) {
            affectedChunks.insert(neighborChunk);
        }
    }
    
    // Mark all affected neighbor chunks dirty
    for (Chunk* chunk : affectedChunks) {
        m_markChunkDirty(chunk);
    }
}

void FaceUpdateCoordinator::updateAfterCubePlace(const glm::ivec3& worldPos) {
    // When a cube is placed (added), we need to:
    // 1. Generate faces for the new cube (based on neighbors)
    // 2. Update faces of neighboring cubes that may now be hidden
    
    LOG_DEBUG_FMT("FaceUpdate", "[SELECTIVE UPDATE] Cube placed at world pos (" << worldPos.x << "," << worldPos.y << "," << worldPos.z << ")");
    
    // Get the chunk containing the placed cube
    Chunk* primaryChunk = m_getChunkAt(worldPos);
    if (primaryChunk) {
        m_markChunkDirty(primaryChunk);
    }
    
    // Check if any neighbors are in different chunks and mark those dirty too
    std::vector<glm::ivec3> neighborPositions = getAffectedNeighborPositions(worldPos);
    std::set<Chunk*> affectedChunks;
    
    for (const glm::ivec3& neighborPos : neighborPositions) {
        Chunk* neighborChunk = m_getChunkAt(neighborPos);
        if (neighborChunk && neighborChunk != primaryChunk) {
            affectedChunks.insert(neighborChunk);
        }
    }
    
    // Mark all affected neighbor chunks dirty
    for (Chunk* chunk : affectedChunks) {
        m_markChunkDirty(chunk);
    }
}

void FaceUpdateCoordinator::updateAfterCubeSubdivision(const glm::ivec3& worldPos) {
    // When a cube is subdivided, we need to:
    // 1. Hide original cube faces (cube becomes invisible)
    // 2. Generate subcube faces (8 or 27 subcubes with their own faces)
    // 3. Update faces of neighboring cubes (original cube is now hidden)
    
    LOG_DEBUG_FMT("FaceUpdate", "[SELECTIVE UPDATE] Cube subdivided at world pos (" << worldPos.x << "," << worldPos.y << "," << worldPos.z << ")");
    
    // Get the chunk containing the subdivided cube
    Chunk* primaryChunk = m_getChunkAt(worldPos);
    if (primaryChunk) {
        m_markChunkDirty(primaryChunk);
    }
    
    // Check if any neighbors are in different chunks and mark those dirty too
    std::vector<glm::ivec3> neighborPositions = getAffectedNeighborPositions(worldPos);
    std::set<Chunk*> affectedChunks;
    
    for (const glm::ivec3& neighborPos : neighborPositions) {
        Chunk* neighborChunk = m_getChunkAt(neighborPos);
        if (neighborChunk && neighborChunk != primaryChunk) {
            affectedChunks.insert(neighborChunk);
        }
    }
    
    // Mark all affected neighbor chunks dirty
    for (Chunk* chunk : affectedChunks) {
        m_markChunkDirty(chunk);
    }
}

void FaceUpdateCoordinator::updateAfterSubcubeBreak(const glm::ivec3& parentWorldPos, const glm::ivec3& subcubeLocalPos) {
    // When a subcube breaks (moves from static to dynamic), we need to:
    // 1. Remove the subcube's faces from static rendering
    // 2. Update faces of neighboring subcubes in the same parent cube
    // 3. Add the subcube to dynamic rendering system
    
    LOG_DEBUG_FMT("FaceUpdate", "[SELECTIVE UPDATE] Subcube broken at parent pos (" << parentWorldPos.x << "," << parentWorldPos.y << "," << parentWorldPos.z 
              << ") local (" << subcubeLocalPos.x << "," << subcubeLocalPos.y << "," << subcubeLocalPos.z << ")");
    
    // For subcube breaking, only update the chunk containing the parent cube
    Chunk* chunk = m_getChunkAt(parentWorldPos);
    if (chunk) {
        m_markChunkDirty(chunk);
    }
}

void FaceUpdateCoordinator::updateFacesForPositionChange(const glm::ivec3& worldPos, bool cubeAdded) {
    // Central method that handles face updates when a cube is added or removed
    // This affects the cube at worldPos and its up to 6 neighbors
    
    // Update faces for the cube at the changed position
    updateSingleCubeFaces(worldPos);
    
    // Update faces for all neighboring cubes (up to 6 neighbors)
    updateNeighborFaces(worldPos);
}

void FaceUpdateCoordinator::updateNeighborFaces(const glm::ivec3& worldPos) {
    // Get all 6 neighboring positions
    std::vector<glm::ivec3> neighborPositions = getAffectedNeighborPositions(worldPos);
    
    // Update faces for each neighbor position
    for (const glm::ivec3& neighborPos : neighborPositions) {
        updateFacesAtPosition(neighborPos);
    }
}

void FaceUpdateCoordinator::updateSingleCubeFaces(const glm::ivec3& worldPos) {
    // Update faces only for the cube at the specified position
    updateFacesAtPosition(worldPos);
}

std::vector<glm::ivec3> FaceUpdateCoordinator::getAffectedNeighborPositions(const glm::ivec3& worldPos) {
    // Return all 6 neighbor positions (may span multiple chunks)
    std::vector<glm::ivec3> neighbors;
    neighbors.reserve(6);
    
    // Face directions: front(+Z), back(-Z), right(+X), left(-X), top(+Y), bottom(-Y)
    neighbors.push_back(worldPos + glm::ivec3(0, 0, 1));   // front (+Z)
    neighbors.push_back(worldPos + glm::ivec3(0, 0, -1));  // back (-Z)  
    neighbors.push_back(worldPos + glm::ivec3(1, 0, 0));   // right (+X)
    neighbors.push_back(worldPos + glm::ivec3(-1, 0, 0));  // left (-X)
    neighbors.push_back(worldPos + glm::ivec3(0, 1, 0));   // top (+Y)
    neighbors.push_back(worldPos + glm::ivec3(0, -1, 0));  // bottom (-Y)
    
    return neighbors;
}

void FaceUpdateCoordinator::updateFacesAtPosition(const glm::ivec3& worldPos) {
    // Update faces for a single cube at the specified world position
    // This may be in any chunk, so we need to find the right chunk first
    
    Chunk* chunk = m_getChunkAt(worldPos);
    if (!chunk) {
        return; // Position is outside loaded chunks
    }
    
    // For now, use the simple approach: mark the chunk dirty and let the update system handle it
    // TODO: Implement true single-cube face updates to avoid rebuilding entire chunks
    m_markChunkDirty(chunk);
}

} // namespace Phyxel
