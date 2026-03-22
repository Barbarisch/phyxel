#include <gtest/gtest.h>
#include "story/StoryTypes.h"
#include <nlohmann/json.hpp>

using namespace Phyxel::Story;

// ============================================================================
// WorldVariable Tests
// ============================================================================

class WorldVariableTest : public ::testing::Test {};

TEST_F(WorldVariableTest, BoolRoundTrip) {
    WorldVariable v{"quest_started", true};
    nlohmann::json j = v;
    WorldVariable v2 = j.get<WorldVariable>();
    EXPECT_EQ(v2.key, "quest_started");
    EXPECT_TRUE(std::get<bool>(v2.value));
}

TEST_F(WorldVariableTest, IntRoundTrip) {
    WorldVariable v{"kill_count", 42};
    nlohmann::json j = v;
    WorldVariable v2 = j.get<WorldVariable>();
    EXPECT_EQ(std::get<int>(v2.value), 42);
}

TEST_F(WorldVariableTest, FloatRoundTrip) {
    WorldVariable v{"tension", 0.75f};
    nlohmann::json j = v;
    WorldVariable v2 = j.get<WorldVariable>();
    EXPECT_FLOAT_EQ(std::get<float>(v2.value), 0.75f);
}

TEST_F(WorldVariableTest, StringRoundTrip) {
    WorldVariable v{"current_ruler", std::string("Elena")};
    nlohmann::json j = v;
    WorldVariable v2 = j.get<WorldVariable>();
    EXPECT_EQ(std::get<std::string>(v2.value), "Elena");
}

// ============================================================================
// Location Tests
// ============================================================================

class LocationTest : public ::testing::Test {};

TEST_F(LocationTest, JsonRoundTrip) {
    Location loc;
    loc.id = "tavern";
    loc.name = "The Rusty Anchor";
    loc.worldPosition = glm::vec3(100, 16, 200);
    loc.radius = 24.0f;
    loc.controllingFaction = "merchants";
    loc.tags = {"social", "shelter", "trade"};

    nlohmann::json j = loc;
    Location loc2 = j.get<Location>();

    EXPECT_EQ(loc2.id, "tavern");
    EXPECT_EQ(loc2.name, "The Rusty Anchor");
    EXPECT_FLOAT_EQ(loc2.worldPosition.x, 100.0f);
    EXPECT_FLOAT_EQ(loc2.worldPosition.y, 16.0f);
    EXPECT_FLOAT_EQ(loc2.worldPosition.z, 200.0f);
    EXPECT_FLOAT_EQ(loc2.radius, 24.0f);
    EXPECT_EQ(loc2.controllingFaction, "merchants");
    EXPECT_EQ(loc2.tags.size(), 3u);
}

// ============================================================================
// Faction Tests
// ============================================================================

class FactionTest : public ::testing::Test {};

TEST_F(FactionTest, JsonRoundTrip) {
    Faction f;
    f.id = "kingdom";
    f.name = "Kingdom of Aldren";
    f.relations["bandits"] = -0.8f;
    f.relations["merchants"] = 0.5f;
    f.memberCharacterIds = {"captain_elena", "guard_01"};
    f.controlledLocationIds = {"castle", "barracks"};

    nlohmann::json j = f;
    Faction f2 = j.get<Faction>();

    EXPECT_EQ(f2.id, "kingdom");
    EXPECT_EQ(f2.name, "Kingdom of Aldren");
    EXPECT_FLOAT_EQ(f2.relations["bandits"], -0.8f);
    EXPECT_FLOAT_EQ(f2.relations["merchants"], 0.5f);
    EXPECT_EQ(f2.memberCharacterIds.size(), 2u);
}

// ============================================================================
// WorldEvent Tests
// ============================================================================

class WorldEventTest : public ::testing::Test {};

