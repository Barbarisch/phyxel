#include <gtest/gtest.h>
#include "core/Inventory.h"

using namespace Phyxel::Core;

// The stacking tests below use bare material ids ("Stone", "Wood"). Since 2026-09-11
// stacking is per-item DATA (no Minecraft 64 default), so the materials the tests rely
// on are registered here the way items.json authors them: stackable, 64 per slot.
#include "core/ItemRegistry.h"
static const bool kInventoryTestMaterials = [] {
    for (const char* id : {"Stone", "Wood", "Dirt", "Grass"}) {
        ItemDefinition d; d.id = id; d.name = id; d.type = ItemType::Material; d.stackable = true; d.maxStack = 64;
        ItemRegistry::instance().registerItem(d);
    }
    return true;
}();

// ============================================================================
// Construction
// ============================================================================

TEST(InventoryTest, DefaultConstruction) {
    Inventory inv;
    EXPECT_EQ(inv.size(), 36);
    EXPECT_EQ(inv.getSelectedSlot(), 0);
    EXPECT_TRUE(inv.isCreativeMode());
}

TEST(InventoryTest, CustomSize) {
    Inventory inv(10);
    EXPECT_EQ(inv.size(), 10);
}

// ============================================================================
// Adding items
// ============================================================================

TEST(InventoryTest, AddSingleItem) {
    Inventory inv;
    int overflow = inv.addItem("Stone", 1);
    EXPECT_EQ(overflow, 0);
    EXPECT_EQ(inv.countItem("Stone"), 1);
}

TEST(InventoryTest, AddItemsToExistingStack) {
    Inventory inv;
    inv.addItem("Wood", 10);
    inv.addItem("Wood", 5);
    EXPECT_EQ(inv.countItem("Wood"), 15);
    // Should be in same slot
    auto slot = inv.getSlot(0);
    ASSERT_TRUE(slot.has_value());
    EXPECT_EQ(slot->count, 15);
}

TEST(InventoryTest, AddItemOverflow) {
    Inventory inv(1); // Only 1 slot
    int overflow = inv.addItem("Stone", 100);
    EXPECT_EQ(overflow, 36); // 100 - 64 max stack
    EXPECT_EQ(inv.countItem("Stone"), 64);
}

TEST(InventoryTest, AddDifferentMaterials) {
    Inventory inv;
    inv.addItem("Stone", 10);
    inv.addItem("Wood", 5);
    EXPECT_EQ(inv.countItem("Stone"), 10);
    EXPECT_EQ(inv.countItem("Wood"), 5);
}

TEST(InventoryTest, AddItemFillsMultipleSlots) {
    Inventory inv(3);
    inv.addItem("Stone", 150); // Needs 3 slots (64+64+22)
    EXPECT_EQ(inv.countItem("Stone"), 150);
    EXPECT_TRUE(inv.getSlot(0).has_value());
    EXPECT_TRUE(inv.getSlot(1).has_value());
    EXPECT_TRUE(inv.getSlot(2).has_value());
}

// ============================================================================
// Removing items
// ============================================================================

TEST(InventoryTest, RemoveItems) {
    Inventory inv;
    inv.addItem("Stone", 20);
    int removed = inv.removeItem("Stone", 5);
    EXPECT_EQ(removed, 5);
    EXPECT_EQ(inv.countItem("Stone"), 15);
}

TEST(InventoryTest, RemoveMoreThanAvailable) {
    Inventory inv;
    inv.addItem("Stone", 5);
    int removed = inv.removeItem("Stone", 10);
    EXPECT_EQ(removed, 5);
    EXPECT_EQ(inv.countItem("Stone"), 0);
}

TEST(InventoryTest, RemoveNonexistentItem) {
    Inventory inv;
    int removed = inv.removeItem("Stone", 5);
    EXPECT_EQ(removed, 0);
}

TEST(InventoryTest, RemoveClearsEmptySlot) {
    Inventory inv;
    inv.addItem("Stone", 5);
    inv.removeItem("Stone", 5);
    EXPECT_FALSE(inv.getSlot(0).has_value());
}

// ============================================================================
// Has / Count
// ============================================================================

