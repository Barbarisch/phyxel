#pragma once
// ============================================================================
// SeatFit — the seat-fit policy as ENGINE code (A4 step 1, docs/AnimationSystemV3Plan.md §4 A4).
//
// "Accuracy over coverage: a character never sits where it does not fit." Until 2026-09-30 the
// rules lived in the editor (Application.cpp) and were unreachable from NPC behaviours, the
// seat solver and headless tests. They are pure functions here; the editor calls them.
//
// Character metrics are measured from the BodyPlan (root = hips, leg chains, arm segments,
// spine chain, headBone) — no bone-name literals; seat features come from the template's
// `asset_metrics` sidecar (v1 fields, plus the v2 additions the chair solver reads).
//
// The margins below are THE single source; tools/interaction_pipeline/interaction_kinds/sit.py
// mirrors them and tests/test_seat_fit_margins.py pins the two equal.
// ============================================================================
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Phyxel::Scene {

class AnimatedVoxelCharacter;

namespace SeatFit {

// ---- margins (world units = metres on the humanoid) --------------------------------------
constexpr float kHipClearance    = 0.05f;   // seat must exceed hip width by at least this
constexpr float kDepthClearance  = 0.10f;   // buttock-to-knee depth headroom
constexpr float kFootDropMax     = 0.20f;   // feet may dangle this far below the seat before "too tall"
constexpr float kBackrestHeadMax = 0.10f;   // backrest may exceed seated eye height by this (warn above)
constexpr float kKneeRiseMax     = 0.35f;   // knees may rise this far above the hips before "too low"

/// Scalar metrics of a character at its current pose (world units). Same fields the Python
/// pipeline's compute_character_metrics produces (tools/interaction_pipeline/character_metrics.py).
struct CharacterFitMetrics {
    float total_height   = 0.0f;
    float hip_height     = 0.0f;
    float eye_height     = 0.0f;
    float leg_length     = 0.0f;
    float arm_reach      = 0.0f;
    float shoulder_width = 0.0f;
    float hip_width      = 0.0f;
    float body_depth     = 0.0f;
    float sitting_height = 0.0f;
    nlohmann::json toJson() const;
};

/// One seat interaction point's features (asset_metrics sidecar). v1 fields are what the fit
/// rules read; v2 fields (A4) are what the seated solve reads — absent means none.
struct SeatFeatures {
    // v1
    float seat_top_y       = 0.0f;
    float seat_width_x     = 0.0f;
    float seat_depth_z     = 0.0f;
    float front_edge_z     = 0.0f;
    float backrest_height  = 0.0f;
    bool  backrest_present = false;
    // v2 (A4 item 1)
    float backrest_angle_deg = 0.0f;              // 0 = upright; positive leans back
    struct Armrest { float top_y = 0.0f, inner_x = 0.0f, z_min = 0.0f, z_max = 0.0f; };
    std::vector<Armrest> armrests;                // 0–2, template space
    bool      has_approach = false;
    glm::vec3 approach{0.0f};                     // template space, on the floor, in front of the seat

    bool valid() const { return seat_top_y > 0.0f; }
    static SeatFeatures fromJson(const nlohmann::json& features);   // the "features" object
};

struct FitIssue {
    std::string ruleId;      // SEAT_TOO_NARROW | SEAT_TOO_SHALLOW | SEAT_TOO_TALL | SEAT_TOO_LOW | BACKREST_BLOCKS_VIEW
    std::string message;
    float measured = 0.0f;
    float required = 0.0f;
    bool  error    = false;  // error = refuse; false = warn
    nlohmann::json toJson() const;
};

/// The sit-kind compatibility rules. Missing seat fields (<= 0) skip their rule.
std::vector<FitIssue> evaluate(const CharacterFitMetrics& c, const SeatFeatures& seat);
bool refused(const std::vector<FitIssue>& issues);

/// Measure a loaded character through its BodyPlan (hips = plan root, legs, arm segments,
/// spine chain, headBone; sitting height sampled from the SittingIdle clip — that sampler
/// poses a scratch skeleton, hence the non-const reference).
CharacterFitMetrics measureCharacter(AnimatedVoxelCharacter& ch);

} // namespace SeatFit
} // namespace Phyxel::Scene
