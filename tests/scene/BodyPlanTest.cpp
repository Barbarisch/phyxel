#include <gtest/gtest.h>
#include "scene/BodyPlan.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "graphics/AnimationSystem.h"

#include <memory>

using namespace Phyxel;
using Phyxel::Scene::AnimatedVoxelCharacter;
using Phyxel::Scene::BodyPlan;
using Phyxel::Scene::BodyPlanRegistry;
using Phyxel::Scene::MorphologyType;

// Phase D (docs/CharacterLibraryPlan.md, CharacterAnimationV2.md §10 P0b):
// the BodyPlan descriptor replaces the humanoid hardcodes. These tests pin
// the NEUTRALITY CONTRACT — the humanoid plan resolves to the exact bone ids
// the legacy mixamorig:* literals produce — and prove the creature plans
// resolve against their real rigs (wrong bone/clip names fail loudly here,
// not silently in-engine). Tests run from the repo root.

namespace {

bool loadRig(const std::string& animFile, Skeleton& skeleton,
             std::vector<AnimationClip>& clips, VoxelModel& model) {
    AnimationSystem anim;
    return anim.loadFromFile(animFile, skeleton, clips, model);
}

BodyPlanRegistry& freshRegistry() {
    auto& reg = BodyPlanRegistry::instance();
    reg.clear();
    reg.ensureLoaded();
    return reg;
}

void expectPlanEqual(const BodyPlan& a, const BodyPlan& b) {
    EXPECT_EQ(a.id, b.id);
    EXPECT_EQ(a.morphology, b.morphology);
    EXPECT_EQ(a.gaitClass, b.gaitClass);
    EXPECT_EQ(a.rootBone, b.rootBone);
    EXPECT_EQ(a.hipAliases, b.hipAliases);
    EXPECT_EQ(a.gripBone, b.gripBone);
    ASSERT_EQ(a.legs.size(), b.legs.size());
    for (size_t i = 0; i < a.legs.size(); ++i) {
        EXPECT_EQ(a.legs[i].upper, b.legs[i].upper) << "leg " << i;
        EXPECT_EQ(a.legs[i].mid, b.legs[i].mid) << "leg " << i;
        EXPECT_EQ(a.legs[i].foot, b.legs[i].foot) << "leg " << i;
        EXPECT_EQ(a.legs[i].footIK, b.legs[i].footIK) << "leg " << i;
    }
    ASSERT_EQ(a.segments.size(), b.segments.size());
    for (size_t i = 0; i < a.segments.size(); ++i) {
        EXPECT_EQ(a.segments[i].bone, b.segments[i].bone) << "segment " << i;
        EXPECT_EQ(a.segments[i].isArm, b.segments[i].isArm) << "segment " << i;
    }
    EXPECT_EQ(a.clipDefaults, b.clipDefaults);
    EXPECT_EQ(a.capsule.mode, b.capsule.mode);
    EXPECT_NEAR(a.capsule.minHalfWidth, b.capsule.minHalfWidth, 1e-6f);
    EXPECT_NEAR(a.capsule.maxHalfWidth, b.capsule.maxHalfWidth, 1e-6f);
}

} // namespace

TEST(BodyPlan, RegistryLoadsAllFourPlans) {
    auto& reg = freshRegistry();
    for (const char* id : {"humanoid", "quadruped_wolf", "arachnid_spider", "dragon"})
        EXPECT_NE(reg.planById(id), nullptr) << id;
    EXPECT_EQ(reg.planFor(MorphologyType::Humanoid).id, "humanoid");
    EXPECT_EQ(reg.planFor(MorphologyType::Quadruped).id, "quadruped_wolf");
    EXPECT_EQ(reg.planFor(MorphologyType::Arachnid).id, "arachnid_spider");
    EXPECT_EQ(reg.planFor(MorphologyType::Dragon).id, "dragon");
    // Unknown morphology falls back to humanoid — never a null/garbage plan.
    EXPECT_EQ(reg.planFor(MorphologyType::Unknown).id, "humanoid");
}

