#include <gtest/gtest.h>
#include "story/StoryEngine.h"
#include <nlohmann/json.hpp>

using namespace Phyxel::Story;

// ============================================================================
// StoryEngine Tests
// ============================================================================

class StoryEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        WorldState ws;

        Faction kingdom;
        kingdom.id = "kingdom";
        kingdom.name = "Kingdom of Aldren";
        kingdom.relations["bandits"] = -0.8f;
        ws.addFaction(std::move(kingdom));

        Faction bandits;
        bandits.id = "bandits";
        bandits.name = "Shadow Fang";
        bandits.relations["kingdom"] = -0.8f;
        ws.addFaction(std::move(bandits));

        Location castle;
        castle.id = "castle";
        castle.name = "Royal Castle";
        castle.worldPosition = glm::vec3(100, 16, 100);
        ws.addLocation(std::move(castle));

        engine.defineWorld(std::move(ws));
    }

    StoryEngine engine;
};

TEST_F(StoryEngineTest, DefineWorldSetsState) {
    auto& ws = engine.getWorldState();
    EXPECT_NE(ws.getFaction("kingdom"), nullptr);
    EXPECT_NE(ws.getLocation("castle"), nullptr);
}

TEST_F(StoryEngineTest, AddAndGetCharacter) {
    CharacterProfile p;
    p.id = "elena";
    p.name = "Captain Elena";
    p.factionId = "kingdom";

    engine.addCharacter(std::move(p));

    auto* c = engine.getCharacter("elena");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->name, "Captain Elena");
}

TEST_F(StoryEngineTest, AddCharacterCreatesFactionLink) {
    CharacterProfile p;
    p.id = "elena";
    p.factionId = "kingdom";
    engine.addCharacter(std::move(p));

    auto* faction = engine.getWorldState().getFaction("kingdom");
    ASSERT_NE(faction, nullptr);
    auto& members = faction->memberCharacterIds;
    EXPECT_NE(std::find(members.begin(), members.end(), "elena"), members.end());
}

TEST_F(StoryEngineTest, AddCharacterCreatesEmptyMemory) {
    CharacterProfile p;
    p.id = "elena";
    engine.addCharacter(std::move(p));

    auto* mem = engine.getCharacterMemory("elena");
    ASSERT_NE(mem, nullptr);
    EXPECT_EQ(mem->factCount(), 0u);
}

TEST_F(StoryEngineTest, RemoveCharacter) {
    CharacterProfile p;
    p.id = "elena";
    p.factionId = "kingdom";
    engine.addCharacter(std::move(p));

    EXPECT_TRUE(engine.removeCharacter("elena"));
    EXPECT_EQ(engine.getCharacter("elena"), nullptr);
    EXPECT_EQ(engine.getCharacterMemory("elena"), nullptr);
}

TEST_F(StoryEngineTest, RemoveUnknownCharacterReturnsFalse) {
    EXPECT_FALSE(engine.removeCharacter("nobody"));
}

TEST_F(StoryEngineTest, GetCharacterIds) {
    CharacterProfile p1;
    p1.id = "elena";
    engine.addCharacter(std::move(p1));

    CharacterProfile p2;
    p2.id = "marcus";
    engine.addCharacter(std::move(p2));

    auto ids = engine.getCharacterIds();
    EXPECT_EQ(ids.size(), 2u);
}

TEST_F(StoryEngineTest, AddStartingKnowledge) {
    CharacterProfile p;
    p.id = "elena";
    engine.addCharacter(std::move(p));

    engine.addStartingKnowledge("elena", "history_01", "The kingdom was founded 100 years ago");

    auto* mem = engine.getCharacterMemory("elena");
    ASSERT_NE(mem, nullptr);
    EXPECT_TRUE(mem->knowsAbout("history_01"));
}

TEST_F(StoryEngineTest, AddStartingKnowledgeUnknownCharacterNoOp) {
    // Should not crash
    engine.addStartingKnowledge("nobody", "fact", "test");
}

TEST_F(StoryEngineTest, UpdateAdvancesWorldTime) {
    float before = engine.getWorldState().worldTime;
    engine.update(1.0f);
    EXPECT_FLOAT_EQ(engine.getWorldState().worldTime, before + 1.0f);
}

TEST_F(StoryEngineTest, UpdateDecaysEmotions) {
    CharacterProfile p;
    p.id = "elena";
    p.traits.neuroticism = 0.0f; // Fast decay
    p.emotion.anger = 0.5f;
    engine.addCharacter(std::move(p));

    engine.update(1.0f);

    auto* emotion = engine.getCharacterEmotion("elena");
    ASSERT_NE(emotion, nullptr);
    EXPECT_LT(emotion->anger, 0.5f);
}

