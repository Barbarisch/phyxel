#include <gtest/gtest.h>
#include "core/ItemDefinition.h"
#include "core/ItemRegistry.h"
#include "core/Inventory.h"

using namespace Phyxel::Core;

// ============================================================================
// ItemDefinition tests
// ============================================================================

TEST(ItemDefinitionTest, DefaultValues) {
    ItemDefinition def;
    EXPECT_TRUE(def.id.empty());
    EXPECT_EQ(def.type, ItemType::Material);
    EXPECT_EQ(def.toolType, ToolType::None);
    EXPECT_EQ(def.equipSlot, EquipSlot::None);
    EXPECT_TRUE(def.stackable);
    EXPECT_EQ(def.maxStack, 64);
    EXPECT_FLOAT_EQ(def.damage, 0.0f);
    EXPECT_FLOAT_EQ(def.speed, 1.0f);
    EXPECT_EQ(def.maxDurability, 0);
}

TEST(ItemDefinitionTest, JsonRoundTrip) {
    ItemDefinition def;
    def.id = "iron_sword";
    def.name = "Iron Sword";
    def.type = ItemType::Weapon;
    def.toolType = ToolType::Sword;
    def.equipSlot = EquipSlot::MainHand;
    def.description = "A sturdy blade";
    def.stackable = false;
    def.maxStack = 1;
    def.damage = 8.0f;
    def.speed = 1.2f;
    def.maxDurability = 250;
    def.reach = 2.0f;
    def.templateFile = "weapons/sword.txt";
    def.attackAnimation = "melee_attack_horizontal";

    auto j = def.toJson();
    ItemDefinition restored = ItemDefinition::fromJson(j);

    EXPECT_EQ(restored.id, "iron_sword");
    EXPECT_EQ(restored.name, "Iron Sword");
    EXPECT_EQ(restored.type, ItemType::Weapon);
    EXPECT_EQ(restored.toolType, ToolType::Sword);
    EXPECT_EQ(restored.equipSlot, EquipSlot::MainHand);
    EXPECT_EQ(restored.description, "A sturdy blade");
    EXPECT_FALSE(restored.stackable);
    EXPECT_EQ(restored.maxStack, 1);
    EXPECT_FLOAT_EQ(restored.damage, 8.0f);
    EXPECT_FLOAT_EQ(restored.speed, 1.2f);
    EXPECT_EQ(restored.maxDurability, 250);
    EXPECT_FLOAT_EQ(restored.reach, 2.0f);
    EXPECT_EQ(restored.templateFile, "weapons/sword.txt");
    EXPECT_EQ(restored.attackAnimation, "melee_attack_horizontal");
}

TEST(ItemDefinitionTest, NonStackableForceMaxStackOne) {
    nlohmann::json j = {
        {"id", "test_item"},
        {"stackable", false},
        {"maxStack", 64}
    };
    auto def = ItemDefinition::fromJson(j);
    EXPECT_FALSE(def.stackable);
    EXPECT_EQ(def.maxStack, 1); // forced to 1
}

TEST(ItemDefinitionTest, EnumToString) {
    EXPECT_STREQ(itemTypeToString(ItemType::Material), "Material");
    EXPECT_STREQ(itemTypeToString(ItemType::Weapon), "Weapon");
    EXPECT_STREQ(itemTypeToString(ItemType::Tool), "Tool");
    EXPECT_STREQ(itemTypeToString(ItemType::Consumable), "Consumable");
    EXPECT_STREQ(itemTypeToString(ItemType::Quest), "Quest");
    EXPECT_STREQ(itemTypeToString(ItemType::Equippable), "Equippable");

    EXPECT_STREQ(toolTypeToString(ToolType::Sword), "Sword");
    EXPECT_STREQ(toolTypeToString(ToolType::Pickaxe), "Pickaxe");

    EXPECT_STREQ(equipSlotToString(EquipSlot::MainHand), "MainHand");
    EXPECT_STREQ(equipSlotToString(EquipSlot::Head), "Head");
}

// ============================================================================
// ItemRegistry tests
// ============================================================================

class ItemRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        ItemRegistry::instance().clear();
    }
    void TearDown() override {
        ItemRegistry::instance().clear();
    }
};

TEST_F(ItemRegistryTest, RegisterAndRetrieve) {
    ItemDefinition def;
    def.id = "test_item";
    def.name = "Test Item";
    def.type = ItemType::Material;

    EXPECT_TRUE(ItemRegistry::instance().registerItem(def));
    EXPECT_TRUE(ItemRegistry::instance().hasItem("test_item"));
    EXPECT_EQ(ItemRegistry::instance().count(), 1u);

    const auto* retrieved = ItemRegistry::instance().getItem("test_item");
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->name, "Test Item");
}

TEST_F(ItemRegistryTest, DuplicateRegistrationFails) {
    ItemDefinition def;
    def.id = "dupe";
    def.name = "First";

    EXPECT_TRUE(ItemRegistry::instance().registerItem(def));

    def.name = "Second";
    EXPECT_FALSE(ItemRegistry::instance().registerItem(def));

    // First registration wins
    EXPECT_EQ(ItemRegistry::instance().getItem("dupe")->name, "First");
}

