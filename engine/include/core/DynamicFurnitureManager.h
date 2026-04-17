#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

class btCompoundShape;
class btBoxShape;
class btRigidBody;
class btDefaultMotionState;

namespace Phyxel {

class ObjectTemplateManager;
class ChunkManager;
class GpuParticlePhysics;

namespace Physics {
    class PhysicsWorld;
    class MaterialManager;
}

namespace Core {

class PlacedObjectManager;
class KinematicVoxelManager;
struct KinematicVoxel;

/// A placed object that has been activated into a physics-driven dynamic body.
struct DynamicFurnitureObject {
    std::string placedObjectId;
    std::string templateName;
    std::string kineticObjId;      ///< ID in KinematicVoxelManager for rendering

    // Bullet physics — owned by this struct, cleaned up by DynamicFurnitureManager
    btCompoundShape*      compoundShape = nullptr;
    btRigidBody*          rigidBody     = nullptr;
    btDefaultMotionState* motionState   = nullptr;
    std::vector<btBoxShape*> childShapes;  ///< Owned child shapes in the compound

    glm::mat4 currentTransform{1.0f};
    float     totalMass       = 0.0f;
    float     sleepTimer      = 0.0f;   ///< Seconds since body went to sleep
    bool      markedForRestaticize = false;
    bool      isGrabbed       = false;  ///< True while player is holding this object

    // Original placement info for re-staticizing
    glm::ivec3 originalPosition{0};
    int        originalRotation = 0;
};

/// Manages the lifecycle of dynamic (physics-driven) furniture objects.
///
/// Placed objects start as static voxels baked into chunks. When hit or bumped
/// with enough force, they are "activated" — static voxels are removed and
/// replaced with a Bullet compound rigid body rendered via KinematicVoxelPipeline.
///
/// After coming to rest, objects automatically re-staticize (voxels placed back
/// into chunks at their new position).
class DynamicFurnitureManager {
public:
    static constexpr int    MAX_DYNAMIC_FURNITURE  = 16;
    static constexpr float  SLEEP_RESTATICIZE_TIME = 3.0f;  ///< Seconds at rest before re-staticize
    static constexpr float  ACTIVATION_THRESHOLD   = 0.5f;  ///< Force multiplier threshold
    static constexpr float  BREAK_FORCE_SCALAR     = 50.0f; ///< Contact force to exceed bondStrength * this
    static constexpr int    MIN_FRAGMENT_VOXELS    = 4;     ///< Fragments below this become GPU debris
    static constexpr float  GRAB_HOLD_DISTANCE    = 3.0f;  ///< Distance in front of camera when holding

    /// Override mass for all newly activated furniture (0 = use calculated mass)
    float massOverride = 0.0f;

    DynamicFurnitureManager() = default;
    ~DynamicFurnitureManager();

    // -- Dependencies (set before use) --
    void setPlacedObjectManager(PlacedObjectManager* m)     { m_placedObjects   = m; }
    void setObjectTemplateManager(ObjectTemplateManager* m)  { m_templateManager = m; }
    void setKinematicVoxelManager(KinematicVoxelManager* m)  { m_kinematic       = m; }
    void setPhysicsWorld(Physics::PhysicsWorld* m)           { m_physicsWorld    = m; }
    void setChunkManager(ChunkManager* m)                    { m_chunkManager    = m; }
    void setGpuParticlePhysics(GpuParticlePhysics* m)        { m_gpuParticles    = m; }

    /// Activate a placed object: remove static voxels, create compound rigid body.
    /// @param placedObjectId  ID from PlacedObjectManager
    /// @param impulse         Initial impulse to apply (world space)
    /// @param contactPoint    World-space point where force is applied
    /// @return true if activation succeeded
    bool activate(const std::string& placedObjectId,
                  const glm::vec3& impulse = glm::vec3(0.0f),
                  const glm::vec3& contactPoint = glm::vec3(0.0f));

    /// Deactivate: remove rigid body, place voxels back into chunks at current position.
    /// @param placedObjectId  ID of the active dynamic furniture
    /// @param restoreOriginal If true, place at original position instead of current
    bool deactivate(const std::string& placedObjectId, bool restoreOriginal = false);

    /// Shatter an active dynamic furniture into fragments based on contact force.
    /// Large fragments (>= MIN_FRAGMENT_VOXELS) become new compound bodies;
    /// small fragments are routed to GPU particle debris.
    /// @param placedObjectId  ID of the active dynamic furniture
    /// @param contactForce    Magnitude of impact force
    /// @param contactPoint    World-space point of impact
    /// @return number of fragments created (0 if shatter conditions not met)
    int shatter(const std::string& placedObjectId,
                float contactForce,
                const glm::vec3& contactPoint);

    /// Grab an active dynamic furniture object — switches to kinematic mode,
    /// held in front of the player camera. Only one object can be grabbed at a time.
    /// @return true if grab succeeded
    bool grab(const std::string& placedObjectId);

