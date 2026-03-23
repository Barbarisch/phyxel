#include <gtest/gtest.h>
#include "core/Inventory.h"

using namespace Phyxel::Core;

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
    EXPECT_EQ(s->material, "Metal");
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
