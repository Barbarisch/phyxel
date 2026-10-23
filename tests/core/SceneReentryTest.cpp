// SceneReentryTest.cpp -- coming back to a scene puts you where you LEFT it (G-170 / G-36 / G-82).
//
// User, 2026-09-23: "travelling to hollow barrow and back drops you at the entry point that you
// enter when coming from the town". Three ledger rows were the same defect: SceneManager saved
// the CAMERA's position on unload and restored the CAMERA on re-entry, while the player entity
// was re-spawned from the scene's authored `player.position` - the town-side entry. In third
// person the camera then snapped back to the player, so the restore was invisible.
//
// The rule now: on unload, record the PLAYER's position; on re-entry, spawn the player THERE by
// overriding the definition's player position before the loader runs. And because a scene is
// usually left by walking into an exit region, a region trigger must ARM on its first evaluation
// rather than fire - otherwise re-entering spawns you inside the exit and bounces you straight
// back (the doorway ping-pong).

#include <gtest/gtest.h>

#include "core/SceneManager.h"
#include "core/TriggerSystem.h"

#include <string>
#include <vector>

using Phyxel::Core::SceneManager;
using Phyxel::Core::TriggerSystem;
using json = nlohmann::json;

// ---------------------------------------------------------------- the player position override

TEST(SceneReentry, AVisitedSceneOverridesTheAuthoredPlayerSpawnWithWhereTheyLeft) {
    json def = {{"player", {{"position", {{"x", 5.0}, {"y", 20.0}, {"z", 5.0}}}, {"type", "animated"}}}};
    SceneManager::SceneReentryState rs;
    rs.visited = true;
    rs.lastPlayerX = 42.5f; rs.lastPlayerY = 17.0f; rs.lastPlayerZ = -9.0f;
    SceneManager::applyReentryPosition(def, rs);
    EXPECT_FLOAT_EQ(def["player"]["position"]["x"].get<float>(), 42.5f);
    EXPECT_FLOAT_EQ(def["player"]["position"]["y"].get<float>(), 17.0f);
    EXPECT_FLOAT_EQ(def["player"]["position"]["z"].get<float>(), -9.0f);
    EXPECT_EQ(def["player"]["type"], "animated") << "only the position changes";
}

TEST(SceneReentry, AFirstVisitKeepsTheAuthoredSpawn) {
    json def = {{"player", {{"position", {{"x", 5.0}, {"y", 20.0}, {"z", 5.0}}}}}};
    SceneManager::SceneReentryState rs;   // visited == false
    rs.lastPlayerX = 42.5f;
    SceneManager::applyReentryPosition(def, rs);
    EXPECT_FLOAT_EQ(def["player"]["position"]["x"].get<float>(), 5.0f)
        << "a scene you have never been to must use its authored entry";
}

TEST(SceneReentry, ADefinitionWithoutAPlayerBlockGetsOneOnReentry) {
    // playerDefaults are merged before this runs, but a scene can still lack the block; the
    // override must not silently do nothing.
    json def = json::object();
    SceneManager::SceneReentryState rs;
    rs.visited = true;
    rs.lastPlayerX = 1.0f; rs.lastPlayerY = 2.0f; rs.lastPlayerZ = 3.0f;
    SceneManager::applyReentryPosition(def, rs);
    ASSERT_TRUE(def.contains("player"));
    EXPECT_FLOAT_EQ(def["player"]["position"]["z"].get<float>(), 3.0f);
}

// ---------------------------------------------------------------- the exit region must not bounce

namespace {

struct ActionLog {
    std::vector<std::string> fired;
    TriggerSystem::ActionExecutor executor() {
        return [this](const json& a, const std::string& tid) { fired.push_back(tid); };
    }
};

json exitRegion(const std::string& id) {
    return {{"id", id},
            {"when", {{"event", "entity_reached_region"}, {"entity", "player"},
                      {"region", {{"from", {{"x", 0}, {"y", 0}, {"z", 0}}},
                                  {"to",   {{"x", 4}, {"y", 4}, {"z", 4}}}}}}},
            {"then", json::array({{{"type", "transition_scene"}, {"target", "town"}}})},
            {"once", true}};
}

}  // namespace

TEST(SceneReentry, AnEntityThatStartsInsideAnExitRegionDoesNotFireItUntilItLeavesAndReturns) {
    // RED before the fix: wasInside began false, so the first update saw inside && !wasInside
    // and fired - re-entering a scene at the exit you left through sent you straight back.
    TriggerSystem ts;
    ActionLog log;
    ts.setActionExecutor(log.executor());
    ASSERT_NE(ts.addTrigger(exitRegion("back_to_town")), "");

    glm::vec3 where(2.0f, 1.0f, 2.0f);   // spawned INSIDE the exit
    auto at = [&where](const std::string&, glm::vec3& out) { out = where; return true; };

    ts.update(0.016f, at);
    ts.update(0.016f, at);
    EXPECT_TRUE(log.fired.empty()) << "spawning inside the exit must not count as reaching it";

    where = glm::vec3(10.0f, 1.0f, 10.0f);   // walk out
    ts.update(0.016f, at);
    EXPECT_TRUE(log.fired.empty());

    where = glm::vec3(2.0f, 1.0f, 2.0f);     // walk back in: THAT is reaching it
    ts.update(0.016f, at);
    ts.update(0.016f, at);
    ASSERT_EQ(log.fired.size(), 1u);
    EXPECT_EQ(log.fired[0], "back_to_town");
}

TEST(SceneReentry, AnEntityThatWalksIntoARegionFromOutsideStillFiresOnEntry) {
    // The control: the ordinary case must be unchanged.
    TriggerSystem ts;
    ActionLog log;
    ts.setActionExecutor(log.executor());
    ASSERT_NE(ts.addTrigger(exitRegion("to_farm")), "");
    glm::vec3 where(10.0f, 1.0f, 10.0f);
    auto at = [&where](const std::string&, glm::vec3& out) { out = where; return true; };
    ts.update(0.016f, at);                 // outside: arms
    where = glm::vec3(1.0f, 1.0f, 1.0f);   // in
    ts.update(0.016f, at);
    ts.update(0.016f, at);
    ASSERT_EQ(log.fired.size(), 1u);
}
