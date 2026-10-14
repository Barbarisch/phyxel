#pragma once
#include "scene/CharacterAppearance.h"
#include "graphics/Animation.h"

#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Scene {

/// Creature-agnostic rig descriptor (docs/CharacterAnimationV2.md §4 item 0,
/// docs/BodyPlan.md): names the bones and clips a body needs — root, leg
/// chains, collision segments, default clip vocabulary — so the character
/// runtime consumes plan data instead of hardcoded mixamorig:* / humanoid
/// clip names.
///
/// CONTRACT (A3 item 2, 2026-09-30 — supersedes the A2-era NEUTRALITY rule): the humanoid
/// plan reproduces the legacy hardcodes EXACTLY — same bone-name strings, same segment
/// order — and now CARRIES the humanoid clip table (clipDefaults + clipSprint +
/// clipFallbacks, transcribed from the pre-refactor FSM switch). The legacy switch in
/// AnimatedVoxelCharacter::clipForState is only the guard for a plan that resolves
/// nothing; ClipSelectionTest pins that it is never reached on the humanoid. Pinned by
/// CharacterGoldenPoseTest (poses) + BodyPlanTest + ClipSelectionTest (names).
struct BodyPlan {
    std::string id;                       // "humanoid", "quadruped_wolf", ...
    MorphologyType morphology = MorphologyType::Humanoid;
    std::string gaitClass;                // "biped_fsm" | "quadruped_clips" | ...
    std::string rootBone;                 // exact name; also the sit/IK hip bone
    std::vector<std::string> hipAliases;  // lowercase substring fallbacks
    std::string gripBone;                 // default held-item attachment bone
    std::string headBone;                 // A4: eye/seated-height measurements ("" = none)

    struct LegChain {
        std::string id;                   // "left", "front_left", ...
        std::string upper, mid, foot;     // exact bone names hip->knee->foot
        bool footIK = false;              // participates in 2-bone foot IK
    };
    std::vector<LegChain> legs;           // order fixed; humanoid = left, right

    struct SegmentDef {
        std::string bone;                 // exact bone name
        bool isArm = false;
    };
    std::vector<SegmentDef> segments;     // ORDER IS CONTRACT (box table, MCP)

    /// FSM state key ("Walk", "SittingIdle", ...) -> exact clip name. Layered
    /// BELOW per-character animationMapping and ABOVE the legacy defaults.
    /// JSON value is a string or {"clip": "run", "sprint": "fast_run"}.
    std::map<std::string, std::string> clipDefaults;
    /// Sprint / fast-gait variant per state (from the {clip, sprint} object form). The RULE
    /// for when "fast" applies (sprint key, or |strafe| > 0.6 on the strafe states) stays in
    /// clipForState; only the NAMES live here.
    std::map<std::string, std::string> clipSprint;
    /// Fallback clip for the member-driven states (Block/Dodge/HitReact/Death/Celebrate/Cast)
    /// when the member is empty. The member itself is runtime state, never plan data.
    std::map<std::string, std::string> clipFallbacks;

    /// A3 modifiers: the trunk bones a posture lean is distributed across, root side first
    /// (exact names). Undeclared → the plan's non-arm, non-leg trunk segments minus the root.
    std::vector<std::string> spineChain;

    /// A3 composition masks: name -> subtree ROOT bone names ("upper", "arms", "head", "legs").
    /// Optional; an undeclared mask derives from roles (legs = leg upper subtrees, arms = topmost
    /// isArm segments' subtrees, upper = everything but the skeleton root and the legs).
    std::map<std::string, std::vector<std::string>> masks;

    /// A3 transition graph: per-edge crossfade seconds and foot-phase sync. `from`/`to` are
    /// FSM state keys; "*" is a wildcard source. An exact edge beats a wildcard; an undeclared
    /// edge uses defaultBlend (seconds, 0 = hard cut; negative is spelled 0 — a negative blend
    /// would divide by zero in the blend advance).
    struct TransitionEdge {
        std::string from = "*", to;
        float blend = -1.0f;              // seconds; < 0 = use defaultBlend
        bool  phaseSync = false;          // enter `to` at the same gait phase (stanceL markers)
    };
    std::vector<TransitionEdge> transitions;
    float defaultBlend = 0.2f;
    /// nullptr when no edge (exact or wildcard) matches.
    const TransitionEdge* findTransition(const std::string& from, const std::string& to) const;

    struct Capsule {
        enum class Mode { Legacy, XZExtent };
        Mode mode = Mode::Legacy;         // Legacy = torso-span ratio path
        float minHalfWidth = 0.12f;
        float maxHalfWidth = 0.60f;
    } capsule;

    /// Plan resolved against a concrete skeleton (ids, not names).
    struct Resolved {
        int rootBoneId = -1;
        struct Leg {
            int upperId = -1, midId = -1, footId = -1;
            bool footIK = false;
        };
        std::vector<Leg> legs;                         // same order as plan
        std::vector<std::pair<int, bool>> segments;    // boneId, isArm; misses skipped
    };
    /// Exact-name resolution; root falls back to hipAliases substring scan
    /// (case-insensitive) — same algorithm as the legacy m_ikHipBoneId path.
    Resolved resolveAgainst(const Skeleton& skeleton) const;

    static BodyPlan fromJson(const nlohmann::json& j);

    /// Compiled fallback, field-equal to resources/body_plans/humanoid.json
    /// (equality asserted in BodyPlanTest) — a missing/corrupt JSON can never
    /// break the player character.
    static BodyPlan builtinHumanoid();
};

/// Loads resources/body_plans/*.json once; hands out the plan for a morphology.
class BodyPlanRegistry {
public:
    static BodyPlanRegistry& instance();

    /// Plan for a morphology; Unknown (and anything unregistered) -> humanoid.
    const BodyPlan& planFor(MorphologyType m) const;

    /// Skeleton-aware selection: among plans of the morphology, pick the one
    /// whose bones actually resolve on this skeleton (score = resolved root +
    /// legs + segments). Lets rig families share a morphology — the engine
    /// wolf (pelvis/upper_leg_front_L) and every Meshy quadruped
    /// (Hips/frontleg0) each get their own plan. Falls back to planFor(m).
    const BodyPlan& planForSkeleton(MorphologyType m, const Skeleton& skeleton) const;
    const BodyPlan* planById(const std::string& id) const;
    std::vector<std::string> getAllPlanIds() const;
    size_t count() const { return m_plans.size(); }

    int loadFromDirectory(const std::string& dir);
    bool registerPlan(const BodyPlan& plan);

    /// Load resources/body_plans/ once if empty; always guarantees the
    /// builtin humanoid plan exists. Safe on every lookup path.
    void ensureLoaded();
    void clear();

private:
    BodyPlanRegistry() = default;
    BodyPlanRegistry(const BodyPlanRegistry&) = delete;
    BodyPlanRegistry& operator=(const BodyPlanRegistry&) = delete;

    std::map<std::string, BodyPlan> m_plans;             // by id
    std::map<MorphologyType, std::string> m_byMorphology; // morphology -> id
    bool m_loadAttempted = false;
};

} // namespace Scene
} // namespace Phyxel
