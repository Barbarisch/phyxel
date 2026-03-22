#include <gtest/gtest.h>
#include "story/KnowledgePropagator.h"

using namespace Phyxel::Story;

// ============================================================================
// KnowledgePropagator Tests
// ============================================================================

class KnowledgePropagatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up position lookup: characters at known locations
        propagator.setPositionLookup([this](const std::string& id, glm::vec3& out) -> bool {
            auto it = positions.find(id);
            if (it == positions.end()) return false;
            out = it->second;
            return true;
        });
    }

    CharacterProfile makeCharacter(const std::string& id, float extraversion = 0.5f,
                                    float agreeableness = 0.5f) {
        CharacterProfile p;
        p.id = id;
        p.name = id;
        p.traits.extraversion = extraversion;
        p.traits.agreeableness = agreeableness;
        p.traits.neuroticism = 0.5f;
        p.traits.openness = 0.5f;
        p.traits.conscientiousness = 0.5f;
        return p;
    }

    WorldEvent makeEvent(const std::string& id, const std::string& type,
                          glm::vec3 loc, float visRadius = 20.0f, float audRadius = 40.0f) {
        WorldEvent e;
        e.id = id;
        e.type = type;
        e.timestamp = 1.0f;
        e.location = loc;
        e.visibleRadius = visRadius;
        e.audibleRadius = audRadius;
        return e;
    }

    KnowledgePropagator propagator;
    std::unordered_map<std::string, glm::vec3> positions;
    std::unordered_map<std::string, CharacterProfile> characters;
    std::unordered_map<std::string, CharacterMemory> memories;
};

// ===================== Channel 1: Witnessing =====================

TEST_F(KnowledgePropagatorTest, ParticipantsAlwaysWitness) {
    auto alice = makeCharacter("alice");
    characters["alice"] = alice;
    memories["alice"] = CharacterMemory{};
    positions["alice"] = glm::vec3(1000, 0, 1000); // Far away

    WorldEvent e = makeEvent("theft", "crime", glm::vec3(0, 0, 0));
    e.participants = {"alice"};

    auto witnesses = propagator.propagateWitness(e, characters, memories);
    ASSERT_EQ(witnesses.size(), 1u);
    EXPECT_EQ(witnesses[0], "alice");
    EXPECT_TRUE(memories["alice"].knowsAbout("theft"));
}

TEST_F(KnowledgePropagatorTest, NearbyCharactersWitness) {
    characters["alice"] = makeCharacter("alice");
    characters["bob"] = makeCharacter("bob");
    memories["alice"] = CharacterMemory{};
    memories["bob"] = CharacterMemory{};
    positions["alice"] = glm::vec3(5, 0, 5);   // Within radius
    positions["bob"] = glm::vec3(100, 0, 100);  // Far away

    WorldEvent e = makeEvent("explosion", "combat", glm::vec3(0, 0, 0), 30.0f, 50.0f);

    auto witnesses = propagator.propagateWitness(e, characters, memories);
    EXPECT_EQ(witnesses.size(), 1u);
    EXPECT_TRUE(memories["alice"].knowsAbout("explosion"));
    EXPECT_FALSE(memories["bob"].knowsAbout("explosion"));
}

TEST_F(KnowledgePropagatorTest, NoPositionLookupReturnsEmpty) {
    KnowledgePropagator noLookup; // No position lookup set
    characters["alice"] = makeCharacter("alice");
    memories["alice"] = CharacterMemory{};

    WorldEvent e = makeEvent("test", "test", glm::vec3(0, 0, 0));
    auto witnesses = noLookup.propagateWitness(e, characters, memories);
    EXPECT_TRUE(witnesses.empty());
}

TEST_F(KnowledgePropagatorTest, ZeroRadiusNoWitnesses) {
    characters["alice"] = makeCharacter("alice");
    memories["alice"] = CharacterMemory{};
    positions["alice"] = glm::vec3(1, 0, 1);

    WorldEvent e = makeEvent("whisper", "dialogue", glm::vec3(0, 0, 0), 0.0f, 0.0f);
    auto witnesses = propagator.propagateWitness(e, characters, memories);
    EXPECT_TRUE(witnesses.empty());
}

TEST_F(KnowledgePropagatorTest, WitnessCreatesFactWithHighConfidence) {
    characters["alice"] = makeCharacter("alice");
    memories["alice"] = CharacterMemory{};
    positions["alice"] = glm::vec3(5, 0, 5);

    WorldEvent e = makeEvent("battle", "combat", glm::vec3(0, 0, 0), 30.0f, 30.0f);
    propagator.propagateWitness(e, characters, memories);

    auto* fact = memories["alice"].getFact("battle");
    ASSERT_NE(fact, nullptr);
    EXPECT_EQ(fact->source, KnowledgeSource::Witnessed);
    EXPECT_GT(fact->confidence, 0.5f);
}

