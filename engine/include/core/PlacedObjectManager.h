#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <functional>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <sqlite3.h>

namespace Phyxel {

class ChunkManager;
class ObjectTemplateManager;

namespace Core {

class SnapshotManager;

/// Types of object interactions. Each type has its own required animations and profile schema.
enum class ObjectInteractionType {
    Seat,        ///< Chair, bench, stool — requires sit/idle/stand animations
    Bed,         ///< Sleeping surface — requires lie-down/sleeping/wake-up animations
    DoorHandle,  ///< Door interaction — requires reach/push/pull animations
    Pickup,      ///< Item to pick up — requires reach/grab animations
    Ledge,       ///< Climbable edge — requires grab/hang/climb animations
    Window,      ///< Lookout point — requires lean animation
    Switch,      ///< Toggle lever/button — requires reach/flip animation
    Counter,     ///< Counter/table surface — requires lean/place animation
    Unknown      ///< Fallback for unrecognized types
};

/// Convert string type to enum (case-insensitive for common ones).
inline ObjectInteractionType objectInteractionTypeFromString(const std::string& s) {
    if (s == "seat")        return ObjectInteractionType::Seat;
    if (s == "bed")         return ObjectInteractionType::Bed;
    if (s == "door_handle") return ObjectInteractionType::DoorHandle;
    if (s == "pickup")      return ObjectInteractionType::Pickup;
    if (s == "ledge")       return ObjectInteractionType::Ledge;
    if (s == "window")      return ObjectInteractionType::Window;
    if (s == "switch")      return ObjectInteractionType::Switch;
    if (s == "counter")     return ObjectInteractionType::Counter;
    return ObjectInteractionType::Unknown;
}

/// Convert enum to string.
inline const char* objectInteractionTypeToString(ObjectInteractionType t) {
    switch (t) {
        case ObjectInteractionType::Seat:       return "seat";
        case ObjectInteractionType::Bed:        return "bed";
        case ObjectInteractionType::DoorHandle: return "door_handle";
        case ObjectInteractionType::Pickup:     return "pickup";
        case ObjectInteractionType::Ledge:      return "ledge";
        case ObjectInteractionType::Window:     return "window";
        case ObjectInteractionType::Switch:     return "switch";
        case ObjectInteractionType::Counter:    return "counter";
        default:                                return "unknown";
    }
}

/// Get required animation clip names for an interaction type.
inline std::vector<std::string> requiredAnimationsForType(ObjectInteractionType t) {
    switch (t) {
        case ObjectInteractionType::Seat:
            return {"stand_to_sit", "sitting_idle", "sit_to_stand"};
        case ObjectInteractionType::Bed:
            return {"lie_down", "sleeping_idle", "wake_up"};
        case ObjectInteractionType::DoorHandle:
            return {"reach_forward", "push_door"};
        case ObjectInteractionType::Pickup:
            return {"reach_down", "grab"};
        case ObjectInteractionType::Ledge:
            return {"grab_ledge", "hang_idle", "climb_up"};
        default:
            return {};
    }
}

/// Template-local definition of an interaction point (loaded from template .txt file, rotation-independent).
struct InteractionPointDef {
    std::string pointId;             ///< e.g. "seat_0"
    std::string type;                ///< "seat", "bed", "counter", etc.
    glm::vec3 localOffset{0.0f};     ///< Seat anchor in template-local space (cube units, 0° rotation)
    float facingYaw = 0.0f;          ///< Character facing direction (radians) at 0° object rotation

    /// Which interaction groups/archetypes can use this point.
    /// Empty = all archetypes supported (backward compatible default).
    std::vector<std::string> supportedGroups;

    /// Interaction radius override. 0 = use type default (seat: 1.5, door: 2.0, NPC: per-entity).
    float interactionRadius = 0.0f;

    /// UI prompt text shown when in range (e.g. "Open/Close", "Sit"). Empty = type default.
    std::string promptText;

    /// Half-angle (degrees) of the view cone required to interact. 0 = no angle check.
    float viewAngleHalf = 0.0f;

    /// When false, compatibility errors (e.g. hip width too wide, seat depth
    /// too short) are reported as warnings instead of blocking the interaction.
    /// Authors set this to false on "forgiving" points (e.g. a bench that any
    /// character can sit on, even if visually clipping). Defaults to true so
    /// strict-fit assets keep their gates.
    bool requireCompatibility = true;

