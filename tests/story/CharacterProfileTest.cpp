#include <gtest/gtest.h>
#include "story/CharacterProfile.h"
#include <nlohmann/json.hpp>

using namespace Phyxel::Story;

// ============================================================================
// AgencyLevel Tests
// ============================================================================

TEST(AgencyLevelTest, StringRoundTrip) {
    EXPECT_EQ(agencyLevelToString(AgencyLevel::Scripted), "scripted");
    EXPECT_EQ(agencyLevelToString(AgencyLevel::Templated), "templated");
    EXPECT_EQ(agencyLevelToString(AgencyLevel::Guided), "guided");
    EXPECT_EQ(agencyLevelToString(AgencyLevel::Autonomous), "autonomous");

    EXPECT_EQ(agencyLevelFromString("scripted"), AgencyLevel::Scripted);
    EXPECT_EQ(agencyLevelFromString("templated"), AgencyLevel::Templated);
    EXPECT_EQ(agencyLevelFromString("guided"), AgencyLevel::Guided);
    EXPECT_EQ(agencyLevelFromString("autonomous"), AgencyLevel::Autonomous);
    EXPECT_EQ(agencyLevelFromString("unknown"), AgencyLevel::Scripted);
}

// ============================================================================
// PersonalityTraits Tests
// ============================================================================

class PersonalityTraitsTest : public ::testing::Test {};

TEST_F(PersonalityTraitsTest, DefaultsAreMiddle) {
    PersonalityTraits t;
    EXPECT_FLOAT_EQ(t.openness, 0.5f);
    EXPECT_FLOAT_EQ(t.conscientiousness, 0.5f);
    EXPECT_FLOAT_EQ(t.extraversion, 0.5f);
    EXPECT_FLOAT_EQ(t.agreeableness, 0.5f);
    EXPECT_FLOAT_EQ(t.neuroticism, 0.5f);
}

TEST_F(PersonalityTraitsTest, GetBigFiveTrait) {
    PersonalityTraits t;
    t.openness = 0.8f;
    EXPECT_FLOAT_EQ(t.getTrait("openness"), 0.8f);
}

TEST_F(PersonalityTraitsTest, GetCustomTrait) {
    PersonalityTraits t;
    t.customTraits["bravery"] = 0.9f;
    EXPECT_FLOAT_EQ(t.getTrait("bravery"), 0.9f);
}

TEST_F(PersonalityTraitsTest, UnknownTraitReturnsDefault) {
    PersonalityTraits t;
    EXPECT_FLOAT_EQ(t.getTrait("nonsense"), 0.5f);
}

TEST_F(PersonalityTraitsTest, JsonRoundTrip) {
    PersonalityTraits t;
    t.openness = 0.3f;
    t.extraversion = 0.9f;
    t.customTraits["greed"] = 0.7f;

    nlohmann::json j = t;
    PersonalityTraits t2 = j.get<PersonalityTraits>();

    EXPECT_FLOAT_EQ(t2.openness, 0.3f);
    EXPECT_FLOAT_EQ(t2.extraversion, 0.9f);
    EXPECT_FLOAT_EQ(t2.customTraits["greed"], 0.7f);
}

// ============================================================================
// EmotionalState Tests
// ============================================================================

class EmotionalStateTest : public ::testing::Test {};

TEST_F(EmotionalStateTest, DominantEmotionNeutralByDefault) {
    EmotionalState e;
    EXPECT_EQ(e.dominantEmotion(), "neutral");
}

TEST_F(EmotionalStateTest, DominantEmotionAngry) {
    EmotionalState e;
    e.anger = 0.8f;
    EXPECT_EQ(e.dominantEmotion(), "angry");
}

TEST_F(EmotionalStateTest, DominantEmotionAfraid) {
    EmotionalState e;
    e.fear = 0.6f;
    EXPECT_EQ(e.dominantEmotion(), "afraid");
}

TEST_F(EmotionalStateTest, DominantEmotionJoyful) {
    EmotionalState e;
    e.joy = 0.9f;
    EXPECT_EQ(e.dominantEmotion(), "joyful");
}

TEST_F(EmotionalStateTest, DominantEmotionGrieving) {
    EmotionalState e;
    e.joy = -0.5f;
    EXPECT_EQ(e.dominantEmotion(), "grieving");
}

TEST_F(EmotionalStateTest, DecayReducesEmotions) {
    PersonalityTraits traits;
    traits.neuroticism = 0.0f; // Fast decay

    EmotionalState e;
    e.anger = 0.5f;
    e.fear = 0.3f;

    e.decay(1.0f, traits);

    EXPECT_LT(e.anger, 0.5f);
    EXPECT_LT(e.fear, 0.3f);
}

TEST_F(EmotionalStateTest, HighNeuroticismSlowsDecay) {
    PersonalityTraits chill;
    chill.neuroticism = 0.0f;
    PersonalityTraits anxious;
    anxious.neuroticism = 1.0f;

    EmotionalState e1;
    e1.anger = 0.8f;
    EmotionalState e2;
    e2.anger = 0.8f;

    e1.decay(1.0f, chill);
    e2.decay(1.0f, anxious);

    EXPECT_GT(e2.anger, e1.anger); // Anxious person still angrier
}

