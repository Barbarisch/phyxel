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

    /// Clear all registered objects (does NOT remove voxels).
    void clear();

    /// Serialization for persistence.
    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

    /// Save/load placed objects to/from the world SQLite database.
    bool saveToDb(sqlite3* db) const;
    bool loadFromDb(sqlite3* db);

    size_t count() const;

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
};

} // namespace Core
} // namespace Phyxel