    // Per-sit-state foot snap offsets (template-local, rotated at placement time)
    // These are default/fallback values; per-archetype profiles override them.
    glm::vec3 sitDownOffset{0.0f};   ///< Feet position during SitDown animation
    glm::vec3 sittingIdleOffset{0.0f};///< Feet position during SittingIdle loop
    glm::vec3 sitStandUpOffset{0.0f};///< Feet position during StandUp animation
    float sitBlendDuration = 0.0f;   ///< Animation crossfade duration (0 = instant clip switch)
    float seatHeightOffset = 0.0f;   ///< Direct Y offset on seat anchor position

    /// Get the typed interaction type enum.
    ObjectInteractionType interactionType() const { return objectInteractionTypeFromString(type); }
};

/// A live interaction point on a specific placed object instance.
struct InteractionPoint {
    std::string pointId;             ///< Matches InteractionPointDef::pointId
    std::string type;                ///< "seat", "bed", "counter", etc.
    glm::vec3 worldPos{0.0f};        ///< World-space seat anchor (updated when object moves/rotates)
    float facingYaw = 0.0f;          ///< Facing direction after applying object rotation
    std::string occupantId;          ///< Entity/NPC ID currently using this point ("" = free)

    /// Which interaction groups/archetypes can use this point (copied from def).
    std::vector<std::string> supportedGroups;

    /// Object rotation in degrees (stashed for on-the-fly profile offset rotation).
    int objectRotation = 0;

    /// Interaction radius for this point. 0 = use type default.
    float interactionRadius = 0.0f;

    /// UI prompt text shown when in range. Empty = type default.
    std::string promptText;

    /// Half-angle (degrees) of the view cone required to interact. 0 = no angle check.
    float viewAngleHalf = 0.0f;

    /// Copied from InteractionPointDef::requireCompatibility. When false,
    /// compat-check errors degrade to warnings (the gate still surfaces them
    /// but `can_interact` returns true).
    bool requireCompatibility = true;

    // Per-sit-state foot snap offsets (world-space, rotated from template-local defaults)
    glm::vec3 worldSitDownOffset{0.0f};
    glm::vec3 worldSittingIdleOffset{0.0f};
    glm::vec3 worldSitStandUpOffset{0.0f};
    float sitBlendDuration = 0.0f;
    float seatHeightOffset = 0.0f;

    bool isFree() const { return occupantId.empty(); }

    /// Check if this point supports a given archetype. Empty supportedGroups = all supported.
    bool supportsArchetype(const std::string& archetype) const {
        if (supportedGroups.empty()) return true;
        for (const auto& g : supportedGroups)
            if (g == archetype) return true;
        return false;
    }
};

/// Metadata for a placed object (template or structure) in the world.
struct PlacedObject {
    std::string id;                  ///< Legacy human-readable ID, e.g. "test_chair_3" (map key / parentId ref)
    std::string uuid;                ///< Stable RFC-4122 v4 UUID (persistent, non-semantic) — see Core::Uuid
    std::string templateName;        ///< Template or structure type name
    std::string category;            ///< "template" or "structure"
    std::string parentId;            ///< Parent object ID (empty = root/world)
    glm::ivec3 position{0};          ///< World-space origin where placed
    int rotation = 0;                ///< Y-axis rotation in degrees (0/90/180/270)
    glm::ivec3 boundingMin{0};       ///< World-space AABB min corner
    glm::ivec3 boundingMax{0};       ///< World-space AABB max corner
    /// MICRO-precise anchor when the object was placed via placeTemplateMicro.
    /// Removal re-rasterizes the template at this exact pose and erases only those
    /// cells; without it, removal falls back to clearing whole CUBES over the bbox,
    /// which takes the wall behind the furniture with it.
    glm::ivec3 microAnchor{0};
    bool placedAtMicro = false;
    std::chrono::system_clock::time_point createdAt;

    /// Live interaction points for this instance (seat surfaces, etc.)
    std::vector<InteractionPoint> interactionPoints;

    /// Extensible per-object state blob. Subsystems store their own keys here
    /// (e.g. doors write {"door_state":"open","current_angle":90.0}).
    /// Persisted to SQLite alongside the rest of the object.
    nlohmann::json metadata = nlohmann::json::object();

