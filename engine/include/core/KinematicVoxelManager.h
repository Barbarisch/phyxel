#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include <btBulletDynamicsCommon.h>

namespace Phyxel {
namespace Physics { class PhysicsWorld; }

namespace Core {

/// Per-face instance data uploaded to the GPU for kinematic voxel rendering.
/// Layout must match kinematic_voxel.vert attribute bindings (locations 0-4) exactly.
/// 40 bytes, tightly packed.
///
/// Texture mapping: uvOffset is pre-computed per face on the CPU so that subcubes
/// and microcubes show only their portion of the parent cube's texture — matching
/// the same visual result as static chunk voxels. For full cubes, uvOffset=(0,0)
/// and scale=1.0, so the entire texture is used (no-op).
struct KinematicFaceData {
    glm::vec3 localPosition;   ///< Voxel center in object-local (hinge) space
    glm::vec3 scale;            ///< Per-axis scale: (1,1,1) = full cube, (1,1,0.125) = thin slab
    glm::vec2 uvOffset;         ///< UV offset within parent cube for sub-tile texture mapping
    uint32_t  textureIndex;    ///< Material texture atlas index
    uint32_t  faceId;           ///< 0=+Z  1=-Z  2=+X  3=-X  4=+Y  5=-Y
};
static_assert(sizeof(KinematicFaceData) == 40, "KinematicFaceData must be 40 bytes");

/// A single voxel within a KinematicVoxelObject, stored in object-local space.
/// Supports all three voxel sizes: cube (1.0), subcube (1/3), microcube (1/9).
struct KinematicVoxel {
    glm::vec3   localPos;                        ///< Voxel center in hinge-local space
    glm::vec3   scale = glm::vec3(1.0f);        ///< Per-axis scale: (1,1,1)=cube, (1/3)=subcube, (1/9)=microcube
    glm::vec3   parentFrac = glm::vec3(0.0f);   ///< Position within parent cube [0,1), for sub-tile UV mapping.
                                                 ///< Set from subcubePos/microcubePos before COM shift.
                                                 ///< Full cubes: (0,0,0). Microcube at grid (1,2,0): (1/9,2/9,0).
    std::string materialName = "Default";       ///< Material name for per-face texture lookup
};

/// A group of voxels rendered together with a shared world transform.
/// The transform is driven externally each frame by the owning system.
/// Voxel composition is fixed at creation; only the transform animates.
///
/// Used for any dynamic voxel object that moves as a rigid group:
/// doors, dynamic furniture, drawbridges, elevators, breakable fragments, etc.
///
/// Rendering: KinematicVoxelPipeline uploads pre-built face data to the GPU
/// and draws via kinematic_voxel.vert + voxel.frag. Supports cubes, subcubes,
/// and microcubes with correct sub-tile texture mapping.
struct KinematicVoxelObject {
    std::string id;
    std::string placedObjectId;            ///< Linked PlacedObject (empty if standalone)
    std::vector<KinematicVoxel>    voxels; ///< Voxels in hinge-local space
    std::vector<KinematicFaceData> faces;  ///< Pre-built face buffer (6 per voxel)
    glm::mat4   currentTransform{1.0f};   ///< World transform — set each frame by owner
    bool        visible = true;

    // Bullet kinematic body — owned by KinematicVoxelManager
    btRigidBody*           collider      = nullptr;
    btCollisionShape*      colliderShape = nullptr;
    btDefaultMotionState*  motionState   = nullptr;
};

/// Owns all KinematicVoxelObjects in the scene.
///
/// Callers update transforms each frame via setTransform(). The KinematicVoxelPipeline
/// reads getObjects() for rendering. syncCollidersToPhysics() must be called once per
/// frame (before the physics step) to keep Bullet in sync.
class KinematicVoxelManager {
public:
    explicit KinematicVoxelManager(Physics::PhysicsWorld* physicsWorld = nullptr);
    ~KinematicVoxelManager();

    void setPhysicsWorld(Physics::PhysicsWorld* pw) { m_physicsWorld = pw; }

    /// Register a new object for rendering. Builds the face buffer and optionally
    /// creates a kinematic Bullet box collider sized to the voxel AABB.
    ///
    /// IMPORTANT: Set skipCollider=true when the object already has its own physics
    /// body (e.g. DynamicFurnitureManager creates a btRigidBody compound shape).
    /// Creating a second collider at the same position causes Bullet to eject
    /// the dynamic body violently.
    std::string add(const std::string& idHint,
                    std::vector<KinematicVoxel> voxels,
                    const glm::mat4& initialTransform = glm::mat4(1.0f),
                    const std::string& placedObjectId = "",
                    bool skipCollider = false);

    /// Remove an object, destroy its Bullet collider, and free all resources.
    void remove(const std::string& id);

    /// Update the world transform. Call each frame for animated objects.
    void setTransform(const std::string& id, const glm::mat4& transform);

    glm::mat4 getTransform(const std::string& id) const;

    /// Push all current transforms to Bullet kinematic bodies.
    /// Call once per frame after all setTransform() calls, before stepSimulation().
    void syncCollidersToPhysics();

    const std::unordered_map<std::string, KinematicVoxelObject>& getObjects() const {
        return m_objects;
    }

    /// Returns true (and clears the flag) if the object set changed since the last call.
    /// Use this to decide whether to call KinematicVoxelPipeline::rebuildBuffer().
    bool consumeBufferDirty() {
        bool d = m_bufferDirty; m_bufferDirty = false; return d;
    }

    void   clear();
    size_t count() const { return m_objects.size(); }

private:
    /// Build 6 face entries per voxel (all faces, no CPU-side culling).
    static std::vector<KinematicFaceData> buildFaces(const std::vector<KinematicVoxel>& voxels);

    /// Compute AABB half-extents from a voxel list for collider sizing.
    static glm::vec3 computeHalfExtents(const std::vector<KinematicVoxel>& voxels);

    /// Create a kinematic Bullet box body and add it to the dynamics world.
    void createCollider(KinematicVoxelObject& obj, const glm::vec3& halfExtents);

    /// Remove the Bullet body from the world and free all Bullet objects.
    void destroyCollider(KinematicVoxelObject& obj);

    /// Convert a glm mat4 to a btTransform.
    static btTransform toBulletTransform(const glm::mat4& m);

    std::string generateId(const std::string& hint);

    Physics::PhysicsWorld* m_physicsWorld = nullptr;
    std::unordered_map<std::string, KinematicVoxelObject> m_objects;
    std::unordered_map<std::string, int> m_idCounters;
    bool m_bufferDirty = false;
};

} // namespace Core
} // namespace Phyxel
