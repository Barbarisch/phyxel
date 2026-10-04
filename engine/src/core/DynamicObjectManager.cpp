#include "core/DynamicObjectManager.h"
#include "core/Subcube.h"
#include "core/Cube.h"
#include "core/Microcube.h"
#include "physics/PhysicsWorld.h"
#include "physics/VoxelRigidBody.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "scene/RagdollCharacter.h"
#include "utils/Logger.h"
#include <glm/gtc/quaternion.hpp>

namespace Phyxel {

// Every path that drops a CPU dynamic object must take its body out of the world first.
// Dropping only the Cube/Subcube/Microcube left the VoxelRigidBody simulating as an
// invisible collider that was never freed (DynamicObjectBodyReleaseTest).
template <typename T>
static void releaseBody(T& obj, Physics::PhysicsWorld* physicsWorld) {
    if (auto* vb = obj.getVoxelBody()) {
        if (physicsWorld && physicsWorld->getVoxelWorld())
            physicsWorld->getVoxelWorld()->removeBody(vb);
        obj.setVoxelBody(nullptr);
    }
}

template <typename Vec>
static void releaseAllBodies(Vec& objects, Physics::PhysicsWorld* physicsWorld) {
    for (auto& o : objects)
        if (o) releaseBody(*o, physicsWorld);
}

DynamicObjectManager::DynamicObjectManager() = default;
DynamicObjectManager::~DynamicObjectManager() = default;

size_t DynamicObjectManager::getDynamicObjectCount() const {
    size_t n = 0;
    if (m_getCubes)      n += m_getCubes().size();
    if (m_getSubcubes)   n += m_getSubcubes().size();
    if (m_getMicrocubes) n += m_getMicrocubes().size();
    return n;
}

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
        Subcube* sub = it->get();

        // VoxelRigidBody marks itself isDead; we must call removeBody() before nulling.
        if (auto* vb = sub->getVoxelBody(); vb && vb->isDead) {
            releaseBody(*sub, physicsWorld);
            removedCount++;
            it = subcubes.erase(it);
            continue;
        }

        sub->updateLifetime(deltaTime);

