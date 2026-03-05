#include "core/ChunkVoxelBreaker.h"
#include "core/ChunkManager.h"
#include "physics/PhysicsWorld.h"
#include "utils/Logger.h"
#include <btBulletDynamicsCommon.h>

namespace VulkanCube {

/**
 * Configure callbacks for accessing Chunk's internal state
 * 
 * CALLBACK PATTERN RATIONALE:
 * ChunkVoxelBreaker needs to modify Chunk's data structures without creating circular dependencies.
 * Instead of holding a Chunk pointer (which would create tight coupling), we use lambda callbacks
 * that give us access to specific operations we need.
 * 
 * BENEFITS:
 * - No circular dependencies (ChunkVoxelBreaker doesn't include Chunk.h)
 * - Testable (can inject mock callbacks for unit testing)
 * - Clear interface (explicitly defines what Chunk state we access)
 * - Zero runtime cost (lambdas are inlined by compiler)
 * 
 * CALLBACKS:
 * - getSubcubesFunc: Access the vector of static subcubes for searching
 * - removeSubcubeFunc: Remove subcube from ALL data structures (voxelTypeMap, subcubeMap, etc.)
 * - rebuildFacesFunc: Trigger face rebuilding after voxel removal
 * - batchUpdateCollisionsFunc: Update physics collision shape after structural changes
 * - getMicrocubesAtFunc: Check if subcube position has been subdivided into microcubes
 * - getSubcubesAtFunc: Debug helper to list all subcubes at a position
 * - setNeedsUpdateFunc: Mark chunk for GPU buffer update
 * - worldOriginFunc: Get chunk's world origin for coordinate conversions
 */
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
    // Store all callbacks for later use
    m_getSubcubes = getSubcubesFunc;
    m_removeSubcube = removeSubcubeFunc;
    m_rebuildFaces = rebuildFacesFunc;
    m_batchUpdateCollisions = batchUpdateCollisionsFunc;
    m_getMicrocubesAt = getMicrocubesAtFunc;
    m_getSubcubesAt = getSubcubesAtFunc;
    m_setNeedsUpdate = setNeedsUpdateFunc;
    m_getWorldOrigin = worldOriginFunc;
}

/**
 * Break a static subcube into a dynamic physics-enabled object
 * 
 * ALGORITHM:
 * 1. Find the subcube in the static list by world position and local position
 * 2. Store subcube properties (color, visibility, lifetime) before removal
 * 3. Remove subcube from ALL data structures (vector, hash maps, collision shape)
 * 4. Create new dynamic Subcube with stored properties
 * 5. Create physics body with CENTER-BASED coordinates (not corner-based!)
 * 6. Transfer to ChunkManager's global dynamic system
 * 7. Rebuild faces and mark chunk dirty
 * 
 * COORDINATE SYSTEMS:
 * - Static subcubes use CORNER-BASED coordinates (bottom-left corner of subcube)
 * - Physics bodies use CENTER-BASED coordinates (center of mass)
 * - Conversion: physicsCenter = corner + (size / 2)
 * 
 * WHY NO IMPULSE FORCE?
 * - Subcubes break "gently" with gravity only (no explosive forces)
 * - Matches the visual behavior of voxels crumbling apart
 * - Physics body falls naturally under gravity (-9.81 m/s²)
 * 
 * @param parentPos Local position of parent cube in chunk (0-31, 0-31, 0-31)
 * @param subcubePos Local position of subcube within parent (0-2, 0-2, 0-2)
 * @param physicsWorld Physics world for creating rigid body (can be null)
 * @param chunkManager ChunkManager for global dynamic object registration
 * @param impulseForce Force to apply on break (currently unused - gravity only)
 * @return true if subcube was found and successfully broken
 */