TEST(InventoryTest, HasItem) {
    Inventory inv;
    inv.addItem("Wood", 10);
    EXPECT_TRUE(inv.hasItem("Wood", 5));
    EXPECT_TRUE(inv.hasItem("Wood", 10));
    EXPECT_FALSE(inv.hasItem("Wood", 11));
    EXPECT_FALSE(inv.hasItem("Stone"));
}

// ============================================================================
// Hotbar selection
// ============================================================================

TEST(InventoryTest, SelectSlot) {
    Inventory inv;
    EXPECT_TRUE(inv.setSelectedSlot(5));
    EXPECT_EQ(inv.getSelectedSlot(), 5);
    EXPECT_FALSE(inv.setSelectedSlot(-1));
    EXPECT_FALSE(inv.setSelectedSlot(9));
    EXPECT_EQ(inv.getSelectedSlot(), 5); // Unchanged on invalid
}

TEST(InventoryTest, SelectedMaterial) {
    Inventory inv;
    inv.addItem("Stone", 10);
    EXPECT_EQ(inv.getSelectedMaterial(), "Stone"); // Slot 0
    inv.setSelectedSlot(1);
    EXPECT_EQ(inv.getSelectedMaterial(), ""); // Slot 1 empty
}

TEST(InventoryTest, ConsumeSelectedCreativeMode) {
    Inventory inv;
    inv.setCreativeMode(true);
    inv.addItem("Stone", 5);
    EXPECT_TRUE(inv.consumeSelected());
    EXPECT_EQ(inv.countItem("Stone"), 5); // Not consumed in creative
}

TEST(InventoryTest, ConsumeSelectedSurvivalMode) {
    Inventory inv;
    inv.setCreativeMode(false);
    inv.addItem("Stone", 3);
    EXPECT_TRUE(inv.consumeSelected());
    EXPECT_EQ(inv.countItem("Stone"), 2);
    EXPECT_TRUE(inv.consumeSelected());
    EXPECT_TRUE(inv.consumeSelected());
    EXPECT_FALSE(inv.consumeSelected()); // Empty now
    EXPECT_EQ(inv.countItem("Stone"), 0);
}

TEST(InventoryTest, ConsumeEmptySlotFails) {
    Inventory inv;
    inv.setCreativeMode(false);
    EXPECT_FALSE(inv.consumeSelected());
}

// ============================================================================
// Slot operations
// ============================================================================

TEST(InventoryTest, SetSlot) {
    Inventory inv;
    ItemStack stack{"Metal", 32, 64};
    EXPECT_TRUE(inv.setSlot(0, stack));
    auto s = inv.getSlot(0);
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->itemId, "Metal");
    EXPECT_EQ(s->count, 32);
}

TEST(InventoryTest, ClearSlot) {
    Inventory inv;
    inv.addItem("Wood", 10);
    inv.clearSlot(0);
    EXPECT_FALSE(inv.getSlot(0).has_value());
    EXPECT_EQ(inv.countItem("Wood"), 0);
}

TEST(InventoryTest, ClearAll) {
    Inventory inv;
    inv.addItem("Stone", 10);
    inv.addItem("Wood", 5);
    inv.clear();
    EXPECT_EQ(inv.countItem("Stone"), 0);
    EXPECT_EQ(inv.countItem("Wood"), 0);
}

TEST(InventoryTest, OutOfBoundsSlotAccess) {
    Inventory inv(5);
    EXPECT_FALSE(inv.getSlot(-1).has_value());
    EXPECT_FALSE(inv.getSlot(5).has_value());
    EXPECT_FALSE(inv.setSlot(-1, ItemStack{"Stone", 1}));
    EXPECT_FALSE(inv.setSlot(5, ItemStack{"Stone", 1}));
}

// ============================================================================
// Serialization
// ============================================================================

TEST(InventoryTest, ToJsonAndBack) {
    Inventory inv;
    inv.addItem("Stone", 30);
    inv.addItem("Wood", 15);
    inv.setSelectedSlot(3);
    inv.setCreativeMode(false);

    auto j = inv.toJson();

    Inventory inv2;
    inv2.fromJson(j);
    EXPECT_EQ(inv2.countItem("Stone"), 30);
    EXPECT_EQ(inv2.countItem("Wood"), 15);
    EXPECT_EQ(inv2.getSelectedSlot(), 3);
    EXPECT_FALSE(inv2.isCreativeMode());
}

