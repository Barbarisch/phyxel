#pragma once

#include <string>
#include <glm/glm.hpp>

namespace Phyxel {
namespace Physics { class VoxelRigidBody; }
namespace Ecs {

// ============================================================================
// World-object domain components (ECS pilot).
//
// The entity carrying `WorldObject` is the single source of truth for a placed
// template/furniture instance. The other components are *handles* to derived
// state owned by subsystems; reconciliation systems create/destroy that derived
// state to match component presence, so destroying the entity tears everything
// down in one place (this is what fixes the "removed chair reappears" bug).
//
// Pilot scope: interaction points and the freeform metadata blob still live in
// PlacedObjectManager (the facade) during the transition — only the
// lifecycle-critical state is owned here.
// ============================================================================

/// Authoritative placement data (mirrors the core of the legacy PlacedObject).
struct WorldObject {
    std::string externalId;     ///< Legacy string id, for facade/MCP lookup.
    std::string templateName;   ///< Template or structure type name.
    std::string category;       ///< "template" | "structure".
    std::string parentId;       ///< External id of parent (hierarchy); "" = root.
    glm::ivec3  position{0};    ///< World-space origin where placed.
    int         rotation = 0;   ///< Y rotation in degrees (0/90/180/270).
    glm::ivec3  boundingMin{0}; ///< World-space AABB min corner.
    glm::ivec3  boundingMax{0}; ///< World-space AABB max corner.
};

/// World transform — present for dynamic/rendered objects. Derived view.
struct Transform {
    glm::mat4 matrix{1.0f};
};

/// Present iff the object is currently a physics-driven dynamic body.
/// Absence means the object is static (baked into chunk voxels). The body's
/// lifetime is owned by VoxelDynamicsWorld; PhysicsBodySystem destroys it when
/// this component (or the entity) goes away.
struct DynamicBody {
    Physics::VoxelRigidBody* body = nullptr;
    float       totalMass      = 0.0f;
    glm::ivec3  staticOriginPos{0};  ///< Where to re-bake voxels on settle/deactivate.
    int         staticOriginRot = 0;
    bool        isGrabbed       = false;
};

/// Present iff the object is rendered via KinematicVoxelManager (dynamic).
/// RenderSyncSystem removes the render object when this component/entity dies.
struct KinematicRender {
    std::string kinematicId;
};

/// DB persistence marker. PersistenceSystem writes dirty entities and deletes
/// rows for destroyed entities.
struct Persistent {
    bool dirty = true;
};

} // namespace Ecs
} // namespace Phyxel
