#pragma once

#include <string>
#include <functional>
#include <unordered_map>
#include <glm/glm.hpp>

namespace Phyxel {

class ObjectTemplateManager;

namespace Core {

class KinematicVoxelManager;
class PlacedObjectManager;
class NavGrid;

/// Runtime state for one registered door.
struct DoorState {
    std::string placedObjectId;          ///< Linked PlacedObject in PlacedObjectManager
    std::string kineticObjId;            ///< Linked KinematicVoxelObject in KinematicVoxelManager
    glm::vec3   worldHingePos{0.0f};    ///< World position of the hinge edge (template origin)
    int         baseRotation = 0;        ///< Placement rotation 0/90/180/270 degrees
    float       openAngle    = 90.0f;    ///< Y-axis rotation (degrees) of the fully-open position
    float       closedAngle  = 0.0f;    ///< Y-axis rotation (degrees) of the fully-closed position
    float       currentAngle = 0.0f;    ///< Animated current angle (degrees)
    float       targetAngle  = 0.0f;    ///< Animation target: openAngle or closedAngle
    float       swingSpeed   = 120.0f;  ///< Degrees per second
    bool        isOpen       = false;
    bool        locked       = false;
    std::string keyItemId;               ///< Required item ID to unlock, empty = no lock
    bool        settled      = true;     ///< True when currentAngle == targetAngle
};

/// Drives door animations and manages the door lifecycle.
///
/// Each door is backed by a KinematicVoxelObject (rendering + collision) linked to a
/// PlacedObject (world position + metadata persistence). The static chunk voxels are
/// removed when the door is registered — the kinematic object renders in their place.
///
/// Usage:
///   1. Place a door template via PlacedObjectManager::placeTemplate()
///   2. Call DoorManager::registerDoor() with the placed object ID
///   3. Call DoorManager::update(dt) each frame (before physics step)
///   4. Call open/close/toggle in response to player interaction (set via DoorCallback)
class DoorManager {
public:
    /// Called when a door finishes swinging open or closed.
    using SettleCallback = std::function<void(const std::string& placedObjectId, bool isOpen)>;

    DoorManager() = default;

    void setKinematicVoxelManager(KinematicVoxelManager* m)  { m_kinematic       = m; }
    void setPlacedObjectManager(PlacedObjectManager* m)       { m_placedObjects   = m; }
    void setObjectTemplateManager(ObjectTemplateManager* m)   { m_templateManager = m; }
    void setNavGrid(NavGrid* m)                               { m_navGrid         = m; }

    /// Fired when a door finishes opening or closing. Typically used to trigger NavGrid rebuild.
    void setSettleCallback(SettleCallback cb) { m_settleCallback = std::move(cb); }

    /// Register a placed object as a door.
    ///
    /// Loads full-cube voxels from the named template, creates a KinematicVoxelObject,
    /// removes the static chunk voxels from the placed object's bounding region, and
    /// writes initial door metadata to PlacedObject::metadata.
    ///
    /// Convention: the template must be authored with its hinge edge at the local origin.
    ///
    /// @param placedObjectId  ID returned by PlacedObjectManager::placeTemplate()
    /// @param templateName    Template name used for voxel data
    /// @param worldHingePos   World position of the hinge edge
    /// @param baseRotation    Placement rotation in degrees (0/90/180/270)
    /// @param openAngle       Y-axis rotation degrees of the fully-open position (default 90)
    /// @param swingSpeed      Degrees per second (default 120)
    /// @param thickness       Z-axis thickness in microcubes (1-16, default 16 = full cube)
    bool registerDoor(const std::string& placedObjectId,
                      const std::string& templateName,
                      const glm::vec3&   worldHingePos,
                      int                baseRotation = 0,
                      float              openAngle    = 90.0f,
                      float              swingSpeed   = 120.0f,
                      int                thickness    = 16);

    /// Remove a door, destroy its KinematicVoxelObject, and re-bake voxels into the chunk.
    void unregisterDoor(const std::string& placedObjectId);

    /// Step all door animations. Call once per frame before the physics step.
    void update(float dt);

    void open(const std::string& placedObjectId);
    void close(const std::string& placedObjectId);
    void toggle(const std::string& placedObjectId);

    bool isOpen(const std::string& placedObjectId) const;
    bool isLocked(const std::string& placedObjectId) const;
    void setLocked(const std::string& placedObjectId, bool locked,
                   const std::string& keyItemId = "");

    const std::unordered_map<std::string, DoorState>& getDoors() const { return m_doors; }

    /// Write open/closed/locked/angle to each door's PlacedObject::metadata.
    void persistStates();

    /// Restore door states from PlacedObject::metadata.
    /// Call after world load for any placed objects whose metadata contains door data.
    void restoreStates();

    void clear() { m_doors.clear(); }

private:
    /// Compute the world transform for a door at its current angle.
    glm::mat4 computeTransform(const DoorState& door) const;

    /// Called when currentAngle reaches targetAngle. Refreshes NavGrid and persists state.
    void onSettle(DoorState& door);

    KinematicVoxelManager* m_kinematic       = nullptr;
    PlacedObjectManager*   m_placedObjects   = nullptr;
    ObjectTemplateManager* m_templateManager = nullptr;
    NavGrid*               m_navGrid         = nullptr;
    SettleCallback         m_settleCallback;

    std::unordered_map<std::string, DoorState> m_doors;
};

} // namespace Core
} // namespace Phyxel
