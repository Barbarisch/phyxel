#include "core/DoorManager.h"
#include "core/KinematicVoxelManager.h"
#include "core/PlacedObjectManager.h"
#include "core/ObjectTemplateManager.h"
#include "core/NavGrid.h"
#include "core/VoxelTemplate.h"
#include "utils/Logger.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace Phyxel {
namespace Core {

// ============================================================================
// Registration / unregistration
// ============================================================================

bool DoorManager::registerDoor(const std::string& placedObjectId,
                                const std::string& templateName,
                                const glm::vec3&   worldHingePos,
                                int                baseRotation,
                                float              openAngle,
                                float              swingSpeed,
                                int                thickness)
{
    if (!m_kinematic || !m_templateManager || !m_placedObjects) {
        LOG_ERROR("DoorManager", "registerDoor called with missing dependencies");
        return false;
    }

    if (m_doors.count(placedObjectId)) {
        LOG_WARN_FMT("DoorManager", "Door '" << placedObjectId << "' already registered");
        return false;
    }

    // Load voxels from the template (full cubes only for the first pass)
    const VoxelTemplate* tmpl = m_templateManager->getTemplate(templateName);
    if (!tmpl) {
        LOG_ERROR_FMT("DoorManager", "Template '" << templateName << "' not found");
        return false;
    }

    std::vector<KinematicVoxel> voxels;
    voxels.reserve(tmpl->cubes.size());
    float zScale = glm::clamp(thickness, 1, 16) / 16.0f;
    for (const auto& cube : tmpl->cubes) {
        KinematicVoxel v;
        // Template positions are corner-based; add 0.5 to center in X/Y.
        // For Z (thickness axis), center at 0.5*zScale so the near face
        // sits at Z=0, flush with the hinge pivot.
        v.localPos     = glm::vec3(cube.relativePos) + glm::vec3(0.5f, 0.5f, 0.5f * zScale);
        v.scale        = glm::vec3(1.0f, 1.0f, zScale);
        v.materialName = cube.material;
        voxels.push_back(v);
    }

    if (voxels.empty()) {
        LOG_WARN_FMT("DoorManager", "Template '" << templateName << "' has no full cubes — door not registered");
        return false;
    }

    // Build initial closed transform
    DoorState door;
    door.placedObjectId = placedObjectId;
    door.worldHingePos  = worldHingePos;
    door.baseRotation   = baseRotation;
    door.openAngle      = openAngle;
    door.closedAngle    = 0.0f;
    door.currentAngle   = 0.0f;
    door.targetAngle    = 0.0f;
    door.swingSpeed     = swingSpeed;
    door.isOpen         = false;
    door.settled        = true;

    glm::mat4 initialTransform = computeTransform(door);

    // Create kinematic voxel object
    door.kineticObjId = m_kinematic->add(
        "door",
        std::move(voxels),
        initialTransform,
        placedObjectId
    );

    // Remove static chunk voxels (kinematic object renders in their place)
    m_placedObjects->clearVoxelsOnly(placedObjectId);

    // Write initial metadata to the placed object
    auto* obj = const_cast<PlacedObject*>(m_placedObjects->get(placedObjectId));
    if (obj) {
        obj->metadata["door_state"]     = door.isOpen ? "open" : "closed";
        obj->metadata["current_angle"]  = door.currentAngle;
        obj->metadata["open_angle"]     = door.openAngle;
        obj->metadata["base_rotation"]  = door.baseRotation;
        obj->metadata["swing_speed"]    = door.swingSpeed;
        obj->metadata["locked"]         = door.locked;
        obj->metadata["key_item_id"]    = door.keyItemId;
        obj->metadata["hinge_x"]        = door.worldHingePos.x;
        obj->metadata["hinge_y"]        = door.worldHingePos.y;
        obj->metadata["hinge_z"]        = door.worldHingePos.z;
        obj->metadata["thickness"]      = thickness;
    }

    m_doors[placedObjectId] = std::move(door);
    LOG_INFO_FMT("DoorManager", "Registered door '" << placedObjectId << "' (template='"
                 << templateName << "', hinge=(" << worldHingePos.x << ","
                 << worldHingePos.y << "," << worldHingePos.z << "), openAngle=" << openAngle << ")");
    return true;
}

void DoorManager::unregisterDoor(const std::string& placedObjectId) {
    auto it = m_doors.find(placedObjectId);
    if (it == m_doors.end()) return;

    if (m_kinematic) {
        m_kinematic->remove(it->second.kineticObjId);
    }

    m_doors.erase(it);
    LOG_INFO_FMT("DoorManager", "Unregistered door '" << placedObjectId << "'");
}

// ============================================================================
// Per-frame update
// ============================================================================

void DoorManager::update(float dt) {
    for (auto& [id, door] : m_doors) {
        if (door.settled) continue;

        float diff = door.targetAngle - door.currentAngle;
        float step = door.swingSpeed * dt;

        if (std::abs(diff) <= step) {
            door.currentAngle = door.targetAngle;
            door.settled      = true;
        } else {
            door.currentAngle += (diff > 0.0f ? 1.0f : -1.0f) * step;
        }

        if (m_kinematic) {
            m_kinematic->setTransform(door.kineticObjId, computeTransform(door));
        }

        if (door.settled) {
            onSettle(door);
        }
    }
}

// ============================================================================
// Control
// ============================================================================

void DoorManager::open(const std::string& placedObjectId) {
    auto it = m_doors.find(placedObjectId);
    if (it == m_doors.end()) return;
    DoorState& door = it->second;
    if (door.locked) {
        LOG_INFO_FMT("DoorManager", "Door '" << placedObjectId << "' is locked");
        return;
    }
    if (door.targetAngle == door.openAngle && !door.settled) return; // Already opening
    door.targetAngle = door.openAngle;
    door.settled     = false;
}

void DoorManager::close(const std::string& placedObjectId) {
    auto it = m_doors.find(placedObjectId);
    if (it == m_doors.end()) return;
    DoorState& door = it->second;
    if (door.targetAngle == door.closedAngle && !door.settled) return; // Already closing
    door.targetAngle = door.closedAngle;
    door.settled     = false;
}

void DoorManager::toggle(const std::string& placedObjectId) {
    auto it = m_doors.find(placedObjectId);
    if (it == m_doors.end()) return;
    DoorState& door = it->second;
    // Toggle direction: if currently open or opening, close; otherwise open
    bool goingOpen = (door.targetAngle == door.openAngle);
    if (goingOpen) {
        close(placedObjectId);
    } else {
        open(placedObjectId);
    }
}

bool DoorManager::isOpen(const std::string& placedObjectId) const {
    auto it = m_doors.find(placedObjectId);
    return (it != m_doors.end()) && it->second.isOpen;
}

bool DoorManager::isLocked(const std::string& placedObjectId) const {
    auto it = m_doors.find(placedObjectId);
    return (it != m_doors.end()) && it->second.locked;
}

void DoorManager::setLocked(const std::string& placedObjectId, bool locked,
                              const std::string& keyItemId)
{
    auto it = m_doors.find(placedObjectId);
    if (it == m_doors.end()) return;
    it->second.locked    = locked;
    it->second.keyItemId = keyItemId;

    auto* obj = const_cast<PlacedObject*>(m_placedObjects->get(placedObjectId));
    if (obj) {
        obj->metadata["locked"]      = locked;
        obj->metadata["key_item_id"] = keyItemId;
    }
}

// ============================================================================
// Persistence
// ============================================================================

void DoorManager::persistStates() {
    if (!m_placedObjects) return;
    for (const auto& [id, door] : m_doors) {
        auto* obj = const_cast<PlacedObject*>(m_placedObjects->get(id));
        if (!obj) continue;
        obj->metadata["door_state"]    = door.isOpen ? "open" : "closed";
        obj->metadata["current_angle"] = door.currentAngle;
        obj->metadata["locked"]        = door.locked;
        obj->metadata["key_item_id"]   = door.keyItemId;
    }
}

void DoorManager::restoreStates() {
    if (!m_placedObjects || !m_kinematic || !m_templateManager) return;

    auto allObjs = m_placedObjects->list();
    for (const auto& obj : allObjs) {
        if (!obj.metadata.contains("door_state")) continue;
        if (m_doors.count(obj.id)) continue; // Already registered

        // Read door metadata written by a previous registerDoor call
        float openAngle   = obj.metadata.value("open_angle",    90.0f);
        int   baseRot     = obj.metadata.value("base_rotation",     0);
        float swingSpeed  = obj.metadata.value("swing_speed",  120.0f);
        float currentAngle = obj.metadata.value("current_angle", 0.0f);
        bool  wasOpen     = (obj.metadata.value("door_state", "closed") == "open");
        int   thickness   = obj.metadata.value("thickness",         2);
        float hx          = obj.metadata.value("hinge_x", (float)obj.position.x);
        float hy          = obj.metadata.value("hinge_y", (float)obj.position.y);
        float hz          = obj.metadata.value("hinge_z", (float)obj.position.z);
        glm::vec3 hingePos(hx, hy, hz);

        // Re-register the door at its saved state
        if (registerDoor(obj.id, obj.templateName, hingePos, baseRot, openAngle, swingSpeed, thickness)) {
            auto it = m_doors.find(obj.id);
            if (it != m_doors.end()) {
                it->second.currentAngle = currentAngle;
                it->second.targetAngle  = currentAngle;
                it->second.isOpen       = wasOpen;
                it->second.locked       = obj.metadata.value("locked", false);
                it->second.keyItemId    = obj.metadata.value("key_item_id", "");
                // Apply restored transform
                if (m_kinematic) {
                    m_kinematic->setTransform(it->second.kineticObjId, computeTransform(it->second));
                }
            }
        }
    }
}

// ============================================================================
// Private helpers
// ============================================================================

glm::mat4 DoorManager::computeTransform(const DoorState& door) const {
    // Build the pivot transform:
    //   1. Translate to the world hinge position
    //   2. Apply placement base rotation
    //   3. Apply animated open/close rotation (around Y axis)
    glm::mat4 t = glm::translate(glm::mat4(1.0f), door.worldHingePos);
    t = t * glm::rotate(glm::mat4(1.0f),
                         glm::radians((float)door.baseRotation),
                         glm::vec3(0.0f, 1.0f, 0.0f));
    t = t * glm::rotate(glm::mat4(1.0f),
                         glm::radians(door.currentAngle),
                         glm::vec3(0.0f, 1.0f, 0.0f));
    return t;
}

void DoorManager::onSettle(DoorState& door) {
    door.isOpen = (door.currentAngle == door.openAngle);

    LOG_INFO_FMT("DoorManager", "Door '" << door.placedObjectId << "' settled "
                 << (door.isOpen ? "OPEN" : "CLOSED"));

    // Granular NavGrid rebuild over the door's world footprint
    if (m_navGrid && m_placedObjects) {
        const auto* obj = m_placedObjects->get(door.placedObjectId);
        if (obj) {
            m_navGrid->rebuildRegion(
                obj->boundingMin.x, obj->boundingMin.z,
                obj->boundingMax.x, obj->boundingMax.z);
        }
    }

    // Persist state
    if (m_placedObjects) {
        auto* obj = const_cast<PlacedObject*>(m_placedObjects->get(door.placedObjectId));
        if (obj) {
            obj->metadata["door_state"]    = door.isOpen ? "open" : "closed";
            obj->metadata["current_angle"] = door.currentAngle;
        }
    }

    if (m_settleCallback) {
        m_settleCallback(door.placedObjectId, door.isOpen);
    }
}

} // namespace Core
} // namespace Phyxel
