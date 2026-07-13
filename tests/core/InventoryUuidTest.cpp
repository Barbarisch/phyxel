#include <gtest/gtest.h>

#include "core/Inventory.h"
#include "core/Uuid.h"

#include <string>
#include <vector>

using namespace Phyxel::Core;

namespace {

TEST(InventoryUuidTest, UniqueItemGetsMintedUuidAndDoesNotMerge) {
    Inventory inv(9);
    ItemStack sword;
    sword.itemId = "iron_sword";
    sword.maxStack = 1;  // non-stackable → unique
    ASSERT_TRUE(sword.isUnique());

    ASSERT_TRUE(inv.addItemStack(sword));
    ASSERT_TRUE(inv.addItemStack(sword));  // a SECOND identical sword

    std::vector<std::string> uuids;
    int swords = 0;
    for (int i = 0; i < inv.size(); ++i) {
        auto s = inv.getSlot(i);
        if (s && s->itemId == "iron_sword") {
            ++swords;
            EXPECT_TRUE(Uuid::isValid(s->instanceUuid)) << "unique item has no minted uuid";
            uuids.push_back(s->instanceUuid);
        }
    }
    EXPECT_EQ(swords, 2) << "two unique swords collapsed into one slot (merged)";
    ASSERT_EQ(uuids.size(), 2u);
    EXPECT_NE(uuids[0], uuids[1]) << "two distinct unique items share a uuid";
}

TEST(InventoryUuidTest, CommoditiesStillStackAndCarryNoUuid) {
    Inventory inv(9);
    ItemStack stone;
    stone.itemId = "Stone";
    stone.count = 10;
    stone.maxStack = 64;
    EXPECT_FALSE(stone.isUnique());

    ASSERT_TRUE(inv.addItemStack(stone));
    ASSERT_TRUE(inv.addItemStack(stone));  // should merge into the same slot

    int stoneSlots = 0, total = 0;
    for (int i = 0; i < inv.size(); ++i) {
        auto s = inv.getSlot(i);
        if (s && s->itemId == "Stone") {
            ++stoneSlots;
            total += s->count;
            EXPECT_TRUE(s->instanceUuid.empty()) << "commodity got an instance uuid";
        }
    }
    EXPECT_EQ(stoneSlots, 1) << "commodity stone did not merge";
    EXPECT_EQ(total, 20);
}

TEST(InventoryUuidTest, CanMergeGateBlocksUuidCarryingStacks) {
    ItemStack a;
    a.itemId = "potion"; a.maxStack = 16; a.instanceUuid = Uuid::generate();
    ItemStack b;
    b.itemId = "potion"; b.maxStack = 16;  // no uuid
    EXPECT_FALSE(a.canMerge(b)) << "a uuid-carrying stack must never merge";
    EXPECT_FALSE(b.canMerge(a)) << "merging INTO a uuid-carrying stack must be blocked too";

    ItemStack c;
    c.itemId = "potion"; c.maxStack = 16;
    EXPECT_TRUE(b.canMerge(c)) << "two plain commodity stacks should still merge";
}

TEST(InventoryUuidTest, SerializationRoundTripAndBackfill) {
    Inventory inv(9);
    ItemStack sword;
    sword.itemId = "iron_sword"; sword.maxStack = 1;
    inv.addItemStack(sword);
    std::string uuid;
    for (int i = 0; i < inv.size(); ++i) {
        auto s = inv.getSlot(i);
        if (s) { uuid = s->instanceUuid; break; }
    }
    ASSERT_TRUE(Uuid::isValid(uuid));

    // Round-trip: the instance uuid must survive toJson/fromJson.
    Inventory inv2(9);
    inv2.fromJson(inv.toJson());
    bool found = false;
    for (int i = 0; i < inv2.size(); ++i) {
        auto s = inv2.getSlot(i);
        if (s && s->itemId == "iron_sword") { EXPECT_EQ(s->instanceUuid, uuid); found = true; }
    }
    EXPECT_TRUE(found) << "unique item lost across serialization";

    // Backfill: a legacy slot with a unique item (max_stack 1) but NO uuid gets one minted on load.
    nlohmann::json legacy = {
        {"size", 9}, {"selected_slot", 0}, {"creative", false},
        {"slots", nlohmann::json::array({
            {{"slot", 0}, {"material", "iron_sword"}, {"count", 1}, {"max_stack", 1}}
        })}
    };
    ASSERT_FALSE(legacy["slots"][0].contains("uuid"));
    Inventory inv3(9);
    inv3.fromJson(legacy);
    auto s0 = inv3.getSlot(0);
    ASSERT_TRUE(s0.has_value());
    EXPECT_TRUE(Uuid::isValid(s0->instanceUuid)) << "unique item loaded without a uuid was not backfilled";
}

}  // namespace
