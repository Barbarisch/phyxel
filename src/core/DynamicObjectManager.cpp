#include "core/DynamicObjectManager.h"
#include "core/Subcube.h"
#include "core/Cube.h"
#include "core/Microcube.h"
#include "core/DebrisSystem.h"
#include "physics/PhysicsWorld.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "utils/Logger.h"
#include <btBulletDynamicsCommon.h>

namespace VulkanCube {

DynamicObjectManager::DynamicObjectManager() = default;
DynamicObjectManager::~DynamicObjectManager() = default;

void DynamicObjectManager::setCallbacks(
    PhysicsWorldAccessFunc getPhysicsWorldFunc,
    DynamicSubcubeVectorAccessFunc getSubcubesFunc,
    DynamicCubeVectorAccessFunc getCubesFunc,
    DynamicMicrocubeVectorAccessFunc getMicrocubesFunc,
    RebuildFacesFunc rebuildFacesFunc,
    ChunkVoxelQuerySystem* voxelQuerySystem
) {
    m_getPhysicsWorld = getPhysicsWorldFunc;
    m_getSubcubes = getSubcubesFunc;
    m_getCubes = getCubesFunc;
    m_getMicrocubes = getMicrocubesFunc;
    m_rebuildFaces = rebuildFacesFunc;
    
    if (voxelQuerySystem) {
        m_debrisSystem = std::make_unique<DebrisSystem>(voxelQuerySystem);
    }
}

// ===============================================================
// SUBCUBE MANAGEMENT
// ===============================================================

void DynamicObjectManager::addGlobalDynamicSubcube(std::unique_ptr<Subcube> subcube) {
    if (subcube) {
        LOG_DEBUG_FMT("DynamicObject", "Adding global dynamic subcube at world position: ("
                  << subcube->getPosition().x << "," << subcube->getPosition().y << "," << subcube->getPosition().z << ")");
        auto& subcubes = m_getSubcubes();
        subcubes.push_back(std::move(subcube));
        m_rebuildFaces();  // Rebuild faces after adding new subcube
    }
}

void DynamicObjectManager::updateGlobalDynamicSubcubes(float deltaTime) {
    auto& subcubes = m_getSubcubes();
    auto physicsWorld = m_getPhysicsWorld();
    auto it = subcubes.begin();
    size_t removedCount = 0;
    
    while (it != subcubes.end()) {
        (*it)->updateLifetime(deltaTime);
        
        if ((*it)->hasExpired()) {
            // Properly remove physics body from physics world
            if (physicsWorld && (*it)->getRigidBody()) {
                LOG_TRACE("DynamicObject", "Removing expired dynamic subcube physics body");
                physicsWorld->removeCube((*it)->getRigidBody());
            }
            
            removedCount++;
            it = subcubes.erase(it);
        } else {
            ++it;
        }
    }
    
    // Rebuild faces if any subcubes were removed
    if (removedCount > 0) {
        LOG_DEBUG_FMT("DynamicObject", "Removed " << removedCount << " expired dynamic subcubes (lifetime ended)");
        m_rebuildFaces();
    }
}

void DynamicObjectManager::updateGlobalDynamicSubcubePositions() {
    auto& subcubes = m_getSubcubes();
    bool transformsChanged = false;
    static int debugCounter = 0;
    static bool firstUpdate = true;
    
    if (firstUpdate && !subcubes.empty()) {
        LOG_TRACE("DynamicObject", "===== FIRST SUBCUBE PHYSICS UPDATE =====");
        LOG_TRACE_FMT("DynamicObject", "Found " << subcubes.size() << " dynamic subcubes to track");
        firstUpdate = false;
    }
    
    for (auto& subcube : subcubes) {
        if (subcube && subcube->getRigidBody()) {
            btRigidBody* body = subcube->getRigidBody();
            btTransform transform = body->getWorldTransform();
            
            glm::vec3 oldStoredPos = subcube->getPhysicsPosition();
            
            btVector3 btPos = transform.getOrigin();
            glm::vec3 newWorldPos(btPos.x(), btPos.y(), btPos.z());
            
            btQuaternion btRot = transform.getRotation();
            glm::vec4 newRotation(btRot.x(), btRot.y(), btRot.z(), btRot.w());
            
            subcube->setPhysicsPosition(newWorldPos);
            subcube->setPhysicsRotation(newRotation);
            transformsChanged = true;
            
            if (debugCounter % 60 == 0) {
                glm::vec3 movement = newWorldPos - oldStoredPos;
                float movementMag = glm::length(movement);
                
                LOG_TRACE("DynamicObject", "===== AFTER PHYSICS SIMULATION =====");
                LOG_TRACE_FMT("DynamicObject", "Physics body final position: (" 
                          << newWorldPos.x << ", " << newWorldPos.y << ", " << newWorldPos.z << ")");
                LOG_TRACE_FMT("DynamicObject", "Movement from last update: (" 
                          << movement.x << ", " << movement.y << ", " << movement.z << ") magnitude: " << movementMag);
                LOG_TRACE_FMT("DynamicObject", "Rotation: (" 
                          << newRotation.x << ", " << newRotation.y << ", " << newRotation.z << ", " << newRotation.w << ")");
                LOG_TRACE("DynamicObject", "===== END SUBCUBE POSITION TRACKING =====");
                break;
            }
        }
    }
    
    debugCounter++;
    
    if (transformsChanged) {
        m_rebuildFaces();
    }
}

void DynamicObjectManager::clearAllGlobalDynamicSubcubes() {
    auto& subcubes = m_getSubcubes();
    auto physicsWorld = m_getPhysicsWorld();
    
    if (physicsWorld) {
        for (auto& subcube : subcubes) {
            if (subcube && subcube->getRigidBody()) {
                LOG_TRACE("DynamicObject", "Cleaning up physics body for subcube during clear");
                physicsWorld->removeCube(subcube->getRigidBody());
                subcube->setRigidBody(nullptr);
            }
        }
    }
    
    LOG_DEBUG_FMT("DynamicObject", "Clearing all " << subcubes.size() << " global dynamic subcubes");
    subcubes.clear();
}

// ===============================================================
// CUBE MANAGEMENT
// ===============================================================

void DynamicObjectManager::addGlobalDynamicCube(std::unique_ptr<Cube> cube) {
    if (cube) {
        LOG_DEBUG_FMT("DynamicObject", "Adding global dynamic cube at world position: ("
                  << cube->getPosition().x << "," << cube->getPosition().y << "," << cube->getPosition().z << ")");
        auto& cubes = m_getCubes();
        cubes.push_back(std::move(cube));
        m_rebuildFaces();
    }
}

void DynamicObjectManager::updateGlobalDynamicCubes(float deltaTime) {
    auto& cubes = m_getCubes();
    auto physicsWorld = m_getPhysicsWorld();
    auto it = cubes.begin();
    size_t removedCount = 0;
    
    while (it != cubes.end()) {
        (*it)->updateLifetime(deltaTime);
        
        if ((*it)->hasExpired()) {
            if (physicsWorld && (*it)->getRigidBody()) {
                LOG_DEBUG("DynamicObject", "Removing expired dynamic cube physics body");
                physicsWorld->removeCube((*it)->getRigidBody());
            }
            
            removedCount++;
            it = cubes.erase(it);
        } else {
            ++it;
        }
    }
    
    if (removedCount > 0) {
        LOG_DEBUG_FMT("DynamicObject", "Removed " << removedCount << " expired dynamic cubes (lifetime ended)");
        m_rebuildFaces();
    }
}

void DynamicObjectManager::updateGlobalDynamicCubePositions() {
    auto& cubes = m_getCubes();
    bool transformsChanged = false;
    
    if (m_firstUpdate && !cubes.empty()) {
        LOG_DEBUG_FMT("DynamicObject", "[POSITION TRACK] ===== FIRST PHYSICS UPDATE =====");
        LOG_DEBUG_FMT("DynamicObject", "[POSITION TRACK] Found " << cubes.size() << " dynamic cubes to track");
        m_firstUpdate = false;
    }
    
    for (auto& cube : cubes) {
        if (cube && cube->getRigidBody()) {
            btRigidBody* body = cube->getRigidBody();
            btTransform transform = body->getWorldTransform();
            
            glm::vec3 oldStoredPos = cube->getPhysicsPosition();
            
            btVector3 btPos = transform.getOrigin();
            glm::vec3 newWorldPos(btPos.x(), btPos.y(), btPos.z());
            
            btQuaternion btRot = transform.getRotation();
            glm::vec4 newRotation(btRot.x(), btRot.y(), btRot.z(), btRot.w());
            
            cube->setPhysicsPosition(newWorldPos);
            cube->setPhysicsRotation(newRotation);
            transformsChanged = true;
            
            if (m_debugCounter % 60 == 0) {
                glm::vec3 movement = newWorldPos - oldStoredPos;
                float movementMag = glm::length(movement);
                
                LOG_DEBUG("DynamicObject", "[POSITION TRACK] ===== AFTER PHYSICS SIMULATION =====");
                LOG_DEBUG_FMT("DynamicObject", "[POSITION TRACK] 6. Physics body final position: (" 
                          << newWorldPos.x << ", " << newWorldPos.y << ", " << newWorldPos.z << ")");
                LOG_DEBUG_FMT("DynamicObject", "[POSITION TRACK] 7. Movement from last update: (" 
                          << movement.x << ", " << movement.y << ", " << movement.z << ") magnitude: " << movementMag);
                LOG_DEBUG_FMT("DynamicObject", "[POSITION TRACK] 8. Rotation: (" 
                          << newRotation.x << ", " << newRotation.y << ", " << newRotation.z << ", " << newRotation.w << ")");
                LOG_DEBUG("DynamicObject", "[POSITION TRACK] ===== END POSITION TRACKING =====");
                break;
            }
        }
    }
    
    m_debugCounter++;
    
    if (transformsChanged) {
        m_rebuildFaces();
    }
}

void DynamicObjectManager::clearAllGlobalDynamicCubes() {
    auto& cubes = m_getCubes();
    auto physicsWorld = m_getPhysicsWorld();
    
    if (physicsWorld) {
        for (auto& cube : cubes) {
            if (cube && cube->getRigidBody()) {
                LOG_TRACE("DynamicObject", "Cleaning up physics body for cube during clear");
                physicsWorld->removeCube(cube->getRigidBody());
                cube->setRigidBody(nullptr);
            }
        }
    }
    
    LOG_DEBUG_FMT("DynamicObject", "Clearing all " << cubes.size() << " global dynamic cubes");
    cubes.clear();
}

// ===============================================================
// MICROCUBE MANAGEMENT
// ===============================================================

void DynamicObjectManager::addGlobalDynamicMicrocube(std::unique_ptr<Microcube> microcube) {
    if (microcube) {
        LOG_DEBUG_FMT("DynamicObject", "[MICROCUBE] Adding global dynamic microcube at world position: ("
                  << microcube->getWorldPosition().x << "," << microcube->getWorldPosition().y << "," << microcube->getWorldPosition().z << ")");
        auto& microcubes = m_getMicrocubes();
        microcubes.push_back(std::move(microcube));
        m_rebuildFaces();
    }
}

void DynamicObjectManager::updateGlobalDynamicMicrocubes(float deltaTime) {
    auto& microcubes = m_getMicrocubes();
    auto physicsWorld = m_getPhysicsWorld();
    auto it = microcubes.begin();
    size_t removedCount = 0;
    
    while (it != microcubes.end()) {
        (*it)->updateLifetime(deltaTime);
        
        if ((*it)->hasExpired()) {
            if (physicsWorld && (*it)->getRigidBody()) {
                LOG_TRACE("DynamicObject", "[MICROCUBE] Removing expired dynamic microcube physics body");
                physicsWorld->removeCube((*it)->getRigidBody());
            }
            
            removedCount++;
            it = microcubes.erase(it);
        } else {
            ++it;
        }
    }
    
    if (removedCount > 0) {
        LOG_DEBUG_FMT("DynamicObject", "[MICROCUBE] Removed " << removedCount << " expired dynamic microcubes (lifetime ended)");
        m_rebuildFaces();
    }
}

void DynamicObjectManager::updateGlobalDynamicMicrocubePositions() {
    auto& microcubes = m_getMicrocubes();
    bool transformsChanged = false;
    
    for (auto& microcube : microcubes) {
        if (microcube && microcube->getRigidBody()) {
            btRigidBody* body = microcube->getRigidBody();
            btTransform transform = body->getWorldTransform();
            
            btVector3 btPos = transform.getOrigin();
            glm::vec3 newWorldPos(btPos.x(), btPos.y(), btPos.z());
            
            btQuaternion btRot = transform.getRotation();
            glm::vec4 newRotation(btRot.x(), btRot.y(), btRot.z(), btRot.w());
            
            microcube->setPhysicsPosition(newWorldPos);
            microcube->setPhysicsRotation(newRotation);
            transformsChanged = true;
        }
    }
    
    if (transformsChanged) {
        m_rebuildFaces();
    }
}

void DynamicObjectManager::clearAllGlobalDynamicMicrocubes() {
    auto& microcubes = m_getMicrocubes();
    auto physicsWorld = m_getPhysicsWorld();
    
    if (physicsWorld) {
        for (auto& microcube : microcubes) {
            if (microcube && microcube->getRigidBody()) {
                LOG_TRACE("DynamicObject", "[MICROCUBE] Cleaning up physics body for microcube during clear");
                physicsWorld->removeCube(microcube->getRigidBody());
                microcube->setRigidBody(nullptr);
            }
        }
    }
    
    LOG_DEBUG_FMT("DynamicObject", "[MICROCUBE] Clearing all " << microcubes.size() << " global dynamic microcubes");
    microcubes.clear();
}

// ===============================================================
// COMBINED OPERATIONS
// ===============================================================

void DynamicObjectManager::updateAllDynamicObjects(float deltaTime) {
    updateGlobalDynamicSubcubes(deltaTime);
    updateGlobalDynamicCubes(deltaTime);
    updateGlobalDynamicMicrocubes(deltaTime);
    
    if (m_debrisSystem) {
        m_debrisSystem->update(deltaTime);
    }
}

void DynamicObjectManager::updateAllDynamicObjectPositions() {
    updateGlobalDynamicSubcubePositions();
    updateGlobalDynamicCubePositions();
    updateGlobalDynamicMicrocubePositions();
}

void DynamicObjectManager::enforceObjectLimits() {
    auto& cubes = m_getCubes();
    auto physicsWorld = m_getPhysicsWorld();
    
    if (cubes.size() > MAX_DYNAMIC_OBJECTS) {
        size_t removeCount = cubes.size() - MAX_DYNAMIC_OBJECTS;
        LOG_INFO_FMT("DynamicObject", "Enforcing object limit: Removing " << removeCount << " oldest dynamic cubes");
        
        // Clean up physics bodies for the oldest cubes
        for (size_t i = 0; i < removeCount && i < cubes.size(); ++i) {
            if (physicsWorld && cubes[i] && cubes[i]->getRigidBody()) {
                physicsWorld->removeCube(cubes[i]->getRigidBody());
            }
        }
        
        // Bulk erase oldest cubes in one O(n) operation
        cubes.erase(cubes.begin(), cubes.begin() + static_cast<ptrdiff_t>(removeCount));
        
        m_rebuildFaces();
    }
}

void DynamicObjectManager::derezCharacter(void* characterPtr, float explosionStrength) {
    if (!characterPtr) return;
    
    auto* character = static_cast<Scene::AnimatedVoxelCharacter*>(characterPtr);
    
    // OPTIMIZATION: Use DebrisSystem if available for lightweight particles
    if (m_debrisSystem) {
        LOG_INFO("DynamicObject", "Derezzing character into debris particles (Verlet System)");
        
        const auto& parts = character->getParts();
        int spawnedCount = 0;
        
        for (const auto& part : parts) {
            if (!part.rigidBody) continue;
            
            // 1. Get current world transform
            btTransform trans;
            if (part.rigidBody->getMotionState()) {
                part.rigidBody->getMotionState()->getWorldTransform(trans);
            } else {
                trans = part.rigidBody->getWorldTransform();
            }
            
            btVector3 offset(part.offset.x, part.offset.y, part.offset.z);
            btVector3 worldPos = trans * offset;
            glm::vec3 pos(static_cast<float>(worldPos.x()), static_cast<float>(worldPos.y()), static_cast<float>(worldPos.z()));
            
            // 2. Velocity + Explosion
            btVector3 currentVel = part.rigidBody->getLinearVelocity();
            
            float randomX = ((rand() % 100) / 100.0f - 0.5f) * 4.0f * explosionStrength;
            float randomY = (((rand() % 100) / 100.0f) * 4.0f + 2.0f) * explosionStrength;
            float randomZ = ((rand() % 100) / 100.0f - 0.5f) * 4.0f * explosionStrength;
            
            glm::vec3 vel(
                currentVel.x() + randomX, 
                currentVel.y() + randomY, 
                currentVel.z() + randomZ
            );
            
            // 3. Spawn Particle
            m_debrisSystem->spawnDebris(
                pos, 
                vel, 
                part.scale, 
                part.color, 
                5.0f + (rand() % 50) / 10.0f
            );
            spawnedCount++;
        }
        
        LOG_INFO_FMT("DynamicObject", "Spawned " << spawnedCount << " debris particles");
        return;
    }

    auto* physicsWorld = m_getPhysicsWorld();
    
    if (!physicsWorld) {
        LOG_ERROR("DynamicObject", "Cannot derez character: Physics world not available");
        return;
    }
    
    LOG_INFO("DynamicObject", "Derezzing character into dynamic physics objects");
    
    // Use getParts() from RagdollCharacter base class
    const auto& parts = character->getParts();
    int spawnedCount = 0;
    
    for (const auto& part : parts) {
        if (!part.rigidBody) continue;
        
        // 1. Get current world transform of the bone
        btTransform trans;
        if (part.rigidBody->getMotionState()) {
            part.rigidBody->getMotionState()->getWorldTransform(trans);
        } else {
            trans = part.rigidBody->getWorldTransform();
        }
        
        // 2. Apply the visual offset (rotated by the body's rotation)
        // The part.offset is in local space relative to the bone
        btVector3 offset(part.offset.x, part.offset.y, part.offset.z);
        btVector3 worldPos = trans * offset;
        
        // 3. Create a new dynamic cube
        auto cube = std::make_unique<Cube>();
        
        // Set non-uniform scale based on the part size
        cube->setDynamicScale(part.scale);
        
        // Create physics body with matching size
        // Explicitly cast to float to avoid ambiguity if btScalar is double
        glm::vec3 pos(static_cast<float>(worldPos.x()), static_cast<float>(worldPos.y()), static_cast<float>(worldPos.z()));
        float mass = 10.0f;
        
        btRigidBody* newBody = physicsWorld->createCube(pos, part.scale, mass);
        
        // Match rotation
        newBody->setWorldTransform(trans); // Use bone rotation
        
        // Transfer velocity (add some randomness for explosion effect)
        btVector3 currentVel = part.rigidBody->getLinearVelocity();
        
        // Random explosion velocity
        float randomX = ((rand() % 100) / 100.0f - 0.5f) * 2.0f; // -1 to 1
        float randomY = ((rand() % 100) / 100.0f) * 2.0f + 1.0f; // 1 to 3 (upward)
        float randomZ = ((rand() % 100) / 100.0f - 0.5f) * 2.0f; // -1 to 1
        
        btVector3 explosionVel(randomX, randomY, randomZ);
        newBody->setLinearVelocity(currentVel + explosionVel);
        
        // Add random torque for tumbling
        newBody->setAngularVelocity(btVector3(randomX, randomY, randomZ));
        
        // Set physics properties
        newBody->setFriction(0.8f);
        newBody->setRestitution(0.3f);
        
        // Aggressive sleeping to save performance
        newBody->setSleepingThresholds(0.5f, 0.5f);
        
        // Link body to cube
        cube->setRigidBody(newBody);
        cube->setPhysicsPosition(glm::vec3(worldPos.x(), worldPos.y(), worldPos.z()));
        
        // Set lifetime (5-10 seconds)
        cube->setLifetime(5.0f + (rand() % 50) / 10.0f);
        
        // Add to manager
        addGlobalDynamicCube(std::move(cube));
        spawnedCount++;
    }
    
    LOG_INFO_FMT("DynamicObject", "Spawned " << spawnedCount << " debris objects from character derez");
    
    // Enforce limits immediately
    enforceObjectLimits();
}

} // namespace VulkanCube
