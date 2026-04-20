#pragma once

#include "physics/VoxelRigidBody.h"
#include "physics/VoxelOccupancyGrid.h"
#include "physics/VoxelContactSolver.h"
#include <vector>
#include <memory>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Phyxel {
namespace Physics {

// Purpose-built dynamics world for voxel rigid bodies.
// Handles dynamic broken voxels and compound kinetic objects (furniture, etc.).
// Static terrain is represented via registered VoxelOccupancyGrids rather than
// btCompoundShape, eliminating the per-removal AABB rebuild bottleneck.
//
// Bullet remains responsible for the character controller and ragdolls —
// this world does NOT replace those.
class VoxelDynamicsWorld {
public:
    VoxelDynamicsWorld();
    ~VoxelDynamicsWorld() = default;

    // ---- Configuration ----
    void setGravity(const glm::vec3& g)      { m_gravity = g; }
    glm::vec3 getGravity() const             { return m_gravity; }
    void setFallThreshold(float y)           { m_fallThreshold = y; }
    float getFallThreshold() const           { return m_fallThreshold; }

    // ---- Terrain ----
    // Register a chunk's occupancy grid. Does not take ownership.
    // Call when a chunk is loaded / physics body created.
    void registerGrid(VoxelOccupancyGrid* grid);
    // Call when a chunk is unloaded.
    void unregisterGrid(VoxelOccupancyGrid* grid);

    // ---- Body management ----
    // Create a new rigid body from one or more local boxes (compound-aware).
    // worldPos is the initial world-space position of the center of mass.
    VoxelRigidBody* createBody(const std::vector<LocalBox>& boxes,
                               const glm::vec3& worldPos,
                               const glm::quat& orientation = glm::quat(1,0,0,0),
                               float restitution = 0.2f,
                               float friction    = 0.6f,
                               float linearDamp  = 0.05f,
                               float angularDamp = 0.08f);

    // Helper: single-voxel box body (most common case for broken voxels)
    VoxelRigidBody* createVoxelBody(const glm::vec3& worldPos,
                                     const glm::vec3& halfExtents,
                                     float mass,
                                     float restitution = 0.2f,
                                     float friction    = 0.6f);

    void removeBody(VoxelRigidBody* body);
    void removeAllBodies();

    // ---- Simulation ----
    // Step the simulation, using substeps for stability.
    void stepSimulation(float deltaTime,
                        int   maxSubsteps  = 3,
                        float fixedStep    = 1.0f / 60.0f);

    // ---- Queries ----
    size_t getBodyCount()  const { return m_bodies.size(); }
    size_t getActiveCount() const;
    VoxelRigidBody* getBodyById(uint32_t id) const;

    // ---- Terrain queries (used by kinematic character controller) ----
    // Returns the highest terrain surface Y within a column below feetPos.
    // Returns -FLT_MAX if no terrain found within maxSearchDown.
    float findGroundY(const glm::vec3& feetPos, float halfWidth, float maxSearchDown) const;

    // Returns true if the given AABB overlaps any terrain voxel.
    bool overlapsTerrain(const glm::vec3& center, const glm::vec3& halfExtents) const;

    // Returns true if the given AABB overlaps any awake dynamic body.
    bool overlapsAnyBody(const glm::vec3& center, const glm::vec3& halfExtents) const;

    // ---- Kinematic obstacles (character segment boxes) ----
    struct KinematicObstacle {
        glm::vec3 center;
        glm::vec3 halfExtents;
        glm::vec3 velocity{0.0f};  // world-space velocity of the obstacle this frame
    };

    // Replaced every frame; voxels generate contacts against these and are deflected.
    // velocity is used by the contact solver to produce speed-proportional push impulses.
    void setKinematicObstacles(std::vector<KinematicObstacle> obstacles);

private:
    std::vector<std::unique_ptr<VoxelRigidBody>> m_bodies;
    std::vector<VoxelOccupancyGrid*>             m_grids;
    std::vector<ContactPoint>                    m_contacts;
    std::vector<KinematicObstacle>               m_kinematicObstacles;

    glm::vec3 m_gravity{0.0f, -9.81f, 0.0f};
    float     m_fallThreshold = -20.0f;
    float     m_accumulator   = 0.0f;
    uint32_t  m_nextId        = 1;

    void substep(float dt);
    void integrateVelocities(float dt);
    void integratePositions(float dt);
    void generateContacts();
    void updateSleepState(float dt);
    void cleanupDead();
};

} // namespace Physics
} // namespace Phyxel
