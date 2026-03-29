#include <gtest/gtest.h>
#include "ai/RelationshipManager.h"
#include <nlohmann/json.hpp>

using namespace Phyxel::AI;
using json = nlohmann::json;

// ============================================================================
// InteractionType string conversion
// ============================================================================

TEST(InteractionTypeTest, ToStringAndBack) {
    EXPECT_EQ(interactionTypeToString(InteractionType::Greeting), "Greeting");
    EXPECT_EQ(interactionTypeToString(InteractionType::Attack), "Attack");
    EXPECT_EQ(interactionTypeToString(InteractionType::Betray), "Betray");

    EXPECT_EQ(interactionTypeFromString("Greeting"), InteractionType::Greeting);
    EXPECT_EQ(interactionTypeFromString("Attack"), InteractionType::Attack);
    EXPECT_EQ(interactionTypeFromString("Betray"), InteractionType::Betray);
    EXPECT_EQ(interactionTypeFromString("bogus"), InteractionType::Greeting); // default
}

// ============================================================================
// RelationshipManager basics
// ============================================================================

TEST(RelationshipManagerTest, DefaultRelationshipIsNeutral) {
    RelationshipManager mgr;
    auto rel = mgr.getRelationship("alice", "bob");
    EXPECT_FLOAT_EQ(rel.trust, 0.0f);
    EXPECT_FLOAT_EQ(rel.affection, 0.0f);
    EXPECT_FLOAT_EQ(rel.respect, 0.0f);
    EXPECT_FLOAT_EQ(rel.fear, 0.0f);
    EXPECT_EQ(rel.targetCharacterId, "bob");
}

TEST(RelationshipManagerTest, SetAndGet) {
    RelationshipManager mgr;
    Phyxel::Story::Relationship rel;
    rel.trust = 0.5f;
    rel.affection = 0.3f;
    rel.respect = 0.7f;
    rel.fear = 0.1f;
    rel.label = "friend";
    mgr.setRelationship("alice", "bob", rel);

    EXPECT_TRUE(mgr.hasRelationship("alice", "bob"));
    EXPECT_FALSE(mgr.hasRelationship("bob", "alice")); // directed

    auto got = mgr.getRelationship("alice", "bob");
    EXPECT_FLOAT_EQ(got.trust, 0.5f);
    EXPECT_FLOAT_EQ(got.affection, 0.3f);
    EXPECT_EQ(got.label, "friend");
}

TEST(RelationshipManagerTest, Size) {
    RelationshipManager mgr;
    EXPECT_EQ(mgr.size(), 0u);
    Phyxel::Story::Relationship rel;
    mgr.setRelationship("a", "b", rel);
    mgr.setRelationship("b", "a", rel);
    EXPECT_EQ(mgr.size(), 2u);
}

// ============================================================================
// Interactions
// ============================================================================

TEST(RelationshipManagerTest, ApplyGreeting) {
    RelationshipManager mgr;
    mgr.applyInteraction("alice", "bob", InteractionType::Greeting);

    auto rel = mgr.getRelationship("alice", "bob");
    EXPECT_GT(rel.trust, 0.0f);
    EXPECT_GT(rel.affection, 0.0f);
}

TEST(RelationshipManagerTest, ApplyAttack) {
    RelationshipManager mgr;
    mgr.applyInteraction("villain", "hero", InteractionType::Attack);

    auto rel = mgr.getRelationship("villain", "hero");
    EXPECT_LT(rel.trust, 0.0f);
    EXPECT_LT(rel.affection, 0.0f);
    EXPECT_GT(rel.fear, 0.0f);
}

TEST(RelationshipManagerTest, ApplyHelp) {
    RelationshipManager mgr;
    mgr.applyInteraction("healer", "patient", InteractionType::Help);

    auto rel = mgr.getRelationship("healer", "patient");
    EXPECT_GT(rel.trust, 0.0f);
    EXPECT_GT(rel.respect, 0.0f);
}

TEST(RelationshipManagerTest, IntensityMultiplier) {
    RelationshipManager mgr;
    mgr.applyInteraction("a", "b", InteractionType::Gift, 2.0f);
    float highAffection = mgr.getRelationship("a", "b").affection;

    RelationshipManager mgr2;
    mgr2.applyInteraction("a", "b", InteractionType::Gift, 0.5f);
    float lowAffection = mgr2.getRelationship("a", "b").affection;

    EXPECT_GT(highAffection, lowAffection);
}

TEST(RelationshipManagerTest, ValuesClamp) {
    RelationshipManager mgr;
    // Many attacks should not go below -1
    for (int i = 0; i < 20; ++i) {
        mgr.applyInteraction("a", "b", InteractionType::Attack, 2.0f);
    }
    auto rel = mgr.getRelationship("a", "b");
    EXPECT_GE(rel.trust, -1.0f);
    EXPECT_GE(rel.affection, -1.0f);
    EXPECT_LE(rel.fear, 1.0f);
}