// ===================== Channel 2: Dialogue =====================

TEST_F(KnowledgePropagatorTest, DialogueSharesFacts) {
    auto alice = makeCharacter("alice", 0.8f); // Extraverted
    auto bob = makeCharacter("bob");
    characters["alice"] = alice;
    characters["bob"] = bob;
    memories["alice"] = CharacterMemory{};
    memories["bob"] = CharacterMemory{};

    // Alice knows a fact
    KnowledgeFact fact;
    fact.factId = "secret";
    fact.summary = "The king is ill";
    fact.source = KnowledgeSource::Witnessed;
    fact.confidence = 0.9f;
    fact.timestamp = 1.0f;
    memories["alice"].addFact(std::move(fact));

    int shared = propagator.propagateDialogue(
        "alice", "bob", alice, bob,
        memories["alice"], memories["bob"]);

    EXPECT_GE(shared, 1);
    EXPECT_TRUE(memories["bob"].knowsAbout("secret"));
}

TEST_F(KnowledgePropagatorTest, DialogueRespectsMaxFacts) {
    auto alice = makeCharacter("alice", 0.8f);
    auto bob = makeCharacter("bob");

    memories["alice"] = CharacterMemory{};
    memories["bob"] = CharacterMemory{};

    // Give Alice 10 facts
    for (int i = 0; i < 10; ++i) {
        KnowledgeFact f;
        f.factId = "fact_" + std::to_string(i);
        f.summary = "Fact number " + std::to_string(i);
        f.source = KnowledgeSource::Witnessed;
        f.confidence = 0.9f;
        f.timestamp = static_cast<float>(i);
        memories["alice"].addFact(std::move(f));
    }

    int shared = propagator.propagateDialogue(
        "alice", "bob", alice, bob,
        memories["alice"], memories["bob"], 3);

    EXPECT_LE(shared, 3);
}

TEST_F(KnowledgePropagatorTest, DialogueSkipsLowConfidenceFacts) {
    auto alice = makeCharacter("alice", 0.8f);
    auto bob = makeCharacter("bob");
    memories["alice"] = CharacterMemory{};
    memories["bob"] = CharacterMemory{};

    KnowledgeFact f;
    f.factId = "vague_rumor";
    f.summary = "Something happened";
    f.source = KnowledgeSource::Rumor;
    f.confidence = 0.1f; // Below 0.3 threshold
    f.timestamp = 1.0f;
    memories["alice"].addFact(std::move(f));

    int shared = propagator.propagateDialogue(
        "alice", "bob", alice, bob,
        memories["alice"], memories["bob"]);

    EXPECT_EQ(shared, 0);
}

TEST_F(KnowledgePropagatorTest, DialogueWontShareWithDeepDistrust) {
    auto alice = makeCharacter("alice", 0.8f);
    auto bob = makeCharacter("bob");

    // Alice deeply distrusts Bob
    Relationship rel;
    rel.targetCharacterId = "bob";
    rel.trust = -0.9f;
    alice.relationships.push_back(rel);

    memories["alice"] = CharacterMemory{};
    memories["bob"] = CharacterMemory{};

    KnowledgeFact f;
    f.factId = "treasure";
    f.summary = "Location of hidden treasure";
    f.source = KnowledgeSource::Witnessed;
    f.confidence = 0.9f;
    f.timestamp = 1.0f;
    memories["alice"].addFact(std::move(f));

    int shared = propagator.propagateDialogue(
        "alice", "bob", alice, bob,
        memories["alice"], memories["bob"]);

    EXPECT_EQ(shared, 0);
}

TEST_F(KnowledgePropagatorTest, DialogueSkipsAlreadyKnownFacts) {
    auto alice = makeCharacter("alice", 0.8f);
    auto bob = makeCharacter("bob");
    memories["alice"] = CharacterMemory{};
    memories["bob"] = CharacterMemory{};

    KnowledgeFact f;
    f.factId = "old_news";
    f.summary = "The bridge is broken";
    f.source = KnowledgeSource::Witnessed;
    f.confidence = 0.7f;
    f.timestamp = 1.0f;

    memories["alice"].addFact(f);

    // Bob already knows with higher confidence
    f.confidence = 0.9f;
    memories["bob"].addFact(std::move(f));

    int shared = propagator.propagateDialogue(
        "alice", "bob", alice, bob,
        memories["alice"], memories["bob"]);

    EXPECT_EQ(shared, 0);
}

// ===================== Channel 3: Rumors =====================

