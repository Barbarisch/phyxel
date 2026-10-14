#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Phyxel::Scene::Motion {

/// One sampled frame of a character's motion, provider-neutral (clip, learned source or
/// live capture). Positions are in ONE consistent space per sequence: model/character
/// space for headless clip evaluation, world space for the live ring buffer.
struct OracleFrame {
    std::vector<glm::quat> localRotations;
    std::vector<glm::vec3> worldJointPositions;
    glm::vec3 generatedRootVelocity{0.0f};
    glm::vec3 capsuleVelocity{0.0f};
    /// Planted flags per joint. Provided by a source that knows contacts, or DERIVED by
    /// derivePlantedJoints() from foot height (the calibrated 3 cm rule — A1).
    std::vector<bool> plantedJoints;
    /// Character forward in the frame's space (+Z at yaw 0 for Phyxel rigs). Used by the
    /// knee-inversion metric to know which way a knee is supposed to bend.
    glm::vec3 rootForward{0.0f, 0.0f, 1.0f};
    /// Optional per-frame world AABBs (min, max) of the character's segment boxes, for the
    /// self-intersection metric. Empty = metric not evaluated.
    std::vector<std::pair<glm::vec3, glm::vec3>> boxes;
    /// Seconds since the PREVIOUS frame, when the sequence was captured live with a
    /// variable frame time. 0 = use the sequence's uniform secondsPerFrame. A main-thread
    /// stall (an HTTP poll) otherwise turns one long frame's displacement, divided by a
    /// short nominal dt, into a bogus 6+ u/s "skate" spike (seen 2026-09-29).
    float dt = 0.0f;
};

/// Terrain height under (x, z) in the frame space. Headless: a declared function (flat,
/// slope, step). Live: the character's own ground query. Must be a pure function of
/// (x, z) — the seam-equality test pins that chunking cannot change the answer.
using OracleTerrain = std::function<float(float x, float z)>;

struct OracleOptions {
    /// Joints treated as feet. Non-empty → plantedJoints are (re)derived from foot height.
    std::vector<std::size_t> footJoints;
    /// A foot is planted when its Y is within this of its own lowest sample in the sequence.
    /// 0.03 recovers every shipped mocap Speed within 1–14 % (tools/anim_pipeline/anim_lint.py).
    float plantedWindow = 0.03f;
    /// Ground height function; empty → penetration/float not evaluated. When set, planting is
    /// TERRAIN-RELATIVE: a foot is planted where its clearance above the ground is within
    /// `plantedWindow` of its own lowest clearance — on a ramp every stance counts, not just the
    /// lowest one in the sequence (A5).
    OracleTerrain ground;
    /// Height of the foot JOINT above the ground when the character stands (the ankle sits above
    /// the sole). Subtracted from the clearance before penetration/float are judged, so a
    /// perfectly planted foot reads 0, not its ankle height. 0 = judge the raw joint (legacy).
    float footClearanceRef = 0.0f;
    /// A foot moving slower than this in world XZ (units/s) is in STANCE for the float metric
    /// when a ground function is given — height-derived planting cannot tell a stance from a
    /// hover on terrain that steps (A5). 0 = use the planted flags.
    float stanceSpeedMax = 0.3f;
    /// Half the sole's length. The foot joint findFeet prefers is the TOE (the contact point), so
    /// the sole extends BACKWARD from it: ground under the foot = MEDIAN of ground at the joint, at
    /// −footHalfLength and at −2·footHalfLength along the frame's rootForward (two of three samples
    /// on a level = the foot rests on that level; a toe over a riser edge with heel + mid on the
    /// step is supported). 0 = judge the joint's own column (legacy).
    float footHalfLength = 0.0f;
    /// (hip, knee, ankle) joint triples for the knee-inversion metric.
    std::vector<std::array<std::size_t, 3>> kneeChains;
    /// Box index pairs that are ALLOWED to overlap (adjacent segments at a joint).
    std::vector<std::pair<std::size_t, std::size_t>> boxAdjacency;
    /// The clip's authored Speed line (u/s), for the mismatch metric. 0 = not evaluated.
    float authoredSpeed = 0.0f;
};

struct MotionOracleMetrics {
    bool valid = false;
    // --- original metrics (MotionBricks era) ---
    float maxPoseDeltaRadians = 0.0f;
    float maxAngularVelocity = 0.0f;
    float maxAngularAcceleration = 0.0f;
    float maxRootVelocityError = 0.0f;
    float maxPlantedJointSpeed = 0.0f;
    float maxChainLengthError = 0.0f;
    // --- A1 (docs/AnimationSystemV3Plan.md) ---
    /// Body velocity implied by the planted feet (= −mean stance-foot XZ velocity), units/s.
    /// For an IN-PLACE clip this is the speed the controller must use; for world-space
    /// frames of a translating character it should be ~0 (feet stay put in the world).
    glm::vec2 stanceBodyVelocity{0.0f};
    float stanceBodySpeed = 0.0f;
    /// Mean |v − mean v| over stance samples: skate/jitter inside stance, units/s.
    float stanceResidual = 0.0f;
    int   stanceSamples = 0;
    /// |stanceBodySpeed − authoredSpeed| / authoredSpeed, or −1 when not evaluated.
    float speedMismatch = -1.0f;
    /// Deepest foot-below-ground over the sequence (0 when never below or no terrain).
    float maxPenetration = 0.0f;
    /// Highest planted-foot clearance above ground (0 when no terrain / no stance).
    float maxStanceFloat = 0.0f;
    int   maxStanceFloatFrame = -1, maxPenetrationFrame = -1;   // A5 diagnostics
    /// Deepest "knee behind the hip–ankle line" against the leg's reference bend direction.
    float maxKneeInversion = 0.0f;
    /// Largest overlap volume between two non-adjacent segment boxes in any frame.
    float maxBoxOverlap = 0.0f;
    bool  terrainEvaluated = false;
};

/// Terrain-relative planting: planted where the foot's clearance above `ground` is within
/// `window` of its own lowest clearance in the sequence (A5 — ramps and steps).
void derivePlantedJointsOnTerrain(std::vector<OracleFrame>& frames,
                                  const std::vector<std::size_t>& footJoints,
                                  float window, const OracleTerrain& ground);
/// Mark each foot joint planted where its Y is within `window` of its lowest sample.
void derivePlantedJoints(std::vector<OracleFrame>& frames,
                         const std::vector<std::size_t>& footJoints,
                         float window = 0.03f);

/// Provider-neutral animation quality metrics. chainEdges contains parent/child joint
/// indices; the first frame is the reference chain length.
MotionOracleMetrics evaluateMotion(const std::vector<OracleFrame>& frames,
                                   float secondsPerFrame,
                                   const std::vector<std::pair<std::size_t,
                                                               std::size_t>>& chainEdges = {});

/// A1 form: same metrics plus the stance / terrain / knee / overlap suite. When
/// `options.footJoints` is non-empty the planted flags are derived (a copy of `frames`
/// is annotated; the caller's vector is untouched).
MotionOracleMetrics evaluateMotion(const std::vector<OracleFrame>& frames,
                                   float secondsPerFrame,
                                   const std::vector<std::pair<std::size_t, std::size_t>>& chainEdges,
                                   const OracleOptions& options);

} // namespace Phyxel::Scene::Motion
