#pragma once

#include "physics/VoxelRigidBody.h"
#include "physics/VoxelOccupancyGrid.h"
#include "physics/VoxelContactSolver.h"
#include <vector>
#include <memory>
#include <cstdint>
#include <thread>
#include <unordered_map>
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

    // Number of threads used for parallel physics phases (default = hardware_concurrency).
    // Set to 1 to disable threading.
    void setThreadCount(int n)   { m_threadCount = std::max(1, n); }
    int  getThreadCount() const  { return m_threadCount; }

    // ---- Terrain ----
    // Register a chunk's occupancy grid. Does not take ownership.
    // Call when a chunk is loaded / physics body created.
    void registerGrid(VoxelOccupancyGrid* grid);
    // Call when a chunk is unloaded.
    void unregisterGrid(VoxelOccupancyGrid* grid);
    // Number of registered terrain occupancy grids. Zero while chunks exist means
    // static-terrain collision is invisible to this world (characters fall through).
    size_t gridCount() const { return m_grids.size(); }

    // True if ANY registered static occupancy (cube/subcube/microcube) overlaps the
    // world-space AABB. This is the solidity characters actually collide with — nav
    // consumers use it so micro-thin geometry (parcel fences) blocks paths the same
    // way it blocks bodies (cube-level hasVoxelAt misses it; measured: residents
    // treadmilling on fence lines the NavGraph routed straight through).
    bool anyStaticSolidInAABB(const glm::vec3& lo, const glm::vec3& hi) const;

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
    VoxelRigidBody* getBodyByIndex(size_t i) const {
        return i < m_bodies.size() ? m_bodies[i].get() : nullptr;
    }

    // ---- Terrain queries (used by kinematic character controller) ----
    // Returns the highest terrain surface Y within a column below feetPos.
    // Returns -FLT_MAX if no terrain found within maxSearchDown.
    float findGroundY(const glm::vec3& feetPos, float halfWidth, float maxSearchDown) const;

    // Unified support query for the kinematic character: highest surface across
    // static terrain AND dynamic rigid bodies (m_bodies) below feetPos, so the
    // character can stand on dynamic furniture. Deliberately does NOT consider
    // kinematic obstacles — those are character segment boxes, and including them
    // makes a character detect its own body as ground (self-grounding). Dynamic
    // bodies contribute their AABB top. Returns -FLT_MAX if nothing found.
    float groundHeight(const glm::vec3& feetPos, float halfWidth, float maxSearchDown) const;

    // Returns true if the given AABB overlaps any terrain voxel.
    bool overlapsTerrain(const glm::vec3& center, const glm::vec3& halfExtents) const;

    // Returns true if the given AABB overlaps any awake dynamic body.
    bool overlapsAnyBody(const glm::vec3& center, const glm::vec3& halfExtents) const;

    // ---- Broadphase profiling (temporary instrumentation, docs/DestructionSystemV2.md §15.5) ----
    // Snapshot of the LAST substep's terrain-broadphase cost. Written at the end of
    // generateContacts, read on the same (main) thread by the debug endpoint. queryAABBCalls
    // is computed arithmetically (Σ awakeBoxes × gridCount) rather than by instrumenting the
    // parallel hot loop, so it does not perturb the timing it sits beside.
    struct BroadphaseStats {
        double   terrainBroadphaseMs = 0.0;  // wall-clock of the body-vs-terrain phase
        double   generateContactsMs  = 0.0;  // wall-clock of all of generateContacts
        uint64_t queryAABBCalls      = 0;    // Σ awakeBoxes × gridCount this substep
        size_t   gridCount           = 0;    // registered occupancy grids scanned
        size_t   awakeBodies         = 0;
        size_t   awakeBoxes          = 0;    // Σ collision boxes over awake bodies
        size_t   contactsGenerated   = 0;
    };
    const BroadphaseStats& lastBroadphaseStats() const { return m_broadphaseStats; }

    // ---- Kinematic obstacles (character segment boxes) ----
    struct KinematicObstacle {
        glm::vec3 center;
        glm::vec3 halfExtents;
        glm::vec3 velocity{0.0f};  // world-space velocity of the obstacle this frame
    };

    // Per-OWNER kinematic obstacles: each character registers its segment boxes under its own
    // key (its `this`) and refreshes them every frame. The solver deflects dynamic bodies away
    // from the UNION of all owners' boxes — so multiple characters all push furniture/debris,
    // not just the last one to update. Owners must removeKinematicObstacles() on destruction.
    void setKinematicObstacles(const void* owner, std::vector<KinematicObstacle> obstacles);
    void removeKinematicObstacles(const void* owner);

private:
    std::vector<std::unique_ptr<VoxelRigidBody>> m_bodies;
    std::vector<VoxelOccupancyGrid*>             m_grids;
    std::vector<ContactPoint>                    m_contacts;
    std::unordered_map<const void*, std::vector<KinematicObstacle>> m_obstaclesByOwner;
    std::vector<KinematicObstacle>               m_kinematicObstacles;  // flattened scratch (rebuilt per step)

    BroadphaseStats m_broadphaseStats;

    glm::vec3 m_gravity{0.0f, -9.81f, 0.0f};
    float     m_fallThreshold = -20.0f;
    float     m_accumulator   = 0.0f;
    uint32_t  m_nextId        = 1;
    int       m_threadCount   = 1;  // initialized to hardware_concurrency in constructor

    void substep(float dt);
    void integrateVelocities(float dt);
    void integratePositions(float dt);
    void generateContacts();
    void updateSleepState(float dt);
    void cleanupDead();

    // §15.5 / U1a broadphase index. m_grids stayed a flat vector scanned in full per
    // collision box per substep — O(worldSize), not O(object). m_gridByChunk keys each
    // registered grid by its chunk coordinate (origin/32, 1:1 with a chunk) so a query
    // touches only the handful of chunks its AABB overlaps. Both are maintained together;
    // the vector remains the ownership/iteration list, the map is the spatial accelerator.
    std::unordered_map<uint64_t, VoxelOccupancyGrid*> m_gridByChunk;

    // Pack a signed chunk coordinate (worldOrigin/32) into a map key (21 bits/axis).
    static uint64_t chunkKey(int cx, int cy, int cz) {
        uint64_t ux = static_cast<uint64_t>(static_cast<uint32_t>(cx + (1 << 20)) & 0x1FFFFF);
        uint64_t uy = static_cast<uint64_t>(static_cast<uint32_t>(cy + (1 << 20)) & 0x1FFFFF);
        uint64_t uz = static_cast<uint64_t>(static_cast<uint32_t>(cz + (1 << 20)) & 0x1FFFFF);
        return ux | (uy << 21) | (uz << 42);
    }

    // Collect the registered grids whose chunk overlaps the world-space AABB [mn,mx]
    // (usually 1, up to a few at chunk borders / for a big body). Conservative: an
    // included grid that a specific box misses is rejected cheaply inside queryAABB.
    void gatherGridsOverlapping(const glm::vec3& mn, const glm::vec3& mx,
                                std::vector<VoxelOccupancyGrid*>& out) const;
};

} // namespace Physics
} // namespace Phyxel