        if (sub->hasExpired()) {
            releaseBody(*sub, physicsWorld);

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
    static int debugCounter = 0;
    static bool firstUpdate = true;
    
    if (firstUpdate && !subcubes.empty()) {
        LOG_TRACE("DynamicObject", "===== FIRST SUBCUBE PHYSICS UPDATE =====");
        LOG_TRACE_FMT("DynamicObject", "Found " << subcubes.size() << " dynamic subcubes to track");
        firstUpdate = false;
    }
    
    for (auto& subcube : subcubes) {
        if (!subcube) continue;

        glm::vec3 newWorldPos;
        glm::vec4 newRotation;

        if (auto* vb = subcube->getVoxelBody()) {
            if (vb->isAsleep) continue;
            newWorldPos = vb->position;
            const glm::quat& q = vb->orientation;
            newRotation = glm::vec4(q.x, q.y, q.z, q.w);
        } else {
            continue;
        }

        glm::vec3 delta = newWorldPos - subcube->getPhysicsPosition();
        float distSq = glm::dot(delta, delta);
        if (distSq > MOVEMENT_THRESHOLD_SQ) {
            subcube->setPhysicsPosition(newWorldPos);
            subcube->setPhysicsRotation(newRotation);
            m_positionsDirty = true;
        }
    }
    
    debugCounter++;
}

void DynamicObjectManager::clearAllGlobalDynamicSubcubes() {
    auto& subcubes = m_getSubcubes();
    LOG_DEBUG_FMT("DynamicObject", "Clearing all " << subcubes.size() << " global dynamic subcubes");
    releaseAllBodies(subcubes, m_getPhysicsWorld ? m_getPhysicsWorld() : nullptr);
    subcubes.clear();
    m_rebuildFaces();
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
        if (auto* vb = (*it)->getVoxelBody(); vb && vb->isDead) {
            releaseBody(**it, physicsWorld);
            removedCount++;
            it = cubes.erase(it);
            continue;
        }

        (*it)->updateLifetime(deltaTime);

        if ((*it)->hasExpired()) {
            releaseBody(**it, physicsWorld);
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
    
    if (m_firstUpdate && !cubes.empty()) {
        LOG_DEBUG_FMT("DynamicObject", "[POSITION TRACK] ===== FIRST PHYSICS UPDATE =====");
        LOG_DEBUG_FMT("DynamicObject", "[POSITION TRACK] Found " << cubes.size() << " dynamic cubes to track");
        m_firstUpdate = false;
    }
    
    for (auto& cube : cubes) {
        if (!cube) continue;

        glm::vec3 newWorldPos;
        glm::vec4 newRotation;

        if (auto* vb = cube->getVoxelBody()) {
            if (vb->isAsleep) continue;
            newWorldPos = vb->position;
            const glm::quat& q = vb->orientation;
            newRotation = glm::vec4(q.x, q.y, q.z, q.w);
        } else {
            continue;
        }

        glm::vec3 delta = newWorldPos - cube->getPhysicsPosition();
        float distSq = glm::dot(delta, delta);
        if (distSq > MOVEMENT_THRESHOLD_SQ) {
            cube->setPhysicsPosition(newWorldPos);
            cube->setPhysicsRotation(newRotation);
            m_positionsDirty = true;
        }
    }

    m_debugCounter++;
}

void DynamicObjectManager::clearAllGlobalDynamicCubes() {
    auto& cubes = m_getCubes();
    LOG_DEBUG_FMT("DynamicObject", "Clearing all " << cubes.size() << " global dynamic cubes");
    releaseAllBodies(cubes, m_getPhysicsWorld ? m_getPhysicsWorld() : nullptr);
    cubes.clear();
    m_rebuildFaces();
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
        if (auto* vb = (*it)->getVoxelBody(); vb && vb->isDead) {
            releaseBody(**it, physicsWorld);
            removedCount++;
            it = microcubes.erase(it);
            continue;
        }

        (*it)->updateLifetime(deltaTime);

        if ((*it)->hasExpired()) {
            releaseBody(**it, physicsWorld);
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
    
    for (auto& microcube : microcubes) {
        if (!microcube) continue;

        glm::vec3 newWorldPos;
        glm::vec4 newRotation;

        if (auto* vb = microcube->getVoxelBody()) {
            if (vb->isAsleep) continue;
            newWorldPos = vb->position;
            const glm::quat& q = vb->orientation;
            newRotation = glm::vec4(q.x, q.y, q.z, q.w);
        } else {
            continue;
        }

        glm::vec3 delta = newWorldPos - microcube->getPhysicsPosition();
        float distSq = glm::dot(delta, delta);
        if (distSq > MOVEMENT_THRESHOLD_SQ) {
            microcube->setPhysicsPosition(newWorldPos);
            microcube->setPhysicsRotation(newRotation);
            m_positionsDirty = true;
        }
    }
}

void DynamicObjectManager::clearAllGlobalDynamicMicrocubes() {
    auto& microcubes = m_getMicrocubes();
    LOG_DEBUG_FMT("DynamicObject", "[MICROCUBE] Clearing all " << microcubes.size() << " global dynamic microcubes");
    releaseAllBodies(microcubes, m_getPhysicsWorld ? m_getPhysicsWorld() : nullptr);
    microcubes.clear();
    m_rebuildFaces();
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

    // Single batched rebuild: only if positions actually changed AND throttle interval elapsed
    if (m_positionsDirty) {
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - m_lastPositionRebuildTime).count();
        if (elapsed >= MIN_REBUILD_INTERVAL) {
            m_rebuildFaces();
            m_lastPositionRebuildTime = now;
        }
        m_positionsDirty = false;
    }
}

} // namespace Phyxel