    nlohmann::json toJson() const;
    static PlacedObject fromJson(const nlohmann::json& j);
};

/// Tracks all placed objects (static templates and structures) in the world.
/// Objects are still voxels in chunks — this provides an addressable registry
/// so they can be listed, moved, rotated, and removed as units.
class PlacedObjectManager {
public:
    PlacedObjectManager(ChunkManager* chunkMgr, ObjectTemplateManager* templateMgr,
                        SnapshotManager* snapshotMgr);

    /// Place a template and register it. Returns object ID, or empty on failure.
    /// When snapToGround is true (default), the template's lowest voxel is
    /// seated on the surface directly beneath the given position — so callers
    /// that pass a slightly-high Y (a common off-by-one against spawn_y) don't
    /// leave the object hovering a cube above the ground. Pass false to honor
    /// the exact Y (intentional elevated placement).
    std::string placeTemplate(const std::string& templateName, const glm::ivec3& position,
                              int rotation = 0, const std::string& parentId = "",
                              bool snapToGround = true);

    /// RENDER-ACCURATE cube bbox for a MICRO-placed template. Unlike computeTemplateBounds (which
    /// anchors on the CUBE-truncated position and uses the template's cube-coordinate extents), this
    /// mirrors ObjectTemplateManager::spawnTemplateMicro exactly: it expands the template to its
    /// template-local MICRO AABB, rotates it about the micro pivot, shifts by the exact (off-grid)
    /// `worldMicro`, and floor-divides to cubes. So the registered bbox equals what actually renders —
    /// including the sub-cube "micro-spill" a wall-inset anchor pushes into the next cube (which the
    /// cube-anchored bounds silently drop). Matches FurniturePlacer::placedCubeSpan (the reservation),
    /// so reservation == registration == render. Returns {min,max}; {worldMicro/9, same} if no template.
    std::pair<glm::ivec3, glm::ivec3> computeMicroPlacedBounds(
        const std::string& templateName, const glm::ivec3& worldMicro, int rotation) const;

    /// Place a STATIC template at a MICRO-precise world position (sub-cube), via
    /// ObjectTemplateManager::spawnTemplateMicro, AND register a PlacedObject so the piece stays
    /// addressable + removable (parent/metadata/bbox). `worldMicro` = cube*9 + micro per axis. Used
    /// for furniture/fixtures so they sit flush against thin sub-cube walls and on the mid-cube
    /// walkable surface (no clipping / sinking). No ground-snap (the caller supplies the exact Y).
    std::string placeTemplateMicro(const std::string& templateName, const glm::ivec3& worldMicro,
                                   int rotation = 0, const std::string& parentId = "");

    /// Outcome of deterministically seating a structure onto the terrain.
    /// All fields are MEASURED from the live world / template geometry — none assumed.
    struct SeatPlan {
        bool ok = false;
        int  seatY = 0;          ///< World Y to place the template origin so its floor is flush.
        int  floorWorldY = 0;    ///< World Y of the structure's lowest (floor) layer after seating.
        int  groundTop = 0;      ///< Highest terrain cube under the occupied footprint (pre-seat).
        int  excavated = 0;      ///< Terrain voxels removed to make room for the structure.
        int  stepsPlaced = 0;    ///< Step cubes added in front of ground-level openings.
        bool flush = false;      ///< True when interior floor surface == exterior walkable surface.
    };

    /// Deterministically seat a structure template onto the terrain WITHOUT placing it.
    /// Computes the floor level from the template's own geometry, samples the ground under
    /// every occupied footprint column, solves the seat Y so the floor surface is flush with
    /// the surrounding walkable surface, excavates the terrain the structure occupies, and
    /// builds steps in front of ground-level openings where the exterior ground is lower.
    /// The caller then places the template at {x, plan.seatY, z} with snapToGround=false.
    /// `stepMaterial` is used for generated steps. Returns a plan with ok=false on bad input.
    SeatPlan seatStructure(const std::string& templateName, const glm::ivec3& requestedPos,
                           int rotation, int maxStepRise = 1,
                           const std::string& stepMaterial = "Stone");

