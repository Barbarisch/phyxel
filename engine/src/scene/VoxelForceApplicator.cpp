#include "scene/VoxelForceApplicator.h"
#include "scene/VoxelInteractionSystem.h"
#include "core/ChunkManager.h"
#include "core/ForceSystem.h"
#include "physics/PhysicsWorld.h"
#include "physics/VoxelDynamicsWorld.h"
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
        if (currentHoveredLocation.isSubcube) {
            breakHoveredSubcubeCallback();
        } else {
            breakHoveredCubeCallback(cameraPos);
        }
        return;
    }

    MouseVelocityTracker* mouseTracker = getMouseVelocity();
    if (!mouseTracker) {
        breakHoveredCubeCallback(cameraPos);
        return;
    }

    glm::vec2 mouseVelocity = mouseTracker->getVelocity();
    float speed = glm::length(mouseVelocity);

    ForceSystem* forceSystem = getForceSystem();
    if (speed > 500.0f && forceSystem) {
        LOG_INFO_FMT("Application", "[FORCE] High mouse speed detected: " << speed << " - propagating force");

        glm::vec3 rayOrigin = cameraPos;
        glm::vec3 cubeCenter = glm::vec3(currentHoveredLocation.worldPos) + glm::vec3(0.5f);
        glm::vec3 rayDirection = glm::normalize(cubeCenter - rayOrigin);
        glm::vec3 hitPoint = cubeCenter;

        ForceSystem::ClickForce clickForce = forceSystem->calculateClickForce(
            mouseVelocity, rayOrigin, rayDirection, hitPoint);

        ChunkManager* chunkManager = getChunkManager();
        if (chunkManager) {
            ForceSystem::PropagationResult result = forceSystem->propagateForce(
                clickForce, currentHoveredLocation.worldPos, chunkManager);
            LOG_INFO_FMT("Application", "[FORCE] Broke " << result.brokenCubes.size()
                      << " cubes, damaged " << result.damagedCubes.size() << " cubes");
        }
    } else {
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

    bool removed = chunk->removeCube(localPos);
    if (!removed) {
        LOG_WARN("Application", "[FORCE BREAKING] Failed to remove cube from chunk");
        return;
    }

    glm::vec3 cubeCornerPos = glm::vec3(worldPos);
    glm::vec3 physicsCenterPos = cubeCornerPos + glm::vec3(0.5f);

    std::vector<std::string> materials = {"stone", "wood", "metal", "ice"};
    int materialIndex = (abs(worldPos.x) + abs(worldPos.z)) % static_cast<int>(materials.size());
    std::string selectedMaterial = materials[materialIndex];

    auto dynamicCube = std::make_unique<Cube>(cubeCornerPos, selectedMaterial);

    Physics::PhysicsWorld* physicsWorld = getPhysicsWorld();
    if (!physicsWorld) {
        LOG_WARN("Application", "[FORCE BREAKING] No PhysicsWorld available");
        return;
    }

    auto* voxelWorld = physicsWorld->getVoxelWorld();
    if (voxelWorld) {
        glm::vec3 halfSize(0.475f); // slightly shrunk like the old breakaway cube
        Physics::VoxelRigidBody* body = voxelWorld->createVoxelBody(
            physicsCenterPos, halfSize, 1.0f, 0.2f, 0.8f);

        if (body && !disableBreakingForces) {
            glm::vec3 impulse(
                (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 200.0f,
                100.0f + (static_cast<float>(rand()) / RAND_MAX) * 100.0f,
                (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 200.0f
            );
            body->applyCentralImpulse(impulse);
            body->angularVelocity = glm::vec3(
                (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 4.0f,
                (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 4.0f,
                (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 4.0f
            );
        }
        dynamicCube->setVoxelBody(body);
    }

    dynamicCube->breakApart();
    chunkManager->addGlobalDynamicCube(std::move(dynamicCube));
    chunkManager->updateAfterCubeBreak(worldPos);
}

} // namespace Phyxel
