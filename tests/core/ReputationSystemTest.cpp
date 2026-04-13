#include <gtest/gtest.h>
#include "core/ReputationSystem.h"

using namespace Phyxel::Core;

// ---------------------------------------------------------------------------
// Tier name helpers
// ---------------------------------------------------------------------------

TEST(ReputationSystemTest, TierRoundTrip) {
    const ReputationTier tiers[] = {
        ReputationTier::Hostile, ReputationTier::Unfriendly, ReputationTier::Neutral,
        ReputationTier::Friendly, ReputationTier::Honored, ReputationTier::Exalted
    };
    for (auto t : tiers) {
        const char* n = reputationTierName(t);
        EXPECT_EQ(reputationTierFromString(n), t) << "Failed for: " << n;
    }
}

TEST(ReputationSystemTest, TierFromStringCaseInsensitive) {
    EXPECT_EQ(reputationTierFromString("hostile"),    ReputationTier::Hostile);
    EXPECT_EQ(reputationTierFromString("FRIENDLY"),   ReputationTier::Friendly);
    EXPECT_EQ(reputationTierFromString("Exalted"),    ReputationTier::Exalted);
    EXPECT_EQ(reputationTierFromString("unknown"),    ReputationTier::Neutral);  // default
}

// ---------------------------------------------------------------------------
// tierForScore
// ---------------------------------------------------------------------------

TEST(ReputationSystemTest, TierForScoreHostile) {
    EXPECT_EQ(ReputationSystem::tierForScore(-1000), ReputationTier::Hostile);
    EXPECT_EQ(ReputationSystem::tierForScore(-501),  ReputationTier::Hostile);
}

TEST(ReputationSystemTest, TierForScoreUnfriendly) {
    EXPECT_EQ(ReputationSystem::tierForScore(-500), ReputationTier::Unfriendly);
    EXPECT_EQ(ReputationSystem::tierForScore(-1),   ReputationTier::Unfriendly);
}

TEST(ReputationSystemTest, TierForScoreNeutral) {
    EXPECT_EQ(ReputationSystem::tierForScore(0),   ReputationTier::Neutral);
    EXPECT_EQ(ReputationSystem::tierForScore(249), ReputationTier::Neutral);
}

TEST(ReputationSystemTest, TierForScoreFriendly) {
    EXPECT_EQ(ReputationSystem::tierForScore(250), ReputationTier::Friendly);
    EXPECT_EQ(ReputationSystem::tierForScore(499), ReputationTier::Friendly);
}

TEST(ReputationSystemTest, TierForScoreHonored) {
    EXPECT_EQ(ReputationSystem::tierForScore(500), ReputationTier::Honored);
    EXPECT_EQ(ReputationSystem::tierForScore(749), ReputationTier::Honored);
}

TEST(ReputationSystemTest, TierForScoreExalted) {
    EXPECT_EQ(ReputationSystem::tierForScore(750),  ReputationTier::Exalted);
    EXPECT_EQ(ReputationSystem::tierForScore(1000), ReputationTier::Exalted);
}

// ---------------------------------------------------------------------------
// ReputationSystem — basic get/set
// ---------------------------------------------------------------------------

class RepTest : public ::testing::Test {
protected:
    ReputationSystem rep;
};

TEST_F(RepTest, DefaultReputationIsZero) {
    EXPECT_EQ(rep.getReputation("alice", "city_guard"), 0);
}

TEST_F(RepTest, SetAndGetReputation) {
    rep.setReputation("alice", "city_guard", 300);
    EXPECT_EQ(rep.getReputation("alice", "city_guard"), 300);
}

TEST_F(RepTest, SetClampedToMax) {
    rep.setReputation("alice", "city_guard", 9999);
    EXPECT_EQ(rep.getReputation("alice", "city_guard"), REP_MAX);
}

TEST_F(RepTest, SetClampedToMin) {
    rep.setReputation("alice", "city_guard", -9999);
    EXPECT_EQ(rep.getReputation("alice", "city_guard"), REP_MIN);
}

TEST_F(RepTest, AdjustPositive) {
    rep.setReputation("alice", "city_guard", 200);
    rep.adjustReputation("alice", "city_guard", 100);
    EXPECT_EQ(rep.getReputation("alice", "city_guard"), 300);
}

TEST_F(RepTest, AdjustNegative) {
    rep.setReputation("alice", "city_guard", 200);
    rep.adjustReputation("alice", "city_guard", -300);
    EXPECT_EQ(rep.getReputation("alice", "city_guard"), -100);
}

TEST_F(RepTest, AdjustClamps) {
    rep.setReputation("alice", "city_guard", 900);
    rep.adjustReputation("alice", "city_guard", 500);
    EXPECT_EQ(rep.getReputation("alice", "city_guard"), REP_MAX);
}

// ---------------------------------------------------------------------------
// getTier
// ---------------------------------------------------------------------------

TEST_F(RepTest, GetTierDefault) {
    EXPECT_EQ(rep.getTier("alice", "city_guard"), ReputationTier::Neutral);
}

TEST_F(RepTest, GetTierAfterSet) {
    rep.setReputation("alice", "city_guard", 500);
    EXPECT_EQ(rep.getTier("alice", "city_guard"), ReputationTier::Honored);
}

// ---------------------------------------------------------------------------
// Convenience predicates
// ---------------------------------------------------------------------------

TEST_F(RepTest, IsHostile) {
    rep.setReputation("alice", "thieves", -600);
    EXPECT_TRUE(rep.isHostile("alice", "thieves"));
    EXPECT_FALSE(rep.isHostile("alice", "city_guard"));  // default Neutral
}