TEST_F(WorldEventTest, JsonRoundTrip) {
    WorldEvent e;
    e.id = "raid_001";
    e.type = "combat";
    e.timestamp = 120.5f;
    e.location = glm::vec3(50, 16, 80);
    e.audibleRadius = 64.0f;
    e.visibleRadius = 32.0f;
    e.participants = {"bandit_01", "guard_02"};
    e.affectedFactions = {"kingdom", "bandits"};
    e.details = {{"outcome", "guard_defeated"}, {"casualties", 1}};
    e.importance = 0.8f;

    nlohmann::json j = e;
    WorldEvent e2 = j.get<WorldEvent>();

    EXPECT_EQ(e2.id, "raid_001");
    EXPECT_EQ(e2.type, "combat");
    EXPECT_FLOAT_EQ(e2.timestamp, 120.5f);
    EXPECT_FLOAT_EQ(e2.location.x, 50.0f);
    EXPECT_FLOAT_EQ(e2.audibleRadius, 64.0f);
    EXPECT_FLOAT_EQ(e2.importance, 0.8f);
    EXPECT_EQ(e2.details["outcome"], "guard_defeated");
}

// ============================================================================
// WorldState Tests
// ============================================================================

class WorldStateTest : public ::testing::Test {
protected:
    void SetUp() override {
        Faction kingdom;
        kingdom.id = "kingdom";
        kingdom.name = "Kingdom";
        kingdom.relations["bandits"] = -0.8f;
        state.addFaction(std::move(kingdom));

        Faction bandits;
        bandits.id = "bandits";
        bandits.name = "Bandits";
        bandits.relations["kingdom"] = -0.8f;
        state.addFaction(std::move(bandits));

        Location castle;
        castle.id = "castle";
        castle.name = "Royal Castle";
        castle.worldPosition = glm::vec3(100, 16, 100);
        state.addLocation(std::move(castle));
    }

    WorldState state;
};

TEST_F(WorldStateTest, AddAndGetFaction) {
    auto* f = state.getFaction("kingdom");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->name, "Kingdom");
}

TEST_F(WorldStateTest, UnknownFactionReturnsNull) {
    EXPECT_EQ(state.getFaction("pirates"), nullptr);
}

TEST_F(WorldStateTest, AddAndGetLocation) {
    auto* loc = state.getLocation("castle");
    ASSERT_NE(loc, nullptr);
    EXPECT_EQ(loc->name, "Royal Castle");
}

TEST_F(WorldStateTest, SetAndGetVariable) {
    state.setVariable("quest_active", true);
    auto* v = state.getVariable("quest_active");
    ASSERT_NE(v, nullptr);
    EXPECT_TRUE(std::get<bool>(v->value));
}

TEST_F(WorldStateTest, FactionRelation) {
    EXPECT_FLOAT_EQ(state.getFactionRelation("kingdom", "bandits"), -0.8f);
    // Reverse lookup
    EXPECT_FLOAT_EQ(state.getFactionRelation("bandits", "kingdom"), -0.8f);
    // Unknown pair
    EXPECT_FLOAT_EQ(state.getFactionRelation("kingdom", "pirates"), 0.0f);
}

TEST_F(WorldStateTest, RecordAndQueryEvents) {
    state.worldTime = 100.0f;

    WorldEvent e1;
    e1.id = "trade_01";
    e1.type = "trade";
    state.recordEvent(e1);

    state.worldTime = 200.0f;
    WorldEvent e2;
    e2.id = "combat_01";
    e2.type = "combat";
    state.recordEvent(e2);

    auto trades = state.getEventsOfType("trade");
    EXPECT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0]->id, "trade_01");

    auto recent = state.getEventsSince(150.0f);
    EXPECT_EQ(recent.size(), 1u);
    EXPECT_EQ(recent[0]->id, "combat_01");
}

TEST_F(WorldStateTest, FullJsonRoundTrip) {
    state.worldTime = 50.0f;
    state.dramaTension = 0.6f;
    state.setVariable("flag", std::string("hello"));

    WorldEvent e;
    e.id = "test_event";
    e.type = "test";
    state.recordEvent(e);

    nlohmann::json j = state;
    WorldState state2 = j.get<WorldState>();

    EXPECT_FLOAT_EQ(state2.worldTime, 50.0f);
    EXPECT_FLOAT_EQ(state2.dramaTension, 0.6f);
    EXPECT_NE(state2.getFaction("kingdom"), nullptr);
    EXPECT_NE(state2.getLocation("castle"), nullptr);
    EXPECT_NE(state2.getVariable("flag"), nullptr);
    EXPECT_EQ(state2.eventHistory.size(), 1u);
}
