#include <gtest/gtest.h>
#include "core/EquipmentSystem.h"
#include "core/ItemDefinition.h"

using namespace Phyxel::Core;

namespace {

ItemDefinition makeSword() {
    ItemDefinition def;
    def.id = "iron_sword";
    def.name = "Iron Sword";
    def.type = ItemType::Weapon;
    def.equipSlot = EquipSlot::MainHand;
    def.damage = 15.0f;
    def.reach = 2.5f;
    def.speed = 1.2f;
    def.maxDurability = 100;
    return def;
}

ItemDefinition makeShield() {
    ItemDefinition def;
    def.id = "wooden_shield";
    def.name = "Wooden Shield";
    def.type = ItemType::Equippable;
    def.equipSlot = EquipSlot::OffHand;
    def.damage = 0.0f;
    def.reach = 0.0f;
    def.speed = 0.0f;
    return def;
}

ItemDefinition makeHelmet() {
    ItemDefinition def;
    def.id = "iron_helmet";
    def.name = "Iron Helmet";
    def.type = ItemType::Equippable;
    def.equipSlot = EquipSlot::Head;
    return def;
}

ItemDefinition makeResource() {
    ItemDefinition def;
    def.id = "wood";
    def.name = "Wood";
    def.type = ItemType::Material;
    def.equipSlot = EquipSlot::None;
    return def;
}

ItemDefinition makeTool() {
    ItemDefinition def;
    def.id = "iron_pickaxe";
    def.name = "Iron Pickaxe";
    def.type = ItemType::Tool;
    def.equipSlot = EquipSlot::MainHand;
    def.damage = 8.0f;
    def.reach = 2.0f;
    def.speed = 0.8f;
    return def;
}

} // anonymous namespace

// ============================================================================
// Equip / Unequip
// ============================================================================

TEST(EquipmentSystemTest, EquipWeapon) {
    EquipmentSlots eq;
    auto sword = makeSword();
    EXPECT_TRUE(eq.equip(sword));
    EXPECT_TRUE(eq.hasItem(EquipSlot::MainHand));
    auto* item = eq.getItem(EquipSlot::MainHand);
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->id, "iron_sword");
}

TEST(EquipmentSystemTest, EquipTool) {
    EquipmentSlots eq;
    auto tool = makeTool();
    EXPECT_TRUE(eq.equip(tool));
    EXPECT_TRUE(eq.hasItem(EquipSlot::MainHand));
}

TEST(EquipmentSystemTest, EquipEquippable) {
    EquipmentSlots eq;
    auto shield = makeShield();
    EXPECT_TRUE(eq.equip(shield));
    EXPECT_TRUE(eq.hasItem(EquipSlot::OffHand));
}

TEST(EquipmentSystemTest, CannotEquipResource) {
    EquipmentSlots eq;
    auto resource = makeResource();
    EXPECT_FALSE(eq.equip(resource));
    EXPECT_FALSE(eq.hasItem(EquipSlot::None));
}

TEST(EquipmentSystemTest, CannotEquipNoSlot) {
    ItemDefinition def;
    def.id = "broken";
    def.name = "Broken";
    def.type = ItemType::Weapon;
    def.equipSlot = EquipSlot::None;
    EquipmentSlots eq;
    EXPECT_FALSE(eq.equip(def));
}

TEST(EquipmentSystemTest, EquipReplacesExisting) {
    EquipmentSlots eq;
    auto sword = makeSword();
    auto tool = makeTool();
    eq.equip(sword);
    eq.equip(tool);
    auto* item = eq.getItem(EquipSlot::MainHand);
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->id, "iron_pickaxe");
}

TEST(EquipmentSystemTest, Unequip) {
    EquipmentSlots eq;
    eq.equip(makeSword());
    auto removed = eq.unequip(EquipSlot::MainHand);
    ASSERT_TRUE(removed.has_value());
    EXPECT_EQ(*removed, "iron_sword");
    EXPECT_FALSE(eq.hasItem(EquipSlot::MainHand));
}

TEST(EquipmentSystemTest, UnequipEmptySlot) {
    EquipmentSlots eq;
    auto removed = eq.unequip(EquipSlot::MainHand);
    EXPECT_FALSE(removed.has_value());
}

TEST(EquipmentSystemTest, Clear) {
    EquipmentSlots eq;
    eq.equip(makeSword());
    eq.equip(makeShield());
    eq.equip(makeHelmet());
    eq.clear();
    EXPECT_FALSE(eq.hasItem(EquipSlot::MainHand));
    EXPECT_FALSE(eq.hasItem(EquipSlot::OffHand));
    EXPECT_FALSE(eq.hasItem(EquipSlot::Head));
}

// ============================================================================
// GetAllEquipped
// ============================================================================

TEST(EquipmentSystemTest, GetAllEquipped) {
    EquipmentSlots eq;
    eq.equip(makeSword());
    eq.equip(makeShield());
    auto all = eq.getAllEquipped();
    EXPECT_EQ(all.size(), 2u);
    EXPECT_EQ(all[EquipSlot::MainHand], "iron_sword");
    EXPECT_EQ(all[EquipSlot::OffHand], "wooden_shield");
}

