#include "core/DamageSystem.h"
#include "scene/VoxelManipulationSystem.h"
#include "core/ChunkManager.h"
#include "core/Chunk.h"
#include "core/Cube.h"
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
// VOXEL BREAKING OPERATIONS (with physics)
// =============================================================================

bool VoxelManipulationSystem::breakCube(const CubeLocation& location, const glm::vec3& cameraPos, bool applyForce) {
    if (!location.isValid()) {
        LOG_DEBUG("VoxelManipulation", "[CUBE BREAKING] Invalid location provided");
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