// ============================================================================
// Disposition
// ============================================================================

TEST(RelationshipManagerTest, Disposition) {
    RelationshipManager mgr;
    Phyxel::Story::Relationship rel;
    rel.trust = 0.5f;
    rel.affection = 0.3f;
    rel.respect = 0.2f;
    rel.fear = 0.1f;
    mgr.setRelationship("a", "b", rel);

    float d = mgr.getDisposition("a", "b");
    EXPECT_FLOAT_EQ(d, 0.5f + 0.3f + 0.2f - 0.1f);
}

// ============================================================================
// Queries
// ============================================================================

TEST(RelationshipManagerTest, GetRelationshipsFor) {
    RelationshipManager mgr;
    Phyxel::Story::Relationship rel;
    rel.trust = 0.5f;
    mgr.setRelationship("alice", "bob", rel);
    mgr.setRelationship("alice", "carol", rel);
    mgr.setRelationship("bob", "alice", rel);

    auto rels = mgr.getRelationshipsFor("alice");
    EXPECT_EQ(rels.size(), 2u);
}

TEST(RelationshipManagerTest, GetRelationshipsAbout) {
    RelationshipManager mgr;
    Phyxel::Story::Relationship rel;
    mgr.setRelationship("alice", "target", rel);
    mgr.setRelationship("bob", "target", rel);
    mgr.setRelationship("carol", "other", rel);

    auto about = mgr.getRelationshipsAbout("target");
    EXPECT_EQ(about.size(), 2u);
}

// ============================================================================
// Decay
// ============================================================================

TEST(RelationshipManagerTest, DecayTowardNeutral) {
    RelationshipManager mgr;
    Phyxel::Story::Relationship rel;
    rel.trust = 0.5f;
    rel.fear = 0.3f;
    mgr.setRelationship("a", "b", rel);

    mgr.update(10.0f); // 10 game hours
    auto r = mgr.getRelationship("a", "b");
    EXPECT_LT(r.trust, 0.5f);
    EXPECT_GT(r.trust, 0.0f);
    EXPECT_LT(r.fear, 0.3f);
}

// ============================================================================
// Faction reputation
// ============================================================================

TEST(RelationshipManagerTest, FactionReputation) {
    RelationshipManager mgr;
    std::vector<std::string> members = {"guard1", "guard2", "guard3"};
    mgr.applyFactionReputation("hero", "guards", 0.5f, members);

    // All guards should now have some trust toward hero
    for (const auto& m : members) {
        auto r = mgr.getRelationship(m, "hero");
        EXPECT_GT(r.trust, 0.0f);
    }
}

// ============================================================================
// Remove character
// ============================================================================

TEST(RelationshipManagerTest, RemoveCharacter) {
    RelationshipManager mgr;
    Phyxel::Story::Relationship rel;
    mgr.setRelationship("alice", "bob", rel);
    mgr.setRelationship("bob", "alice", rel);
    mgr.setRelationship("carol", "alice", rel);
    EXPECT_EQ(mgr.size(), 3u);

    mgr.removeCharacter("alice");
    EXPECT_EQ(mgr.size(), 0u); // all 3 involve alice
}

// ============================================================================
// Callback
// ============================================================================

TEST(RelationshipManagerTest, ChangeCallback) {
    RelationshipManager mgr;
    int callCount = 0;
    mgr.onRelationshipChanged([&](const std::string&, const std::string&,
                                   const Phyxel::Story::Relationship&, InteractionType) {
        ++callCount;
    });

    mgr.applyInteraction("a", "b", InteractionType::Greeting);
    EXPECT_EQ(callCount, 1);
    mgr.applyInteraction("b", "a", InteractionType::Gift);
    EXPECT_EQ(callCount, 2);
}

// ============================================================================
// JSON round-trip
// ============================================================================

TEST(RelationshipManagerTest, JsonRoundTrip) {
    RelationshipManager mgr;
    Phyxel::Story::Relationship rel;
    rel.trust = 0.7f;
    rel.affection = -0.3f;
    rel.label = "rival";
    mgr.setRelationship("alice", "bob", rel);
    mgr.applyInteraction("bob", "alice", InteractionType::Help);

    json j = mgr.toJson();
    RelationshipManager mgr2;
    mgr2.fromJson(j);

    EXPECT_EQ(mgr2.size(), mgr.size());
    auto r = mgr2.getRelationship("alice", "bob");
    EXPECT_FLOAT_EQ(r.trust, 0.7f);
    EXPECT_EQ(r.label, "rival");
}
