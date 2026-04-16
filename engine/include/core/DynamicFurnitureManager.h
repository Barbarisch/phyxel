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

    DynamicFurnitureManager() = default;
    ~DynamicFurnitureManager();

    // -- Dependencies (set before use) --
    void setPlacedObjectManager(PlacedObjectManager* m)     { m_placedObjects   = m; }
    void setObjectTemplateManager(ObjectTemplateManager* m)  { m_templateManager = m; }
    void setKinematicVoxelManager(KinematicVoxelManager* m)  { m_kinematic       = m; }
    void setPhysicsWorld(Physics::PhysicsWorld* m)           { m_physicsWorld    = m; }
    void setChunkManager(ChunkManager* m)                    { m_chunkManager    = m; }

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

    /// Per-frame update: sync transforms from Bullet, check sleep for re-staticize.
    void update(float dt);

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

    /// Build a btCompoundShape from KinematicVoxel positions.
    /// Returns the compound shape and populates childShapes vector.
    /// Also computes totalMass from per-voxel materials.
    btCompoundShape* buildCompoundShape(const std::vector<KinematicVoxel>& voxels,
                                        std::vector<btBoxShape*>& childShapes,
                                        float& totalMass) const;

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

    /// Destroy all Bullet objects for a DynamicFurnitureObject.
    void cleanupPhysics(DynamicFurnitureObject& obj);

    // Dependencies (non-owning)
    PlacedObjectManager*    m_placedObjects   = nullptr;
    ObjectTemplateManager*  m_templateManager = nullptr;
    KinematicVoxelManager*  m_kinematic       = nullptr;
    Physics::PhysicsWorld*  m_physicsWorld    = nullptr;
    ChunkManager*           m_chunkManager    = nullptr;

    std::unordered_map<std::string, DynamicFurnitureObject> m_active;
};

} // namespace Core
} // namespace Phyxel