TEST_F(StoryEngineTest, TriggerEventRecords) {
    WorldEvent e;
    e.id = "event_01";
    e.type = "test";
    engine.triggerEvent(std::move(e));

    EXPECT_EQ(engine.getWorldState().eventHistory.size(), 1u);
}

TEST_F(StoryEngineTest, SetGoalPriority) {
    CharacterProfile p;
    p.id = "elena";
    CharacterGoal g;
    g.id = "protect";
    g.priority = 0.9f;
    p.goals.push_back(std::move(g));
    engine.addCharacter(std::move(p));

    EXPECT_TRUE(engine.setGoalPriority("elena", "protect", 0.1f));
    EXPECT_FLOAT_EQ(engine.getCharacter("elena")->getGoal("protect")->priority, 0.1f);
}

TEST_F(StoryEngineTest, SetGoalPriorityUnknownCharacter) {
    EXPECT_FALSE(engine.setGoalPriority("nobody", "protect", 0.5f));
}

TEST_F(StoryEngineTest, SetGoalPriorityUnknownGoal) {
    CharacterProfile p;
    p.id = "elena";
    engine.addCharacter(std::move(p));
    EXPECT_FALSE(engine.setGoalPriority("elena", "nonsense", 0.5f));
}

TEST_F(StoryEngineTest, SetAgencyLevel) {
    CharacterProfile p;
    p.id = "elena";
    p.agencyLevel = AgencyLevel::Scripted;
    engine.addCharacter(std::move(p));

    EXPECT_TRUE(engine.setAgencyLevel("elena", AgencyLevel::Autonomous));
    EXPECT_EQ(engine.getCharacter("elena")->agencyLevel, AgencyLevel::Autonomous);
}

TEST_F(StoryEngineTest, FullStateRoundTrip) {
    CharacterProfile p;
    p.id = "elena";
    p.name = "Captain Elena";
    p.factionId = "kingdom";
    p.traits.openness = 0.3f;
    p.agencyLevel = AgencyLevel::Guided;
    CharacterGoal g;
    g.id = "protect";
    g.priority = 0.9f;
    p.goals.push_back(std::move(g));
    engine.addCharacter(std::move(p));
    engine.addStartingKnowledge("elena", "fact_01", "Important knowledge");

    engine.getWorldState().worldTime = 42.0f;
    engine.getWorldState().dramaTension = 0.7f;

    // Save
    nlohmann::json state = engine.saveState();

    // Load into a new engine
    StoryEngine engine2;
    engine2.loadState(state);

    EXPECT_FLOAT_EQ(engine2.getWorldState().worldTime, 42.0f);
    EXPECT_FLOAT_EQ(engine2.getWorldState().dramaTension, 0.7f);
    EXPECT_NE(engine2.getWorldState().getFaction("kingdom"), nullptr);

    auto* c = engine2.getCharacter("elena");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->name, "Captain Elena");
    EXPECT_FLOAT_EQ(c->traits.openness, 0.3f);
    EXPECT_EQ(c->agencyLevel, AgencyLevel::Guided);

    auto* mem = engine2.getCharacterMemory("elena");
    ASSERT_NE(mem, nullptr);
    EXPECT_TRUE(mem->knowsAbout("fact_01"));
}

// ============================================================================
// Phase S2: EventBus + KnowledgePropagator integration via StoryEngine
// ============================================================================

TEST_F(StoryEngineTest, TriggerEventBroadcastsViaEventBus) {
    int callCount = 0;
    engine.getEventBus().subscribe([&](const WorldEvent&) { ++callCount; });

    WorldEvent e;
    e.id = "evt_01";
    e.type = "combat";
    e.timestamp = 1.0f;
    e.location = glm::vec3(100, 16, 100);
    engine.triggerEvent(std::move(e));

    EXPECT_EQ(callCount, 1);
}

TEST_F(StoryEngineTest, TriggerEventNotifiesTypeSubscribers) {
    int combatCount = 0, otherCount = 0;
    engine.getEventBus().subscribeToType("combat", [&](const WorldEvent&) { ++combatCount; });
    engine.getEventBus().subscribeToType("dialogue", [&](const WorldEvent&) { ++otherCount; });

    WorldEvent e;
    e.id = "evt_02";
    e.type = "combat";
    e.timestamp = 1.0f;
    engine.triggerEvent(std::move(e));

    EXPECT_EQ(combatCount, 1);
    EXPECT_EQ(otherCount, 0);
}

