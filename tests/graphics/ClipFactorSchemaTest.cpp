#include <gtest/gtest.h>

#include "graphics/ClipMetaSchema.h"

#include <string>
#include <vector>

using namespace Phyxel;
using namespace Phyxel::ClipMeta;

// ============================================================================
// A3 item 1 — `# clip_meta:` has ONE typed schema (resources/anim/clip_meta_schema.json)
// shared with tools/anim_pipeline/clip_meta_schema.py. Before this the runtime ran every
// value through std::stof, so `grip=2h_heavy` was an "unknown key" WARN and a factor
// coordinate had nowhere to live. These tests pin parse, reject and select.
// ============================================================================

namespace {
AnimationClip clipWith(const char* name, const char* meta) {
    AnimationClip c; c.name = name; c.duration = 1.0f;
    applyToClip(c, parseFields(meta));
    return c;
}
} // namespace

TEST(ClipFactorSchema, SchemaComesFromTheSharedJsonFile) {
    ASSERT_TRUE(schemaLoadedFromFile()) << schemaPath() << " missing — the lint and the engine must read the same file";
    const auto& s = schema();
    ASSERT_TRUE(s.count("grip"));
    EXPECT_EQ(s.at("grip").type, ValueType::Enum);
    EXPECT_NE(std::find(s.at("grip").values.begin(), s.at("grip").values.end(), "2h_heavy"), s.at("grip").values.end());
    EXPECT_TRUE(s.at("grip").factor);
    // every legacy runtime key is still a typed key
    for (const char* k : {"hitFrameFraction", "releaseFrame", "warpEnabled", "interruptible", "stairStepHeight", "footIKEnabled"})
        EXPECT_TRUE(s.count(k)) << k;
}

TEST(ClipFactorSchema, ParsesFactorsNumbersAndTypeWithoutIssues) {
    const auto p = parseFields("type=locomotion role=layer grip=2h_heavy mask=upper additive=1 hitFrameFraction=0.5 meleeFamily=two_handed");
    EXPECT_TRUE(p.issues.empty());
    EXPECT_EQ(p.type, "locomotion");
    EXPECT_EQ(p.factors.at("role"), "layer");
    EXPECT_EQ(p.factors.at("grip"), "2h_heavy");
    EXPECT_EQ(p.factors.at("mask"), "upper");
    EXPECT_EQ(p.factors.at("additive"), "1");
    EXPECT_FLOAT_EQ(p.numbers.at("hitFrameFraction"), 0.5f);
    EXPECT_EQ(p.strings.at("meleeFamily"), "two_handed");
}

TEST(ClipFactorSchema, RejectsWrongTypeBadEnumAndUnknownKey) {
    auto bad = parseFields("grip=1.5");
    ASSERT_EQ(bad.issues.size(), 1u);
    EXPECT_EQ(bad.issues[0].kind, Issue::BadEnum);
    EXPECT_TRUE(bad.factors.empty());

    auto wrong = parseFields("hitFrameFraction=abc");
    ASSERT_EQ(wrong.issues.size(), 1u);
    EXPECT_EQ(wrong.issues[0].kind, Issue::WrongType);
    EXPECT_TRUE(wrong.numbers.empty());

    auto unknown = parseFields("bogus=1");
    ASSERT_EQ(unknown.issues.size(), 1u);
    EXPECT_EQ(unknown.issues[0].kind, Issue::UnknownKey);
}

TEST(ClipFactorSchema, ReleaseFrameAliasesHitFrameButExplicitWinsInEitherOrder) {
    auto a = clipWith("cast", "releaseFrame=0.3");
    EXPECT_FLOAT_EQ(a.hitFrameFraction, 0.3f);
    auto b = clipWith("cast", "releaseFrame=0.3 hitFrameFraction=0.6");
    EXPECT_FLOAT_EQ(b.hitFrameFraction, 0.6f);
    auto c = clipWith("cast", "hitFrameFraction=0.6 releaseFrame=0.3");
    EXPECT_FLOAT_EQ(c.hitFrameFraction, 0.6f);
}

