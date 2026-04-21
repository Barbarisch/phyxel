#include "core/DynamicObjectManager.h"
#include "core/Subcube.h"
#include "core/Cube.h"
#include "core/Microcube.h"
#include "core/DebrisSystem.h"
#include "physics/PhysicsWorld.h"
#include "physics/VoxelRigidBody.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "scene/RagdollCharacter.h"
#include "utils/Logger.h"
#include <glm/gtc/quaternion.hpp>

namespace Phyxel {

DynamicObjectManager::DynamicObjectManager() = default;
DynamicObjectManager::~DynamicObjectManager() = default;

size_t DynamicObjectManager::getActiveBulletCount() const { return 0; }
size_t DynamicObjectManager::getTotalBulletCount() const { return 0; }

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
        Subcube* sub = it->get();

        // VoxelRigidBody marks itself isDead; we must call removeBody() before nulling.
        if (auto* vb = sub->getVoxelBody(); vb && vb->isDead) {
            if (physicsWorld && physicsWorld->getVoxelWorld())
                physicsWorld->getVoxelWorld()->removeBody(vb);
            sub->setVoxelBody(nullptr);
            removedCount++;
            it = subcubes.erase(it);
            continue;
        }

        sub->updateLifetime(deltaTime);

        if (sub->hasExpired()) {
            sub->setVoxelBody(nullptr);

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
            if (physicsWorld && physicsWorld->getVoxelWorld())
                physicsWorld->getVoxelWorld()->removeBody(vb);
            (*it)->setVoxelBody(nullptr);
            removedCount++;
            it = cubes.erase(it);
            continue;
        }

        (*it)->updateLifetime(deltaTime);

        if ((*it)->hasExpired()) {
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
            if (physicsWorld && physicsWorld->getVoxelWorld())
                physicsWorld->getVoxelWorld()->removeBody(vb);
            (*it)->setVoxelBody(nullptr);
            removedCount++;
            it = microcubes.erase(it);
            continue;
        }

        (*it)->updateLifetime(deltaTime);

        if ((*it)->hasExpired()) {
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
    
    if (m_debrisSystem) {
        m_debrisSystem->update(deltaTime);
    }
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

void DynamicObjectManager::enforceObjectLimits() {
    auto& cubes = m_getCubes();
    auto physicsWorld = m_getPhysicsWorld();
    
    if (cubes.size() > MAX_DYNAMIC_OBJECTS) {
        size_t removeCount = cubes.size() - MAX_DYNAMIC_OBJECTS;
        LOG_INFO_FMT("DynamicObject", "Enforcing object limit: Removing " << removeCount << " oldest dynamic cubes");
        
        // Bulk erase oldest cubes in one O(n) operation
        cubes.erase(cubes.begin(), cubes.begin() + static_cast<ptrdiff_t>(removeCount));
        
        m_rebuildFaces();
    }
}

void DynamicObjectManager::derezCharacter(Scene::RagdollCharacter* character, float explosionStrength) {
    if (!character) return;
    
    // OPTIMIZATION: Use DebrisSystem if available for lightweight particles
    if (m_debrisSystem) {
        LOG_INFO("DynamicObject", "Derezzing character into debris particles (Verlet System)");
        
        const auto& parts = character->getParts();
        int spawnedCount = 0;
        
        for (const auto& part : parts) {
            glm::vec3 pos = part.worldPos + part.offset;

            float randomX = ((rand() % 100) / 100.0f - 0.5f) * 4.0f * explosionStrength;
            float randomY = (((rand() % 100) / 100.0f) * 4.0f + 2.0f) * explosionStrength;
            float randomZ = ((rand() % 100) / 100.0f - 0.5f) * 4.0f * explosionStrength;

            glm::vec3 vel(randomX, randomY, randomZ);
            
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

    const auto& parts = character->getParts();
    int spawnedCount = 0;

    for (const auto& part : parts) {
        glm::vec3 pos = part.worldPos + part.offset;

        auto cube = std::make_unique<Cube>();
        cube->setDynamicScale(part.scale);

        float mass = 10.0f;
        auto* vw = physicsWorld->getVoxelWorld();
        Physics::VoxelRigidBody* newBody = vw
            ? vw->createVoxelBody(pos, part.scale * 0.5f, mass, 0.3f, 0.8f)
            : nullptr;

        if (!newBody) { ++spawnedCount; continue; }

        newBody->orientation = part.worldRot;

        float randomX = ((rand() % 100) / 100.0f - 0.5f) * 2.0f;
        float randomY = ((rand() % 100) / 100.0f) * 2.0f + 1.0f;
        float randomZ = ((rand() % 100) / 100.0f - 0.5f) * 2.0f;

        newBody->linearVelocity  = glm::vec3(randomX, randomY, randomZ) * explosionStrength;
        newBody->angularVelocity = glm::vec3(randomX, randomY, randomZ);

        cube->setVoxelBody(newBody);
        cube->setPhysicsPosition(pos);
        
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

} // namespace Phyxel