    /// Release the currently grabbed object back to dynamic mode.
    /// If velocity is near zero, the object will re-staticize after SLEEP_RESTATICIZE_TIME.
    void releaseGrab();

    /// Throw the grabbed object with an impulse in the given direction.
    void throwGrab(const glm::vec3& impulse);

    /// Update the grabbed object's position to track the camera.
    /// Call each frame while an object is grabbed.
    void updateGrabPosition(const glm::vec3& cameraPos, const glm::vec3& cameraFront);

    /// Check if a sprinting player collides with any placed furniture, activating it.
    /// Call each frame from the update loop.
    void checkPlayerFurnitureCollision(const glm::vec3& playerPos,
                                        const glm::vec3& playerVelocity,
                                        bool isSprinting);

    /// Is any object currently being grabbed?
    bool isGrabbing() const { return !m_grabbedObjectId.empty(); }

    /// Get the ID of the currently grabbed object.
    const std::string& getGrabbedObjectId() const { return m_grabbedObjectId; }
    void update(float dt);

    /// Set the mass of an active dynamic furniture object at runtime.
    /// Updates the Bullet rigid body inertia immediately.
    void setObjectMass(const std::string& placedObjectId, float mass);

    /// Is a placed object currently active as dynamic furniture?
    bool isActive(const std::string& placedObjectId) const;

    /// Get the current world transform of an active object.
    glm::mat4 getTransform(const std::string& placedObjectId) const;

    /// Get the number of currently active dynamic furniture objects.
    size_t activeCount() const { return m_active.size(); }

    /// Force deactivate all active furniture.
    void deactivateAll();

    /// Clear all state (does not re-staticize — use deactivateAll() first).
    void clear();

    const std::unordered_map<std::string, DynamicFurnitureObject>& getActiveObjects() const {
        return m_active;
    }

private:
    /// Build KinematicVoxel list from a VoxelTemplate (cubes + subcubes + microcubes).
    std::vector<KinematicVoxel> buildVoxelsFromTemplate(const std::string& templateName) const;

    /// A merged axis-aligned box used as a child shape in the compound.
    struct MergedBox {
        glm::vec3 center;       ///< Center position (local space)
        glm::vec3 halfExtents;  ///< Half-extents of the merged box
        float     mass;         ///< Accumulated mass from merged voxels
    };

    /// Greedy box merge: group adjacent voxels into larger axis-aligned boxes.
    /// Reduces child shape count from O(voxels) to O(10-30) for typical furniture.
    /// Uses a 3D occupancy grid at the finest voxel resolution and sweeps X→Y→Z.
    static std::vector<MergedBox> mergeVoxelsGreedy(const std::vector<KinematicVoxel>& voxels,
                                                     const Physics::MaterialManager& matMgr);

    /// Build a btCompoundShape from KinematicVoxel positions.
    /// Uses greedy box merge to minimize child shape count.
    /// Child shapes are recentered so the compound origin = center of mass.
    /// Returns the compound shape and populates childShapes vector.
    /// Also computes totalMass and centerOfMass (local-space offset from template origin).
    btCompoundShape* buildCompoundShape(const std::vector<KinematicVoxel>& voxels,
                                        std::vector<btBoxShape*>& childShapes,
                                        float& totalMass,
                                        glm::vec3& centerOfMass) const;

    /// Create a dynamic btRigidBody from a compound shape.
    btRigidBody* createDynamicBody(btCompoundShape* shape, float mass,
                                    const glm::mat4& transform,
                                    btDefaultMotionState*& outMotionState) const;

    /// Sync the Bullet rigid body transform to glm::mat4.
    static glm::mat4 readBulletTransform(btRigidBody* body);

    /// Convert glm::mat4 to Bullet transform.
    static void glmToBullet(const glm::mat4& m, btRigidBody* body);

    /// Evict the oldest active object to make room (re-staticize it).
    void evictOldest();

    /// Split voxels into connected components using flood fill.
    /// Returns a vector of voxel groups (each group is a connected fragment).
    static std::vector<std::vector<KinematicVoxel>> findConnectedComponents(
        const std::vector<KinematicVoxel>& voxels,
        const glm::vec3& contactPoint,
        float fractureRadius);

    /// Check Bullet contact manifolds for fracture-triggering impacts.
    void checkFractureContacts();

    /// Destroy all Bullet objects for a DynamicFurnitureObject.
    void cleanupPhysics(DynamicFurnitureObject& obj);

    // Dependencies (non-owning)
    PlacedObjectManager*    m_placedObjects   = nullptr;
    ObjectTemplateManager*  m_templateManager = nullptr;
    KinematicVoxelManager*  m_kinematic       = nullptr;
    Physics::PhysicsWorld*  m_physicsWorld    = nullptr;
    ChunkManager*           m_chunkManager    = nullptr;
    GpuParticlePhysics*     m_gpuParticles    = nullptr;

    std::unordered_map<std::string, DynamicFurnitureObject> m_active;
    std::string m_grabbedObjectId;  ///< Currently grabbed object (empty = none)
};

} // namespace Core
} // namespace Phyxel
