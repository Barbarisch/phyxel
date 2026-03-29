#include <gtest/gtest.h>
#include "ai/SocialConsiderations.h"

using namespace Phyxel::AI;

// ============================================================================
// NeedUrgency consideration
// ============================================================================

TEST(SocialConsiderationsTest, NeedUrgency) {
    NeedsSystem needs;
    Need hunger;
    hunger.type = NeedType::Hunger;
    hunger.value = 20.0f; // low = urgent
    needs.setNeed(hunger);

    ActionContext ctx{};
    auto scorer = SocialConsiderations::needUrgency(&needs, NeedType::Hunger);
    float score = scorer(ctx);
    EXPECT_FLOAT_EQ(score, 0.8f); // 1.0 - 20/100

    // Satisfied need
    Need rest;
    rest.type = NeedType::Rest;
    rest.value = 100.0f;
    needs.setNeed(rest);
    auto restScorer = SocialConsiderations::needUrgency(&needs, NeedType::Rest);
    EXPECT_FLOAT_EQ(restScorer(ctx), 0.0f);
}

TEST(SocialConsiderationsTest, NeedIsUrgent) {
    NeedsSystem needs;
    Need hunger;
    hunger.type = NeedType::Hunger;
    hunger.value = 10.0f;
    hunger.urgencyThreshold = 30.0f;
    needs.setNeed(hunger);

    ActionContext ctx{};
    auto scorer = SocialConsiderations::needIsUrgent(&needs, NeedType::Hunger);
    EXPECT_FLOAT_EQ(scorer(ctx), 1.0f);

    hunger.value = 50.0f;
    needs.setNeed(hunger);
    EXPECT_FLOAT_EQ(scorer(ctx), 0.0f);
}

// ============================================================================
// Disposition consideration
// ============================================================================

TEST(SocialConsiderationsTest, Disposition) {
    RelationshipManager rels;
    Phyxel::Story::Relationship rel;
    rel.trust = 1.0f;
    rel.affection = 1.0f;
    rel.respect = 1.0f;
    rel.fear = 0.0f;
    rels.setRelationship("alice", "bob", rel);

    ActionContext ctx{};
    auto scorer = SocialConsiderations::dispositionToward(&rels, "alice", "bob");
    float score = scorer(ctx);
    // Disposition = 1+1+1-0 = 3.0, mapped to (3+4)/8 = 0.875
    EXPECT_NEAR(score, 0.875f, 0.01f);
}

TEST(SocialConsiderationsTest, DispositionNeutral) {
    RelationshipManager rels;
    ActionContext ctx{};
    auto scorer = SocialConsiderations::dispositionToward(&rels, "a", "b");
    // No relationship → disposition=0 → (0+4)/8 = 0.5
    EXPECT_NEAR(scorer(ctx), 0.5f, 0.01f);
}

// ============================================================================
// WorldView considerations
// ============================================================================

TEST(SocialConsiderationsTest, HasBelief) {
    WorldView wv;
    ActionContext ctx{};

    auto scorer = SocialConsiderations::hasBelief(&wv, "test_key");
    EXPECT_FLOAT_EQ(scorer(ctx), 0.0f);

    wv.setBelief("test_key", "value");
    EXPECT_FLOAT_EQ(scorer(ctx), 1.0f);
}

TEST(SocialConsiderationsTest, SentimentToward) {
    WorldView wv;
    wv.setOpinion("guards", -0.5f);
    ActionContext ctx{};

    auto scorer = SocialConsiderations::sentimentToward(&wv, "guards");
    // sentiment=-0.5, mapped: (-0.5+1)/2 = 0.25
    EXPECT_NEAR(scorer(ctx), 0.25f, 0.01f);
}

// ============================================================================
// Null safety
// ============================================================================

TEST(SocialConsiderationsTest, NullNeedsReturnsZero) {
    ActionContext ctx{};
    auto scorer = SocialConsiderations::needUrgency(nullptr, NeedType::Hunger);
    EXPECT_FLOAT_EQ(scorer(ctx), 0.0f);
}

TEST(SocialConsiderationsTest, NullRelationshipsReturnsNeutral) {
    ActionContext ctx{};
    auto scorer = SocialConsiderations::dispositionToward(nullptr, "a", "b");
    EXPECT_FLOAT_EQ(scorer(ctx), 0.5f);
}

TEST(SocialConsiderationsTest, NullWorldViewReturnsDefault) {
    ActionContext ctx{};
    auto scorer = SocialConsiderations::hasBelief(nullptr, "key");
    EXPECT_FLOAT_EQ(scorer(ctx), 0.0f);

    auto scorer2 = SocialConsiderations::sentimentToward(nullptr, "subject");
    EXPECT_FLOAT_EQ(scorer2(ctx), 0.5f);
}