TEST(InventoryTest, ToJsonContainsHotbar) {
    Inventory inv;
    inv.addItem("Stone", 10);
    auto j = inv.toJson();
    ASSERT_TRUE(j.contains("hotbar"));
    EXPECT_EQ(j["hotbar"].size(), 9); // Always 9 hotbar slots
}

TEST(InventoryTest, ItemStackMergeCheck) {
    ItemStack a{"Stone", 50, 64};
    ItemStack b{"Stone", 10, 64};
    ItemStack c{"Wood", 10, 64};
    EXPECT_TRUE(a.canMerge(b));
    EXPECT_FALSE(a.canMerge(c));
    ItemStack full{"Stone", 64, 64};
    EXPECT_FALSE(full.canMerge(b));
}

TEST(InventoryTest, ItemStackSpaceLeft) {
    ItemStack s{"Stone", 50, 64};
    EXPECT_EQ(s.spaceLeft(), 14);
}


// ============================================================================
// No Minecraft stacks (user, 2026-09-11). The stack size is the item definition's:
// a registered consumable stacks to its own maxStack, an unregistered loot id is one
// per slot, and nothing defaults to 64 any more. RED before: addItem used a literal 64.
// ============================================================================
#include "core/ItemRegistry.h"
TEST(InventoryTest, StackSizeComesFromTheItemDefinitionNotSixtyFour) {
    auto& reg = ItemRegistry::instance();
    ItemDefinition potion; potion.id = "test_potion_stack"; potion.name = "Potion"; potion.type = ItemType::Consumable;
    potion.stackable = true; potion.maxStack = 8;
    reg.registerItem(potion);
    ItemDefinition sword; sword.id = "test_sword_stack"; sword.name = "Sword"; sword.type = ItemType::Weapon;   // unauthored stacking
    reg.registerItem(sword);
    EXPECT_EQ(Inventory::stackSizeFor("test_potion_stack"), 8);
    EXPECT_EQ(Inventory::stackSizeFor("test_sword_stack"), 1);
    EXPECT_EQ(Inventory::stackSizeFor("never_registered_relic"), 1);

    Inventory inv;
    EXPECT_EQ(inv.addItem("test_potion_stack", 10), 0);
    EXPECT_EQ(inv.countItem("test_potion_stack"), 10);
    int potionSlots = 0, maxSeen = 0;
    for (int i = 0; i < inv.size(); ++i)
        if (const auto s = inv.getSlot(i); s && s->itemId == "test_potion_stack") { ++potionSlots; maxSeen = std::max(maxSeen, s->count); }
    EXPECT_EQ(potionSlots, 2) << "8 + 2, not one stack of 10";
    EXPECT_EQ(maxSeen, 8);
    Inventory inv2;
    inv2.addItem("test_sword_stack", 3);
    int swordSlots = 0;
    for (int i = 0; i < inv2.size(); ++i)
        if (const auto s = inv2.getSlot(i); s && s->itemId == "test_sword_stack") { ++swordSlots; EXPECT_EQ(s->count, 1); }
    EXPECT_EQ(swordSlots, 3) << "a weapon is one per slot";
}

TEST(ItemDefinitionTest, UnauthoredItemsDoNotStackAndConsumablesStackToAQuiver) {
    const auto weapon = ItemDefinition::fromJson(nlohmann::json::parse(R"({"id":"w","name":"W","type":2})"));
    EXPECT_FALSE(weapon.stackable); EXPECT_EQ(weapon.maxStack, 1);
    const auto potion = ItemDefinition::fromJson(nlohmann::json::parse(R"({"id":"p","name":"P","type":3})"));
    EXPECT_TRUE(potion.stackable); EXPECT_EQ(potion.maxStack, ItemDefinition::kDefaultConsumableStack);
    const auto arrows = ItemDefinition::fromJson(nlohmann::json::parse(R"({"id":"a","name":"A","type":0,"stackable":true,"maxStack":20})"));
    EXPECT_EQ(arrows.maxStack, 20);   // authored stacking still wins
}
