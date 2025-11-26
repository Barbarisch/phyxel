#include "core/ChunkVoxelBreaker.h"
#include "core/ChunkManager.h"
#include "physics/PhysicsWorld.h"
#include "utils/Logger.h"
#include <btBulletDynamicsCommon.h>

namespace VulkanCube {

void ChunkVoxelBreaker::setCallbacks(
    GetSubcubesFunc getSubcubesFunc,
    RemoveSubcubeFunc removeSubcubeFunc,
    RebuildFacesFunc rebuildFacesFunc,
    BatchUpdateCollisionsFunc batchUpdateCollisionsFunc,
    GetMicrocubesAtFunc getMicrocubesAtFunc,
    GetSubcubesAtFunc getSubcubesAtFunc,
    SetNeedsUpdateFunc setNeedsUpdateFunc,
    WorldOriginAccessFunc worldOriginFunc
) {
    m_getSubcubes = getSubcubesFunc;
    m_removeSubcube = removeSubcubeFunc;
    m_rebuildFaces = rebuildFacesFunc;
    m_batchUpdateCollisions = batchUpdateCollisionsFunc;
    m_getMicrocubesAt = getMicrocubesAtFunc;
    m_getSubcubesAt = getSubcubesAtFunc;
    m_setNeedsUpdate = setNeedsUpdateFunc;
    m_getWorldOrigin = worldOriginFunc;
}

bool ChunkVoxelBreaker::breakSubcube(
    const glm::ivec3& parentPos,
    const glm::ivec3& subcubePos,
    Physics::PhysicsWorld* physicsWorld,
    ChunkManager* chunkManager,
    const glm::vec3& impulseForce
) {
    auto& staticSubcubes = m_getSubcubes();
    const glm::ivec3& worldOrigin = m_getWorldOrigin();
    
    LOG_DEBUG_FMT("ChunkVoxelBreaker", "[SUBCUBE BREAKING] Attempting to break subcube at parent (" 
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
            LOG_DEBUG("ChunkVoxelBreaker", "[INCREMENTAL] BEFORE: Using proper subcube removal to update all data structures");
            bool removed = m_removeSubcube(parentPos, subcubePos);
            if (!removed) {
                LOG_ERROR("ChunkVoxelBreaker", "[ERROR] Failed to remove subcube from data structures");
                return false;
            }
            // NEW: Fast collision update (already done in removeSubcube)
            m_batchUpdateCollisions();
            LOG_DEBUG("ChunkVoxelBreaker", "[INCREMENTAL] AFTER: All data structures updated and collision shape updated incrementally");
            
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
                LOG_DEBUG("ChunkVoxelBreaker", "[SUBCUBE PHYSICS] Created physics body for subcube (no forces applied - gravity only)");
                
                // Enable gravity for natural falling behavior
                if (rigidBody) {
                    rigidBody->setGravity(btVector3(0, -9.81f, 0));
                }
            }
            
            // Transfer the dynamic subcube directly to global system
            if (chunkManager) {
                // The dynamicSubcube is already properly configured, just transfer it
                chunkManager->addGlobalDynamicSubcube(std::move(dynamicSubcube));
                
                LOG_DEBUG("ChunkVoxelBreaker", "[GLOBAL TRANSFER] Moved broken subcube directly to global dynamic system (safe transfer)");
            } else {
                LOG_ERROR("ChunkVoxelBreaker", "[ERROR] No ChunkManager provided - cannot transfer to global system");
            }
            
            // Note: No need to delete subcube - it was already properly removed by removeSubcube()
            
            // Rebuild static faces only (no more dynamic faces in chunks)
            m_rebuildFaces();
            m_setNeedsUpdate(true);
            
            LOG_DEBUG_FMT("ChunkVoxelBreaker", "[CHUNK] Broke subcube at parent pos (" 
                      << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                      << ") subcube pos (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z 
                      << ") - transferred to global system safely");
            return true;
        }
        ++it;
    }
    
    LOG_ERROR_FMT("ChunkVoxelBreaker", "[SUBCUBE BREAKING] Subcube NOT FOUND in static list at parent (" 
                  << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                  << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z << ")");
    
    // Debug: Check if this position has microcubes instead
    auto microcubes = m_getMicrocubesAt(parentPos, subcubePos);
    if (!microcubes.empty()) {
        LOG_ERROR_FMT("ChunkVoxelBreaker", "[SUBCUBE BREAKING] DEBUG: Found " << microcubes.size() 
                      << " microcubes at this subcube position - subcube may have been subdivided");
    }
    
    // Debug: Check all subcubes at this parent position
    auto allSubcubes = m_getSubcubesAt(parentPos);
    LOG_ERROR_FMT("ChunkVoxelBreaker", "[SUBCUBE BREAKING] DEBUG: Total subcubes at parent position: " << allSubcubes.size());
    for (auto* sc : allSubcubes) {
        if (sc) {
            glm::ivec3 scPos = sc->getLocalPosition();
            LOG_ERROR_FMT("ChunkVoxelBreaker", "[SUBCUBE BREAKING] DEBUG:   - Subcube at (" 
                          << scPos.x << "," << scPos.y << "," << scPos.z << ")");
        }
    }
    
    return false; // Subcube not found in static list
}

} // namespace VulkanCube
