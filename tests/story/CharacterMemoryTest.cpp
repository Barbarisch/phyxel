#include <gtest/gtest.h>
#include "story/CharacterMemory.h"
#include <nlohmann/json.hpp>

using namespace Phyxel::Story;

// ============================================================================
// KnowledgeSource Tests
// ============================================================================

TEST(KnowledgeSourceTest, StringRoundTrip) {
    EXPECT_EQ(knowledgeSourceToString(KnowledgeSource::Witnessed), "witnessed");
    EXPECT_EQ(knowledgeSourceToString(KnowledgeSource::ToldBy), "told_by");
    EXPECT_EQ(knowledgeSourceToString(KnowledgeSource::Rumor), "rumor");
    EXPECT_EQ(knowledgeSourceToString(KnowledgeSource::Innate), "innate");

    EXPECT_EQ(knowledgeSourceFromString("witnessed"), KnowledgeSource::Witnessed);
    EXPECT_EQ(knowledgeSourceFromString("told_by"), KnowledgeSource::ToldBy);
    EXPECT_EQ(knowledgeSourceFromString("rumor"), KnowledgeSource::Rumor);
    EXPECT_EQ(knowledgeSourceFromString("innate"), KnowledgeSource::Innate);
    EXPECT_EQ(knowledgeSourceFromString("unknown"), KnowledgeSource::Innate);
}

// ============================================================================
// KnowledgeFact Tests
// ============================================================================

TEST(KnowledgeFactTest, JsonRoundTrip) {
    KnowledgeFact f;
    f.factId = "raid_001";
    f.summary = "Bandits raided the trade caravan";
    f.source = KnowledgeSource::Witnessed;
    f.confidence = 1.0f;
    f.timestamp = 100.0f;
    f.perceivedDetails = {{"casualties", 3}};

    nlohmann::json j = f;
    KnowledgeFact f2 = j.get<KnowledgeFact>();

    EXPECT_EQ(f2.factId, "raid_001");
    EXPECT_EQ(f2.summary, "Bandits raided the trade caravan");
    EXPECT_EQ(f2.source, KnowledgeSource::Witnessed);
    EXPECT_FLOAT_EQ(f2.confidence, 1.0f);
    EXPECT_FLOAT_EQ(f2.timestamp, 100.0f);
}

// ============================================================================
// CharacterMemory Tests
// ============================================================================

class CharacterMemoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        traits.openness = 0.5f;
        traits.neuroticism = 0.5f;
        traits.agreeableness = 0.5f;
    }

    CharacterMemory memory;
    PersonalityTraits traits;
};

TEST_F(CharacterMemoryTest, EmptyByDefault) {
    EXPECT_EQ(memory.factCount(), 0u);
    EXPECT_FALSE(memory.knowsAbout("anything"));
}

TEST_F(CharacterMemoryTest, AddInnateKnowledge) {
    memory.addInnateKnowledge("history_01", "The kingdom was founded 100 years ago");

    EXPECT_TRUE(memory.knowsAbout("history_01"));
    auto* fact = memory.getFact("history_01");
    ASSERT_NE(fact, nullptr);
    EXPECT_EQ(fact->source, KnowledgeSource::Innate);
    EXPECT_FLOAT_EQ(fact->confidence, 1.0f);
}

TEST_F(CharacterMemoryTest, WitnessEvent) {
    WorldEvent event;
    event.id = "explosion_01";
    event.type = "combat";
    event.timestamp = 50.0f;
    event.details = {{"severity", 0.8f}};

    memory.witness(event, traits);

    EXPECT_TRUE(memory.knowsAbout("explosion_01"));
    auto* fact = memory.getFact("explosion_01");
    ASSERT_NE(fact, nullptr);
    EXPECT_EQ(fact->source, KnowledgeSource::Witnessed);
    EXPECT_FLOAT_EQ(fact->confidence, 1.0f);
    EXPECT_FLOAT_EQ(fact->timestamp, 50.0f);
}

