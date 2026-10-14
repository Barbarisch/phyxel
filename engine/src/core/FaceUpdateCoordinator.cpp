#include "core/FaceUpdateCoordinator.h"
#include "core/MaterialRegistry.h"
#include "core/Chunk.h"
#include "core/Subcube.h"
#include "core/Cube.h"
#include "core/Microcube.h"
#include "core/Types.h"
#include "utils/Logger.h"
#include <set>

namespace Phyxel {

void FaceUpdateCoordinator::setCallbacks(
    ChunkLookupFunc getChunkAtFunc,
    MarkChunkDirtyFunc markChunkDirtyFunc
) {
    m_getChunkAt = getChunkAtFunc;
    m_markChunkDirty = markChunkDirtyFunc;
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