    /// Register a structure that was already placed (e.g. by StructureGenerator).
    /// The caller provides the bounding box since structures compute it during generation.
    std::string registerStructure(const std::string& typeName, const glm::ivec3& position,
                                  int rotation, const glm::ivec3& bboxMin, const glm::ivec3& bboxMax,
                                  const std::string& parentId = "");

    /// Register a world item prop (category "item"): a holdable item lying in the
    /// world. Item props are NEVER baked into chunks — they render as kinematic
    /// voxel groups owned by ItemPropManager — so remove() does not clear voxels
    /// for them. Creates a "pickup" interaction point at the bbox center.
    /// metadata["itemId"] links back to the ItemDefinition.
    std::string registerItemProp(const std::string& itemId, const std::string& templateName,
                                 const glm::ivec3& position, int rotation,
                                 const glm::ivec3& bboxMin, const glm::ivec3& bboxMax,
                                 const std::string& displayName);

    /// Update a DYNAMIC item prop's pose (category "item" only): position,
    /// bounding box, and the synthetic pickup interaction point all move to the
    /// new bbox — so [E] Take follows a tumbling/settled item. This is the
    /// sanctioned mutator (move() refuses non-template categories; do NOT
    /// const_cast around it). Returns false for unknown ids / non-item objects.
    bool updateItemPropPose(const std::string& id,
                            const glm::ivec3& bboxMin, const glm::ivec3& bboxMax);

    /// Remove a placed object: clears its voxels and deletes the registry entry.
    bool remove(const std::string& id);

    /// Move a placed object to a new position (re-voxelizes).
    bool move(const std::string& id, const glm::ivec3& newPosition);

    /// Rotate a placed object (re-voxelizes at same position with new rotation).
    bool rotate(const std::string& id, int newRotation);

    /// Get a placed object by its legacy id OR its uuid (resolve-by-either).
    const PlacedObject* get(const std::string& idOrUuid) const;

    /// Merge one key/value into an object's metadata blob (persisted to SQLite via saveToDb).
    /// Returns false if the id is unknown. Used to tag a placed fixture with its semantic identity
    /// (structure/room/purpose/type) so it can be addressed later in a session.
    bool setMetadata(const std::string& id, const std::string& key, const nlohmann::json& value);

    /// List all placed objects.
    std::vector<PlacedObject> list() const;

    /// Find placed objects whose bounding box contains a world position.
    std::vector<std::string> getAt(const glm::ivec3& worldPos) const;

    /// Set the parent of a placed object. Returns false if id or parentId not found.
    bool setParent(const std::string& id, const std::string& parentId);

    /// Get direct children of a placed object (or root objects if parentId is empty).
    std::vector<PlacedObject> getChildren(const std::string& parentId) const;

    /// Get all descendants of a placed object recursively.
    std::vector<PlacedObject> getDescendants(const std::string& parentId) const;

    /// Get the full tree under a placed object as nested JSON.
    nlohmann::json getTree(const std::string& rootId) const;

    /// Register interaction point definitions for a template (call at startup).
    /// These are applied to every new instance when that template is placed.
    void registerTemplateDefs(const std::string& templateName,
                              const std::vector<InteractionPointDef>& defs);

    /// Direct access to the template def catalog for runtime tuning (e.g. ImGui sliders).
    /// After editing, call recomputeAllInteractionPoints() to apply changes.
    std::unordered_map<std::string, std::vector<InteractionPointDef>>& getMutableTemplateDefs() {
        return m_templateDefs;
    }

    /// Recompute world-space interaction points for all loaded objects using registered defs.
    /// Call this after loadFromDb() so that objects restored from save also have interaction points.
    void recomputeAllInteractionPoints();

    /// Build the synthetic pickup interaction point for an item prop.
    static InteractionPoint makeItemPickupPoint(const PlacedObject& obj);

    /// Find the nearest free interaction point of a given type within radius.
    /// Returns {objectId, pointId} or {"", ""} if none found.
    std::pair<std::string, std::string> findNearestFreePoint(
        const glm::vec3& worldPos, float radius, const std::string& type = "seat") const;

    /// Result structure for extended interaction point search.
    struct NearestPointResult {
        std::string objectId;
        std::string pointId;
        glm::vec3   worldPos{0.0f};
        std::string promptText;     ///< Empty = use caller's default
        float       interactionRadius = 0.0f; ///< 0 = was using defaultRadius
        float       viewAngleHalf = 0.0f;
        bool        found = false;
    };

