#pragma once

#include "physics/VoxelDynamicsWorld.h"
#include <glm/glm.hpp>
#include <memory>

namespace Phyxel {
namespace Physics {

// Thin wrapper around VoxelDynamicsWorld.  All Bullet Physics code has been
// removed; this class now exists only for API compatibility at call sites that
// still hold a PhysicsWorld* pointer.
class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    bool initialize();
    void cleanup();

    void stepSimulation(float deltaTime, int maxSubSteps = 1, float fixedTimeStep = 1.0f/60.0f);
    void reset();

    void setGravity(const glm::vec3& gravity);
    glm::vec3 getGravity() const;

    void setFallThreshold(float threshold) { m_fallThreshold = threshold; }
    float getFallThreshold() const { return m_fallThreshold; }

    // Custom voxel physics world
    VoxelDynamicsWorld* getVoxelWorld() const { return m_voxelWorld.get(); }

private:
    std::unique_ptr<VoxelDynamicsWorld> m_voxelWorld;
    float m_fallThreshold = -20.0f;
};

} // namespace Physics
} // namespace Phyxel
