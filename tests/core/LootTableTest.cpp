#include <gtest/gtest.h>
#include "core/LootTable.h"

using namespace Phyxel::Core;

// ---------------------------------------------------------------------------
// LootEntry JSON
// ---------------------------------------------------------------------------

TEST(LootEntryTest, JsonRoundTrip) {
    LootEntry e;
    e.itemId   = "dagger";
    e.weight   = 2.5f;
    e.minCount = 1;
    e.maxCount = 3;
    e.chance   = 0.75f;

    auto j = e.toJson();
    auto r = LootEntry::fromJson(j);

    EXPECT_EQ(r.itemId,   "dagger");
    EXPECT_FLOAT_EQ(r.weight,   2.5f);
    EXPECT_EQ(r.minCount, 1);
    EXPECT_EQ(r.maxCount, 3);
    EXPECT_FLOAT_EQ(r.chance, 0.75f);
}

// ---------------------------------------------------------------------------
// LootTable::roll — basic behaviour
// ---------------------------------------------------------------------------

class LootTableTest : public ::testing::Test {
protected:
    void SetUp()    override { DiceSystem::setSeed(7); }
    void TearDown() override { DiceSystem::setSeed(0); }
    DiceSystem dice;
};

TEST_F(LootTableTest, EmptyTableReturnsNoLoot) {
    LootTable t;
    t.id       = "empty";
    t.minRolls = 3;
    t.maxRolls = 3;
    // No entries — nothing to pick from
    auto results = t.roll(dice);
    EXPECT_TRUE(results.empty());
}

TEST_F(LootTableTest, SingleEntryAlwaysDrops) {
    LootTable t;
    t.id       = "single";
    t.minRolls = 1;
    t.maxRolls = 1;

    LootEntry e;
    e.itemId   = "gold";
    e.weight   = 1.0f;
    e.minCount = 5;
    e.maxCount = 5;
    e.chance   = 1.0f;  // guaranteed
    t.entries.push_back(e);

    auto results = t.roll(dice);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].itemId, "gold");
    EXPECT_EQ(results[0].count,  5);
}

TEST_F(LootTableTest, ZeroChanceNeverDrops) {
    LootTable t;
    t.id       = "zero_chance";
    t.minRolls = 10;
    t.maxRolls = 10;

    LootEntry e;
    e.itemId = "ghost_item";
    e.weight = 1.0f;
    e.chance = 0.0f;  // never drops
    t.entries.push_back(e);

    // Run 10 rolls — nothing should drop
    auto results = t.roll(dice);
    EXPECT_TRUE(results.empty());
}

TEST_F(LootTableTest, StacksSameItemAcrossRolls) {
    LootTable t;
    t.id       = "stacker";
    t.minRolls = 3;
    t.maxRolls = 3;

    LootEntry e;
    e.itemId   = "gold";
    e.weight   = 1.0f;
    e.minCount = 1;
    e.maxCount = 1;
    e.chance   = 1.0f;
    t.entries.push_back(e);

    auto results = t.roll(dice);
    // Should be merged into one entry with count 3
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].count, 3);
}

TEST_F(LootTableTest, MultipleEntriesProduceDifferentItems) {
    // Run many rolls and verify multiple distinct item types can drop
    LootTable t;
    t.id       = "varied";
    t.minRolls = 20;
    t.maxRolls = 20;

    for (const char* id : {"sword", "shield", "potion", "gold", "gem"}) {
        LootEntry e;
        e.itemId = id;
        e.weight = 1.0f;
        e.chance = 1.0f;
        t.entries.push_back(e);
    }

    auto results = t.roll(dice);
    EXPECT_GT(results.size(), 1u);  // multiple distinct drops
}

TEST_F(LootTableTest, MinRollsRespected) {
    LootTable t;
    t.minRolls = 5;
    t.maxRolls = 5;

    LootEntry e;
    e.itemId   = "coin";
    e.weight   = 1.0f;
    e.minCount = 1;
    e.maxCount = 1;
    e.chance   = 1.0f;
    t.entries.push_back(e);

    auto results = t.roll(dice);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].count, 5);
}

