#include "core/DynamicObjectManager.h"
#include "core/Subcube.h"
#include "core/Cube.h"
#include "core/Microcube.h"
#include "physics/PhysicsWorld.h"
#include "utils/Logger.h"
#include <btBulletDynamicsCommon.h>

namespace VulkanCube {

void DynamicObjectManager::setCallbacks(
    PhysicsWorldAccessFunc getPhysicsWorldFunc,
    DynamicSubcubeVectorAccessFunc getSubcubesFunc,
    DynamicCubeVectorAccessFunc getCubesFunc,
    DynamicMicrocubeVectorAccessFunc getMicrocubesFunc,
    RebuildFacesFunc rebuildFacesFunc
) {
    m_getPhysicsWorld = getPhysicsWorldFunc;
    m_getSubcubes = getSubcubesFunc;
    m_getCubes = getCubesFunc;
    m_getMicrocubes = getMicrocubesFunc;
    m_rebuildFaces = rebuildFacesFunc;
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
}

void DynamicObjectManager::updateAllDynamicObjectPositions() {
    updateGlobalDynamicSubcubePositions();
    updateGlobalDynamicCubePositions();
    updateGlobalDynamicMicrocubePositions();
}

} // namespace VulkanCube
