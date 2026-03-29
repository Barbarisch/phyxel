#include <gtest/gtest.h>
#include "ai/BTLoader.h"
#include "ai/Schedule.h"
#include "scene/behaviors/ScheduledBehavior.h"
#include "scene/NPCBehavior.h"
#include "graphics/DayNightCycle.h"
#include "core/LocationRegistry.h"
#include <nlohmann/json.hpp>

using namespace Phyxel;
using namespace Phyxel::AI;
using json = nlohmann::json;

// ============================================================================
// BTLoader Tests
// ============================================================================

TEST(BTLoaderTest, ParseSequence) {
    json j = {
        {"type", "Sequence"},
        {"name", "TestSeq"},
        {"children", {
            {{"type", "Action"}, {"action", "Wait"}, {"duration", 1.0}},
            {{"type", "Action"}, {"action", "Wait"}, {"duration", 2.0}}
        }}
    };

    auto node = BTLoader::fromJson(j);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->getName(), "TestSeq");
}

TEST(BTLoaderTest, ParseSelector) {
    json j = {
        {"type", "Selector"},
        {"children", {
            {{"type", "Action"}, {"action", "Wait"}, {"duration", 1.0}},
            {{"type", "Action"}, {"action", "Speak"}, {"message", "Hello"}}
        }}
    };

    auto node = BTLoader::fromJson(j);
    ASSERT_NE(node, nullptr);
}

TEST(BTLoaderTest, ParseParallel) {
    json j = {
        {"type", "Parallel"},
        {"required", 1},
        {"children", {
            {{"type", "Action"}, {"action", "Wait"}, {"duration", 3.0}}
        }}
    };

    auto node = BTLoader::fromJson(j);
    ASSERT_NE(node, nullptr);
}

TEST(BTLoaderTest, ParseInverter) {
    json j = {
        {"type", "Inverter"},
        {"child", {{"type", "Action"}, {"action", "Wait"}, {"duration", 1.0}}}
    };

    auto node = BTLoader::fromJson(j);
    ASSERT_NE(node, nullptr);
}

TEST(BTLoaderTest, ParseRepeater) {
    json j = {
        {"type", "Repeater"},
        {"count", 3},
        {"child", {{"type", "Action"}, {"action", "Wait"}, {"duration", 0.5}}}
    };

    auto node = BTLoader::fromJson(j);
    ASSERT_NE(node, nullptr);
}

TEST(BTLoaderTest, ParseCooldown) {
    json j = {
        {"type", "Cooldown"},
        {"seconds", 5.0},
        {"child", {{"type", "Action"}, {"action", "Speak"}, {"message", "Hmm"}}}
    };

    auto node = BTLoader::fromJson(j);
    ASSERT_NE(node, nullptr);
}

TEST(BTLoaderTest, ParseSucceeder) {
    json j = {
        {"type", "Succeeder"},
        {"child", {{"type", "Action"}, {"action", "Wait"}, {"duration", 1.0}}}
    };

    auto node = BTLoader::fromJson(j);
    ASSERT_NE(node, nullptr);
}

TEST(BTLoaderTest, ParseBBCheck) {
    json j = {
        {"type", "BBCheck"},
        {"key", "hasThreat"},
        {"expected", true}
    };

    auto node = BTLoader::fromJson(j);
    ASSERT_NE(node, nullptr);
}

TEST(BTLoaderTest, ParseBBHasKey) {
    json j = {
        {"type", "BBHasKey"},
        {"key", "target"}
    };

    auto node = BTLoader::fromJson(j);
    ASSERT_NE(node, nullptr);
}

TEST(BTLoaderTest, ParseAllActions) {
    struct ActionCase {
        std::string action;
        json extra;
    };
    std::vector<ActionCase> cases = {
        {"MoveTo", {{"x", 10}, {"y", 20}, {"z", 30}}},
        {"MoveToBlackboard", {{"key", "taskLocationPos"}}},
        {"LookAt", {{"x", 0}, {"y", 0}, {"z", 0}}},
        {"Wait", {{"duration", 2.0}}},
        {"Speak", {{"message", "Hello"}, {"duration", 3.0}}},
        {"SetBlackboard", {{"key", "flag"}, {"value", "true"}}},
        {"MoveToEntity", {{"entityId", "player"}, {"speed", 3.0}}},
        {"Flee", {{"fromKey", "threat"}, {"speed", 4.0}}}
    };

    for (const auto& c : cases) {
        json j = {{"type", "Action"}, {"action", c.action}};
        j.update(c.extra);

        auto node = BTLoader::fromJson(j);
        ASSERT_NE(node, nullptr) << "Failed to parse action: " << c.action;
    }
}

TEST(BTLoaderTest, ParseNestedTree) {
    // A realistic guard behavior tree
    json j = {
        {"type", "Selector"},
        {"name", "GuardBehavior"},
        {"children", {
            {{"type", "Sequence"}, {"name", "RespondToThreat"}, {"children", {
                {{"type", "BBCheck"}, {"key", "hasThreat"}, {"expected", true}},
                {{"type", "Action"}, {"action", "Speak"}, {"message", "Halt!"}},
                {{"type", "Action"}, {"action", "Flee"}, {"fromKey", "threat"}}
            }}},
            {{"type", "Sequence"}, {"name", "StandGuard"}, {"children", {
                {{"type", "BBCheck"}, {"key", "atTaskLocation"}, {"expected", true}},
                {{"type", "Action"}, {"action", "Wait"}, {"duration", 5.0}}
            }}},
            {{"type", "Action"}, {"action", "Wait"}, {"duration", 2.0}}
        }}
    };

    auto node = BTLoader::fromJson(j);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->getName(), "GuardBehavior");
}

