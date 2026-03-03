#include "scene/VoxelManipulationSystem.h"
#include "core/ChunkManager.h"
#include "core/Chunk.h"
#include "core/Cube.h"
#include "core/Subcube.h"
#include "core/Microcube.h"
#include "physics/PhysicsWorld.h"
#include "utils/Logger.h"
#include "utils/CoordinateUtils.h"
#include <btBulletDynamicsCommon.h>
#include <random>

namespace VulkanCube {

void VoxelManipulationSystem::setCallbacks(
    GetChunkManagerFunc chunkManagerFunc,
    GetPhysicsWorldFunc physicsWorldFunc) {
    
    getChunkManager = chunkManagerFunc;
    getPhysicsWorld = physicsWorldFunc;
}

// =============================================================================
// VOXEL REMOVAL OPERATIONS
// =============================================================================

bool VoxelManipulationSystem::removeVoxel(const CubeLocation& location) {
    if (!location.isValid()) {
        LOG_DEBUG("VoxelManipulation", "[REMOVAL] Invalid location provided");
        return false;
    }
    
    Chunk* chunk = location.chunk;
    if (!chunk) {
        LOG_ERROR("VoxelManipulation", "[REMOVAL] ERROR: Invalid chunk pointer");
        return false;
    }
    
    bool removed = false;
    
    if (location.isSubcube) {
        // VALIDATION: Verify subcube actually exists before trying to remove
        Subcube* subcube = chunk->getSubcubeAt(location.localPos, location.subcubePos);
        if (!subcube) {
            LOG_ERROR("VoxelManipulation", "[SUBCUBE REMOVAL] *** BUG *** Subcube not found at hover location!");
            
            // Debug what actually exists
            auto allSubcubes = chunk->getSubcubesAt(location.localPos);
            LOG_ERROR_FMT("VoxelManipulation", "[DEBUG] Found " << allSubcubes.size() << " subcubes at this cube position");
            auto voxelType = chunk->getVoxelType(location.localPos);
            LOG_ERROR_FMT("VoxelManipulation", "[DEBUG] VoxelTypeMap: " << (int)voxelType);
            
            return false;
        }
        
        // Remove a specific subcube
        removed = chunk->removeSubcube(location.localPos, location.subcubePos);
        if (removed) {
            LOG_DEBUG_FMT("VoxelManipulation", "[SUBCUBE REMOVAL] Successfully removed subcube at world pos: (" 
                      << location.worldPos.x << "," << location.worldPos.y << "," << location.worldPos.z 
                      << ") subcube: (" << location.subcubePos.x << "," << location.subcubePos.y << "," << location.subcubePos.z << ")");
                      
            // Check if this was the last subcube - if so, restore the parent cube
            auto remainingSubcubes = chunk->getSubcubesAt(location.localPos);
            if (remainingSubcubes.empty()) {
                // No more subcubes left, restore the parent cube
                Cube* parentCube = chunk->getCubeAt(location.localPos);
                if (parentCube) {
                    parentCube->show(); // Make parent cube visible again
                    LOG_DEBUG("VoxelManipulation", "[CUBE RESTORATION] Restored parent cube as no subcubes remain");
                }
            }
        }
    } else {
        // Remove a regular cube (this will also remove all its subcubes if subdivided)
        std::vector<Subcube*> subcubes = chunk->getSubcubesAt(location.localPos);
        if (!subcubes.empty()) {
            // First clear all subcubes
            chunk->clearSubdivisionAt(location.localPos);
            LOG_DEBUG("VoxelManipulation", "[SUBDIVISION REMOVAL] Cleared all subcubes for cube removal");
        }
        
        // Remove the cube itself
        removed = chunk->removeCube(location.localPos);
        if (removed) {
            LOG_DEBUG_FMT("VoxelManipulation", "[CUBE REMOVAL] Successfully removed cube at world pos: (" 
                      << location.worldPos.x << "," << location.worldPos.y << "," << location.worldPos.z << ")");
        }
    }
    
    if (!removed) {
        LOG_WARN("VoxelManipulation", "[REMOVAL] WARNING: Failed to remove object - it may not exist");
    }
    
    return removed;
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
    
    // Remove the cube from the chunk
    bool removed = chunk->removeCube(location.localPos);
    if (!removed) {
        LOG_WARN("VoxelManipulation", "[CUBE BREAKING] WARNING: Failed to remove cube from chunk");
        return false;
    }
    
    // Create a dynamic cube at the position
    glm::vec3 cubeCornerPos = cubeWorldPos;
    glm::vec3 physicsCenterPos = cubeCornerPos + glm::vec3(0.5f);
    
    // Select material based on position
    std::string selectedMaterial = selectMaterialForCube(cubeWorldPos);
    
    auto dynamicCube = std::make_unique<Cube>(cubeCornerPos, selectedMaterial);
    
    // Create physics body
    Physics::PhysicsWorld* physicsWorld = getPhysicsWorld();
    if (physicsWorld) {
        glm::vec3 cubeSize(1.0f);
        btRigidBody* rigidBody = physicsWorld->createBreakawayCube(physicsCenterPos, cubeSize, selectedMaterial);
        dynamicCube->setRigidBody(rigidBody);
        dynamicCube->setPhysicsPosition(physicsCenterPos);
        
        // Calculate and apply impulse force
        if (rigidBody && applyForce) {
            glm::vec3 forceDirection = normalize(cubeWorldPos - cameraPos);
            glm::vec3 impulseForce = forceDirection * 1.5f + glm::vec3(0.0f, 2.5f, 0.0f);
            
            btVector3 btImpulse(impulseForce.x, impulseForce.y, impulseForce.z);
            rigidBody->applyCentralImpulse(btImpulse);
            
            // Add random angular velocity for tumbling effect
            btVector3 angularVelocity(
                (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 4.0f,
                (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 4.0f,
                (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 4.0f
            );
            rigidBody->setAngularVelocity(angularVelocity);
        }
        
        if (rigidBody) {
            rigidBody->setGravity(btVector3(0, -9.81f, 0));
        }
    }
    
    // Mark as broken
    dynamicCube->breakApart();
    
    // Add to global dynamic cubes system
    ChunkManager* chunkManager = getChunkManager();
    if (chunkManager) {
        chunkManager->addGlobalDynamicCube(std::move(dynamicCube));
        
        // Update affected chunks
        chunkManager->updateAfterCubeBreak(location.worldPos);
    }
    
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
    
    LOG_DEBUG("VoxelManipulation", "[SUBCUBE BREAKING] Breaking subcube without forces (gentle removal)");
    
    // Break subcube WITHOUT any impulse forces (as requested)
    glm::vec3 noForce(0.0f, 0.0f, 0.0f); // No forces applied
    
    Physics::PhysicsWorld* physicsWorld = getPhysicsWorld();
    ChunkManager* chunkManager = getChunkManager();
    
    bool broken = chunk->breakSubcube(location.localPos, location.subcubePos, 
                                     physicsWorld, chunkManager, noForce);
    if (broken) {
        LOG_INFO_FMT("VoxelManipulation", "[SUBCUBE BREAKING] Successfully broke subcube (no forces) and transferred to global system at world pos: (" 
                  << location.worldPos.x << "," << location.worldPos.y << "," << location.worldPos.z 
                  << ") subcube: (" << location.subcubePos.x << "," << location.subcubePos.y << "," << location.subcubePos.z << ")");
                  
        // Use efficient selective update for subcube breaking
        if (chunkManager) {
            chunkManager->updateAfterSubcubeBreak(location.worldPos, location.subcubePos);
        }
    } else {
        LOG_WARN("VoxelManipulation", "[SUBCUBE BREAKING] WARNING: Failed to break subcube");
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
    
    // Remove the microcube from chunk
    bool removed = chunk->removeMicrocube(localPos, subcubePos, microcubePos);
    if (!removed) {
        LOG_WARN("VoxelManipulation", "[MICROCUBE BREAKING] WARNING: Failed to remove microcube from chunk");
        return false;
    }
    
    // Create new dynamic microcube for physics
    auto dynamicMicrocube = std::make_unique<Microcube>(parentCubePos, subcubePos, microcubePos);
    dynamicMicrocube->setVisible(isVisible);
    dynamicMicrocube->setLifetime(lifetime);
    dynamicMicrocube->breakApart(); // Mark as broken
    
    // Create physics body for dynamic microcube
    Physics::PhysicsWorld* physicsWorld = getPhysicsWorld();
    if (physicsWorld) {
        // Microcube is 1/9 scale, so size is 1/9 of a regular cube
        glm::vec3 microcubeCornerPos = worldPos; // Corner position
        glm::vec3 microcubeSize(1.0f / 9.0f);    // Match visual microcube size
        glm::vec3 physicsCenterPos = microcubeCornerPos + (microcubeSize * 0.5f); // Physics center position
        
        LOG_INFO_FMT("VoxelManipulation", "[MICROCUBE PHYSICS DEBUG] Corner pos: (" << microcubeCornerPos.x << "," << microcubeCornerPos.y << "," << microcubeCornerPos.z << ")");
        LOG_INFO_FMT("VoxelManipulation", "[MICROCUBE PHYSICS DEBUG] Center pos: (" << physicsCenterPos.x << "," << physicsCenterPos.y << "," << physicsCenterPos.z << ")");
        LOG_INFO_FMT("VoxelManipulation", "[MICROCUBE PHYSICS DEBUG] Size: " << microcubeSize.x);
        
        // Create dynamic physics body at center position (very light mass for tiny microcube)
        btRigidBody* rigidBody = physicsWorld->createBreakawayCube(physicsCenterPos, microcubeSize, 0.1f); // 0.1kg mass
        dynamicMicrocube->setRigidBody(rigidBody);
        dynamicMicrocube->setPhysicsPosition(physicsCenterPos);
        
        LOG_DEBUG("VoxelManipulation", "[MICROCUBE PHYSICS] Created physics body for microcube (no forces applied - gravity only)");
        
        // Enable gravity for natural falling behavior
        if (rigidBody) {
            rigidBody->setGravity(btVector3(0, -9.81f, 0));
        }
    }
    
    // Transfer the dynamic microcube to global system
    ChunkManager* chunkManager = getChunkManager();
    if (chunkManager) {
        chunkManager->addGlobalDynamicMicrocube(std::move(dynamicMicrocube));
        LOG_DEBUG("VoxelManipulation", "[GLOBAL TRANSFER] Moved broken microcube to global dynamic system");
        
        // Rebuild faces for this chunk
        chunkManager->updateAfterCubeSubdivision(location.worldPos);
    } else {
        LOG_ERROR("VoxelManipulation", "[ERROR] No ChunkManager provided - cannot transfer to global system");
    }
    
    LOG_INFO_FMT("VoxelManipulation", "[MICROCUBE BREAKING] Successfully broke microcube at world pos: (" 
              << location.worldPos.x << "," << location.worldPos.y << "," << location.worldPos.z 
              << ") subcube: (" << location.subcubePos.x << "," << location.subcubePos.y << "," << location.subcubePos.z 
              << ") microcube: (" << location.microcubePos.x << "," << location.microcubePos.y << "," << location.microcubePos.z << ")");
    
    return true;
}

bool VoxelManipulationSystem::breakCubeAtPosition(const glm::ivec3& worldPos, bool disableForces) {
    ChunkManager* chunkManager = getChunkManager();
    if (!chunkManager) {
        LOG_ERROR("VoxelManipulation", "[BREAK AT POSITION] No ChunkManager available");
        return false;
    }
    
    // Find the chunk containing this world position
    Chunk* chunk = chunkManager->getChunkAtFast(glm::vec3(worldPos));
    if (!chunk) {
        LOG_DEBUG_FMT("VoxelManipulation", "[BREAK AT POSITION] No chunk found at world pos (" 
                  << worldPos.x << "," << worldPos.y << "," << worldPos.z << ")");
        return false;
    }
    
    // Convert world position to local position within chunk
    glm::ivec3 localPos = worldPos - chunk->getWorldOrigin();
    
    // Get the cube
    const Cube* originalCube = chunk->getCubeAt(localPos);
    if (!originalCube) {
        LOG_DEBUG("VoxelManipulation", "[BREAK AT POSITION] No cube exists at this location");
        return false;
    }
    
    glm::vec3 cubeWorldPos = glm::vec3(worldPos);
    
    // Remove cube from chunk
    bool removed = chunk->removeCube(localPos);
    if (!removed) {
        LOG_WARN("VoxelManipulation", "[BREAK AT POSITION] Failed to remove cube");
        return false;
    }
    
    // Create dynamic cube
    glm::vec3 cubeCornerPos = cubeWorldPos;
    glm::vec3 physicsCenterPos = cubeCornerPos + glm::vec3(0.5f);
    
    std::string selectedMaterial = selectMaterialForCube(cubeWorldPos);
    auto dynamicCube = std::make_unique<Cube>(cubeCornerPos, selectedMaterial);
    
    // Create physics body
    Physics::PhysicsWorld* physicsWorld = getPhysicsWorld();
    if (physicsWorld) {
        glm::vec3 cubeSize(1.0f);
        btRigidBody* rigidBody = physicsWorld->createBreakawayCube(physicsCenterPos, cubeSize, selectedMaterial);
        dynamicCube->setRigidBody(rigidBody);
        dynamicCube->setPhysicsPosition(physicsCenterPos);
        
        if (rigidBody && !disableForces) {
            glm::vec3 forceDirection = glm::vec3(0.0f, 1.0f, 0.0f); // Default upward force
            glm::vec3 impulseForce = forceDirection * 2.0f;
            
            btVector3 btImpulse(impulseForce.x, impulseForce.y, impulseForce.z);
            rigidBody->applyCentralImpulse(btImpulse);
        }
        
        if (rigidBody) {
            rigidBody->setGravity(btVector3(0, -9.81f, 0));
        }
    }
    
    dynamicCube->breakApart();
    
    // Add to global system
    chunkManager->addGlobalDynamicCube(std::move(dynamicCube));
    chunkManager->updateAfterCubeBreak(worldPos);
    
    LOG_DEBUG_FMT("VoxelManipulation", "[BREAK AT POSITION] Broke cube at (" 
              << worldPos.x << "," << worldPos.y << "," << worldPos.z << ")");
    
    return true;
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
    bool success = chunkManager->addCube(worldPos);
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
    bool success = chunk->addSubcube(localPos, subcubePos);
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
    bool success = chunk->addMicrocube(localPos, subcubePos, microcubePos);
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
    // Select material based on position
    std::vector<std::string> materials = {"Wood", "Metal", "Glass", "Rubber", "Stone", "Ice", "Cork"};
    int materialIndex = (abs(static_cast<int>(cubeWorldPos.x) + static_cast<int>(cubeWorldPos.z))) % materials.size();
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

} // namespace VulkanCube