TEST(BodyPlan, HumanoidJsonMatchesBuiltinFallback) {
    // The compiled fallback must stay field-identical to the JSON so a
    // missing resources/ directory can never change player behavior.
    auto& reg = freshRegistry();
    const BodyPlan* fromJson = reg.planById("humanoid");
    ASSERT_NE(fromJson, nullptr);
    expectPlanEqual(*fromJson, BodyPlan::builtinHumanoid());
}

TEST(BodyPlan, BuiltinHumanoidUsedWhenJsonMissing) {
    auto& reg = BodyPlanRegistry::instance();
    reg.clear();
    // Simulate missing resources: skip directory load entirely.
    EXPECT_EQ(reg.loadFromDirectory("nonexistent_dir_zzz"), 0);
    reg.ensureLoaded();   // dir already attempted? ensure guarantees humanoid
    EXPECT_NE(reg.planById("humanoid"), nullptr);
    reg.clear();          // leave clean for other tests
    reg.ensureLoaded();
}

// ---------------------------------------------------------------------------
// Neutrality: plan resolution == legacy literal lookups, on every
// humanoid-family rig.
// ---------------------------------------------------------------------------

TEST(BodyPlan, HumanoidResolutionMatchesLegacyLiterals) {
    auto& reg = freshRegistry();
    const BodyPlan& plan = reg.planFor(MorphologyType::Humanoid);

    for (const char* rig : {"resources/animated_characters/humanoid.anim",
                            "resources/animated_characters/ogre.anim",
                            "resources/animated_characters/kotor_humanoid.anim"}) {
        Skeleton skel;
        std::vector<AnimationClip> clips;
        VoxelModel model;
        ASSERT_TRUE(loadRig(rig, skel, clips, model)) << rig;

        auto legacy = [&](const char* name) -> int {
            auto it = skel.boneMap.find(name);
            return it != skel.boneMap.end() ? it->second : -1;
        };

        BodyPlan::Resolved r = plan.resolveAgainst(skel);
        EXPECT_EQ(r.rootBoneId, legacy("mixamorig:Hips")) << rig;
        ASSERT_EQ(r.legs.size(), 2u) << rig;
        EXPECT_EQ(r.legs[0].upperId, legacy("mixamorig:LeftUpLeg")) << rig;
        EXPECT_EQ(r.legs[0].midId,   legacy("mixamorig:LeftLeg")) << rig;
        EXPECT_EQ(r.legs[0].footId,  legacy("mixamorig:LeftFoot")) << rig;
        EXPECT_EQ(r.legs[1].upperId, legacy("mixamorig:RightUpLeg")) << rig;
        EXPECT_EQ(r.legs[1].midId,   legacy("mixamorig:RightLeg")) << rig;
        EXPECT_EQ(r.legs[1].footId,  legacy("mixamorig:RightFoot")) << rig;

        // Segment table: identical names, identical ORDER (order is the
        // contract for the box table / attack origin / MCP output).
        static const struct { const char* name; bool isArm; } kLegacySegments[12] = {
            { "mixamorig:Head",         false },
            { "mixamorig:Spine2",       false },
            { "mixamorig:Spine1",       false },
            { "mixamorig:Hips",         false },
            { "mixamorig:LeftArm",      true  },
            { "mixamorig:RightArm",     true  },
            { "mixamorig:LeftForeArm",  true  },
            { "mixamorig:RightForeArm", true  },
            { "mixamorig:LeftUpLeg",    false },
            { "mixamorig:RightUpLeg",   false },
            { "mixamorig:LeftLeg",      false },
            { "mixamorig:RightLeg",     false },
        };
        ASSERT_EQ(r.segments.size(), 12u) << rig;
        for (size_t i = 0; i < 12; ++i) {
            EXPECT_EQ(r.segments[i].first, legacy(kLegacySegments[i].name))
                << rig << " segment " << i;
            EXPECT_EQ(r.segments[i].second, kLegacySegments[i].isArm)
                << rig << " segment " << i;
        }

        // The sit-anchor path assumes the root is bone 0 on Mixamo rigs.
        EXPECT_EQ(r.rootBoneId, 0) << rig;
    }
}