TEST(ClipFactorSchema, AppliesLegacyNumericFieldsExactlyAsBefore) {
    auto c = clipWith("stair_down", "type=stair warpEnabled=0 authoredFallDist=0.667 takeoffEnd=0.1 contactFrame=0.85 "
                                   "warpScaleMin=0.4 warpScaleMax=2.5 hitFrameFraction=0.4 interruptible=0 interruptAfter=0.5 "
                                   "footIKEnabled=1 stairStepHeight=0.667 stairStepDepth=0.667 contactFrame1=0.182 contactFrame2=0.545 "
                                   "footIKSurfaceReach=0.111 footIKBodyRange=0.111");
    EXPECT_EQ(c.clipType, "stair");
    EXPECT_FALSE(c.warpEnabled);
    EXPECT_FLOAT_EQ(c.authoredFallDist, 0.667f);
    EXPECT_FLOAT_EQ(c.contactFrame, 0.85f);
    EXPECT_FALSE(c.interruptible);
    EXPECT_FLOAT_EQ(c.interruptAfter, 0.5f);
    EXPECT_FLOAT_EQ(c.stairStepHeight, 0.667f);
    EXPECT_FLOAT_EQ(c.contactFrame1, 0.182f);
    EXPECT_FLOAT_EQ(c.contactFrame2, 0.545f);
    EXPECT_FLOAT_EQ(c.footIKSurfaceReach, 0.111f);
    EXPECT_TRUE(c.factors.empty());
}

TEST(ClipFactorSchema, EveryShippedRigHeaderValidatesClean) {
    // Keys the old parser warned about (weaponRole, impactFrameFraction, gatheringRole) are
    // now typed; nothing shipped may carry an unknown or wrong-typed key.
    for (const char* rig : {"humanoid", "deer", "monster_yeti", "quad_wolf", "forge_bear"}) {
        const auto issues = validateFile(std::string("resources/animated_characters/") + rig + ".anim");
        EXPECT_TRUE(issues.empty()) << rig << ": " << ::testing::PrintToString(issues);
    }
}

TEST(ClipFactorSchema, SelectsNearestBaseAndEveryMatchingLayer) {
    std::vector<AnimationClip> clips = {
        clipWith("walk",      "role=base gait=biped state=walk"),
        clipWith("walk_2h",   "role=base gait=biped state=walk grip=2h_heavy"),
        clipWith("run",       "role=base gait=biped state=run"),
        clipWith("carry_2h",  "role=layer grip=2h_heavy mask=upper additive=1"),
        clipWith("carry_1h",  "role=layer grip=1h mask=arms additive=1"),
        clipWith("tired",     "role=layer condition=tired mask=upper additive=1"),
        clipWith("gallop",    "role=base gait=quadruped state=run"),
        clipWith("untagged",  "type=combat"),
    };
    Factors f; f.state = "walk"; f.grip = "2h_heavy";
    auto c = selectComposition(clips, f);
    EXPECT_EQ(c.base, 1) << "the base declaring grip=2h_heavy is nearer than the generic walk";
    EXPECT_EQ(c.layers, (std::vector<int>{3}));

    f.grip = "1h";
    c = selectComposition(clips, f);
    EXPECT_EQ(c.base, 0) << "walk_2h declares a grip that differs → vetoed";
    EXPECT_EQ(c.layers, (std::vector<int>{4}));

    f.grip = "empty"; f.condition = "tired"; f.state = "run";
    c = selectComposition(clips, f);
    EXPECT_EQ(c.base, 2) << "biped run, not the quadruped gallop";
    EXPECT_EQ(c.layers, (std::vector<int>{5}));

    f.state = "swim";
    c = selectComposition(clips, f);
    EXPECT_EQ(c.base, -1);
    EXPECT_EQ(c.layers, (std::vector<int>{5})) << "layers do not depend on the state unless they declare one";
}

// Roadmap R1 (docs/CharacterAnimationRoadmap.md decisions 2 + 5): the engine reads the two new
// keys from the shared file — attackKind as a factor coordinate, review as a tool-only key it never
// applies. RED 2026-10-01: both keys are unknown.
TEST(ClipFactorSchema, AttackKindIsAFactorAndReviewIsToolOnly) {
    ASSERT_TRUE(schemaLoadedFromFile());
    const auto& s = schema();
    ASSERT_TRUE(s.count("attackKind"));
    EXPECT_EQ(s.at("attackKind").type, ValueType::Enum);
    EXPECT_TRUE(s.at("attackKind").factor);
    ASSERT_TRUE(s.count("review"));
    EXPECT_TRUE(s.at("review").toolOnly);
    EXPECT_FALSE(s.at("review").factor);
    const auto p = parseFields("attackKind=bite review=draft");
    EXPECT_TRUE(p.issues.empty());
    EXPECT_EQ(p.factors.at("attackKind"), "bite");
    EXPECT_EQ(p.factors.count("review"), 0u) << "review is status, not a composition coordinate";
}