TEST_F(EmotionalStateTest, JsonRoundTrip) {
    EmotionalState e;
    e.joy = 0.5f;
    e.anger = 0.3f;
    e.fear = 0.1f;

    nlohmann::json j = e;
    EmotionalState e2 = j.get<EmotionalState>();

    EXPECT_FLOAT_EQ(e2.joy, 0.5f);
    EXPECT_FLOAT_EQ(e2.anger, 0.3f);
    EXPECT_FLOAT_EQ(e2.fear, 0.1f);
}

// ============================================================================
// CharacterGoal Tests
// ============================================================================

TEST(CharacterGoalTest, JsonRoundTrip) {
    CharacterGoal g;
    g.id = "protect_town";
    g.description = "Keep the town safe from bandits";
    g.priority = 0.9f;
    g.isActive = true;
    g.completionCondition = "bandits_defeated";

    nlohmann::json j = g;
    CharacterGoal g2 = j.get<CharacterGoal>();

    EXPECT_EQ(g2.id, "protect_town");
    EXPECT_EQ(g2.description, "Keep the town safe from bandits");
    EXPECT_FLOAT_EQ(g2.priority, 0.9f);
    EXPECT_TRUE(g2.isActive);
    EXPECT_EQ(g2.completionCondition, "bandits_defeated");
}

// ============================================================================
// Relationship Tests
// ============================================================================

TEST(RelationshipTest, JsonRoundTrip) {
    Relationship r;
    r.targetCharacterId = "merchant_sofia";
    r.trust = 0.6f;
    r.affection = 0.3f;
    r.respect = 0.5f;
    r.fear = 0.0f;
    r.label = "friend";

    nlohmann::json j = r;
    Relationship r2 = j.get<Relationship>();

    EXPECT_EQ(r2.targetCharacterId, "merchant_sofia");
    EXPECT_FLOAT_EQ(r2.trust, 0.6f);
    EXPECT_FLOAT_EQ(r2.affection, 0.3f);
    EXPECT_EQ(r2.label, "friend");
}

// ============================================================================
// CharacterProfile Tests
// ============================================================================

class CharacterProfileTest : public ::testing::Test {
protected:
    void SetUp() override {
        profile.id = "captain_elena";
        profile.name = "Captain Elena";
        profile.description = "Stern guard captain";
        profile.factionId = "kingdom";
        profile.traits.conscientiousness = 0.9f;
        profile.traits.agreeableness = 0.4f;
        profile.traits.customTraits["bravery"] = 0.9f;
        profile.agencyLevel = AgencyLevel::Guided;
        profile.defaultBehavior = "patrol";
        profile.roles = {"questgiver", "authority"};

        CharacterGoal g;
        g.id = "protect_town";
        g.description = "Keep the town safe";
        g.priority = 0.9f;
        profile.goals.push_back(std::move(g));

        Relationship r;
        r.targetCharacterId = "merchant_sofia";
        r.trust = 0.6f;
        r.label = "friend";
        profile.relationships.push_back(std::move(r));
    }

    CharacterProfile profile;
};

TEST_F(CharacterProfileTest, GetRelationship) {
    auto* r = profile.getRelationship("merchant_sofia");
    ASSERT_NE(r, nullptr);
    EXPECT_FLOAT_EQ(r->trust, 0.6f);
}

TEST_F(CharacterProfileTest, UnknownRelationshipReturnsNull) {
    EXPECT_EQ(profile.getRelationship("stranger"), nullptr);
}

TEST_F(CharacterProfileTest, GetGoal) {
    auto* g = profile.getGoal("protect_town");
    ASSERT_NE(g, nullptr);
    EXPECT_FLOAT_EQ(g->priority, 0.9f);
}

TEST_F(CharacterProfileTest, UnknownGoalReturnsNull) {
    EXPECT_EQ(profile.getGoal("nonsense"), nullptr);
}

TEST_F(CharacterProfileTest, MutableGoalAccess) {
    auto* g = profile.getGoalMut("protect_town");
    ASSERT_NE(g, nullptr);
    g->priority = 0.1f;
    EXPECT_FLOAT_EQ(profile.getGoal("protect_town")->priority, 0.1f);
}

TEST_F(CharacterProfileTest, FullJsonRoundTrip) {
    nlohmann::json j = profile;
    CharacterProfile p2 = j.get<CharacterProfile>();

    EXPECT_EQ(p2.id, "captain_elena");
    EXPECT_EQ(p2.name, "Captain Elena");
    EXPECT_EQ(p2.factionId, "kingdom");
    EXPECT_FLOAT_EQ(p2.traits.conscientiousness, 0.9f);
    EXPECT_FLOAT_EQ(p2.traits.customTraits["bravery"], 0.9f);
    EXPECT_EQ(p2.agencyLevel, AgencyLevel::Guided);
    EXPECT_EQ(p2.goals.size(), 1u);
    EXPECT_EQ(p2.relationships.size(), 1u);
    EXPECT_EQ(p2.roles.size(), 2u);
}
