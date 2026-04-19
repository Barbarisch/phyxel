#pragma once

#include <vector>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Phyxel {
namespace Physics {

// One box in the body's local coordinate frame.
// A single broken voxel has one LocalBox. A piece of furniture has several.
struct LocalBox {
    glm::vec3 offset;       // center offset from body COM, in local space
    glm::vec3 halfExtents;
    float     mass;         // individual mass contribution (used to place COM)
};

// The world-space OBB representation of a LocalBox after applying the body transform.
struct WorldBox {
    glm::vec3 center;
    glm::vec3 halfExtents;  // local half-extents (same magnitude as LocalBox)
    glm::mat3 axes;         // columns are the OBB local axes in world space
};

class VoxelRigidBody {
public:
    explicit VoxelRigidBody(uint32_t id);

    // ---- Shape ----
    // Add boxes before calling finalizeShape().
    void addLocalBox(const LocalBox& box);

    // Compute COM, total mass, and inertia tensor from the current local boxes.
    // Must be called once before the body is stepped.
    void finalizeShape();

    const std::vector<LocalBox>& getLocalBoxes() const { return m_localBoxes; }
    float getTotalMass()  const { return m_totalMass; }

    // ---- State ----
    glm::vec3 position{0.0f};           // world-space COM
    glm::quat orientation{1,0,0,0};     // world-space orientation
    glm::vec3 linearVelocity{0.0f};
    glm::vec3 angularVelocity{0.0f};

    // ---- Material ----
    float friction      = 0.6f;
    float restitution   = 0.2f;
    float linearDamping = 0.05f;
    float angularDamping= 0.08f;

    // ---- Lifecycle ----
    float    lifetime = 30.0f;
    bool     isDead   = false;

    // ---- Sleeping ----
    bool  isAsleep    = false;
    float sleepTimer  = 0.0f;
    static constexpr float SLEEP_VELOCITY_SQ = 0.02f * 0.02f;
    static constexpr float SLEEP_ANGULAR_SQ  = 0.05f * 0.05f;
    static constexpr float SLEEP_TIME        = 1.2f;

    // ---- Cached inertia (updated by VoxelDynamicsWorld each step) ----
    float     invMass = 0.0f;
    glm::mat3 invInertiaTensorWorld{1.0f};

    uint32_t id = 0;

    // ---- Impulse application ----
    // Apply impulse at a world-space contact point (produces linear + angular change).
    void applyImpulse(const glm::vec3& impulse, const glm::vec3& worldPoint);

    // Apply impulse through the center of mass (linear only).
    void applyCentralImpulse(const glm::vec3& impulse);

    // Wake from sleep.
    void wake() { isAsleep = false; sleepTimer = 0.0f; }

    // Rebuild world-space inverse inertia tensor from current orientation.
    // Called by VoxelDynamicsWorld at the start of each substep.
    void updateInertiaTensorWorld();

    // Get the world-space OBB for a given local box index.
    WorldBox getWorldBox(size_t boxIndex) const;

    // World-space AABB enclosing all boxes (for broadphase).
    void getWorldAABB(glm::vec3& outMin, glm::vec3& outMax) const;

private:
    std::vector<LocalBox> m_localBoxes;
    float     m_totalMass = 1.0f;
    glm::mat3 m_invInertiaTensorLocal{1.0f};

    // Box inertia tensor for a single box about its own center, mass m, half-extents h.
    static glm::mat3 boxInertiaTensor(const glm::vec3& h, float m);
};

} // namespace Physics
} // namespace Phyxel
