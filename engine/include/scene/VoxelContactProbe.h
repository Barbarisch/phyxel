// VoxelContactProbe — Phase M1 of the edge/push/pull/climb plan.
//
// Frame-coherent inspection of the voxel world around a kinematic character.
// Reuses the existing column-probe primitive (VoxelDynamicsWorld::findGroundY)
// rather than introducing a new collision path, so it stays cheap and stays
// in lockstep with the engine's voxel-occupancy view of the world.
//
// One sample() call per character per tick. Result is read by the
// AnimatedVoxelCharacter FSM (edge teeter, climb-up, climb-down) and exposed
// via the MCP /api/character/contact route for debugging.
#pragma once

#include <glm/glm.hpp>
#include <string>

namespace Phyxel {
namespace Physics { class VoxelDynamicsWorld; }
namespace Scene {

/// Direction of the nearest air column adjacent to the character's feet,
/// expressed in the character's facing frame. NONE means all four lateral
/// columns (forward / back / left / right) are at the same surface height.
enum class EdgeDir : uint8_t {
    None = 0,
    Forward,
    Back,
    Left,
    Right,
};

/// What kind of vertical feature lies in front of the character.
enum class ClimbFeature : uint8_t {
    None = 0,
    LedgeUp,    ///< Forward face top is reachable by mantle.
    LedgeDown,  ///< Forward column drops away below feet.
    Ladder,     ///< Forward voxel is part of a climbable PlacedObject. (Phase M7)
};

/// Per-tick contact information for a kinematic character. Cube-grain.
struct CharacterContact {
    // ---- Ground ----
    bool      groundFound = false;
    float     groundY     = 0.0f;     ///< Top of the column directly below feet.
    EdgeDir   nearEdgeDir = EdgeDir::None;
    float     edgeDrop    = 0.0f;     ///< Height difference at the near-edge column.

    // ---- Forward face (the voxel surface the character is facing into) ----
    bool      forwardHit  = false;
    glm::vec3 forwardHitPoint{0.0f};
    glm::vec3 forwardFaceNormal{0.0f};  ///< Outward normal of the hit face.
    float     forwardFaceYMin = 0.0f;
    float     forwardFaceYMax = 0.0f;
    float     forwardHeadroomAbove = 0.0f; ///< Air above the face top.
    float     forwardDepthClear   = 0.0f;  ///< Air depth on top of the face.

    // ---- Climb classification ----
    ClimbFeature climb        = ClimbFeature::None;
    float        climbGrabY   = 0.0f; ///< Y at which the character grabs (face top).
    float        climbTopY    = 0.0f; ///< Y the character must reach to clear the ledge.
    glm::vec3    climbAnchorXZ{0.0f}; ///< Centre of the forward face on XZ.

    // ---- Push handle (filled in Phase M3; placeholder in M1) ----
    enum class PushKind : uint8_t { None = 0, DynamicObj, KinematicPart };
    PushKind    push          = PushKind::None;
    std::string pushObjectId;
    std::string pushPartId;
};

/// Tunable parameters for the probe. Defaults match the kinematic character.
struct ContactProbeParams {
    float maxClimbHeight   = 1.6f;  ///< Mantle reach above feet.
    float minLedgeDrop     = 0.6f;  ///< Drop threshold for LedgeDown.
    float headroomRequired = 1.8f;  ///< Air above the ledge top for LedgeUp.
    float depthClearMin    = 0.5f;  ///< Solid floor depth behind the ledge.
    float forwardRange     = 0.6f;  ///< Horizontal probe distance for forward face.
    float lateralProbeXZ   = 0.0f;  ///< 0 → use character halfWidth+0.05.
};

/// Sample the voxel contact context for one character.
/// `feetPos`     world-space feet position (the kinematic capsule base).
/// `facingDir`   unit XZ facing direction; Y is ignored.
/// `halfExtents` character half-extents (x,y,z); y is half-height of the capsule.
CharacterContact sampleVoxelContact(const Physics::VoxelDynamicsWorld& world,
                                    const glm::vec3& feetPos,
                                    const glm::vec3& facingDir,
                                    const glm::vec3& halfExtents,
                                    const ContactProbeParams& params = {});

const char* toString(EdgeDir d);
const char* toString(ClimbFeature c);
const char* toString(CharacterContact::PushKind k);

} // namespace Scene
} // namespace Phyxel
