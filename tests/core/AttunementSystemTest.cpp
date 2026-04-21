#include <gtest/gtest.h>
#include "core/AttunementSystem.h"

using namespace Phyxel::Core;

class AttunementTest : public ::testing::Test {
protected:
    AttunementSystem at;
};

// ---------------------------------------------------------------------------
// Attune
// ---------------------------------------------------------------------------

TEST_F(AttunementTest, AttuneSucceeds) {
    EXPECT_TRUE(at.attune("alice", "ring_of_protection"));
    EXPECT_TRUE(at.isAttuned("alice", "ring_of_protection"));
}

TEST_F(AttunementTest, AttuneSameItemTwiceFails) {
    at.attune("alice", "ring_of_protection");
    EXPECT_FALSE(at.attune("alice", "ring_of_protection"));
    EXPECT_EQ(at.attunedCount("alice"), 1);
}

TEST_F(AttunementTest, CanAttuneUpToMax) {
    EXPECT_TRUE(at.attune("alice", "item_a"));
    EXPECT_TRUE(at.attune("alice", "item_b"));
    EXPECT_TRUE(at.attune("alice", "item_c"));
    EXPECT_EQ(at.attunedCount("alice"), 3);
}

TEST_F(AttunementTest, AttuningFourthItemFails) {
    at.attune("alice", "item_a");
    at.attune("alice", "item_b");
    at.attune("alice", "item_c");
    EXPECT_FALSE(at.attune("alice", "item_d"));
    EXPECT_EQ(at.attunedCount("alice"), 3);
}

TEST_F(AttunementTest, DifferentEntitiesHaveSeparateSlots) {
    at.attune("alice", "item_a");
    at.attune("alice", "item_b");
    at.attune("alice", "item_c");
    // Bob still has all 3 slots
    EXPECT_TRUE(at.attune("bob", "item_a"));
    EXPECT_TRUE(at.attune("bob", "item_b"));
    EXPECT_TRUE(at.attune("bob", "item_c"));
    EXPECT_EQ(at.attunedCount("bob"), 3);
}

// ---------------------------------------------------------------------------
// canAttune
// ---------------------------------------------------------------------------

TEST_F(AttunementTest, CanAttuneWhenBelowMax) {
    EXPECT_TRUE(at.canAttune("alice"));
    at.attune("alice", "item_a");
    EXPECT_TRUE(at.canAttune("alice"));
}

TEST_F(AttunementTest, CannotAttuneAtMax) {
    at.attune("alice", "item_a");
    at.attune("alice", "item_b");
    at.attune("alice", "item_c");
    EXPECT_FALSE(at.canAttune("alice"));
}

// ---------------------------------------------------------------------------
// Unattune
// ---------------------------------------------------------------------------

TEST_F(AttunementTest, UnattuneRemovesItem) {
    at.attune("alice", "ring_of_protection");
    EXPECT_TRUE(at.unattune("alice", "ring_of_protection"));
    EXPECT_FALSE(at.isAttuned("alice", "ring_of_protection"));
}

TEST_F(AttunementTest, UnattuneNonAttuned) {
    EXPECT_FALSE(at.unattune("alice", "nonexistent"));
}

TEST_F(AttunementTest, UnattuneFreesSlot) {
    at.attune("alice", "item_a");
    at.attune("alice", "item_b");
    at.attune("alice", "item_c");
    at.unattune("alice", "item_b");
    EXPECT_TRUE(at.canAttune("alice"));
    EXPECT_TRUE(at.attune("alice", "item_d"));
}

TEST_F(AttunementTest, UnattuneAll) {
    at.attune("alice", "item_a");
    at.attune("alice", "item_b");
    at.unattuneAll("alice");
    EXPECT_EQ(at.attunedCount("alice"), 0);
    EXPECT_FALSE(at.isAttuned("alice", "item_a"));
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

TEST_F(AttunementTest, AttunedItemsList) {
    at.attune("alice", "item_x");
    at.attune("alice", "item_y");
    const auto& items = at.attunedItems("alice");
    EXPECT_EQ(items.size(), 2u);
    EXPECT_NE(std::find(items.begin(), items.end(), "item_x"), items.end());
    EXPECT_NE(std::find(items.begin(), items.end(), "item_y"), items.end());
}

TEST_F(AttunementTest, AttunedItemsEmptyForNoEntity) {
    const auto& items = at.attunedItems("nobody");
    EXPECT_TRUE(items.empty());
}

TEST_F(AttunementTest, EntitiesAttunedTo) {
    at.attune("alice", "flame_tongue");
    at.attune("bob",   "flame_tongue");

    auto entities = at.entitiesAttunedTo("flame_tongue");
    EXPECT_EQ(entities.size(), 2u);
}

TEST_F(AttunementTest, AttunedCountZeroForNewEntity) {
    EXPECT_EQ(at.attunedCount("nobody"), 0);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST_F(AttunementTest, RemoveEntityClearsAttunements) {
    at.attune("alice", "item_a");
    at.attune("alice", "item_b");
    at.removeEntity("alice");
    EXPECT_EQ(at.attunedCount("alice"), 0);
    EXPECT_FALSE(at.isAttuned("alice", "item_a"));
}

TEST_F(AttunementTest, Clear) {
    at.attune("alice", "item_a");
    at.attune("bob",   "item_b");
    at.clear();
    EXPECT_EQ(at.attunedCount("alice"), 0);
    EXPECT_EQ(at.attunedCount("bob"),   0);
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

TEST_F(AttunementTest, JsonRoundTrip) {
    at.attune("alice", "ring_of_protection");
    at.attune("alice", "cloak_of_elvenkind");
    at.attune("bob",   "flame_tongue");

    auto j = at.toJson();
    AttunementSystem at2;
    at2.fromJson(j);

    EXPECT_EQ(at2.attunedCount("alice"), 2);
    EXPECT_TRUE(at2.isAttuned("alice", "ring_of_protection"));
    EXPECT_TRUE(at2.isAttuned("alice", "cloak_of_elvenkind"));
    EXPECT_EQ(at2.attunedCount("bob"), 1);
    EXPECT_TRUE(at2.isAttuned("bob", "flame_tongue"));
}

TEST_F(AttunementTest, JsonDoesNotExceedMaxOnLoad) {
    // Manually craft JSON with 4 items for one entity
    nlohmann::json j;
    j["alice"] = {"a", "b", "c", "d"};  // 4 > MAX_ATTUNED=3

    at.fromJson(j);
    EXPECT_EQ(at.attunedCount("alice"), 3);  // clamped to max
}

TEST_F(AttunementTest, MaxAttuneConstantIsThree) {
    EXPECT_EQ(AttunementSystem::MAX_ATTUNED, 3);
}
