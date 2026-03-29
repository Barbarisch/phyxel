#include <gtest/gtest.h>
#include "ai/SocialInteraction.h"
#include <glm/glm.hpp>

using namespace Phyxel::AI;

// ============================================================================
// ChooseInteraction
// ============================================================================

TEST(SocialInteractionTest, ChooseInteraction_Hostile) {
    auto type = SocialInteractionSystem::chooseInteraction(-2.0f, 0.5f);
    EXPECT_EQ(type, InteractionType::Insult);
}

TEST(SocialInteractionTest, ChooseInteraction_Neutral) {
    auto type = SocialInteractionSystem::chooseInteraction(0.0f, 0.3f);
    EXPECT_EQ(type, InteractionType::Greeting);
}

TEST(SocialInteractionTest, ChooseInteraction_NeutralHighNeed) {
    auto type = SocialInteractionSystem::chooseInteraction(0.0f, 0.8f);
    EXPECT_EQ(type, InteractionType::Conversation);
}

TEST(SocialInteractionTest, ChooseInteraction_Friendly) {
    auto type = SocialInteractionSystem::chooseInteraction(1.0f, 0.3f);
    EXPECT_EQ(type, InteractionType::Conversation);
}

TEST(SocialInteractionTest, ChooseInteraction_VeryFriendly) {
    auto type = SocialInteractionSystem::chooseInteraction(2.0f, 0.5f);
    EXPECT_EQ(type, InteractionType::Gossip);
}

// ============================================================================
// Proximity-based interaction
// ============================================================================

TEST(SocialInteractionTest, NoPairsWhenFarApart) {
    SocialInteractionSystem sys;
    RelationshipManager rels;

    NeedsSystem needs1;
    needs1.initDefaults();
    NeedsSystem needs2;
    needs2.initDefaults();

    std::vector<SocialParticipant> participants = {
        {"alice", glm::vec3(0, 0, 0), "Socialize", &needs1, nullptr},
        {"bob", glm::vec3(100, 0, 0), "Socialize", &needs2, nullptr},
    };

    auto results = sys.update(0.1f, participants, rels);
    EXPECT_TRUE(results.empty());
}

TEST(SocialInteractionTest, InteractionWhenClose) {
    SocialInteractionSystem sys;
    RelationshipManager rels;

    NeedsSystem needs1;
    needs1.initDefaults();
    NeedsSystem needs2;
    needs2.initDefaults();

    std::vector<SocialParticipant> participants = {
        {"alice", glm::vec3(0, 0, 0), "Socialize", &needs1, nullptr},
        {"bob", glm::vec3(2, 0, 0), "Socialize", &needs2, nullptr},
    };

    auto results = sys.update(0.1f, participants, rels);
    EXPECT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].initiatorId, "alice");
    EXPECT_EQ(results[0].targetId, "bob");
}

TEST(SocialInteractionTest, NeedsFulfilledAfterInteraction) {
    SocialInteractionSystem sys;
    RelationshipManager rels;

    NeedsSystem needs1;
    needs1.initDefaults();
    // Set social need low
    Need social;
    social.type = NeedType::Social;
    social.value = 20.0f;
    needs1.setNeed(social);

    NeedsSystem needs2;
    needs2.initDefaults();

    std::vector<SocialParticipant> participants = {
        {"alice", glm::vec3(0, 0, 0), "Socialize", &needs1, nullptr},
        {"bob", glm::vec3(1, 0, 0), "Socialize", &needs2, nullptr},
    };

    sys.update(0.1f, participants, rels);

    // Social need should be higher now
    EXPECT_GT(needs1.getNeed(NeedType::Social)->value, 20.0f);
}

TEST(SocialInteractionTest, RequiresAtLeastOneSocialActivity) {
    SocialInteractionSystem sys;
    RelationshipManager rels;

    NeedsSystem needs1;
    needs1.initDefaults();
    NeedsSystem needs2;
    needs2.initDefaults();

    // Both working — no social interaction
    std::vector<SocialParticipant> participants = {
        {"alice", glm::vec3(0, 0, 0), "Work", &needs1, nullptr},
        {"bob", glm::vec3(1, 0, 0), "Work", &needs2, nullptr},
    };

    auto results = sys.update(0.1f, participants, rels);
    EXPECT_TRUE(results.empty());
}

