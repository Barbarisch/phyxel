#include <gtest/gtest.h>
#include "core/Party.h"

using namespace Phyxel::Core;

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class PartyTest : public ::testing::Test {
protected:
    void SetUp() override {
        party.addMember("alice", "Alice",  5);
        party.addMember("bob",   "Bob",    3);
        party.addMember("carol", "Carol",  7);
    }
    Party party;
};

// ---------------------------------------------------------------------------
// Membership
// ---------------------------------------------------------------------------

TEST_F(PartyTest, SizeAfterAdd) {
    EXPECT_EQ(party.size(), 3);
}

TEST_F(PartyTest, HasMember) {
    EXPECT_TRUE(party.hasMember("alice"));
    EXPECT_FALSE(party.hasMember("dave"));
}

TEST_F(PartyTest, GetMember) {
    const auto* m = party.getMember("bob");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->name,            "Bob");
    EXPECT_EQ(m->characterLevel,   3);
    EXPECT_EQ(party.getMember("nobody"), nullptr);
}

TEST_F(PartyTest, DuplicateAddIgnored) {
    party.addMember("alice", "Alice2", 10);
    EXPECT_EQ(party.size(), 3);
    EXPECT_EQ(party.getMember("alice")->characterLevel, 5);  // unchanged
}

TEST_F(PartyTest, RemoveMember) {
    party.removeMember("bob");
    EXPECT_EQ(party.size(), 2);
    EXPECT_FALSE(party.hasMember("bob"));
}

TEST_F(PartyTest, RemoveNonMemberNoOp) {
    party.removeMember("nobody");
    EXPECT_EQ(party.size(), 3);
}

// ---------------------------------------------------------------------------
// Level queries
// ---------------------------------------------------------------------------

TEST_F(PartyTest, TotalLevel) {
    EXPECT_EQ(party.totalLevel(), 15);  // 5+3+7
}

TEST_F(PartyTest, AverageLevel) {
    EXPECT_EQ(party.averageLevel(), 5);  // 15/3 = 5
}

TEST_F(PartyTest, AverageLevelRoundsDown) {
    Party p;
    p.addMember("a", "A", 1);
    p.addMember("b", "B", 2);
    EXPECT_EQ(p.averageLevel(), 1);  // 3/2 = 1 (floor)
}

TEST_F(PartyTest, AverageLevelEmptyParty) {
    Party p;
    EXPECT_EQ(p.averageLevel(), 0);
    EXPECT_EQ(p.totalLevel(),   0);
}

// ---------------------------------------------------------------------------
// Leader
// ---------------------------------------------------------------------------

TEST_F(PartyTest, FirstMemberBecomesLeader) {
    EXPECT_EQ(party.getLeaderId(), "alice");
}

TEST_F(PartyTest, SetLeader) {
    party.setLeader("carol");
    EXPECT_EQ(party.getLeaderId(), "carol");
}

TEST_F(PartyTest, SetLeaderNonMemberIgnored) {
    party.setLeader("nobody");
    EXPECT_EQ(party.getLeaderId(), "alice");
}

TEST_F(PartyTest, RemoveLeaderPromotesNext) {
    party.removeMember("alice");
    // Leader should now be one of the remaining members
    EXPECT_TRUE(party.hasMember(party.getLeaderId()));
}

// ---------------------------------------------------------------------------
// Alive status
// ---------------------------------------------------------------------------

TEST_F(PartyTest, AllAlivByDefault) {
    EXPECT_EQ(party.aliveCount(), 3);
    EXPECT_FALSE(party.isWiped());
}

TEST_F(PartyTest, SetAliveDeadCount) {
    party.setAlive("alice", false);
    EXPECT_EQ(party.aliveCount(), 2);
    EXPECT_FALSE(party.isWiped());
}

TEST_F(PartyTest, WipeCondition) {
    party.setAlive("alice", false);
    party.setAlive("bob",   false);
    party.setAlive("carol", false);
    EXPECT_TRUE(party.isWiped());
}

TEST_F(PartyTest, EmptyPartyNotWiped) {
    Party p;
    EXPECT_FALSE(p.isWiped());
}

TEST_F(PartyTest, SetAliveUnknownEntityNoOp) {
    party.setAlive("nobody", false);  // Should not crash
    EXPECT_EQ(party.aliveCount(), 3);
}

// ---------------------------------------------------------------------------
// Clear
// ---------------------------------------------------------------------------

TEST_F(PartyTest, ClearResetsAll) {
    party.clear();
    EXPECT_EQ(party.size(), 0);
    EXPECT_EQ(party.getLeaderId(), "");
    EXPECT_EQ(party.totalLevel(), 0);
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

TEST_F(PartyTest, JsonRoundTrip) {
    party.setLeader("carol");
    party.setAlive("bob", false);

    auto j = party.toJson();
    Party p2;
    p2.fromJson(j);

    EXPECT_EQ(p2.size(),        3);
    EXPECT_EQ(p2.getLeaderId(), "carol");
    EXPECT_TRUE(p2.hasMember("alice"));
    EXPECT_TRUE(p2.hasMember("bob"));
    EXPECT_EQ(p2.getMember("alice")->characterLevel, 5);
    EXPECT_FALSE(p2.getMember("bob")->isAlive);
    EXPECT_EQ(p2.totalLevel(), 15);
}

TEST_F(PartyTest, MemberJsonRoundTrip) {
    PartyMember m { "hero", "Gandalf", 20, true };
    auto j = m.toJson();
    auto m2 = PartyMember::fromJson(j);
    EXPECT_EQ(m2.entityId,        "hero");
    EXPECT_EQ(m2.name,            "Gandalf");
    EXPECT_EQ(m2.characterLevel,  20);
    EXPECT_TRUE(m2.isAlive);
}