// ---------------------------------------------------------------------------
// LootTable JSON
// ---------------------------------------------------------------------------

TEST_F(LootTableTest, TableJsonRoundTrip) {
    LootTable original;
    original.id       = "test_table";
    original.name     = "Test Table";
    original.minRolls = 2;
    original.maxRolls = 4;

    LootEntry e1; e1.itemId = "sword"; e1.weight = 1.0f; e1.chance = 0.5f;
    LootEntry e2; e2.itemId = "gold";  e2.weight = 3.0f; e2.chance = 1.0f;
    original.entries = { e1, e2 };

    auto j = original.toJson();
    auto restored = LootTable::fromJson(j);

    EXPECT_EQ(restored.id,       "test_table");
    EXPECT_EQ(restored.name,     "Test Table");
    EXPECT_EQ(restored.minRolls, 2);
    EXPECT_EQ(restored.maxRolls, 4);
    ASSERT_EQ(restored.entries.size(), 2u);
    EXPECT_EQ(restored.entries[0].itemId, "sword");
    EXPECT_EQ(restored.entries[1].itemId, "gold");
}

// ---------------------------------------------------------------------------
// LootTableRegistry
// ---------------------------------------------------------------------------

class LootTableRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        LootTableRegistry::instance().clear();
        DiceSystem::setSeed(11);
    }
    void TearDown() override {
        LootTableRegistry::instance().clear();
        DiceSystem::setSeed(0);
    }
    DiceSystem dice;
};

TEST_F(LootTableRegistryTest, RegisterAndRetrieve) {
    LootTable t;
    t.id = "my_table";
    LootTableRegistry::instance().registerTable(t);

    EXPECT_NE(LootTableRegistry::instance().getTable("my_table"), nullptr);
    EXPECT_EQ(LootTableRegistry::instance().getTable("unknown"),  nullptr);
}

TEST_F(LootTableRegistryTest, RollByIdNotFound) {
    auto results = LootTableRegistry::instance().rollTable("missing", dice);
    EXPECT_TRUE(results.empty());
}

TEST_F(LootTableRegistryTest, RollByIdWorks) {
    LootTable t;
    t.id       = "quick";
    t.minRolls = 1;
    t.maxRolls = 1;

    LootEntry e; e.itemId = "gold"; e.weight = 1.0f; e.chance = 1.0f; e.minCount = 10; e.maxCount = 10;
    t.entries.push_back(e);
    LootTableRegistry::instance().registerTable(t);

    auto results = LootTableRegistry::instance().rollTable("quick", dice);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].itemId, "gold");
}

TEST_F(LootTableRegistryTest, LoadFromFileCommonLoot) {
    bool ok = LootTableRegistry::instance().loadFromFile("resources/loot_tables/common_loot.json");
    EXPECT_TRUE(ok);
    EXPECT_GE(LootTableRegistry::instance().count(), 3u);

    EXPECT_NE(LootTableRegistry::instance().getTable("goblin_hoard"),  nullptr);
    EXPECT_NE(LootTableRegistry::instance().getTable("bandit_stash"),  nullptr);
    EXPECT_NE(LootTableRegistry::instance().getTable("dragon_hoard"),  nullptr);

    const auto* goblin = LootTableRegistry::instance().getTable("goblin_hoard");
    EXPECT_GE(goblin->minRolls, 1);
    EXPECT_GE(goblin->entries.size(), 3u);
}

TEST_F(LootTableRegistryTest, EmptyTableRollsEmpty) {
    LootTableRegistry::instance().loadFromFile("resources/loot_tables/common_loot.json");
    auto results = LootTableRegistry::instance().rollTable("empty", dice);
    EXPECT_TRUE(results.empty());
}