TEST_F(CharacterMemoryTest, HearFromReducesConfidence) {
    KnowledgeFact original;
    original.factId = "secret_01";
    original.summary = "There is a spy in the guard";
    original.source = KnowledgeSource::Witnessed;
    original.confidence = 1.0f;
    original.timestamp = 30.0f;

    memory.hearFrom("teller_npc", original, traits);

    auto* fact = memory.getFact("secret_01");
    ASSERT_NE(fact, nullptr);
    EXPECT_EQ(fact->source, KnowledgeSource::ToldBy);
    EXPECT_EQ(fact->sourceCharacterId, "teller_npc");
    EXPECT_LT(fact->confidence, 1.0f); // Should be reduced
}

TEST_F(CharacterMemoryTest, HighAgreeabilityTrustsMore) {
    PersonalityTraits trusting;
    trusting.agreeableness = 1.0f;
    PersonalityTraits suspicious;
    suspicious.agreeableness = 0.0f;

    KnowledgeFact original;
    original.factId = "rumor_01";
    original.summary = "Rumor";
    original.confidence = 1.0f;

    CharacterMemory mem1, mem2;
    mem1.hearFrom("npc", original, trusting);
    mem2.hearFrom("npc", original, suspicious);

    EXPECT_GT(mem1.getFact("rumor_01")->confidence,
              mem2.getFact("rumor_01")->confidence);
}

TEST_F(CharacterMemoryTest, DoesNotOverwriteHigherConfidence) {
    memory.addInnateKnowledge("fact_01", "I know this for certain");

    KnowledgeFact lowConfidence;
    lowConfidence.factId = "fact_01";
    lowConfidence.summary = "Heard from someone";
    lowConfidence.confidence = 0.5f;

    memory.hearFrom("someone", lowConfidence, traits);

    // Should keep the innate version (confidence 1.0)
    EXPECT_FLOAT_EQ(memory.getFact("fact_01")->confidence, 1.0f);
    EXPECT_EQ(memory.getFact("fact_01")->source, KnowledgeSource::Innate);
}

TEST_F(CharacterMemoryTest, GetFactsAbout) {
    memory.addInnateKnowledge("bandit_raid_01", "Bandits raided the north road");
    memory.addInnateKnowledge("bandit_raid_02", "Bandits raided the south bridge");
    memory.addInnateKnowledge("trade_deal_01", "New trade agreement signed");

    auto banditFacts = memory.getFactsAbout("bandit");
    EXPECT_EQ(banditFacts.size(), 2u);

    auto tradeFacts = memory.getFactsAbout("trade");
    EXPECT_EQ(tradeFacts.size(), 1u);
}

TEST_F(CharacterMemoryTest, GetRecentFacts) {
    KnowledgeFact f1;
    f1.factId = "old";
    f1.summary = "Old news";
    f1.timestamp = 10.0f;
    memory.addFact(f1);

    KnowledgeFact f2;
    f2.factId = "new";
    f2.summary = "Recent news";
    f2.timestamp = 100.0f;
    memory.addFact(f2);

    auto recent = memory.getRecentFacts(1);
    ASSERT_EQ(recent.size(), 1u);
    EXPECT_EQ(recent[0]->factId, "new");
}

TEST_F(CharacterMemoryTest, BuildContextSummary) {
    memory.addInnateKnowledge("fact_01", "The kingdom is at war");
    memory.addInnateKnowledge("fact_02", "The merchant guild is neutral");

    std::string summary = memory.buildContextSummary();
    EXPECT_FALSE(summary.empty());
    EXPECT_NE(summary.find("kingdom is at war"), std::string::npos);
    EXPECT_NE(summary.find("merchant guild"), std::string::npos);
}

// ============================================================================
// Memory Decay Tests
// ============================================================================

class MemoryDecayTest : public ::testing::Test {
protected:
    CharacterMemory memory;
};

TEST_F(MemoryDecayTest, InnateKnowledgeDoesNotDecay) {
    memory.addInnateKnowledge("permanent", "Background knowledge");
    memory.update(1000.0f); // Long time
    EXPECT_TRUE(memory.knowsAbout("permanent"));
    EXPECT_FLOAT_EQ(memory.getFact("permanent")->confidence, 1.0f);
}