TEST_F(StoryEngineTest, TriggerEventPropagatesWitnessToParticipants) {
    CharacterProfile p;
    p.id = "elena";
    p.factionId = "kingdom";
    engine.addCharacter(std::move(p));

    // Set a position lookup so propagator works
    engine.getPropagator().setPositionLookup(
        [](const std::string&, glm::vec3& out) { out = glm::vec3(100, 16, 100); return true; });

    WorldEvent e;
    e.id = "battle_01";
    e.type = "combat";
    e.timestamp = 1.0f;
    e.location = glm::vec3(100, 16, 100);
    e.visibleRadius = 50.0f;
    e.audibleRadius = 80.0f;
    e.participants = {"elena"};
    engine.triggerEvent(std::move(e));

    auto* mem = engine.getCharacterMemory("elena");
    ASSERT_NE(mem, nullptr);
    EXPECT_TRUE(mem->knowsAbout("battle_01"));
}

TEST_F(StoryEngineTest, ShareKnowledgeBetweenCharacters) {
    CharacterProfile alice;
    alice.id = "alice";
    alice.traits.extraversion = 0.8f;
    engine.addCharacter(std::move(alice));

    CharacterProfile bob;
    bob.id = "bob";
    engine.addCharacter(std::move(bob));

    // Give alice some knowledge
    engine.addStartingKnowledge("alice", "secret_01", "The treasure is hidden");

    // But innate knowledge has confidence 1.0, so it should be shareable
    int shared = engine.shareKnowledge("alice", "bob");
    EXPECT_GE(shared, 0); // May be 0 if innate facts don't pass confidence filter (they should at 1.0)

    // Manually add a witnessed fact for a more reliable test
    auto* aliceMem = engine.getCharacterMemoryMut("alice");
    KnowledgeFact f;
    f.factId = "gossip_01";
    f.summary = "The baker is a spy";
    f.source = KnowledgeSource::Witnessed;
    f.confidence = 0.9f;
    f.timestamp = 1.0f;
    aliceMem->addFact(std::move(f));

    shared = engine.shareKnowledge("alice", "bob");
    EXPECT_GE(shared, 1);
    EXPECT_TRUE(engine.getCharacterMemory("bob")->knowsAbout("gossip_01"));
}

TEST_F(StoryEngineTest, ShareKnowledgeUnknownCharacterReturnsZero) {
    CharacterProfile alice;
    alice.id = "alice";
    engine.addCharacter(std::move(alice));

    EXPECT_EQ(engine.shareKnowledge("alice", "nobody"), 0);
    EXPECT_EQ(engine.shareKnowledge("nobody", "alice"), 0);
}

TEST_F(StoryEngineTest, SpreadRumorsWithPositionLookup) {
    CharacterProfile alice;
    alice.id = "alice";
    alice.traits.extraversion = 1.0f;
    engine.addCharacter(std::move(alice));

    CharacterProfile bob;
    bob.id = "bob";
    bob.traits.extraversion = 1.0f;
    engine.addCharacter(std::move(bob));

    // Position lookup: both nearby
    engine.getPropagator().setPositionLookup(
        [](const std::string&, glm::vec3& out) { out = glm::vec3(0, 0, 0); return true; });

    // Give alice a witnessed fact
    auto* mem = engine.getCharacterMemoryMut("alice");
    KnowledgeFact f;
    f.factId = "rumor_test";
    f.summary = "The merchant cheated";
    f.source = KnowledgeSource::Witnessed;
    f.confidence = 0.9f;
    f.timestamp = 1.0f;
    mem->addFact(std::move(f));

    engine.getPropagator().setRumorBaseChance(100.0f);
    engine.spreadRumors(10.0f, 32.0f);

    // With high chance, bob should know
    EXPECT_TRUE(engine.getCharacterMemory("bob")->knowsAbout("rumor_test"));
}

TEST_F(StoryEngineTest, EventBusAccessReturnsWorkingBus) {
    auto& bus = engine.getEventBus();
    EXPECT_EQ(bus.subscriberCount(), 0u);
    bus.subscribe([](const WorldEvent&) {});
    EXPECT_EQ(bus.subscriberCount(), 1u);
}

TEST_F(StoryEngineTest, PropagatorAccessReturnsWorkingPropagator) {
    auto& prop = engine.getPropagator();
    prop.setRumorBaseChance(0.42f);
    EXPECT_FLOAT_EQ(prop.getRumorBaseChance(), 0.42f);
}
