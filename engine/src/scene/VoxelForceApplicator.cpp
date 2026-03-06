#include "scene/VoxelForceApplicator.h"
#include "scene/VoxelInteractionSystem.h"
#include "core/ChunkManager.h"
#include "core/ForceSystem.h"
#include "physics/PhysicsWorld.h"
#include "core/Chunk.h"
#include "core/Cube.h"
#include "utils/CoordinateUtils.h"
#include "utils/Logger.h"
#include <random>

namespace Phyxel {

void VoxelForceApplicator::breakHoveredCubeWithForce(
    const glm::vec3& cameraPos,
    double mouseX, double mouseY,
    const CubeLocation& currentHoveredLocation,
    bool hasHoveredCube,
    std::function<void(const glm::vec3&)> breakHoveredCubeCallback,
    std::function<void()> breakHoveredSubcubeCallback,
    ChunkManagerAccessFunc getChunkManager,
    ForceSystemAccessFunc getForceSystem,
    MouseVelocityAccessFunc getMouseVelocity
) {
    if (!hasHoveredCube || currentHoveredLocation.isSubcube) {
        // Just break normally if not applicable for force system
        if (currentHoveredLocation.isSubcube) {
            breakHoveredSubcubeCallback();
        } else {
            breakHoveredCubeCallback(cameraPos);
        }
        return;
    }
    
    // Get mouse velocity
    MouseVelocityTracker* mouseTracker = getMouseVelocity();
    if (!mouseTracker) {
        breakHoveredCubeCallback(cameraPos);
        return;
    }
    
    glm::vec2 mouseVelocity = mouseTracker->getVelocity();
    float speed = glm::length(mouseVelocity);
    
    // If high speed, use force propagation system
    ForceSystem* forceSystem = getForceSystem();
    if (speed > 500.0f && forceSystem) {
        LOG_INFO_FMT("Application", "[FORCE] High mouse speed detected: " << speed << " - propagating force");
        
        // Calculate click force
        glm::vec3 rayOrigin = cameraPos;
        glm::vec3 cubeCenter = glm::vec3(currentHoveredLocation.worldPos) + glm::vec3(0.5f);
        glm::vec3 rayDirection = glm::normalize(cubeCenter - rayOrigin);
        glm::vec3 hitPoint = cubeCenter;
        
        ForceSystem::ClickForce clickForce = forceSystem->calculateClickForce(
            mouseVelocity,
            rayOrigin,
            rayDirection,
            hitPoint
        );
        
        // Propagate force through chunk system
        ChunkManager* chunkManager = getChunkManager();
        if (chunkManager) {
            ForceSystem::PropagationResult result = forceSystem->propagateForce(
                clickForce,
                currentHoveredLocation.worldPos,
                chunkManager
            );
            
            LOG_INFO_FMT("Application", "[FORCE] Broke " << result.brokenCubes.size() 
                      << " cubes, damaged " << result.damagedCubes.size() << " cubes");
        }
    } else {
        // Normal break
        breakHoveredCubeCallback(cameraPos);
    }
}

void VoxelForceApplicator::breakCubeAtPosition(
    const glm::ivec3& worldPos,
    ChunkManagerAccessFunc getChunkManager,
    PhysicsWorldAccessFunc getPhysicsWorld,
    bool disableBreakingForces
) {
    ChunkManager* chunkManager = getChunkManager();
    if (!chunkManager) {
        LOG_WARN("Application", "[FORCE BREAKING] No ChunkManager available");
        return;
    }
    
    Chunk* chunk = chunkManager->getChunkAt(worldPos);
    if (!chunk) {
        LOG_WARN_FMT("Application", "[FORCE BREAKING] No chunk found for position (" 
                  << worldPos.x << "," << worldPos.y << "," << worldPos.z << ")");
        return;
    }
    
    glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(worldPos);
    const Cube* originalCube = chunk->getCubeAt(localPos);
    if (!originalCube) {
        LOG_WARN_FMT("Application", "[FORCE BREAKING] No cube found at position (" 
                  << worldPos.x << "," << worldPos.y << "," << worldPos.z << ")");
        return;
    }
    
    // Remove cube from chunk
    bool removed = chunk->removeCube(localPos);
    if (!removed) {
        LOG_WARN("Application", "[FORCE BREAKING] Failed to remove cube from chunk");
        return;
    }
    
    // Create dynamic cube for physics
    glm::vec3 cubeCornerPos = glm::vec3(worldPos);
    glm::vec3 physicsCenterPos = cubeCornerPos + glm::vec3(0.5f);
    
    // Select material based on position
    std::vector<std::string> materials = {"stone", "wood", "metal", "ice"};
    int materialIndex = (abs(worldPos.x) + abs(worldPos.z)) % materials.size();
    std::string selectedMaterial = materials[materialIndex];
    
    auto dynamicCube = std::make_unique<Cube>(cubeCornerPos, selectedMaterial);
    
    // Create physics body
    Physics::PhysicsWorld* physicsWorld = getPhysicsWorld();
    if (!physicsWorld) {
        LOG_WARN("Application", "[FORCE BREAKING] No PhysicsWorld available");
        return;
    }
    
    glm::vec3 cubeSize(1.0f);
    btRigidBody* rigidBody = physicsWorld->createBreakawayCube(physicsCenterPos, cubeSize, selectedMaterial);
    dynamicCube->setRigidBody(rigidBody);
    
    // Apply a small random force for natural breaking effect
    if (rigidBody && !disableBreakingForces) {
        glm::vec3 randomForce(
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 200.0f,
            100.0f + (static_cast<float>(rand()) / RAND_MAX) * 100.0f,
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 200.0f
        );
        
        btVector3 btImpulse(randomForce.x, randomForce.y, randomForce.z);
        rigidBody->applyCentralImpulse(btImpulse);
        
        // Add random angular velocity for tumbling effect
        btVector3 angularVelocity(
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 4.0f,
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 4.0f,
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 4.0f
        );
        rigidBody->setAngularVelocity(angularVelocity);
        rigidBody->setGravity(btVector3(0, -9.81f, 0));
    }
    
    // Mark as broken and add to global system
    dynamicCube->breakApart();
    chunkManager->addGlobalDynamicCube(std::move(dynamicCube));
    
    // Update affected chunks
    chunkManager->updateAfterCubeBreak(worldPos);
}

} // namespace Phyxel