TEST_F(RepTest, IsFriendly) {
    rep.setReputation("alice", "city_guard", 300);
    EXPECT_TRUE(rep.isFriendly("alice", "city_guard"));
    EXPECT_FALSE(rep.isFriendly("alice", "thieves"));  // default Neutral
}

TEST_F(RepTest, IsHonored) {
    rep.setReputation("alice", "merchants", 550);
    EXPECT_TRUE(rep.isHonored("alice", "merchants"));

    rep.setReputation("bob", "merchants", 300);
    EXPECT_FALSE(rep.isHonored("bob", "merchants"));  // Friendly, not Honored
}

// ---------------------------------------------------------------------------
// Multiple entities are independent
// ---------------------------------------------------------------------------

TEST_F(RepTest, SeparateEntitiesIndependent) {
    rep.setReputation("alice", "city_guard", 300);
    rep.setReputation("bob",   "city_guard", -300);
    EXPECT_EQ(rep.getReputation("alice", "city_guard"), 300);
    EXPECT_EQ(rep.getReputation("bob",   "city_guard"), -300);
}

// ---------------------------------------------------------------------------
// removeEntity / clear
// ---------------------------------------------------------------------------

TEST_F(RepTest, RemoveEntityClearsRecords) {
    rep.setReputation("alice", "city_guard", 500);
    rep.removeEntity("alice");
    EXPECT_EQ(rep.getReputation("alice", "city_guard"), 0);
}

TEST_F(RepTest, ClearRemovesAll) {
    rep.setReputation("alice", "city_guard", 500);
    rep.setReputation("bob",   "merchants",  250);
    rep.clear();
    EXPECT_EQ(rep.getReputation("alice", "city_guard"), 0);
    EXPECT_EQ(rep.getReputation("bob",   "merchants"),  0);
}

// ---------------------------------------------------------------------------
// JSON round-trip
// ---------------------------------------------------------------------------

TEST_F(RepTest, JsonRoundTrip) {
    rep.setReputation("alice", "city_guard",  350);
    rep.setReputation("alice", "thieves",    -500);
    rep.setReputation("bob",   "merchants",   200);

    auto j = rep.toJson();
    ReputationSystem rep2;
    rep2.fromJson(j);

    EXPECT_EQ(rep2.getReputation("alice", "city_guard"), 350);
    EXPECT_EQ(rep2.getReputation("alice", "thieves"),   -500);
    EXPECT_EQ(rep2.getReputation("bob",   "merchants"),  200);
}

TEST_F(RepTest, JsonClampsOnLoad) {
    nlohmann::json j;
    j["alice"]["city_guard"] = 99999;   // > REP_MAX
    j["alice"]["thieves"]    = -99999;  // < REP_MIN
    rep.fromJson(j);
    EXPECT_EQ(rep.getReputation("alice", "city_guard"), REP_MAX);
    EXPECT_EQ(rep.getReputation("alice", "thieves"),    REP_MIN);
}

// ---------------------------------------------------------------------------
// FactionRegistry
// ---------------------------------------------------------------------------

class FactionRegistryTest : public ::testing::Test {
protected:
    void SetUp()    override { FactionRegistry::instance().clear(); }
    void TearDown() override { FactionRegistry::instance().clear(); }
};

TEST_F(FactionRegistryTest, RegisterAndRetrieve) {
    FactionDefinition def;
    def.id   = "city_guard";
    def.name = "City Guard";
    FactionRegistry::instance().registerFaction(def);

    const auto* f = FactionRegistry::instance().getFaction("city_guard");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->name, "City Guard");
    EXPECT_EQ(FactionRegistry::instance().getFaction("unknown"), nullptr);
}

TEST_F(FactionRegistryTest, GetAllFactionIds) {
    FactionDefinition a; a.id = "a";
    FactionDefinition b; b.id = "b";
    FactionRegistry::instance().registerFaction(a);
    FactionRegistry::instance().registerFaction(b);
    auto ids = FactionRegistry::instance().getAllFactionIds();
    EXPECT_EQ(ids.size(), 2u);
}

TEST_F(FactionRegistryTest, StartingReputationUsedAsDefault) {
    FactionDefinition def;
    def.id = "merchants";
    def.startingReputation = 100;
    FactionRegistry::instance().registerFaction(def);

    ReputationSystem rep;
    EXPECT_EQ(rep.getReputation("alice", "merchants"), 100);
}

TEST_F(FactionRegistryTest, LoadFromFileCommonFactions) {
    bool ok = FactionRegistry::instance().loadFromFile("resources/factions/common_factions.json");
    EXPECT_TRUE(ok);
    EXPECT_GE(FactionRegistry::instance().count(), 5u);

    const auto* guard = FactionRegistry::instance().getFaction("city_guard");
    ASSERT_NE(guard, nullptr);
    EXPECT_EQ(guard->name, "City Guard");
    EXPECT_FALSE(guard->enemyFactionIds.empty());
    EXPECT_FALSE(guard->allyFactionIds.empty());
}

TEST_F(FactionRegistryTest, FactionDefinitionJsonRoundTrip) {
    FactionDefinition def;
    def.id   = "merchants";
    def.name = "Merchants Guild";
    def.startingReputation = 50;
    def.enemyFactionIds = {"thieves"};
    def.allyFactionIds  = {"city_guard"};

    auto j = def.toJson();
    auto restored = FactionDefinition::fromJson(j);

    EXPECT_EQ(restored.id,   "merchants");
    EXPECT_EQ(restored.name, "Merchants Guild");
    EXPECT_EQ(restored.startingReputation, 50);
    EXPECT_EQ(restored.enemyFactionIds.size(), 1u);
    EXPECT_EQ(restored.enemyFactionIds[0], "thieves");
    EXPECT_EQ(restored.allyFactionIds[0],  "city_guard");
}