TEST(BTLoaderTest, ParseUnknownTypeReturnsNull) {
    json j = {{"type", "NonExistentNode"}};
    auto node = BTLoader::fromJson(j);
    EXPECT_EQ(node, nullptr);
}

TEST(BTLoaderTest, ParseUnknownActionReturnsNull) {
    json j = {{"type", "Action"}, {"action", "DoSomethingCrazy"}};
    auto node = BTLoader::fromJson(j);
    EXPECT_EQ(node, nullptr);
}

// ============================================================================
// ScheduledBehavior Tests
// ============================================================================

TEST(ScheduledBehaviorTest, Construction) {
    Schedule sched = Schedule::guardSchedule();
    Scene::ScheduledBehavior behavior(sched);

    EXPECT_EQ(behavior.getBehaviorName(), "Scheduled");
    EXPECT_FALSE(behavior.getSchedule().getEntries().empty());
}

TEST(ScheduledBehaviorTest, ConstructionWithTree) {
    Schedule sched = Schedule::defaultSchedule();

    json treeJson = {
        {"type", "Selector"},
        {"children", {
            {{"type", "Action"}, {"action", "Wait"}, {"duration", 1.0}}
        }}
    };
    auto tree = BTLoader::fromJson(treeJson);

    Scene::ScheduledBehavior behavior(sched, tree);
    EXPECT_EQ(behavior.getBehaviorName(), "Scheduled");
}

TEST(ScheduledBehaviorTest, SetSchedule) {
    Scene::ScheduledBehavior behavior(Schedule::defaultSchedule());

    Schedule newSched = Schedule::guardSchedule();
    behavior.setSchedule(newSched);

    // Guard schedule should have guard-related entries
    EXPECT_FALSE(behavior.getSchedule().getEntries().empty());
}

TEST(ScheduledBehaviorTest, UpdateWritesBlackboardKeys) {
    Schedule sched;
    ScheduleEntry work;
    work.startHour = 8.0f;
    work.endHour = 17.0f;
    work.activity = ActivityType::Work;
    work.locationId = "shop_1";
    sched.addEntry(work);

    Scene::ScheduledBehavior behavior(sched);

    // Set up DayNightCycle
    Graphics::DayNightCycle dnc;
    dnc.setTimeOfDay(12.0f); // noon — should be in work period

    // Set up LocationRegistry
    Core::LocationRegistry locRegistry;
    Core::Location shopLoc;
    shopLoc.id = "shop_1";
    shopLoc.name = "Shop";
    shopLoc.position = glm::vec3(10, 20, 30);
    shopLoc.radius = 5.0f;
    locRegistry.addLocation(shopLoc);

    // Create minimal NPCContext
    Scene::NPCContext ctx;
    ctx.dayNightCycle = &dnc;
    ctx.locationRegistry = &locRegistry;
    // self is null, but ScheduledBehavior should handle that gracefully

    // Tick the behavior — this should write blackboard keys
    behavior.update(0.016f, ctx);

    // Check blackboard was written
    auto& bb = behavior.getBlackboard();
    EXPECT_EQ(bb.getString("currentActivity"), "Work");
    EXPECT_EQ(bb.getString("taskLocationId"), "shop_1");
    EXPECT_FLOAT_EQ(bb.getFloat("currentHour"), 12.0f);

    auto taskPos = bb.getVec3("taskLocationPos");
    EXPECT_FLOAT_EQ(taskPos.x, 10.0f);
    EXPECT_FLOAT_EQ(taskPos.y, 20.0f);
    EXPECT_FLOAT_EQ(taskPos.z, 30.0f);
}

TEST(ScheduledBehaviorTest, NoActivityWritesWander) {
    Schedule sched;
    // Empty schedule — no entries
    Scene::ScheduledBehavior behavior(sched);

    Graphics::DayNightCycle dnc;
    dnc.setTimeOfDay(12.0f);

    Scene::NPCContext ctx;
    ctx.dayNightCycle = &dnc;

    behavior.update(0.016f, ctx);

    auto& bb = behavior.getBlackboard();
    EXPECT_EQ(bb.getString("currentActivity"), "Wander");
}

TEST(ScheduledBehaviorTest, NightDetection) {
    Schedule sched = Schedule::defaultSchedule();
    Scene::ScheduledBehavior behavior(sched);

    Graphics::DayNightCycle dnc;
    dnc.setTimeOfDay(23.0f); // night

    Scene::NPCContext ctx;
    ctx.dayNightCycle = &dnc;

    behavior.update(0.016f, ctx);

    auto& bb = behavior.getBlackboard();
    EXPECT_TRUE(bb.getBool("isNight"));
}

TEST(ScheduledBehaviorTest, DayDetection) {
    Schedule sched = Schedule::defaultSchedule();
    Scene::ScheduledBehavior behavior(sched);

    Graphics::DayNightCycle dnc;
    dnc.setTimeOfDay(12.0f); // daytime

    Scene::NPCContext ctx;
    ctx.dayNightCycle = &dnc;

    behavior.update(0.016f, ctx);

    auto& bb = behavior.getBlackboard();
    EXPECT_FALSE(bb.getBool("isNight"));
}