    /// Find the nearest free interaction point with per-point radius and view angle filtering.
    /// Points with interactionRadius > 0 use their own radius; otherwise defaultRadius is used.
    /// If playerFront is non-zero, points with viewAngleHalf > 0 require the player to face them.
    NearestPointResult findNearestFreePointEx(
        const glm::vec3& worldPos, const glm::vec3& playerFront,
        float defaultRadius, const std::string& type = "seat") const;

    /// Claim an interaction point for an occupant. Returns false if already occupied.
    bool claimInteractionPoint(const std::string& objectId, const std::string& pointId,
                               const std::string& occupantId);

    /// Release a specific interaction point.
    void releaseInteractionPoint(const std::string& objectId, const std::string& pointId);

    /// Release all interaction points held by an occupant.
    void releaseAllByOccupant(const std::string& occupantId);

    /// Clear only the voxels for an object without removing its registry entry.
    /// Used by systems (e.g. DoorManager) that take over rendering of an object
    /// and need to remove the static chunk voxels it was baked into.
    bool clearVoxelsOnly(const std::string& id);

    /// Clear all registered objects (does NOT remove voxels).
    void clear();

    /// Serialization for persistence.
    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

    /// Save/load placed objects to/from the world SQLite database.
    bool saveToDb(sqlite3* db) const;
    bool loadFromDb(sqlite3* db);

    size_t count() const;

    /// Get read-only access to all placed objects.
    const std::unordered_map<std::string, PlacedObject>& getAllObjects() const { return m_objects; }

    /// Callback fired for each object id about to be removed (before its voxels
    /// are cleared and its entry erased). Subsystems that hold derived state for
    /// an object (e.g. DynamicFurnitureManager) hook this to tear that state
    /// down. The callback MUST NOT call back into PlacedObjectManager.
    using PreRemoveCallback = std::function<void(const std::string& id)>;
    void setPreRemoveCallback(PreRemoveCallback cb) { m_preRemove = std::move(cb); }

private:
    /// Generate a unique ID for a template/structure placement.
    std::string generateId(const std::string& baseName);

    /// Insert a freshly-built object (m_mutex held): mint a stable uuid if it has
    /// none (create paths) or keep the one restored from JSON (load path), guard
    /// uuid uniqueness, index it in m_uuidToId, then store keyed by the legacy id.
    void insertObjectLocked(PlacedObject&& obj);

    /// Resolve a caller-supplied "id or uuid" to the canonical legacy id (m_mutex
    /// held). A strict-v4 uuid resolves via m_uuidToId (empty string if unknown);
    /// anything else is treated as a legacy id verbatim. This is how every public
    /// entry point accepts either form. Relies on Core::Uuid::isValid's strictness
    /// so a legacy base_N id can never be misread as a uuid.
    std::string resolveIdLocked(const std::string& idOrUuid) const;

    /// Clear voxels in a bounding box region (cubes + subcubes + microcubes).
    void clearRegion(const glm::ivec3& min, const glm::ivec3& max);

    /// Compute the world-space bounding box for a template at a given position with rotation.
    /// Returns {min, max}.
    std::pair<glm::ivec3, glm::ivec3> computeTemplateBounds(
        const std::string& templateName, const glm::ivec3& position, int rotation) const;

    ChunkManager* m_chunkManager;
    ObjectTemplateManager* m_templateManager;
    SnapshotManager* m_snapshotManager;

    PreRemoveCallback m_preRemove;

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, PlacedObject> m_objects;
    std::unordered_map<std::string, std::string> m_uuidToId;  ///< uuid → legacy id (resolve-by-uuid index)
    std::unordered_map<std::string, int> m_idCounters;  ///< Per-template name counters for ID generation
    std::unordered_map<std::string, std::vector<InteractionPointDef>> m_templateDefs; ///< Catalog interaction defs

    /// Compute world-space interaction points for an object given its position and rotation.
    static std::vector<InteractionPoint> computeInteractionPoints(
        const std::vector<InteractionPointDef>& defs,
        const glm::ivec3& position, int rotation);
};

} // namespace Core
} // namespace Phyxel
