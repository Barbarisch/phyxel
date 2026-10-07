#include <gtest/gtest.h>

#include "scene/AnimatedVoxelCharacter.h"
#include "scene/BodyPlan.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>

using Phyxel::Scene::AnimatedVoxelCharacter;
using Phyxel::Scene::AnimatedCharacterState;
namespace fs = std::filesystem;

// ============================================================================
// Roadmap R1 decision 3 (docs/CharacterAnimationRoadmap.md): the coverage report must say what the
// ENGINE would play, not what a Python re-implementation guesses. This test is the single source:
// for every rig the catalogue uses (monster bindings + race visuals + the humanoid), it loads the
// rig, resolves every FSM state through AnimatedVoxelCharacter::clipForState, takes Death through
// die() and HitReact through hitReact() + one update (both pick clips by literal names inside the
// engine), and records the body plan + gait class + clip list.
//   PHYXEL_WRITE_CLIP_FIXTURE=1  -> (re)write tests/fixtures/clip_resolution.json
//   otherwise                   -> assert the engine still matches the committed fixture
// tools/anim_pipeline/coverage_report.py reads the fixture; tests/test_coverage_report.py checks
// it is not stale against the rig files. Either side drifting fails a test.
// Lives in the STRESS suite: loading all 56 catalogue rigs took 106 s in Debug (2026-10-01), over
// the 60 s line the R1 gate set; the rig list is never shrunk to make it faster. Run from the repo
// root (the add_test WORKING_DIRECTORY does that):
//   build/tests/stress/Debug/phyxel_stress_tests.exe --gtest_filter=ClipResolutionFixture.*
// ============================================================================

namespace {

const fs::path kFixture = "tests/fixtures/clip_resolution.json";

std::set<std::string> catalogueRigs() {
    std::set<std::string> rigs{"resources/animated_characters/humanoid.anim"};
    std::ifstream b("resources/monsters/visuals/bindings.json");
    nlohmann::json j; b >> j;
    for (auto it = j.begin(); it != j.end(); ++it)
        if (it.value().is_object() && it.value().contains("animFile"))
            rigs.insert(it.value()["animFile"].get<std::string>());
    for (const auto& e : fs::directory_iterator("resources/races")) {
        std::ifstream r(e.path()); nlohmann::json rj; r >> rj;
        if (rj.contains("visual") && rj["visual"].contains("animFile"))
            rigs.insert(rj["visual"]["animFile"].get<std::string>());
    }
    return rigs;
}

const AnimatedCharacterState kStates[] = {
    AnimatedCharacterState::Idle, AnimatedCharacterState::StartWalk, AnimatedCharacterState::Walk,
    AnimatedCharacterState::Run, AnimatedCharacterState::Jump, AnimatedCharacterState::Fall,
    AnimatedCharacterState::Land, AnimatedCharacterState::Crouch, AnimatedCharacterState::CrouchIdle,
    AnimatedCharacterState::CrouchWalk, AnimatedCharacterState::StandUp, AnimatedCharacterState::Attack,
    AnimatedCharacterState::TurnLeft, AnimatedCharacterState::TurnRight, AnimatedCharacterState::StrafeLeft,
    AnimatedCharacterState::StrafeRight, AnimatedCharacterState::WalkStrafeLeft, AnimatedCharacterState::WalkStrafeRight,
    AnimatedCharacterState::BackwardWalk, AnimatedCharacterState::StopWalk, AnimatedCharacterState::StopRun,
    AnimatedCharacterState::ClimbStairs, AnimatedCharacterState::DescendStairs, AnimatedCharacterState::SitDown,
    AnimatedCharacterState::SittingIdle, AnimatedCharacterState::SitStandUp, AnimatedCharacterState::Cast,
    AnimatedCharacterState::Block, AnimatedCharacterState::Dodge, AnimatedCharacterState::KnockedOut,
    AnimatedCharacterState::GetUp, AnimatedCharacterState::Celebrate,
};

nlohmann::json resolveRig(const std::string& rig) {
    nlohmann::json out;
    auto ch = std::make_unique<AnimatedVoxelCharacter>(nullptr, glm::vec3(0.0f));
    if (!ch->loadModel(rig)) { out["error"] = "load failed"; return out; }
    out["plan"] = ch->bodyPlan().id;
    out["gaitClass"] = ch->bodyPlan().gaitClass;
    auto names = ch->getAnimationNames();
    std::sort(names.begin(), names.end());
    out["clips"] = names;
    nlohmann::json states = nlohmann::json::object();
    for (auto s : kStates) states[ch->stateToString(s)] = ch->clipForState(s, false);
    // HitReact: the engine picks hit_head / hit_stomach / hit_rib by literal name, else the plan
    // fallback, else the legacy "idle" — exercise the real path.
    ch->update(1.0f / 60.0f);
    ch->hitReact(false);
    ch->update(1.0f / 60.0f);
    states["HitReact"] = ch->getAnimationState() == AnimatedCharacterState::HitReact
        ? ch->clipForState(AnimatedCharacterState::HitReact, false) : std::string("<no hit state>");
    // Death: die() picks death_front / death_back by literal name, then clipForState resolves.
    ch->die(false);
    states["Death"] = ch->clipForState(AnimatedCharacterState::Death, false);
    out["states"] = states;
    return out;
}

} // namespace

TEST(ClipResolutionFixture, EngineResolutionMatchesTheCommittedFixture) {
    const auto t0 = std::chrono::steady_clock::now();
    nlohmann::json table = nlohmann::json::object();
    for (const auto& rig : catalogueRigs()) table[rig] = resolveRig(rig);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("[ClipResolutionFixture] %zu rigs resolved in %.1f s\n", table.size(), secs);

    if (const char* w = std::getenv("PHYXEL_WRITE_CLIP_FIXTURE"); w && std::string(w) == "1") {
        fs::create_directories(kFixture.parent_path());
        std::ofstream(kFixture) << table.dump(1) << "\n";
        std::printf("[ClipResolutionFixture] wrote %s\n", kFixture.string().c_str());
        SUCCEED();
        return;
    }
    std::ifstream in(kFixture);
    ASSERT_TRUE(in.is_open()) << kFixture << " missing — run with PHYXEL_WRITE_CLIP_FIXTURE=1";
    nlohmann::json committed; in >> committed;
    for (auto it = table.begin(); it != table.end(); ++it) {
        ASSERT_TRUE(committed.contains(it.key())) << "fixture lacks rig " << it.key() << " — regenerate";
        EXPECT_EQ(committed[it.key()], it.value()) << "engine resolution drifted for " << it.key() << " — regenerate and review the diff";
    }
    EXPECT_EQ(committed.size(), table.size()) << "fixture has rigs the catalogue no longer uses — regenerate";
}
