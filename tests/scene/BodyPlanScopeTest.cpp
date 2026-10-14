#include <gtest/gtest.h>

#include "scene/BodyPlan.h"
#include "graphics/AnimationSystem.h"
#include "scene/motion/MotionOracleSampling.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <string>
#include <vector>

using namespace Phyxel;
using Phyxel::Scene::BodyPlan;
using Phyxel::Scene::BodyPlanRegistry;

// ============================================================================
// A2 (docs/AnimationSystemV3Plan.md): every GAMEPLAY rig must resolve, on some shipped
// plan, a root bone and at least one complete leg (hip, knee, foot) — the two things the
// runtime needs to anchor sitting, strip root motion and run the foot metrics without a
// literal bone name. The scope list is resources/animated_characters/rig_scope.json
// (tools/anim_pipeline/rig_scope.py; tests/test_rig_scope.py keeps it fresh).
// RED 2026-09-29 on rigs whose plan cannot resolve a knee.
// ============================================================================

namespace {

std::vector<std::string> gameplayRigs() {
    std::ifstream in("resources/animated_characters/rig_scope.json");
    std::vector<std::string> out;
    if (!in.is_open()) return out;
    nlohmann::json j; in >> j;
    for (const auto& r : j.value("gameplay", nlohmann::json::array())) out.push_back(r.get<std::string>());
    return out;
}

struct BestPlan {
    const BodyPlan* plan = nullptr;
    BodyPlan::Resolved resolved;
    int completeLegs = 0;
};

BestPlan bestPlanFor(const Skeleton& sk) {
    auto& reg = BodyPlanRegistry::instance();
    reg.ensureLoaded();
    BestPlan best;
    int bestScore = -1;
    for (const auto& id : reg.getAllPlanIds()) {
        const BodyPlan* p = reg.planById(id);
        if (!p) continue;
        auto r = p->resolveAgainst(sk);
        int legs = 0;
        for (const auto& l : r.legs) if (l.upperId >= 0 && l.midId >= 0 && l.footId >= 0) ++legs;
        const int score = (r.rootBoneId >= 0 ? 100 : 0) + legs * 10 + static_cast<int>(r.segments.size());
        if (score > bestScore) { bestScore = score; best = {p, r, legs}; }
    }
    return best;
}

} // namespace

TEST(BodyPlanScope, ScopeFileExistsAndNamesGameplayRigs) {
    const auto rigs = gameplayRigs();
    ASSERT_FALSE(rigs.empty()) << "resources/animated_characters/rig_scope.json missing or empty — run tools/anim_pipeline/rig_scope.py";
    EXPECT_GE(rigs.size(), 60u);
}

TEST(BodyPlanScope, EveryGameplayRigResolvesRootAndACompleteLegOnSomePlan) {
    const auto rigs = gameplayRigs();
    ASSERT_FALSE(rigs.empty());
    std::vector<std::string> noRoot, noLeg, footMismatch, unparsed;
    for (const auto& rig : rigs) {
        Skeleton sk; std::vector<AnimationClip> clips; VoxelModel model;
        AnimationSystem sys;
        if (!sys.loadFromFile("resources/animated_characters/" + rig + ".anim", sk, clips, model) || sk.bones.empty()) {
            unparsed.push_back(rig); continue;
        }
        const BestPlan b = bestPlanFor(sk);
        if (!b.plan || b.resolved.rootBoneId < 0) { noRoot.push_back(rig); continue; }
        // Legless plans (ooze, plant, serpent, shark, swarm, winged-only) declare no legs by
        // design: for them a resolved root + at least one segment is the contract. A plan that
        // DOES declare legs must resolve at least one completely on the rig it was picked for.
        if (b.plan->legs.empty()) {
            if (b.resolved.segments.empty()) noLeg.push_back(rig + " (legless plan " + b.plan->id + " resolved no segment)");
            continue;
        }
        if (b.completeLegs < 1) { noLeg.push_back(rig + " (plan " + b.plan->id + ")"); continue; }
        // The plan's feet and the name-suffix rule (anim_lint / oracle) must agree where the
        // suffix rule finds anything — otherwise the oracle judges different joints than the
        // runtime anchors.
        const auto suffixFeet = Scene::Motion::findFeet(sk);
        if (!suffixFeet.empty()) {
            bool agree = false;
            for (const auto& l : b.resolved.legs)
                for (auto f : suffixFeet)
                    if (l.footId >= 0 && (static_cast<int>(f) == l.footId ||
                        (f < sk.bones.size() && sk.bones[f].parentId == l.footId))) agree = true;
            if (!agree) footMismatch.push_back(rig + " (plan " + b.plan->id + ")");
        }
    }
    EXPECT_TRUE(unparsed.empty()) << "unparsable rigs: " << ::testing::PrintToString(unparsed);
    EXPECT_TRUE(noRoot.empty()) << "no plan resolves a root on: " << ::testing::PrintToString(noRoot);
    EXPECT_TRUE(noLeg.empty()) << "no plan resolves a complete leg (hip,knee,foot) on: " << ::testing::PrintToString(noLeg);
    EXPECT_TRUE(footMismatch.empty()) << "plan feet disagree with the suffix rule on: " << ::testing::PrintToString(footMismatch);
}

// ============================================================================
// A2 — the plan a rig adopts at RUNTIME must be the plan the scope test found for it.
// The scope test above scores every plan; the runtime routes by appearance.morphology,
// and the NPC visual resolver stamps morphology from the FILE NAME (Humanoid unless the
// name says wolf/spider/dragon/_meshy/forge_). Live-caught 2026-09-29: a patrolling
// `deer.anim` NPC entered HitReact and played `Idle` — it was on humanoid.json, so its
// Quaternius clip vocabulary (Gallop, Idle_HitReact1, Death) was unreachable. RED first.
// ============================================================================
#include "core/CharacterVisualResolver.h"
#include "scene/AnimatedVoxelCharacter.h"

namespace {
struct NpcRoute { const char* rig; const char* plan; const char* hitClip; };
}

TEST(BodyPlanScope, NpcResolvedRigsAdoptTheirScopedPlanAtRuntime) {
    const NpcRoute routes[] = {
        {"deer",           "quaternius_quadruped", "Idle_HitReact1"},
        {"quad_wolf",      "quaternius_quadruped", "Idle_HitReact1"},
        {"monster_yeti",   "quaternius_biped",     "HitReact"},
        {"monster_dragon", "quaternius_wyvern",    "HitReact"},
        {"humanoid",       "humanoid",             "idle"},   // control: neutrality contract
    };
    for (const auto& r : routes) {
        const std::string file = std::string("resources/animated_characters/") + r.rig + ".anim";
        nlohmann::json def = {{"animFile", file}};
        const auto resolved = Core::CharacterVisualResolver::resolve(def, std::string("npc_") + r.rig);
        Scene::AnimatedVoxelCharacter ch(nullptr, glm::vec3(0.0f, 16.05f, 0.0f));
        ch.setAppearance(resolved.appearance);          // exactly what NPCEntity does before load
        ASSERT_TRUE(ch.loadModel(file)) << r.rig;
        EXPECT_EQ(ch.bodyPlan().id, r.plan) << r.rig << " adopted the wrong plan at runtime";
        EXPECT_EQ(ch.clipForState(Scene::AnimatedCharacterState::HitReact, false), r.hitClip) << r.rig;
    }
}