TEST(BodyPlan, HumanoidClipDefaultsAreEmpty) {
    // NEUTRALITY: humanoid clip selection must keep falling through to the
    // legacy FSM switch (sprint variants + multi-candidate lists live there).
    auto& reg = freshRegistry();
    EXPECT_TRUE(reg.planFor(MorphologyType::Humanoid).clipDefaults.empty());
}

// ---------------------------------------------------------------------------
// Creature plans resolve against their real rigs.
// ---------------------------------------------------------------------------

namespace {

void expectPlanResolvesOnRig(const char* planId, const char* rig,
                             size_t expectLegs, size_t minSegments) {
    auto& reg = freshRegistry();
    const BodyPlan* plan = reg.planById(planId);
    ASSERT_NE(plan, nullptr) << planId;

    Skeleton skel;
    std::vector<AnimationClip> clips;
    VoxelModel model;
    ASSERT_TRUE(loadRig(rig, skel, clips, model)) << rig;

    BodyPlan::Resolved r = plan->resolveAgainst(skel);
    EXPECT_GE(r.rootBoneId, 0) << planId << ": root did not resolve";
    ASSERT_EQ(r.legs.size(), expectLegs) << planId;
    for (size_t i = 0; i < r.legs.size(); ++i) {
        EXPECT_GE(r.legs[i].upperId, 0) << planId << " leg " << i;
        EXPECT_GE(r.legs[i].midId, 0)   << planId << " leg " << i;
        EXPECT_GE(r.legs[i].footId, 0)  << planId << " leg " << i;
    }
    EXPECT_GE(r.segments.size(), minSegments)
        << planId << ": segment bones missing from skeleton";

    // Every clip default must name a clip that actually exists (exact match)
    // — catches namespace/underscore typos at unit-test time.
    for (const auto& [state, clipName] : plan->clipDefaults) {
        bool found = false;
        for (const auto& c : clips)
            if (c.name == clipName) { found = true; break; }
        EXPECT_TRUE(found) << planId << ": clipDefaults[" << state
                           << "] = '" << clipName << "' not in " << rig;
    }
}

} // namespace

TEST(BodyPlan, WolfPlanResolvesOnWolfRig) {
    expectPlanResolvesOnRig("quadruped_wolf",
                            "resources/animated_characters/character_wolf.anim",
                            4, 10);
}

TEST(BodyPlan, SpiderPlanResolvesOnSpider2Rig) {
    expectPlanResolvesOnRig("arachnid_spider",
                            "resources/animated_characters/character_spider2.anim",
                            8, 11);
}

TEST(BodyPlan, DragonPlanResolvesOnDragonRig) {
    expectPlanResolvesOnRig("dragon",
                            "resources/animated_characters/character_dragon.anim",
                            4, 12);
}

// ---------------------------------------------------------------------------
// Stress (CharacterAnimationV2.md §10 P0b acceptance): all four rig families
// load into the real character class without crashing.
// ---------------------------------------------------------------------------

TEST(BodyPlan, AllFourRigFamiliesLoadIntoCharacter) {
    struct Case { const char* rig; MorphologyType expected; };
    // NOTE: humanoid.anim detects as Unknown, not Humanoid — detectMorphology
    // matches "hips"/"mixamorighips" but the real bone is "mixamorig:Hips"
    // (colon survives lowercasing). Pre-existing behavior; Unknown falls back
    // to humanoid everywhere (planFor(Unknown) == humanoid is pinned above).
    // Fixing the detector would silently flip humanoids from getLimbScales'
    // default branch to its Humanoid branch — out of scope for Phase D.
    const Case cases[] = {
        { "resources/animated_characters/humanoid.anim",         MorphologyType::Unknown },
        { "resources/animated_characters/character_wolf.anim",    MorphologyType::Quadruped },
        { "resources/animated_characters/character_spider2.anim", MorphologyType::Arachnid },
        { "resources/animated_characters/character_dragon.anim",  MorphologyType::Dragon },
    };
    for (const auto& c : cases) {
        auto ch = std::make_unique<AnimatedVoxelCharacter>(nullptr, glm::vec3(0.0f));
        ASSERT_TRUE(ch->loadModel(c.rig)) << c.rig;
        EXPECT_EQ(ch->getAppearance().morphology, c.expected) << c.rig;
    }
}
