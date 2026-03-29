#include <gtest/gtest.h>
#include "ai/WorldView.h"
#include <nlohmann/json.hpp>

using namespace Phyxel::AI;
using json = nlohmann::json;

// ============================================================================
// Belief
// ============================================================================

TEST(BeliefTest, JsonRoundTrip) {
    Belief b;
    b.key = "king_is_alive";
    b.value = "true";
    b.confidence = 0.8f;
    b.timestamp = 12.5f;

    json j = b.toJson();
    Belief b2 = Belief::fromJson(j);
    EXPECT_EQ(b2.key, "king_is_alive");
    EXPECT_EQ(b2.value, "true");
    EXPECT_FLOAT_EQ(b2.confidence, 0.8f);
    EXPECT_FLOAT_EQ(b2.timestamp, 12.5f);
}

// ============================================================================
// Opinion
// ============================================================================

TEST(OpinionTest, JsonRoundTrip) {
    Opinion o;
    o.subject = "guards";
    o.sentiment = -0.5f;
    o.reason = "They arrested my friend";
    o.timestamp = 3.0f;

    json j = o.toJson();
    Opinion o2 = Opinion::fromJson(j);
    EXPECT_EQ(o2.subject, "guards");
    EXPECT_FLOAT_EQ(o2.sentiment, -0.5f);
    EXPECT_EQ(o2.reason, "They arrested my friend");
}

// ============================================================================
// Observation
// ============================================================================

TEST(ObservationTest, JsonRoundTrip) {
    Observation obs;
    obs.eventId = "evt_001";
    obs.description = "Saw a dragon fly over the village";
    obs.location = "village_square";
    obs.timestamp = 18.0f;
    obs.firsthand = true;

    json j = obs.toJson();
    Observation obs2 = Observation::fromJson(j);
    EXPECT_EQ(obs2.eventId, "evt_001");
    EXPECT_EQ(obs2.description, "Saw a dragon fly over the village");
    EXPECT_EQ(obs2.location, "village_square");
    EXPECT_TRUE(obs2.firsthand);
}

// ============================================================================
// WorldView — Beliefs
// ============================================================================

TEST(WorldViewTest, SetAndGetBelief) {
    WorldView wv;
    wv.setBelief("weather", "sunny", 0.9f, 10.0f);

    EXPECT_TRUE(wv.hasBelief("weather"));
    const Belief* b = wv.getBelief("weather");
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->value, "sunny");
    EXPECT_FLOAT_EQ(b->confidence, 0.9f);
}

TEST(WorldViewTest, RemoveBelief) {
    WorldView wv;
    wv.setBelief("test", "value");
    EXPECT_TRUE(wv.hasBelief("test"));
    wv.removeBelief("test");
    EXPECT_FALSE(wv.hasBelief("test"));
}

TEST(WorldViewTest, BeliefOverwrite) {
    WorldView wv;
    wv.setBelief("king_alive", "true", 0.9f);
    wv.setBelief("king_alive", "false", 0.6f); // Updated belief

    const Belief* b = wv.getBelief("king_alive");
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->value, "false");
    EXPECT_FLOAT_EQ(b->confidence, 0.6f);
}

// ============================================================================
// WorldView — Opinions
// ============================================================================

TEST(WorldViewTest, SetAndGetOpinion) {
    WorldView wv;
    wv.setOpinion("guards", 0.7f, "They protect the village");

    const Opinion* o = wv.getOpinion("guards");
    ASSERT_NE(o, nullptr);
    EXPECT_FLOAT_EQ(o->sentiment, 0.7f);
    EXPECT_EQ(o->reason, "They protect the village");
}

TEST(WorldViewTest, GetSentiment) {
    WorldView wv;
    EXPECT_FLOAT_EQ(wv.getSentiment("unknown"), 0.0f); // no opinion

    wv.setOpinion("friend", 0.8f);
    EXPECT_FLOAT_EQ(wv.getSentiment("friend"), 0.8f);
}

TEST(WorldViewTest, RemoveOpinion) {
    WorldView wv;
    wv.setOpinion("test", 0.5f);
    wv.removeOpinion("test");
    EXPECT_EQ(wv.getOpinion("test"), nullptr);
}

// ============================================================================
// WorldView — Observations
// ============================================================================