// ============================================================================
// Cooldown
// ============================================================================

TEST(SocialInteractionTest, CooldownPreventsRepeatedInteraction) {
    SocialInteractionSystem sys;
    sys.setCooldownHours(1.0f);
    RelationshipManager rels;

    NeedsSystem needs1;
    needs1.initDefaults();
    NeedsSystem needs2;
    needs2.initDefaults();

    std::vector<SocialParticipant> participants = {
        {"alice", glm::vec3(0, 0, 0), "Socialize", &needs1, nullptr},
        {"bob", glm::vec3(1, 0, 0), "Socialize", &needs2, nullptr},
    };

    auto r1 = sys.update(0.1f, participants, rels);
    EXPECT_EQ(r1.size(), 1u);

    // Second interaction immediately — should be on cooldown
    auto r2 = sys.update(0.1f, participants, rels);
    EXPECT_TRUE(r2.empty());

    // After cooldown expires
    auto r3 = sys.update(1.1f, participants, rels);
    EXPECT_EQ(r3.size(), 1u);
}

TEST(SocialInteractionTest, ClearCooldowns) {
    SocialInteractionSystem sys;
    sys.setCooldownHours(100.0f); // Very long cooldown
    RelationshipManager rels;

    NeedsSystem needs1;
    needs1.initDefaults();
    NeedsSystem needs2;
    needs2.initDefaults();

    std::vector<SocialParticipant> participants = {
        {"alice", glm::vec3(0, 0, 0), "Socialize", &needs1, nullptr},
        {"bob", glm::vec3(1, 0, 0), "Socialize", &needs2, nullptr},
    };

    sys.update(0.1f, participants, rels);
    sys.clearCooldowns();

    auto r2 = sys.update(0.1f, participants, rels);
    EXPECT_EQ(r2.size(), 1u); // Cooldown was cleared
}

// ============================================================================
// Gossip sharing
// ============================================================================

TEST(SocialInteractionTest, GossipSharing) {
    WorldView sharer;
    sharer.setBelief("secret", "treasure_location", 0.9f);

    WorldView listener;
    EXPECT_FALSE(listener.hasBelief("secret"));

    SocialInteractionSystem::shareGossip(sharer, listener, 5.0f);
    EXPECT_TRUE(listener.hasBelief("secret"));
    // Secondhand belief has reduced confidence
    EXPECT_LT(listener.getBelief("secret")->confidence, 0.9f);
}

TEST(SocialInteractionTest, GossipDoesNotDuplicate) {
    WorldView sharer;
    sharer.setBelief("known_fact", "yes", 0.9f);

    WorldView listener;
    listener.setBelief("known_fact", "yes", 0.8f);

    // Listener already knows — gossip should share observation instead
    Observation obs;
    obs.eventId = "e1";
    obs.description = "Saw something";
    obs.firsthand = true;
    sharer.addObservation(obs);

    size_t before = listener.getObservations().size();
    SocialInteractionSystem::shareGossip(sharer, listener, 5.0f);
    EXPECT_EQ(listener.getObservations().size(), before + 1);
}

// ============================================================================
// Interaction with WorldView observations
// ============================================================================

TEST(SocialInteractionTest, InteractionCreatesObservations) {
    SocialInteractionSystem sys;
    RelationshipManager rels;

    NeedsSystem needs1;
    needs1.initDefaults();
    NeedsSystem needs2;
    needs2.initDefaults();
    WorldView wv1, wv2;

    std::vector<SocialParticipant> participants = {
        {"alice", glm::vec3(0, 0, 0), "Socialize", &needs1, &wv1},
        {"bob", glm::vec3(1, 0, 0), "Socialize", &needs2, &wv2},
    };

    sys.update(0.1f, participants, rels);

    // Both should have an observation about the interaction
    EXPECT_GE(wv1.getObservations().size(), 1u);
    EXPECT_GE(wv2.getObservations().size(), 1u);
}