TEST(EquipmentSystemTest, GetAllEquippedEmpty) {
    EquipmentSlots eq;
    auto all = eq.getAllEquipped();
    EXPECT_TRUE(all.empty());
}

// ============================================================================
// Stats
// ============================================================================

TEST(EquipmentSystemTest, TotalDamage) {
    EquipmentSlots eq;
    EXPECT_FLOAT_EQ(eq.getTotalDamage(), 0.0f);
    eq.equip(makeSword());
    EXPECT_FLOAT_EQ(eq.getTotalDamage(), 15.0f);
}

TEST(EquipmentSystemTest, TotalReachFist) {
    EquipmentSlots eq;
    EXPECT_FLOAT_EQ(eq.getTotalReach(), 1.5f); // fist default
}

TEST(EquipmentSystemTest, TotalReachWithWeapon) {
    EquipmentSlots eq;
    eq.equip(makeSword());
    EXPECT_FLOAT_EQ(eq.getTotalReach(), 2.5f);
}

TEST(EquipmentSystemTest, TotalSpeed) {
    EquipmentSlots eq;
    EXPECT_FLOAT_EQ(eq.getTotalSpeed(), 1.0f); // multiplicative base
    eq.equip(makeSword());
    EXPECT_FLOAT_EQ(eq.getTotalSpeed(), 1.2f);
}

// ============================================================================
// Callback
// ============================================================================

TEST(EquipmentSystemTest, OnEquipmentChangedFired) {
    EquipmentSlots eq;
    EquipSlot firedSlot = EquipSlot::None;
    std::string firedItemId;
    eq.setOnEquipmentChanged([&](EquipSlot slot, const std::string& itemId) {
        firedSlot = slot;
        firedItemId = itemId;
    });

    eq.equip(makeSword());
    EXPECT_EQ(firedSlot, EquipSlot::MainHand);
    EXPECT_EQ(firedItemId, "iron_sword");
}

TEST(EquipmentSystemTest, OnEquipmentChangedOnUnequip) {
    EquipmentSlots eq;
    eq.equip(makeSword());

    EquipSlot firedSlot = EquipSlot::None;
    std::string firedItemId = "not-called";
    eq.setOnEquipmentChanged([&](EquipSlot slot, const std::string& itemId) {
        firedSlot = slot;
        firedItemId = itemId;
    });

    eq.unequip(EquipSlot::MainHand);
    EXPECT_EQ(firedSlot, EquipSlot::MainHand);
    EXPECT_EQ(firedItemId, "");
}

// ============================================================================
// Serialization
// ============================================================================

TEST(EquipmentSystemTest, ToJson) {
    EquipmentSlots eq;
    eq.equip(makeSword());
    eq.equip(makeHelmet());
    auto j = eq.toJson();
    EXPECT_EQ(j.size(), 2u);
    EXPECT_EQ(j["MainHand"], "iron_sword");
    EXPECT_EQ(j["Head"], "iron_helmet");
}

TEST(EquipmentSystemTest, ToJsonEmpty) {
    EquipmentSlots eq;
    auto j = eq.toJson();
    EXPECT_TRUE(j.is_object());
    EXPECT_EQ(j.size(), 0u);
}

// ============================================================================
// equipSlotFromString / equipSlotToString roundtrip
// ============================================================================

TEST(EquipmentSystemTest, SlotStringRoundtrip) {
    EXPECT_EQ(equipSlotFromString("MainHand"), EquipSlot::MainHand);
    EXPECT_EQ(equipSlotFromString("OffHand"), EquipSlot::OffHand);
    EXPECT_EQ(equipSlotFromString("Head"), EquipSlot::Head);
    EXPECT_EQ(equipSlotFromString("Chest"), EquipSlot::Chest);
    EXPECT_EQ(equipSlotFromString("Legs"), EquipSlot::Legs);
    EXPECT_EQ(equipSlotFromString("Feet"), EquipSlot::Feet);
    EXPECT_EQ(equipSlotFromString("None"), EquipSlot::None);
    EXPECT_EQ(equipSlotFromString("invalid"), EquipSlot::None);
}

TEST(EquipmentSystemTest, SlotToStringAll) {
    EXPECT_STREQ(equipSlotToString(EquipSlot::MainHand), "MainHand");
    EXPECT_STREQ(equipSlotToString(EquipSlot::OffHand), "OffHand");
    EXPECT_STREQ(equipSlotToString(EquipSlot::Head), "Head");
    EXPECT_STREQ(equipSlotToString(EquipSlot::Chest), "Chest");
    EXPECT_STREQ(equipSlotToString(EquipSlot::Legs), "Legs");
    EXPECT_STREQ(equipSlotToString(EquipSlot::Feet), "Feet");
    EXPECT_STREQ(equipSlotToString(EquipSlot::None), "None");
}
