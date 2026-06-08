#include <gtest/gtest.h>
#include "core/TriggerSystem.h"

using Phyxel::Core::TriggerSystem;
using nlohmann::json;

namespace {

// Collects executed actions for assertions.
struct ActionLog {
    std::vector<std::pair<std::string, std::string>> executed; // (type, triggerId)
    TriggerSystem::ActionExecutor executor() {
        return [this](const json& a, const std::string& tid) {
            executed.emplace_back(a.value("type", ""), tid);
        };
    }
};

json basicTrigger(const std::string& id, const std::string& event, bool once = true) {
    return {
        {"id", id},
        {"when", {{"event", event}}},
        {"then", json::array({{{"type", "quit_game"}}})},
        {"once", once}
    };
}

} // namespace

TEST(TriggerSystemTest, EventTriggerFiresOnceByDefault) {
    TriggerSystem ts;
    ActionLog log;
    ts.setActionExecutor(log.executor());
    ASSERT_NE(ts.addTrigger(basicTrigger("t1", "player_jumped")), "");

    ts.onEvent("player_jumped");
    ts.onEvent("player_jumped"); // second event before update — still once
    ts.update(0.016f);
    ts.onEvent("player_jumped"); // after firing — suppressed
    ts.update(0.016f);

    ASSERT_EQ(log.executed.size(), 1u);
    EXPECT_EQ(log.executed[0].first, "quit_game");
    EXPECT_EQ(log.executed[0].second, "t1");
}

TEST(TriggerSystemTest, ActionsAreQueuedUntilUpdate) {
    TriggerSystem ts;
    ActionLog log;
    ts.setActionExecutor(log.executor());
    ts.addTrigger(basicTrigger("t1", "player_landed"));

    ts.onEvent("player_landed");
    EXPECT_TRUE(log.executed.empty()) << "actions must not run inside onEvent";
    ts.update(0.016f);
    EXPECT_EQ(log.executed.size(), 1u);
}

TEST(TriggerSystemTest, RepeatsWhenOnceFalse) {
    TriggerSystem ts;
    ActionLog log;
    ts.setActionExecutor(log.executor());
    ts.addTrigger(basicTrigger("t1", "player_jumped", /*once=*/false));

    ts.onEvent("player_jumped");
    ts.update(0.016f);
    ts.onEvent("player_jumped");
    ts.update(0.016f);
    EXPECT_EQ(log.executed.size(), 2u);
}

TEST(TriggerSystemTest, PayloadIdMatching) {
    TriggerSystem ts;
    ActionLog log;
    ts.setActionExecutor(log.executor());
    json t = {
        {"id", "obj_win"},
        {"when", {{"event", "objective_complete"}, {"id", "win"}}},
        {"then", json::array({{{"type", "transition_scene"}, {"target", "credits"}}})}
    };
    ASSERT_NE(ts.addTrigger(t), "");

    ts.onEvent("objective_complete", {{"id", "other"}});
    ts.update(0.016f);
    EXPECT_TRUE(log.executed.empty());

    ts.onEvent("objective_complete", {{"id", "win"}});
    ts.update(0.016f);
    ASSERT_EQ(log.executed.size(), 1u);
    EXPECT_EQ(log.executed[0].first, "transition_scene");
}

TEST(TriggerSystemTest, TimerFiresAfterElapsed) {
    TriggerSystem ts;
    ActionLog log;
    ts.setActionExecutor(log.executor());
    json t = {
        {"id", "timed"},
        {"when", {{"event", "timer"}, {"seconds", 1.0f}}},
        {"then", json::array({{{"type", "complete_objective"}, {"id", "survive"}}})}
    };
    ASSERT_NE(ts.addTrigger(t), "");

    for (int i = 0; i < 9; ++i) ts.update(0.1f); // 0.9s
    EXPECT_TRUE(log.executed.empty());
    ts.update(0.2f); // 1.1s
    ASSERT_EQ(log.executed.size(), 1u);
    EXPECT_EQ(log.executed[0].first, "complete_objective");
}

TEST(TriggerSystemTest, RegionFiresOnEnterEdgeOnly) {
    TriggerSystem ts;
    ActionLog log;
    ts.setActionExecutor(log.executor());
    json t = {
        {"id", "reach"},
        {"when", {{"event", "entity_reached_region"}, {"entity", "player"},
                  {"region", {{"from", {{"x", 0}, {"y", 0}, {"z", 0}}},
                              {"to",   {{"x", 10}, {"y", 10}, {"z", 10}}}}}}},
        {"then", json::array({{{"type", "quit_game"}}})},
        {"once", false}
    };
    ASSERT_NE(ts.addTrigger(t), "");

    glm::vec3 pos(50.0f, 5.0f, 5.0f); // outside
    auto resolver = [&](const std::string& id, glm::vec3& out) {
        if (id != "player") return false;
        out = pos;
        return true;
    };

    ts.update(0.016f, resolver);
    EXPECT_TRUE(log.executed.empty());

    pos = glm::vec3(5.0f, 5.0f, 5.0f); // enter
    ts.update(0.016f, resolver);
    EXPECT_EQ(log.executed.size(), 1u);

    ts.update(0.016f, resolver); // still inside — no re-fire (edge-triggered)
    EXPECT_EQ(log.executed.size(), 1u);

    pos = glm::vec3(50.0f, 5.0f, 5.0f); // leave
    ts.update(0.016f, resolver);
    pos = glm::vec3(5.0f, 5.0f, 5.0f);  // re-enter (once=false)
    ts.update(0.016f, resolver);
    EXPECT_EQ(log.executed.size(), 2u);
}