TEST_F(MemoryDecayTest, WitnessedKnowledgeDoesNotDecay) {
    KnowledgeFact f;
    f.factId = "saw_it";
    f.summary = "I saw it happen";
    f.source = KnowledgeSource::Witnessed;
    f.confidence = 1.0f;
    memory.addFact(f);

    memory.update(1000.0f);
    EXPECT_TRUE(memory.knowsAbout("saw_it"));
    EXPECT_FLOAT_EQ(memory.getFact("saw_it")->confidence, 1.0f);
}

TEST_F(MemoryDecayTest, RumorsDecayFast) {
    KnowledgeFact f;
    f.factId = "rumor_01";
    f.summary = "I heard something";
    f.source = KnowledgeSource::Rumor;
    f.confidence = 0.5f;
    memory.addFact(f);

    memory.update(10.0f);
    auto* fact = memory.getFact("rumor_01");
    ASSERT_NE(fact, nullptr);
    EXPECT_LT(fact->confidence, 0.5f);
}

TEST_F(MemoryDecayTest, ToldByDecaysSlowly) {
    KnowledgeFact f;
    f.factId = "told_01";
    f.summary = "Someone told me";
    f.source = KnowledgeSource::ToldBy;
    f.confidence = 0.5f;
    memory.addFact(f);

    memory.update(10.0f);
    auto* fact = memory.getFact("told_01");
    ASSERT_NE(fact, nullptr);
    EXPECT_LT(fact->confidence, 0.5f);
    // ToldBy should decay slower than rumor — compare manually
    // Rumor rate=0.02, ToldBy rate=0.005. After 10s: rumor loses 0.2, told loses 0.05
}

TEST_F(MemoryDecayTest, FadedKnowledgeRemoved) {
    KnowledgeFact f;
    f.factId = "fading";
    f.summary = "Will be forgotten";
    f.source = KnowledgeSource::Rumor;
    f.confidence = 0.01f; // Almost gone
    memory.addFact(f);

    memory.update(1.0f); // 0.02 * 1.0 = 0.02 decay > 0.01 remaining
    EXPECT_FALSE(memory.knowsAbout("fading"));
}

// ============================================================================
// Personality Distortion Tests
// ============================================================================

TEST(PerceptionDistortionTest, NeuroticExaggeratesThreat) {
    PersonalityTraits neurotic;
    neurotic.neuroticism = 0.9f;

    WorldEvent event;
    event.id = "scary_01";
    event.type = "combat";
    event.timestamp = 0;
    event.details = {{"threat_level", 0.5f}};

    CharacterMemory memory;
    memory.witness(event, neurotic);

    auto* fact = memory.getFact("scary_01");
    ASSERT_NE(fact, nullptr);
    float perceivedThreat = fact->perceivedDetails.value("threat_level", 0.0f);
    EXPECT_GT(perceivedThreat, 0.5f); // Should be exaggerated
}

TEST(PerceptionDistortionTest, AgreeableDownplaysSeverity) {
    PersonalityTraits agreeable;
    agreeable.agreeableness = 0.9f;

    WorldEvent event;
    event.id = "conflict_01";
    event.type = "social";
    event.timestamp = 0;
    event.details = {{"severity", 0.8f}};

    CharacterMemory memory;
    memory.witness(event, agreeable);

    auto* fact = memory.getFact("conflict_01");
    ASSERT_NE(fact, nullptr);
    float perceivedSeverity = fact->perceivedDetails.value("severity", 0.0f);
    EXPECT_LT(perceivedSeverity, 0.8f); // Should be downplayed
}

// ============================================================================
// CharacterMemory Serialization Tests
// ============================================================================

TEST(CharacterMemorySerializationTest, JsonRoundTrip) {
    CharacterMemory memory;
    memory.addInnateKnowledge("fact_01", "The sky is blue");
    memory.addInnateKnowledge("fact_02", "Water is wet");

    nlohmann::json j = memory.toJson();
    CharacterMemory memory2;
    memory2.fromJson(j);

    EXPECT_EQ(memory2.factCount(), 2u);
    EXPECT_TRUE(memory2.knowsAbout("fact_01"));
    EXPECT_TRUE(memory2.knowsAbout("fact_02"));
    EXPECT_EQ(memory2.getFact("fact_01")->summary, "The sky is blue");
}
