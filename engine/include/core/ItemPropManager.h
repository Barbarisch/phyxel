#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

#include "physics/VoxelRigidBody.h"   // Physics::LocalBox (item collision compound)

namespace Phyxel {

class ChunkManager;
class ObjectTemplateManager;
class VoxelTemplate;

namespace Physics { class VoxelDynamicsWorld; }

namespace Core {

class PlacedObjectManager;
class KinematicVoxelManager;
class ItemEffectSystem;
struct KinematicVoxel;

// ============================================================================
// ItemPropManager — holdable items lying in the world ("item props").
//
// An item prop is the world-form of an ItemDefinition: spawned from the item's
// voxel template, rendered as a kinematic voxel group, and registered as a
// category="item" PlacedObject carrying a "pickup" interaction point. Item
// props are NEVER baked into chunk voxels — they are not terrain (unlike
// furniture, which re-staticizes at rest).
//
// Lifecycle: spawnProp (drop / world authoring) → [E] pickup → inventory.
// The held-in-hand presentation is separate (Application's held-item update).
// ============================================================================
class ItemPropManager {
public:
    struct Prop {
        std::string placedObjectId;
        std::string itemId;
        std::string kinId;        ///< KinematicVoxelManager object id
        std::string instanceUuid; ///< Item-instance uuid for a UNIQUE dropped item (empty for commodities)

        // --- Physics state (dynamic item props) ---
        uint32_t bodyId = 0;      ///< VoxelDynamicsWorld body id (0 = none); id-handle so a
                                  ///< world-killed body is seen as null, never dangling.
        bool  dynamic = false;    ///< Currently simulated (body alive).
        float restDwell = 0.0f;   ///< Seconds the body has been island-asleep.
        float scale = 1.0f;       ///< held.scale baked into voxels/boxes at spawn.
        glm::vec3 localCOM{0.0f}; ///< COM in (scaled) template-local space.
        glm::vec3 localLo{0.0f};  ///< Render AABB relative to COM (for placed-pose sync).
        glm::vec3 localHi{0.0f};
        std::vector<Physics::LocalBox> localBoxes;  ///< Collision compound (COM-relative);
                                                     ///< kept for bump re-physicalization.
        glm::mat4 lastTransform{1.0f};               ///< Last synced render pose.
        bool elongated = false;   ///< Long axis (local Y) dominates the footprint.
        int  tipAssists = 0;      ///< Tip-over nudges spent this rest cycle (cap 3).
        uint64_t dynamicSeq = 0;  ///< Monotonic "went dynamic" order (cap eviction).
    };

    /// Body sleeps this long (island rest) before the body is RETIRED — removed
    /// from the physics world, prop frozen as a plain kinematic at its settled
    /// pose. The item-class analogue of furniture's re-staticize (items never
    /// bake into chunks).
    static constexpr float kRestRetireSeconds = 1.5f;

    /// STATIC-FIRST cost bounds (2026-08-07 perf plan): at most this many item
    /// bodies simulate at once (evict-oldest-by-retire — narrowphase pairs grow
    /// as C(n,2) x M x N), and the collision compound is a COARSE geometry-only
    /// merge capped at kMaxColliderBoxes (render detail stays full).
    static constexpr int kMaxDynamicItems  = 6;
    static constexpr int kMaxColliderBoxes = 8;

    /// Tip-over assist: an ELONGATED item that falls asleep while its long
    /// axis is still this upright (|worldLongAxis.y|) gets woken with a small
    /// topple nudge instead of retiring — velocity-threshold sleep freezes a
    /// slow inverted-pendulum topple mid-lean, which reads as levitation
    /// (verified live twice: tip-balanced sword, mid-topple maul). Capped at
    /// 3 nudges per rest cycle so an item genuinely propped against geometry
    /// may legitimately stay leaning.
    static constexpr float kTipAssistUprightness = 0.6f;
    static constexpr int   kTipAssistMax = 3;

    void setDependencies(PlacedObjectManager* placed, ObjectTemplateManager* templates,
                         KinematicVoxelManager* kinematic, ChunkManager* chunks) {
        m_placed = placed; m_templates = templates; m_kinematic = kinematic; m_chunks = chunks;
    }

    /// Optional: item props register/unregister their declarative effects here.
    void setItemEffectSystem(ItemEffectSystem* effects) { m_effects = effects; }

    /// Wire the physics world. When set, spawned props are DYNAMIC rigid bodies
    /// (fall/tumble/slide, settle, retire); when null, props are static
    /// kinematic groups exactly as before (unit tests, headless tools).
    void setDynamicsWorld(Physics::VoxelDynamicsWorld* world) { m_dynamics = world; }