TEST_F(KnowledgePropagatorTest, RumorsSpreadBetweenNearbyCharacters) {
    auto alice = makeCharacter("alice", 1.0f); // Max extraversion
    auto bob = makeCharacter("bob", 1.0f);
    characters["alice"] = alice;
    characters["bob"] = bob;
    memories["alice"] = CharacterMemory{};
    memories["bob"] = CharacterMemory{};
    positions["alice"] = glm::vec3(0, 0, 0);
    positions["bob"] = glm::vec3(5, 0, 0); // Nearby

    // Alice has a witnessed fact (not Innate, so eligible for rumor)
    KnowledgeFact f;
    f.factId = "gossip";
    f.summary = "The merchant cheated";
    f.source = KnowledgeSource::Witnessed;
    f.confidence = 0.9f;
    f.timestamp = 1.0f;
    memories["alice"].addFact(std::move(f));

    // Spread with very high chance (large dt * high base chance)
    propagator.setRumorBaseChance(100.0f); // Guarantee spread
    propagator.propagateRumors(10.0f, 32.0f, characters, memories);

    // Bob may or may not have learned the rumor (depends on deterministic hash threshold)
    // With base=100 and dt=10, chance = 100 * 1.0 * 10 = 1000, which should exceed any threshold
    EXPECT_TRUE(memories["bob"].knowsAbout("gossip"));
}

TEST_F(KnowledgePropagatorTest, RumorsDontSpreadBetweenDistantCharacters) {
    characters["alice"] = makeCharacter("alice", 1.0f);
    characters["bob"] = makeCharacter("bob", 1.0f);
    memories["alice"] = CharacterMemory{};
    memories["bob"] = CharacterMemory{};
    positions["alice"] = glm::vec3(0, 0, 0);
    positions["bob"] = glm::vec3(500, 0, 0); // Far away

    KnowledgeFact f;
    f.factId = "distant_news";
    f.summary = "Something faraway";
    f.source = KnowledgeSource::Witnessed;
    f.confidence = 0.9f;
    f.timestamp = 1.0f;
    memories["alice"].addFact(std::move(f));

    propagator.setRumorBaseChance(100.0f);
    propagator.propagateRumors(10.0f, 32.0f, characters, memories);

    EXPECT_FALSE(memories["bob"].knowsAbout("distant_news"));
}

TEST_F(KnowledgePropagatorTest, RumorsDontSpreadInnateKnowledge) {
    characters["alice"] = makeCharacter("alice", 1.0f);
    characters["bob"] = makeCharacter("bob", 1.0f);
    memories["alice"] = CharacterMemory{};
    memories["bob"] = CharacterMemory{};
    positions["alice"] = glm::vec3(0, 0, 0);
    positions["bob"] = glm::vec3(5, 0, 0);

    memories["alice"].addInnateKnowledge("background", "I am a guard");

    propagator.setRumorBaseChance(100.0f);
    propagator.propagateRumors(10.0f, 32.0f, characters, memories);

    EXPECT_FALSE(memories["bob"].knowsAbout("background"));
}

TEST_F(KnowledgePropagatorTest, RumorsReduceConfidence) {
    characters["alice"] = makeCharacter("alice", 1.0f);
    characters["bob"] = makeCharacter("bob", 1.0f);
    memories["alice"] = CharacterMemory{};
    memories["bob"] = CharacterMemory{};
    positions["alice"] = glm::vec3(0, 0, 0);
    positions["bob"] = glm::vec3(5, 0, 0);

    KnowledgeFact f;
    f.factId = "gossip2";
    f.summary = "The baker stole bread";
    f.source = KnowledgeSource::Witnessed;
    f.confidence = 0.8f;
    f.timestamp = 1.0f;
    memories["alice"].addFact(std::move(f));

    propagator.setRumorBaseChance(100.0f);
    propagator.propagateRumors(10.0f, 32.0f, characters, memories);

    if (memories["bob"].knowsAbout("gossip2")) {
        auto* bobFact = memories["bob"].getFact("gossip2");
        ASSERT_NE(bobFact, nullptr);
        // Rumor confidence should be less than the original
        EXPECT_LT(bobFact->confidence, 0.8f);
    }
}

TEST_F(KnowledgePropagatorTest, NoPositionLookupSkipsRumors) {
    KnowledgePropagator noLookup;
    characters["alice"] = makeCharacter("alice", 1.0f);
    characters["bob"] = makeCharacter("bob", 1.0f);
    memories["alice"] = CharacterMemory{};
    memories["bob"] = CharacterMemory{};

    KnowledgeFact f;
    f.factId = "secret";
    f.summary = "Something secret";
    f.source = KnowledgeSource::Witnessed;
    f.confidence = 0.9f;
    f.timestamp = 1.0f;
    memories["alice"].addFact(std::move(f));

    // Should not crash and not spread
    noLookup.propagateRumors(10.0f, 32.0f, characters, memories);
    EXPECT_FALSE(memories["bob"].knowsAbout("secret"));
}

// ===================== Configuration =====================

TEST_F(KnowledgePropagatorTest, ConfigureRumorBaseChance) {
    propagator.setRumorBaseChance(0.5f);
    EXPECT_FLOAT_EQ(propagator.getRumorBaseChance(), 0.5f);
}