bool ChunkVoxelBreaker::breakSubcube(
    const glm::ivec3& parentPos,
    const glm::ivec3& subcubePos,
    Physics::PhysicsWorld* physicsWorld,
    ChunkManager* chunkManager,
    const glm::vec3& impulseForce
) {
    // Get access to chunk's static subcube list via callback
    auto& staticSubcubes = m_getSubcubes();
    const glm::ivec3& worldOrigin = m_getWorldOrigin();
    
    LOG_DEBUG_FMT("ChunkVoxelBreaker", "[SUBCUBE BREAKING] Attempting to break subcube at parent (" 
                  << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                  << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z 
                  << ") - searching through " << staticSubcubes.size() << " static subcubes");
    
    // LINEAR SEARCH: Find the subcube in static list
    // Why not hash map? We already have parentPos and subcubePos, but subcubes are stored
    // in a flat vector. Hash map lookup requires world position, which we calculate here.
    // Performance: O(n) but n is typically small (< 100 subcubes per chunk)
    auto it = staticSubcubes.begin();
    while (it != staticSubcubes.end()) {
        Subcube* subcube = it->get();
        
        // Match both world position AND local position for precise identification
        // worldOrigin + parentPos = parent cube's world position
        // subcubePos = local offset within parent (0-2, 0-2, 0-2)
        if (subcube && 
            subcube->getPosition() == worldOrigin + parentPos && 
            subcube->getLocalPosition() == subcubePos) {
            
            // CRITICAL: Store ALL subcube properties before removal
            // We must preserve these because the removal operation will delete the object
            glm::vec3 worldPos = subcube->getWorldPosition();      // For physics positioning
            bool isVisible = subcube->isVisible();                 // Visibility state
            float lifetime = subcube->getLifetime();               // Time until expiration
            
            // CRITICAL DATA STRUCTURE UPDATE:
            // removeSubcube() updates ALL of Chunk's internal state:
            // 1. Removes from staticSubcubes vector (via erase-remove idiom)
            // 2. Removes from subcubeMap hash map (for O(1) lookups)
            // 3. Removes from voxelTypeMap (for hover detection)
            // 4. Updates parent cube's subdivision state if last subcube
            // If we skip this and just erase from vector, we get stale hash map entries!
            LOG_DEBUG("ChunkVoxelBreaker", "[INCREMENTAL] BEFORE: Using proper subcube removal to update all data structures");
            bool removed = m_removeSubcube(parentPos, subcubePos);
            if (!removed) {
                LOG_ERROR("ChunkVoxelBreaker", "[ERROR] Failed to remove subcube from data structures");
                return false;
            }
            
            // PHYSICS COLLISION UPDATE:
            // After removing a voxel, we must update the chunk's compound collision shape
            // This ensures physics simulation doesn't collide with the removed subcube
            // batchUpdateCollisions() rebuilds the entire compound shape efficiently
            m_batchUpdateCollisions();
            LOG_DEBUG("ChunkVoxelBreaker", "[INCREMENTAL] AFTER: All data structures updated and collision shape updated incrementally");
            
            // Create new dynamic subcube for physics (since original was removed)
            auto dynamicSubcube = std::make_unique<Subcube>(
                worldOrigin + parentPos, 
                subcubePos
            );
            
            // Set properties from stored data
            dynamicSubcube->setVisible(isVisible);
            dynamicSubcube->setLifetime(lifetime);
            dynamicSubcube->breakApart(); // Mark as broken
            
            // PHYSICS BODY CREATION:
            // Create a Bullet Physics rigid body if physics world is available
            if (physicsWorld) {
                // CRITICAL COORDINATE CONVERSION:
                // Static rendering uses CORNER-based coordinates (bottom-left corner of voxel)
                // Physics simulation uses CENTER-based coordinates (center of mass)
                // Why? Physics engines calculate torque/forces from center of mass
                // 
                // Subcube visual size: 1/3 of parent cube (0.333... units)
                // Conversion formula: center = corner + (size / 2)
                // 
                // Example: Corner at (1.0, 2.0, 3.0), size 0.333
                //          Center at (1.167, 2.167, 3.167)
                glm::vec3 subcubeCornerPos = worldPos;          // Corner (from static subcube)
                glm::vec3 subcubeSize(1.0f / 3.0f);             // 0.333... units (1/3 scale)
                glm::vec3 physicsCenterPos = subcubeCornerPos + (subcubeSize * 0.5f); // Center of mass
                
                // Create dynamic physics body:
                // - physicsCenterPos: Initial position (center of mass)
                // - subcubeSize: Collision box dimensions (0.333 x 0.333 x 0.333)
                // - 0.5f: Mass in kg (lightweight for realistic tumbling)
                btRigidBody* rigidBody = physicsWorld->createBreakawayCube(physicsCenterPos, subcubeSize, 0.5f);
                dynamicSubcube->setRigidBody(rigidBody);
                dynamicSubcube->setPhysicsPosition(physicsCenterPos);
                
                // GENTLE BREAKING BEHAVIOR:
                // No explosive impulse force applied - subcube falls naturally under gravity
                // This creates a "crumbling" effect rather than "exploding" effect
                // Gravity: -9.81 m/s² (Earth standard)
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