    /// Spawn an item prop in the world. snapToGround scans downward for the
    /// first solid voxel and rests the prop on top of it.
    /// STATIC-FIRST: by default the prop spawns SETTLED with no physics body
    /// (world-authored placement costs zero physics). Pass dynamic=true for
    /// drops/throws — initialVelocity then applies to the rigid body.
    /// Returns the placed-object id, or "" on failure (unknown/not-holdable
    /// item, missing template).
    std::string spawnProp(const std::string& itemId, const glm::vec3& position,
                          float yawDeg = 0.0f, bool snapToGround = true,
                          const std::string& instanceUuid = "",
                          const glm::vec3& initialVelocity = glm::vec3(0.0f),
                          bool dynamic = false);

    /// Explicit disturbance: physicalize a settled prop with an impulse (the
    /// attack-hit path — the ONLY revive besides drop/throw under static-first).
    /// Returns false for unknown props; a no-op true if already dynamic.
    bool hitProp(const std::string& placedObjectId, const glm::vec3& impulse);

    /// Per-frame: sync body poses to render + placed objects, retire rested
    /// bodies, revive props bumped by the player (playerPos/playerVel of the
    /// controlled character; defaults mean "no player nearby").
    void update(float dt,
                const glm::vec3& playerPos = glm::vec3(1.0e9f),
                const glm::vec3& playerVel = glm::vec3(0.0f));

    /// Remove a prop (kinematic group + placed-object entry).
    bool removeProp(const std::string& placedObjectId);

    /// Pick a prop up: removes it and returns its itemId ("" if not a prop). If outInstanceUuid is
    /// given, it receives the item-instance uuid (empty for a commodity) so the caller can restore
    /// the same instance identity into the inventory.
    std::string pickupProp(const std::string& placedObjectId, std::string* outInstanceUuid = nullptr);

    /// Tear down render state when a placed object is removed by someone else
    /// (PlacedObjectManager preRemove hook). Idempotent.
    void onPlacedObjectRemoved(const std::string& placedObjectId);

    /// Re-create kinematic groups for category="item" placed objects restored
    /// from the world DB. Call after loadFromDb().
    void rebuildFromPlacedObjects();

    const Prop* get(const std::string& placedObjectId) const {
        auto it = m_props.find(placedObjectId);
        return it != m_props.end() ? &it->second : nullptr;
    }
    bool isItemProp(const std::string& placedObjectId) const { return m_props.count(placedObjectId) > 0; }
    size_t count() const { return m_props.size(); }

    /// TIGHT world-space AABB of a prop's render geometry (its COM-local bounds
    /// pushed through the last synced pose). The registry's integer cube bbox
    /// makes every goblet a full-cube click target — neighbors on a crowded
    /// shelf then steal each other's clicks. False if the id is not a prop.
    bool worldAabb(const std::string& placedObjectId, glm::vec3& lo, glm::vec3& hi) const;

    /// Convert a voxel template to kinematic voxels (template-local space).
    /// Shared with DynamicFurnitureManager — the single source of truth for
    /// template→kinematic conversion.
    static std::vector<KinematicVoxel> voxelsFromTemplate(const VoxelTemplate& tmpl);

    /// Resolve an ItemDefinition::templateFile ("weapons/sword.voxel") to a
    /// loaded template, loading it on demand from resources/templates/ (the
    /// startup scan is non-recursive, so subdirectory templates are lazy).
    const VoxelTemplate* resolveItemTemplate(const std::string& templateFile) const;

private:
    /// Register a prop's effects (no-op without an effect system).
    void registerPropEffects(const Prop& prop);

    /// Create the rigid body for a prop from its stored collision compound at
    /// the given COM pose. Returns false when no dynamics world is wired.
    bool physicalizeProp(Prop& prop, const glm::vec3& comWorldPos,
                         const glm::quat& orientation, const glm::vec3& initialVelocity);

    /// Remove the body (if alive), freeze the prop at `pose`, and write the
    /// settled pose back to the placed object (bbox + pickup point).
    void retireProp(Prop& prop, const glm::mat4& pose);

    /// Push the current pose into the placed object (bbox + pickup point).
    void syncPlacedPose(const Prop& prop, const glm::mat4& pose);

    PlacedObjectManager*    m_placed    = nullptr;
    ObjectTemplateManager*  m_templates = nullptr;
    KinematicVoxelManager*  m_kinematic = nullptr;
    ChunkManager*           m_chunks    = nullptr;
    ItemEffectSystem*       m_effects   = nullptr;
    Physics::VoxelDynamicsWorld* m_dynamics = nullptr;
    uint64_t m_dynamicSeq = 0;   ///< Monotonic counter for Prop::dynamicSeq.

    std::unordered_map<std::string, Prop> m_props;  // by placedObjectId
};

} // namespace Core
} // namespace Phyxel
