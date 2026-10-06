#include "core/DamageSystem.h"
#include "scene/VoxelManipulationSystem.h"
#include "core/ChunkManager.h"
#include "core/Chunk.h"
#include "core/Cube.h"
#include "core/Subcube.h"
#include "core/Microcube.h"
#include "physics/PhysicsWorld.h"
#include "physics/VoxelDynamicsWorld.h"
#include "physics/VoxelRigidBody.h"
#include "utils/Logger.h"
#include "utils/CoordinateUtils.h"
#include <random>

namespace Phyxel {

void VoxelManipulationSystem::setCallbacks(
    GetChunkManagerFunc chunkManagerFunc,
    GetPhysicsWorldFunc physicsWorldFunc) {
    
    getChunkManager = chunkManagerFunc;
    getPhysicsWorld = physicsWorldFunc;
}

// =============================================================================
// VOXEL SUBDIVISION OPERATIONS
// =============================================================================

bool VoxelManipulationSystem::subdivideCube(const CubeLocation& location) {
    if (!location.isValid()) {
        LOG_DEBUG("VoxelManipulation", "[CUBE SUBDIVISION] Invalid location provided");
        return false;
    }
    
    // Only subdivide regular cubes (not subcubes)
    if (location.isSubcube) {
        LOG_DEBUG("VoxelManipulation", "[CUBE SUBDIVISION] Cannot subdivide individual subcubes");
        return false;
    }
    
    Chunk* chunk = location.chunk;
    if (!chunk) {
        LOG_ERROR("VoxelManipulation", "[CUBE SUBDIVISION] ERROR: Invalid chunk pointer");
        return false;
    }
    
    // Check if cube is already subdivided
    if (chunk->getSubcubesAt(location.localPos).size() > 0) {
        LOG_DEBUG_FMT("VoxelManipulation", "[CUBE SUBDIVISION] Cube at world pos (" 
                  << location.worldPos.x << "," << location.worldPos.y << "," << location.worldPos.z 
                  << ") is already subdivided");
        return false;
    }
    
    // Subdivide the cube into 27 static subcubes
    bool subdivided = chunk->subdivideAt(location.localPos);
    if (subdivided) {
        LOG_INFO_FMT("VoxelManipulation", "[CUBE SUBDIVISION] Successfully subdivided cube at world pos: (" 
                  << location.worldPos.x << "," << location.worldPos.y << "," << location.worldPos.z 
                  << ") into 27 static subcubes");
        
        // Use efficient selective update instead of marking entire chunk dirty
        ChunkManager* chunkManager = getChunkManager();
        if (chunkManager) {
            chunkManager->updateAfterCubeSubdivision(location.worldPos);
        }
    } else {
        LOG_WARN("VoxelManipulation", "[CUBE SUBDIVISION] WARNING: Failed to subdivide cube - cube may not exist");
    }
    
    return subdivided;
}

bool VoxelManipulationSystem::subdivideSubcube(const CubeLocation& location) {
    if (!location.isValid()) {
        LOG_DEBUG("VoxelManipulation", "[SUBCUBE SUBDIVISION] Invalid location provided");
        return false;
    }
    
    // Only subdivide subcubes (not regular cubes or microcubes)
    if (!location.isSubcube) {
        LOG_DEBUG("VoxelManipulation", "[SUBCUBE SUBDIVISION] Cannot subdivide regular cubes");
        return false;
    }
    
    Chunk* chunk = location.chunk;
    if (!chunk) {
        LOG_ERROR("VoxelManipulation", "[SUBCUBE SUBDIVISION] ERROR: Invalid chunk pointer");
        return false;
    }
    
    // Check if subcube is already subdivided into microcubes
    if (chunk->getMicrocubesAt(location.localPos, location.subcubePos).size() > 0) {
        LOG_DEBUG_FMT("VoxelManipulation", "[SUBCUBE SUBDIVISION] Subcube at cube world pos (" 
                  << location.worldPos.x << "," << location.worldPos.y << "," << location.worldPos.z 
                  << ") subcube pos (" << location.subcubePos.x << "," << location.subcubePos.y << "," << location.subcubePos.z 
                  << ") is already subdivided");
        return false;
    }
    
    // Subdivide the subcube into 27 static microcubes
    bool subdivided = chunk->subdivideSubcubeAt(location.localPos, location.subcubePos);
    if (subdivided) {
        LOG_INFO_FMT("VoxelManipulation", "[SUBCUBE SUBDIVISION] Successfully subdivided subcube at cube world pos: (" 
                  << location.worldPos.x << "," << location.worldPos.y << "," << location.worldPos.z 
                  << ") subcube pos (" << location.subcubePos.x << "," << location.subcubePos.y << "," << location.subcubePos.z 
                  << ") into 27 static microcubes");
        
        // Use efficient selective update instead of marking entire chunk dirty
        ChunkManager* chunkManager = getChunkManager();
        if (chunkManager) {
            chunkManager->updateAfterCubeSubdivision(location.worldPos);
        }
    } else {
        LOG_WARN("VoxelManipulation", "[SUBCUBE SUBDIVISION] WARNING: Failed to subdivide subcube - subcube may not exist");
    }
    
    return subdivided;
}

// =============================================================================
// VOXEL BREAKING OPERATIONS (with physics)
// =============================================================================

bool VoxelManipulationSystem::breakCube(const CubeLocation& location, const glm::vec3& cameraPos, bool applyForce) {
    if (!location.isValid()) {
        LOG_DEBUG("VoxelManipulation", "[CUBE BREAKING] Invalid location provided");
        return false;
    }
    
    // Only break regular cubes (not subcubes)
    if (location.isSubcube) {
        LOG_DEBUG("VoxelManipulation", "[CUBE BREAKING] Cannot break subcubes with this method");
        return false;
    }
    
    Chunk* chunk = location.chunk;
    if (!chunk) {
        LOG_ERROR("VoxelManipulation", "[CUBE BREAKING] ERROR: Invalid chunk pointer");
        return false;
    }
    
    // Get the cube's original color before removing it
    const Cube* originalCube = chunk->getCubeAt(location.localPos);
    if (!originalCube) {
        LOG_DEBUG("VoxelManipulation", "[CUBE BREAKING] No cube exists at this location");
        return false;
    }
    
    glm::vec3 cubeWorldPos = glm::vec3(location.worldPos);
    
    // Save material before removal (pointer becomes dangling after removeCube)
    std::string selectedMaterial = originalCube->getMaterialName();

    // Remove the cube from the chunk
    bool removed = chunk->removeCube(location.localPos);
    if (!removed) {
        LOG_WARN("VoxelManipulation", "[CUBE BREAKING] WARNING: Failed to remove cube from chunk");
        return false;
    }

    // The piece becomes GPU debris (1d: the CPU single-box debris path is gone).
    (void)applyForce;
    spawnPiece(cubeWorldPos + glm::vec3(0.5f), 1.0f, selectedMaterial);

    if (ChunkManager* chunkManager = getChunkManager())
        chunkManager->updateAfterCubeBreak(location.worldPos);

    LOG_INFO_FMT("VoxelManipulation", "[CUBE BREAKING] Successfully broke cube at world pos: (" 
              << location.worldPos.x << "," << location.worldPos.y << "," << location.worldPos.z << ")");
    
    return true;
}

bool VoxelManipulationSystem::breakSubcube(const CubeLocation& location, bool applyForce) {
    if (!location.isValid()) {
        LOG_DEBUG("VoxelManipulation", "[SUBCUBE BREAKING] Invalid location provided");
        return false;
    }
    
    // Only break subcubes (not regular cubes)
    if (!location.isSubcube) {
        LOG_DEBUG("VoxelManipulation", "[SUBCUBE BREAKING] Object is not a subcube");
        return false;
    }
    
    Chunk* chunk = location.chunk;
    if (!chunk) {
        LOG_ERROR("VoxelManipulation", "[SUBCUBE BREAKING] ERROR: Invalid chunk pointer");
        return false;
    }
    
    (void)applyForce;
    const Subcube* sc = chunk->getSubcubeAt(location.localPos, location.subcubePos);
    if (!sc) {
        LOG_DEBUG("VoxelManipulation", "[SUBCUBE BREAKING] No subcube at this location");
        return false;
    }
    const std::string material = sc->getMaterialName();
    const bool broken = chunk->removeSubcube(location.localPos, location.subcubePos);
    if (broken) {
        // Centre of the 1/3 cell inside its parent cube.
        const glm::vec3 centre = glm::vec3(location.worldPos) +
                                 (glm::vec3(location.subcubePos) + 0.5f) / 3.0f;
        spawnPiece(centre, 1.0f / 3.0f, material);
        if (ChunkManager* chunkManager = getChunkManager())
            chunkManager->updateAfterSubcubeBreak(location.worldPos, location.subcubePos);
    } else {
        LOG_WARN("VoxelManipulation", "[SUBCUBE BREAKING] WARNING: Failed to remove subcube");
    }
    return broken;
}

bool VoxelManipulationSystem::breakMicrocube(const CubeLocation& location, bool applyForce) {
    LOG_INFO("VoxelManipulation", "[MICROCUBE BREAKING] Breaking microcube");
    
    if (!location.isValid()) {
        LOG_DEBUG("VoxelManipulation", "[MICROCUBE BREAKING] Invalid location provided");
        return false;
    }
    
    // Only break microcubes
    if (!location.isMicrocube) {
        LOG_ERROR_FMT("VoxelManipulation", "[MICROCUBE BREAKING] *** BUG DETECTED *** Object is NOT a microcube! isSubcube=" 
                  << location.isSubcube);
        return false;
    }
    
    Chunk* chunk = location.chunk;
    if (!chunk) {
        LOG_ERROR("VoxelManipulation", "[MICROCUBE BREAKING] ERROR: Invalid chunk pointer");
        return false;
    }
    
    LOG_DEBUG("VoxelManipulation", "[MICROCUBE BREAKING] Breaking microcube and creating dynamic physics object");
    
    // Get the microcube before removing it
    glm::ivec3 localPos = location.localPos;
    glm::ivec3 subcubePos = location.subcubePos;
    glm::ivec3 microcubePos = location.microcubePos;
    
    Microcube* microcube = chunk->getMicrocubeAt(localPos, subcubePos, microcubePos);
    if (!microcube) {
        LOG_ERROR("VoxelManipulation", "[MICROCUBE BREAKING] *** CRITICAL BUG *** Microcube not found at detected location!");
        
        // Debug: Check what actually exists
        auto subcubes = chunk->getSubcubesAt(localPos);
        LOG_ERROR_FMT("VoxelManipulation", "[DEBUG] Found " << subcubes.size() << " subcubes at parent position");
        
        bool foundAnyMicrocubes = false;
        for (int sx = 0; sx < 3; sx++) {
            for (int sy = 0; sy < 3; sy++) {
                for (int sz = 0; sz < 3; sz++) {
                    glm::ivec3 checkSubcubePos(sx, sy, sz);
                    auto micros = chunk->getMicrocubesAt(localPos, checkSubcubePos);
                    if (!micros.empty()) {
                        LOG_ERROR_FMT("VoxelManipulation", "[DEBUG] Found " << micros.size() 
                                  << " microcubes at subcube (" << sx << "," << sy << "," << sz << ")");
                        foundAnyMicrocubes = true;
                    }
                }
            }
        }
        
        if (!foundAnyMicrocubes) {
            LOG_ERROR("VoxelManipulation", "[DEBUG] NO microcubes found at this cube position!");
        }
        
        // Check voxelTypeMap
        auto voxelType = chunk->getVoxelType(localPos);
        LOG_ERROR_FMT("VoxelManipulation", "[DEBUG] VoxelTypeMap shows: " << (int)voxelType 
                  << " (0=EMPTY, 1=CUBE, 2=SUBDIVIDED)");
        
        return false;
    }
    
    // Store microcube data before removal
    glm::vec3 worldPos = microcube->getWorldPosition();
    bool isVisible = microcube->isVisible();
    float lifetime = microcube->getLifetime();
    glm::ivec3 parentCubePos = microcube->getParentCubePosition();
    std::string materialName = microcube->getMaterialName();
    
    // Remove the microcube from chunk
    bool removed = chunk->removeMicrocube(localPos, subcubePos, microcubePos);
    if (!removed) {
        LOG_WARN("VoxelManipulation", "[MICROCUBE BREAKING] WARNING: Failed to remove microcube from chunk");
        return false;
    }
    
    (void)applyForce; (void)isVisible; (void)lifetime; (void)parentCubePos;
    // worldPos is the microcube's min corner; the piece is 1/9 on a side.
    spawnPiece(worldPos + glm::vec3(0.5f / 9.0f), 1.0f / 9.0f, materialName);

    if (ChunkManager* chunkManager = getChunkManager())
        chunkManager->updateAfterCubeSubdivision(location.worldPos);

    LOG_INFO_FMT("VoxelManipulation", "[MICROCUBE BREAKING] Successfully broke microcube at world pos: (" 
              << location.worldPos.x << "," << location.worldPos.y << "," << location.worldPos.z 
              << ") subcube: (" << location.subcubePos.x << "," << location.subcubePos.y << "," << location.subcubePos.z 
              << ") microcube: (" << location.microcubePos.x << "," << location.microcubePos.y << "," << location.microcubePos.z << ")");
    
    return true;
}

bool VoxelManipulationSystem::spawnPiece(const glm::vec3& centre, float edge, const std::string& material) {
    GpuParticlePhysics* gpu = m_gpuDebris ? m_gpuDebris() : nullptr;
    return DamageSystem::spawnBreakDebris(gpu, centre, glm::vec3(0.0f), edge, material, glm::vec3(0.0f));
}

// =============================================================================
// VOXEL PLACEMENT OPERATIONS
// =============================================================================

bool VoxelManipulationSystem::placeCube(const glm::ivec3& worldPos, const glm::vec3& color) {
    ChunkManager* chunkManager = getChunkManager();
    if (!chunkManager) {
        LOG_ERROR("VoxelManipulation", "[PLACE CUBE] ChunkManager not available");
        return false;
    }
    
    LOG_INFO_FMT("VoxelManipulation", "[PLACE CUBE] Attempting to place at (" 
              << worldPos.x << "," << worldPos.y << "," << worldPos.z << ")");
    
    // Ensure chunk exists at target position (auto-create if needed)
    if (!ensureChunkExists(worldPos)) {
        LOG_ERROR_FMT("VoxelManipulation", "[PLACE CUBE] Failed to ensure chunk exists at world pos (" 
                  << worldPos.x << "," << worldPos.y << "," << worldPos.z << ")");
        return false;
    }
    
    LOG_INFO("VoxelManipulation", "[PLACE CUBE] Chunk exists, checking occupation...");
    
    // Check if position is already occupied
    if (chunkManager->hasVoxelAt(worldPos)) {
        VoxelLocation::Type existingType = chunkManager->getVoxelTypeAt(worldPos);
        LOG_WARN_FMT("VoxelManipulation", "[PLACE CUBE] Cannot place - position already occupied at (" 
                  << worldPos.x << "," << worldPos.y << "," << worldPos.z 
                  << ") with voxel type: " << (int)existingType 
                  << " (try placing on a face pointing to empty space)");
        return false;
    }
    
    LOG_INFO("VoxelManipulation", "[PLACE CUBE] Position empty, placing cube...");
    
    // Place the cube
    std::string material = m_materialProvider ? m_materialProvider() : "";
    bool success = material.empty()
        ? chunkManager->addCube(worldPos)
        : chunkManager->m_voxelModificationSystem.addCubeWithMaterial(worldPos, material);
    if (success) {
        LOG_INFO_FMT("VoxelManipulation", "[PLACE CUBE] Successfully placed cube at world pos (" 
                  << worldPos.x << "," << worldPos.y << "," << worldPos.z << ")");
        
        // Verify chunk is marked dirty for database persistence
        Chunk* targetChunk = chunkManager->getChunkAt(worldPos);
        if (targetChunk) {
            LOG_INFO_FMT("VoxelManipulation", "[PLACE CUBE] Target chunk dirty state: " 
                      << (targetChunk->getIsDirty() ? "DIRTY" : "CLEAN"));
        }
    } else {
        LOG_ERROR_FMT("VoxelManipulation", "[PLACE CUBE] addCube returned false at (" 
                  << worldPos.x << "," << worldPos.y << "," << worldPos.z << ")");
    }
    
    return success;
}

bool VoxelManipulationSystem::placeSubcube(const glm::ivec3& worldPos, const glm::ivec3& subcubePos, const glm::vec3& color) {
    ChunkManager* chunkManager = getChunkManager();
    if (!chunkManager) {
        LOG_ERROR("VoxelManipulation", "[PLACE SUBCUBE] ChunkManager not available");
        return false;
    }
    
    // Validate subcube position (must be 0-2 for each axis)
    if (subcubePos.x < 0 || subcubePos.x >= 3 ||
        subcubePos.y < 0 || subcubePos.y >= 3 ||
        subcubePos.z < 0 || subcubePos.z >= 3) {
        LOG_ERROR_FMT("VoxelManipulation", "[PLACE SUBCUBE] Invalid subcube position (" 
                  << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z << ")");
        return false;
    }
    
    // Ensure chunk exists at target position
    if (!ensureChunkExists(worldPos)) {
        LOG_ERROR_FMT("VoxelManipulation", "[PLACE SUBCUBE] Failed to ensure chunk exists at world pos (" 
                  << worldPos.x << "," << worldPos.y << "," << worldPos.z << ")");
        return false;
    }
    
    // Get the chunk
    Chunk* chunk = chunkManager->getChunkAt(worldPos);
    if (!chunk) {
        LOG_ERROR("VoxelManipulation", "[PLACE SUBCUBE] Chunk not found after creation");
        return false;
    }
    
    // Get local position within chunk
    glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(worldPos);
    
    // Check if this specific subcube position is already occupied
    Subcube* existing = chunk->getSubcubeAt(localPos, subcubePos);
    if (existing) {
        LOG_DEBUG_FMT("VoxelManipulation", "[PLACE SUBCUBE] Subcube position already occupied at cube (" 
                  << worldPos.x << "," << worldPos.y << "," << worldPos.z 
                  << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z << ")");
        return false;
    }
    
    // Place the subcube (this creates a standalone subcube without requiring parent cube)
    std::string material = m_materialProvider ? m_materialProvider() : "";
    bool success = material.empty()
        ? chunk->addSubcube(localPos, subcubePos)
        : chunkManager->m_voxelModificationSystem.addSubcubeWithMaterial(worldPos, subcubePos, material);
    if (success) {
        LOG_INFO_FMT("VoxelManipulation", "[PLACE SUBCUBE] Successfully placed subcube at world pos (" 
                  << worldPos.x << "," << worldPos.y << "," << worldPos.z 
                  << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z << ")");
        
        // Mark chunk as dirty and rebuild faces so the subcube becomes visible
        chunk->setDirty();
        chunk->setNeedsUpdate(true);
        chunkManager->updateAfterCubePlace(worldPos);
    } else {
        LOG_WARN("VoxelManipulation", "[PLACE SUBCUBE] Failed to place subcube");
    }
    
    return success;
}

bool VoxelManipulationSystem::placeMicrocube(const glm::ivec3& parentCubePos, const glm::ivec3& subcubePos, 
                                            const glm::ivec3& microcubePos, const glm::vec3& color) {
    ChunkManager* chunkManager = getChunkManager();
    if (!chunkManager) {
        LOG_ERROR("VoxelManipulation", "[PLACE MICROCUBE] ChunkManager not available");
        return false;
    }
    
    // Validate subcube and microcube positions (must be 0-2 for each axis)
    if (subcubePos.x < 0 || subcubePos.x >= 3 ||
        subcubePos.y < 0 || subcubePos.y >= 3 ||
        subcubePos.z < 0 || subcubePos.z >= 3) {
        LOG_ERROR_FMT("VoxelManipulation", "[PLACE MICROCUBE] Invalid subcube position (" 
                  << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z << ")");
        return false;
    }
    
    if (microcubePos.x < 0 || microcubePos.x >= 3 ||
        microcubePos.y < 0 || microcubePos.y >= 3 ||
        microcubePos.z < 0 || microcubePos.z >= 3) {
        LOG_ERROR_FMT("VoxelManipulation", "[PLACE MICROCUBE] Invalid microcube position (" 
                  << microcubePos.x << "," << microcubePos.y << "," << microcubePos.z << ")");
        return false;
    }
    
    // Ensure chunk exists at target position
    if (!ensureChunkExists(parentCubePos)) {
        LOG_ERROR_FMT("VoxelManipulation", "[PLACE MICROCUBE] Failed to ensure chunk exists at world pos (" 
                  << parentCubePos.x << "," << parentCubePos.y << "," << parentCubePos.z << ")");
        return false;
    }
    
    // Get the chunk
    Chunk* chunk = chunkManager->getChunkAt(parentCubePos);
    if (!chunk) {
        LOG_ERROR("VoxelManipulation", "[PLACE MICROCUBE] Chunk not found after creation");
        return false;
    }
    
    // Get local position within chunk
    glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(parentCubePos);
    
    // Check if this specific microcube position is already occupied
    Microcube* existing = chunk->getMicrocubeAt(localPos, subcubePos, microcubePos);
    if (existing) {
        LOG_DEBUG_FMT("VoxelManipulation", "[PLACE MICROCUBE] Microcube position already occupied at cube (" 
                  << parentCubePos.x << "," << parentCubePos.y << "," << parentCubePos.z 
                  << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z 
                  << ") microcube (" << microcubePos.x << "," << microcubePos.y << "," << microcubePos.z << ")");
        return false;
    }
    
    // Place the microcube (standalone, doesn't require parent subcube)
    std::string material = m_materialProvider ? m_materialProvider() : "";
    bool success = material.empty()
        ? chunk->addMicrocube(localPos, subcubePos, microcubePos)
        : chunkManager->m_voxelModificationSystem.addMicrocubeWithMaterial(parentCubePos, subcubePos, microcubePos, material);
    if (success) {
        LOG_INFO_FMT("VoxelManipulation", "[PLACE MICROCUBE] Successfully placed microcube at world pos (" 
                  << parentCubePos.x << "," << parentCubePos.y << "," << parentCubePos.z 
                  << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z 
                  << ") microcube (" << microcubePos.x << "," << microcubePos.y << "," << microcubePos.z << ")");
        
        // Mark chunk as dirty and rebuild faces so the microcube becomes visible
        chunk->setDirty();
        chunk->setNeedsUpdate(true);
        chunkManager->updateAfterCubePlace(parentCubePos);
    } else {
        LOG_WARN("VoxelManipulation", "[PLACE MICROCUBE] Failed to place microcube");
    }
    
    return success;
}

// =============================================================================
// HELPER METHODS
// =============================================================================

std::string VoxelManipulationSystem::selectMaterialForCube(const glm::vec3& cubeWorldPos) const {
    // Select material based on position — uses all registered materials
    std::vector<std::string> materials = {
        "Stone", "Cobblestone", "StoneBricks", "Dirt", "Gravel", "Sand",
        "Wood", "Log", "Bricks", "Sandstone", "Metal", "Gold", "Glass", "Ice", "Leaf", "glow"
    };
    int materialIndex = (abs(static_cast<int>(cubeWorldPos.x) + static_cast<int>(cubeWorldPos.z))) % static_cast<int>(materials.size());
    return materials[materialIndex];
}

glm::vec3 VoxelManipulationSystem::getCurrentPlacementColor() const {
    // For now, return a default grass-green color
    // TODO: Add material/color selection UI
    return glm::vec3(0.2f, 0.7f, 0.2f); // Grass green
}

bool VoxelManipulationSystem::ensureChunkExists(const glm::ivec3& worldPos) {
    ChunkManager* chunkManager = getChunkManager();
    if (!chunkManager) {
        return false;
    }
    
    // Check if chunk already exists
    Chunk* existingChunk = chunkManager->getChunkAt(worldPos);
    if (existingChunk) {
        return true; // Chunk already exists
    }
    
    // Calculate chunk coordinate from world position
    glm::ivec3 chunkCoord = Utils::CoordinateUtils::worldToChunkCoord(worldPos);
    
    LOG_INFO_FMT("VoxelManipulation", "[CHUNK CREATION] Auto-creating EMPTY chunk at coord (" 
              << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << ") for placement");
    
    // Create an EMPTY chunk (populate=false) for player placement - don't use world generator
    glm::ivec3 chunkOrigin = Utils::CoordinateUtils::chunkCoordToOrigin(chunkCoord);
    chunkManager->createChunk(chunkOrigin, false);
    
    // Verify creation succeeded
    Chunk* newChunk = chunkManager->getChunkAt(worldPos);
    if (!newChunk) {
        LOG_ERROR_FMT("VoxelManipulation", "[CHUNK CREATION] Failed to create chunk at coord (" 
                  << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << ")");
        return false;
    }
    
    LOG_DEBUG("VoxelManipulation", "[CHUNK CREATION] Successfully created chunk for placement");
    return true;
}

} // namespace Phyxel
