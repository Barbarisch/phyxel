#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <chrono>
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
    std::string id;                  ///< Unique ID, e.g. "test_chair_3"
    std::string templateName;        ///< Template or structure type name
    std::string category;            ///< "template" or "structure"
    std::string parentId;            ///< Parent object ID (empty = root/world)
    glm::ivec3 position{0};          ///< World-space origin where placed
    int rotation = 0;                ///< Y-axis rotation in degrees (0/90/180/270)
    glm::ivec3 boundingMin{0};       ///< World-space AABB min corner
    glm::ivec3 boundingMax{0};       ///< World-space AABB max corner
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
    std::string placeTemplate(const std::string& templateName, const glm::ivec3& position,
                              int rotation = 0, const std::string& parentId = "");

    /// Register a structure that was already placed (e.g. by StructureGenerator).
    /// The caller provides the bounding box since structures compute it during generation.
    std::string registerStructure(const std::string& typeName, const glm::ivec3& position,
                                  int rotation, const glm::ivec3& bboxMin, const glm::ivec3& bboxMax,
                                  const std::string& parentId = "");

    /// Remove a placed object: clears its voxels and deletes the registry entry.
    bool remove(const std::string& id);

    /// Move a placed object to a new position (re-voxelizes).
    bool move(const std::string& id, const glm::ivec3& newPosition);

    /// Rotate a placed object (re-voxelizes at same position with new rotation).
    bool rotate(const std::string& id, int newRotation);

    /// Get a placed object by ID.
    const PlacedObject* get(const std::string& id) const;

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

private:
    /// Generate a unique ID for a template/structure placement.
    std::string generateId(const std::string& baseName);

    /// Clear voxels in a bounding box region (cubes + subcubes + microcubes).
    void clearRegion(const glm::ivec3& min, const glm::ivec3& max);

    /// Compute the world-space bounding box for a template at a given position with rotation.
    /// Returns {min, max}.
    std::pair<glm::ivec3, glm::ivec3> computeTemplateBounds(
        const std::string& templateName, const glm::ivec3& position, int rotation) const;

    ChunkManager* m_chunkManager;
    ObjectTemplateManager* m_templateManager;
    SnapshotManager* m_snapshotManager;

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, PlacedObject> m_objects;
    std::unordered_map<std::string, int> m_idCounters;  ///< Per-template name counters for ID generation
    std::unordered_map<std::string, std::vector<InteractionPointDef>> m_templateDefs; ///< Catalog interaction defs

    /// Compute world-space interaction points for an object given its position and rotation.
    static std::vector<InteractionPoint> computeInteractionPoints(
        const std::vector<InteractionPointDef>& defs,
        const glm::ivec3& position, int rotation);
};

} // namespace Core
} // namespace Phyxel
