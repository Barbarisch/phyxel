#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "physics/VoxelRigidBody.h"

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

    Physics::VoxelRigidBody* voxelBody = nullptr;  ///< Owned by VoxelDynamicsWorld

    glm::mat4 currentTransform{1.0f};
    float     totalMass       = 0.0f;
    float     sleepTimer      = 0.0f;
    bool      markedForRestaticize = false;
    bool      isGrabbed       = false;

    glm::vec3 prevLinearVelocity{0.0f};  ///< velocity from last frame for impact detection

    // Original placement info for re-staticizing
    glm::ivec3 originalPosition{0};
    int        originalRotation = 0;
};

/// Manages the lifecycle of dynamic (physics-driven) furniture objects.
///
/// Placed objects start as static voxels baked into chunks. When hit or bumped
/// with enough force, they are "activated" — static voxels are removed and
/// replaced with a VoxelRigidBody rendered via KinematicVoxelPipeline.
///
/// After coming to rest, objects automatically re-staticize (voxels placed back
/// into chunks at their new position).
class DynamicFurnitureManager {
public:
    static constexpr int    MAX_DYNAMIC_FURNITURE  = 16;
    static constexpr float  SLEEP_RESTATICIZE_TIME = 3.0f;
    static constexpr float  ACTIVATION_THRESHOLD   = 0.5f;
    static constexpr float  BREAK_FORCE_SCALAR     = 5000.0f;
    static constexpr int    MIN_FRAGMENT_VOXELS    = 4;
    static constexpr float  GRAB_HOLD_DISTANCE    = 3.0f;

    float massOverride = 0.0f;

    DynamicFurnitureManager() = default;
    ~DynamicFurnitureManager();

    void setPlacedObjectManager(PlacedObjectManager* m)     { m_placedObjects   = m; }
    void setObjectTemplateManager(ObjectTemplateManager* m)  { m_templateManager = m; }
    void setKinematicVoxelManager(KinematicVoxelManager* m)  { m_kinematic       = m; }
    void setPhysicsWorld(Physics::PhysicsWorld* m)           { m_physicsWorld    = m; }
    void setChunkManager(ChunkManager* m)                    { m_chunkManager    = m; }
    void setGpuParticlePhysics(GpuParticlePhysics* m)        { m_gpuParticles    = m; }

    bool activate(const std::string& placedObjectId,
                  const glm::vec3& impulse = glm::vec3(0.0f),
                  const glm::vec3& contactPoint = glm::vec3(0.0f));

    bool deactivate(const std::string& placedObjectId, bool restoreOriginal = false);

    int shatter(const std::string& placedObjectId,
                float contactForce,
                const glm::vec3& contactPoint);

    bool grab(const std::string& placedObjectId);
    void releaseGrab();
    void throwGrab(const glm::vec3& impulse);
    void updateGrabPosition(const glm::vec3& cameraPos, const glm::vec3& cameraFront);

    void checkPlayerFurnitureCollision(const glm::vec3& playerPos,
                                        const glm::vec3& playerVelocity,
                                        bool isSprinting);

    bool isGrabbing() const { return !m_grabbedObjectId.empty(); }
    const std::string& getGrabbedObjectId() const { return m_grabbedObjectId; }
    void update(float dt);

    void setObjectMass(const std::string& placedObjectId, float mass);

    bool isActive(const std::string& placedObjectId) const;
    glm::mat4 getTransform(const std::string& placedObjectId) const;
    size_t activeCount() const { return m_active.size(); }

    void deactivateAll();
    void clear();

    const std::unordered_map<std::string, DynamicFurnitureObject>& getActiveObjects() const {
        return m_active;
    }

private:
    std::vector<KinematicVoxel> buildVoxelsFromTemplate(const std::string& templateName) const;

    struct MergedBox {
        glm::vec3 center;
        glm::vec3 halfExtents;
        float     mass;
    };

    static std::vector<MergedBox> mergeVoxelsGreedy(const std::vector<KinematicVoxel>& voxels,
                                                     const Physics::MaterialManager& matMgr);

    static std::vector<std::vector<KinematicVoxel>> findConnectedComponents(
        const std::vector<KinematicVoxel>& voxels,
        const glm::vec3& contactPoint,
        float fractureRadius);

    void cleanupVoxelBody(DynamicFurnitureObject& obj);
    void checkFractureContacts(float dt);
    void evictOldest();

    PlacedObjectManager*    m_placedObjects   = nullptr;
    ObjectTemplateManager*  m_templateManager = nullptr;
    KinematicVoxelManager*  m_kinematic       = nullptr;
    Physics::PhysicsWorld*  m_physicsWorld    = nullptr;
    ChunkManager*           m_chunkManager    = nullptr;
    GpuParticlePhysics*     m_gpuParticles    = nullptr;

    std::unordered_map<std::string, DynamicFurnitureObject> m_active;
    std::string m_grabbedObjectId;
};

} // namespace Core
} // namespace Phyxel