TEST(TriggerSystemTest, ValidationRejectsBadDefinitions) {
    TriggerSystem ts;
    std::string err;
    EXPECT_EQ(ts.addTrigger(json::object(), &err), "");
    EXPECT_EQ(ts.addTrigger({{"when", {{"event", ""}}}, {"then", json::array()}}, &err), "");
    EXPECT_EQ(ts.addTrigger({{"when", {{"event", "timer"}}},
                             {"then", json::array({{{"type", "quit_game"}}})}}, &err), "")
        << "timer without seconds must be rejected";
    EXPECT_EQ(ts.count(), 0u);
}

TEST(TriggerSystemTest, LoadListRemove) {
    TriggerSystem ts;
    json arr = json::array({
        basicTrigger("a", "player_jumped"),
        basicTrigger("b", "player_landed"),
        json::object() // invalid — skipped
    });
    EXPECT_EQ(ts.loadFromJson(arr), 2);
    EXPECT_EQ(ts.count(), 2u);
    EXPECT_EQ(ts.listTriggers().size(), 2u);

    EXPECT_TRUE(ts.removeTrigger("a"));
    EXPECT_FALSE(ts.removeTrigger("a"));
    EXPECT_EQ(ts.count(), 1u);

    // Re-adding the same id replaces, not duplicates.
    ts.addTrigger(basicTrigger("b", "player_jumped"));
    EXPECT_EQ(ts.count(), 1u);
}

TEST(TriggerSystemTest, EventSinkReportsTriggerFired) {
    TriggerSystem ts;
    ActionLog log;
    ts.setActionExecutor(log.executor());
    std::vector<std::string> sinkEvents;
    ts.setEventSink([&](const std::string& type, const json& data) {
        sinkEvents.push_back(type + ":" + data.value("id", ""));
    });
    ts.addTrigger(basicTrigger("t1", "player_jumped"));
    ts.onEvent("player_jumped");
    ts.update(0.016f);
    ASSERT_EQ(sinkEvents.size(), 1u);
    EXPECT_EQ(sinkEvents[0], "trigger_fired:t1");
}

TEST(TriggerSystemTest, HudCountdownExposesRemainingTime) {
    TriggerSystem ts;
    ActionLog log;
    ts.setActionExecutor(log.executor());

    // A hud timer, a non-hud timer, and a hud event trigger (not a countdown).
    ts.addTrigger({{"id", "escape"},
                   {"when", {{"event", "timer"}, {"seconds", 10.0f}}},
                   {"then", json::array({{{"type", "quit_game"}}})},
                   {"hud", true}, {"hudLabel", "Escape!"}});
    ts.addTrigger({{"id", "silent"},
                   {"when", {{"event", "timer"}, {"seconds", 5.0f}}},
                   {"then", json::array({{{"type", "quit_game"}}})}});
    ts.addTrigger({{"id", "jump_hud"},
                   {"when", {{"event", "player_jumped"}}},
                   {"then", json::array({{{"type", "quit_game"}}})},
                   {"hud", true}});

    auto cds = ts.getActiveCountdowns();
    ASSERT_EQ(cds.size(), 1u);             // only the hud TIMER counts down
    EXPECT_EQ(cds[0].id, "escape");
    EXPECT_EQ(cds[0].label, "Escape!");
    EXPECT_FLOAT_EQ(cds[0].total, 10.0f);
    EXPECT_FLOAT_EQ(cds[0].remaining, 10.0f);

    ts.update(4.0f);
    cds = ts.getActiveCountdowns();
    ASSERT_EQ(cds.size(), 1u);
    EXPECT_FLOAT_EQ(cds[0].remaining, 6.0f);

    // After firing (once=true default), the countdown disappears.
    ts.update(6.0f);
    EXPECT_TRUE(ts.getActiveCountdowns().empty());

    // listTriggers carries the hud fields for MCP visibility.
    bool sawHud = false;
    for (const auto& t : ts.listTriggers())
        if (t.value("id", "") == "jump_hud") sawHud = t.value("hud", false);
    EXPECT_TRUE(sawHud);
}