TEST(WorldViewTest, AddObservations) {
    WorldView wv;
    Observation obs1;
    obs1.eventId = "e1";
    obs1.description = "Saw a fight";
    obs1.location = "tavern";
    obs1.firsthand = true;

    Observation obs2;
    obs2.eventId = "e2";
    obs2.description = "Heard about a robbery";
    obs2.location = "market";
    obs2.firsthand = false;

    wv.addObservation(obs1);
    wv.addObservation(obs2);

    EXPECT_EQ(wv.getObservations().size(), 2u);
}

TEST(WorldViewTest, GetObservationsAtLocation) {
    WorldView wv;
    Observation obs1;
    obs1.location = "tavern";
    Observation obs2;
    obs2.location = "market";
    Observation obs3;
    obs3.location = "tavern";

    wv.addObservation(obs1);
    wv.addObservation(obs2);
    wv.addObservation(obs3);

    auto atTavern = wv.getObservationsAt("tavern");
    EXPECT_EQ(atTavern.size(), 2u);
}

TEST(WorldViewTest, GetRecentObservations) {
    WorldView wv;
    for (int i = 0; i < 10; ++i) {
        Observation obs;
        obs.eventId = "e_" + std::to_string(i);
        wv.addObservation(obs);
    }

    auto recent = wv.getRecentObservations(3);
    EXPECT_EQ(recent.size(), 3u);
    EXPECT_EQ(recent[0]->eventId, "e_7"); // Most recent 3: 7, 8, 9
}

// ============================================================================
// WorldView — Update / Decay
// ============================================================================

TEST(WorldViewTest, BeliefDecay) {
    WorldView wv;
    wv.setBelief("rumor", "yes", 0.1f); // Low confidence

    wv.update(20.0f); // Enough hours to remove it
    EXPECT_FALSE(wv.hasBelief("rumor"));
}

TEST(WorldViewTest, HighConfidenceBeliefSurvives) {
    WorldView wv;
    wv.setBelief("fact", "true", 1.0f);

    wv.update(5.0f); // Some hours
    EXPECT_TRUE(wv.hasBelief("fact"));
    EXPECT_GT(wv.getBelief("fact")->confidence, 0.0f);
}

// ============================================================================
// WorldView — Context Summary
// ============================================================================

TEST(WorldViewTest, BuildContextSummary) {
    WorldView wv;
    wv.setBelief("weather", "rainy", 0.9f);
    wv.setOpinion("blacksmith", 0.6f, "Good prices");
    Observation obs;
    obs.description = "Dragon sighting";
    obs.location = "mountain";
    obs.firsthand = true;
    wv.addObservation(obs);

    std::string summary = wv.buildContextSummary();
    EXPECT_FALSE(summary.empty());
    EXPECT_NE(summary.find("weather"), std::string::npos);
    EXPECT_NE(summary.find("blacksmith"), std::string::npos);
    EXPECT_NE(summary.find("Dragon"), std::string::npos);
}

// ============================================================================
// WorldView — Size / Serialization
// ============================================================================

TEST(WorldViewTest, Size) {
    WorldView wv;
    EXPECT_EQ(wv.size(), 0u);
    wv.setBelief("a", "b");
    wv.setOpinion("c", 0.5f);
    Observation obs;
    wv.addObservation(obs);
    EXPECT_EQ(wv.size(), 3u);
}

TEST(WorldViewTest, JsonRoundTrip) {
    WorldView wv;
    wv.setBelief("k1", "v1", 0.8f, 5.0f);
    wv.setBelief("k2", "v2", 0.5f, 10.0f);
    wv.setOpinion("subject1", 0.7f, "reason1", 3.0f);
    Observation obs;
    obs.eventId = "e1";
    obs.description = "Test obs";
    obs.location = "loc1";
    obs.timestamp = 8.0f;
    obs.firsthand = false;
    wv.addObservation(obs);

    json j = wv.toJson();
    WorldView wv2;
    wv2.fromJson(j);

    EXPECT_EQ(wv2.size(), wv.size());
    EXPECT_TRUE(wv2.hasBelief("k1"));
    EXPECT_TRUE(wv2.hasBelief("k2"));
    EXPECT_NE(wv2.getOpinion("subject1"), nullptr);
    EXPECT_EQ(wv2.getObservations().size(), 1u);
    EXPECT_FALSE(wv2.getObservations()[0].firsthand);
}
