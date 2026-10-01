#include <gtest/gtest.h>

#include "scene/SeatFit.h"
#include "scene/AnimatedVoxelCharacter.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <memory>

using namespace Phyxel;
using namespace Phyxel::Scene::SeatFit;
using Phyxel::Scene::AnimatedVoxelCharacter;

// ============================================================================
// A4 step 1 — the seat-fit policy is engine code with the editor as a caller. Rules pinned at
// their margins; the character metrics come from the BodyPlan, not bone-name literals.
// ============================================================================

namespace {
CharacterFitMetrics standard() {
    CharacterFitMetrics c;
    c.total_height = 1.85f; c.hip_height = 0.95f; c.eye_height = 1.70f; c.leg_length = 0.90f;
    c.arm_reach = 0.60f; c.shoulder_width = 0.40f; c.hip_width = 0.22f; c.body_depth = 0.24f;
    c.sitting_height = 0.90f;
    return c;
}
SeatFeatures seat(float top, float width, float depth, float backrest = 0.0f) {
    SeatFeatures s; s.seat_top_y = top; s.seat_width_x = width; s.seat_depth_z = depth;
    s.backrest_height = backrest; s.backrest_present = backrest > 0.0f; return s;
}
bool has(const std::vector<FitIssue>& v, const char* id) {
    for (const auto& i : v) if (i.ruleId == id) return true;
    return false;
}
} // namespace

TEST(SeatFit, AFittingSeatRaisesNoIssue) {
    const auto issues = evaluate(standard(), seat(0.667f, 0.667f, 0.667f, 0.444f));   // chair_wood
    EXPECT_TRUE(issues.empty()) << issues.size();
    EXPECT_FALSE(refused(issues));
}

TEST(SeatFit, EachRuleFiresExactlyAtItsMargin) {
    const auto c = standard();
    // too narrow: width must be >= hip_width + clearance
    // (seat top 0.6 keeps the 0.9 leg inside the knee-rise band so only the rule under test fires)
    EXPECT_TRUE(has(evaluate(c, seat(0.6f, c.hip_width + kHipClearance - 0.001f, 0.6f)), "SEAT_TOO_NARROW"));
    EXPECT_FALSE(has(evaluate(c, seat(0.6f, c.hip_width + kHipClearance + 0.001f, 0.6f)), "SEAT_TOO_NARROW"));
    // too shallow
    EXPECT_TRUE(has(evaluate(c, seat(0.6f, 0.6f, c.body_depth + kDepthClearance - 0.001f)), "SEAT_TOO_SHALLOW"));
    // too tall: seat higher than legs by more than the foot drop (1 mm either side of the margin)
    EXPECT_TRUE(has(evaluate(c, seat(c.leg_length + kFootDropMax + 0.001f, 0.6f, 0.6f)), "SEAT_TOO_TALL"));
    EXPECT_FALSE(has(evaluate(c, seat(c.leg_length + kFootDropMax - 0.001f, 0.6f, 0.6f)), "SEAT_TOO_TALL"));
    // too low: knees rise above the hips by more than the cap
    EXPECT_TRUE(has(evaluate(c, seat(c.leg_length - kKneeRiseMax - 0.001f, 0.6f, 0.6f)), "SEAT_TOO_LOW"));
    // backrest above the seated eye is a WARN, not a refusal
    const auto warn = evaluate(c, seat(0.6f, 0.6f, 0.6f, c.sitting_height - 0.1f + kBackrestHeadMax + 0.01f));
    EXPECT_TRUE(has(warn, "BACKREST_BLOCKS_VIEW"));
    EXPECT_FALSE(refused(warn));
}

TEST(SeatFit, MissingSeatFieldsSkipTheirRuleAndAnErrorRefuses) {
    const auto c = standard();
    EXPECT_TRUE(evaluate(c, SeatFeatures{}).empty()) << "no features → nothing to judge (the caller denies on missing)";
    EXPECT_TRUE(refused(evaluate(c, seat(0.6f, 0.10f, 0.6f))));
}

TEST(SeatFit, SidecarFeaturesParseIncludingV2Fields) {
    std::ifstream in("resources/templates/furniture/chair_wood.metrics.json");
    ASSERT_TRUE(in.is_open());
    nlohmann::json j; in >> j;
    const auto s = SeatFeatures::fromJson(j["interaction_points"][0]["features"]);
    EXPECT_NEAR(s.seat_top_y, 0.6667f, 1e-3f);
    EXPECT_NEAR(s.seat_width_x, 0.6667f, 1e-3f);
    EXPECT_TRUE(s.backrest_present);
    EXPECT_TRUE(s.armrests.empty()) << "chair_wood has no armrests";
    EXPECT_TRUE(s.has_approach) << "v2 sidecars carry an approach point";
    EXPECT_NEAR(s.backrest_angle_deg, 0.0f, 1.0f) << "chair_wood's back is a vertical slab";
    const auto v2 = SeatFeatures::fromJson(nlohmann::json{{"seat_top_y", 0.5}, {"backrest_angle_deg", 12.0},
        {"armrests", {{{"top_y", 0.75}, {"inner_x", 0.1}, {"z_min", 0.0}, {"z_max", 0.5}}}}, {"approach", {0.3, 0.0, 1.1}}});
    EXPECT_FLOAT_EQ(v2.backrest_angle_deg, 12.0f);
    ASSERT_EQ(v2.armrests.size(), 1u);
    EXPECT_FLOAT_EQ(v2.armrests[0].top_y, 0.75f);
    EXPECT_TRUE(v2.has_approach);
    EXPECT_FLOAT_EQ(v2.approach.z, 1.1f);
}

TEST(SeatFit, HumanoidMetricsComeFromThePlanAndAreBodySized) {
    AnimatedVoxelCharacter ch(nullptr, glm::vec3(0.0f));
    ASSERT_TRUE(ch.loadModel("resources/animated_characters/humanoid.anim"));
    for (int i = 0; i < 5; ++i) ch.update(1.0f / 60.0f);   // segment boxes follow the posed skeleton only after an update
    const auto m = measureCharacter(ch);
    // the seat-matrix expectations (standard fits chair_wood) depend on these bands
    EXPECT_GT(m.total_height, 1.5f);  EXPECT_LT(m.total_height, 2.2f);
    EXPECT_GT(m.hip_width, 0.10f);    EXPECT_LT(m.hip_width, 0.40f);
    EXPECT_GT(m.leg_length, 0.70f);   EXPECT_LT(m.leg_length, 1.10f);
    EXPECT_GT(m.arm_reach, 0.40f);    EXPECT_LT(m.arm_reach, 0.80f);
    EXPECT_GT(m.shoulder_width, 0.25f);
    EXPECT_GT(m.body_depth, 0.05f);
    EXPECT_GT(m.sitting_height, 0.50f);
    EXPECT_GT(m.eye_height, m.hip_height);
    EXPECT_GE(ch.headBoneId(), 0) << "humanoid.json declares headBone";
    // and chair_wood fits the standard humanoid — the seat matrix's anchor expectation
    std::ifstream in("resources/templates/furniture/chair_wood.metrics.json");
    nlohmann::json j; in >> j;
    EXPECT_FALSE(refused(evaluate(m, SeatFeatures::fromJson(j["interaction_points"][0]["features"]))));
}
