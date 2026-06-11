#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

namespace Phyxel {

class ChunkManager;
class ObjectTemplateManager;
class VoxelTemplate;

namespace Core {

class PlacedObjectManager;
class KinematicVoxelManager;
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
    };

    void setDependencies(PlacedObjectManager* placed, ObjectTemplateManager* templates,
                         KinematicVoxelManager* kinematic, ChunkManager* chunks) {
        m_placed = placed; m_templates = templates; m_kinematic = kinematic; m_chunks = chunks;
    }

    /// Spawn an item prop in the world. snapToGround scans downward for the
    /// first solid voxel and rests the prop on top of it.
    /// Returns the placed-object id, or "" on failure (unknown/not-holdable
    /// item, missing template).
    std::string spawnProp(const std::string& itemId, const glm::vec3& position,
                          float yawDeg = 0.0f, bool snapToGround = true);

    /// Remove a prop (kinematic group + placed-object entry).
    bool removeProp(const std::string& placedObjectId);

    /// Pick a prop up: removes it and returns its itemId ("" if not a prop).
    std::string pickupProp(const std::string& placedObjectId);

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

    /// Convert a voxel template to kinematic voxels (template-local space).
    /// Shared with DynamicFurnitureManager — the single source of truth for
    /// template→kinematic conversion.
    static std::vector<KinematicVoxel> voxelsFromTemplate(const VoxelTemplate& tmpl);

    /// Resolve an ItemDefinition::templateFile ("weapons/sword.voxel") to a
    /// loaded template, loading it on demand from resources/templates/ (the
    /// startup scan is non-recursive, so subdirectory templates are lazy).
    const VoxelTemplate* resolveItemTemplate(const std::string& templateFile) const;

private:
    PlacedObjectManager*    m_placed    = nullptr;
    ObjectTemplateManager*  m_templates = nullptr;
    KinematicVoxelManager*  m_kinematic = nullptr;
    ChunkManager*           m_chunks    = nullptr;

    std::unordered_map<std::string, Prop> m_props;  // by placedObjectId
};

} // namespace Core
} // namespace Phyxel