TEST_F(ItemRegistryTest, EmptyIdRejected) {
    ItemDefinition def;
    def.id = "";
    EXPECT_FALSE(ItemRegistry::instance().registerItem(def));
}

TEST_F(ItemRegistryTest, GetNonexistent) {
    EXPECT_EQ(ItemRegistry::instance().getItem("nonexistent"), nullptr);
    EXPECT_FALSE(ItemRegistry::instance().hasItem("nonexistent"));
}

TEST_F(ItemRegistryTest, GetItemsByType) {
    ItemDefinition mat;
    mat.id = "stone";
    mat.type = ItemType::Material;
    ItemRegistry::instance().registerItem(mat);

    ItemDefinition wpn;
    wpn.id = "sword";
    wpn.type = ItemType::Weapon;
    ItemRegistry::instance().registerItem(wpn);

    ItemDefinition wpn2;
    wpn2.id = "axe";
    wpn2.type = ItemType::Weapon;
    ItemRegistry::instance().registerItem(wpn2);

    auto weapons = ItemRegistry::instance().getItemsByType(ItemType::Weapon);
    EXPECT_EQ(weapons.size(), 2u);

    auto materials = ItemRegistry::instance().getItemsByType(ItemType::Material);
    EXPECT_EQ(materials.size(), 1u);
}

TEST_F(ItemRegistryTest, GetAllItemIds) {
    ItemDefinition d1;
    d1.id = "alpha";
    ItemRegistry::instance().registerItem(d1);

    ItemDefinition d2;
    d2.id = "beta";
    ItemRegistry::instance().registerItem(d2);

    auto ids = ItemRegistry::instance().getAllItemIds();
    EXPECT_EQ(ids.size(), 2u);
}

TEST_F(ItemRegistryTest, Clear) {
    ItemDefinition def;
    def.id = "temp";
    ItemRegistry::instance().registerItem(def);
    EXPECT_EQ(ItemRegistry::instance().count(), 1u);

    ItemRegistry::instance().clear();
    EXPECT_EQ(ItemRegistry::instance().count(), 0u);
}

TEST_F(ItemRegistryTest, LoadFromJsonArray) {
    nlohmann::json items = nlohmann::json::array({
        {{"id", "sword"}, {"name", "Sword"}, {"type", 2}, {"damage", 10.0}},
        {{"id", "shield"}, {"name", "Shield"}, {"type", 5}, {"equipSlot", 2}}
    });

    int loaded = ItemRegistry::instance().loadFromJson(items);
    EXPECT_EQ(loaded, 2);
    EXPECT_TRUE(ItemRegistry::instance().hasItem("sword"));
    EXPECT_TRUE(ItemRegistry::instance().hasItem("shield"));

    auto* sword = ItemRegistry::instance().getItem("sword");
    EXPECT_FLOAT_EQ(sword->damage, 10.0f);
    EXPECT_EQ(sword->type, ItemType::Weapon);
}

TEST_F(ItemRegistryTest, RegisterMaterialItems) {
    ItemRegistry::instance().registerMaterialItems();
    // Should have at least the predefined materials: Wood, Metal, Glass, etc.
    EXPECT_TRUE(ItemRegistry::instance().hasItem("Wood"));
    EXPECT_TRUE(ItemRegistry::instance().hasItem("Stone"));
    EXPECT_TRUE(ItemRegistry::instance().hasItem("Metal"));
    EXPECT_TRUE(ItemRegistry::instance().hasItem("Default"));

    auto* wood = ItemRegistry::instance().getItem("Wood");
    ASSERT_NE(wood, nullptr);
    EXPECT_EQ(wood->type, ItemType::Material);
    EXPECT_TRUE(wood->stackable);
    EXPECT_EQ(wood->maxStack, 64);
}

// ============================================================================
// ItemStack durability tests
// ============================================================================

TEST(ItemStackTest, DefaultDurability) {
    ItemStack stack{"Stone", 10, 64};
    EXPECT_EQ(stack.durability, -1); // not applicable by default
}

TEST(ItemStackTest, DurabilityPreventsStacking) {
    ItemStack a{"iron_sword", 1, 1, 100};
    ItemStack b{"iron_sword", 1, 1, 90};
    EXPECT_FALSE(a.canMerge(b)); // items with durability don't stack
}

TEST(ItemStackTest, MaterialAlias) {
    ItemStack stack{"Stone", 5, 64};
    EXPECT_EQ(stack.material(), "Stone");
    EXPECT_EQ(stack.itemId, "Stone");
}

TEST(ItemStackTest, DurabilityInJson) {
    ItemStack stack{"iron_sword", 1, 1, 100};
    auto j = stack.toJson();
    EXPECT_TRUE(j.contains("durability"));
    EXPECT_EQ(j["durability"].get<int>(), 100);

    // Without durability
    ItemStack stack2{"Stone", 10, 64};
    auto j2 = stack2.toJson();
    EXPECT_FALSE(j2.contains("durability"));
}
