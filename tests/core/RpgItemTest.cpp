#include <gtest/gtest.h>
#include "core/RpgItem.h"

using namespace Phyxel::Core;

// ---------------------------------------------------------------------------
// Name helpers
// ---------------------------------------------------------------------------

TEST(RpgItemTest, ItemTypeRoundTrip) {
    const RpgItemType types[] = {
        RpgItemType::Weapon, RpgItemType::Armor, RpgItemType::Shield,
        RpgItemType::Potion, RpgItemType::Staff, RpgItemType::Ring,
        RpgItemType::Scroll, RpgItemType::MagicItem, RpgItemType::Misc
    };
    for (auto t : types) {
        const char* n = rpgItemTypeName(t);
        EXPECT_EQ(rpgItemTypeFromString(n), t) << "Failed for: " << n;
    }
}

TEST(RpgItemTest, RarityRoundTrip) {
    const ItemRarity rarities[] = {
        ItemRarity::Common, ItemRarity::Uncommon, ItemRarity::Rare,
        ItemRarity::VeryRare, ItemRarity::Legendary, ItemRarity::Artifact
    };
    for (auto r : rarities) {
        const char* n = itemRarityName(r);
        EXPECT_EQ(itemRarityFromString(n), r) << "Failed for: " << n;
    }
}

TEST(RpgItemTest, WeaponPropertyRoundTrip) {
    const WeaponProperty props[] = {
        WeaponProperty::Ammunition, WeaponProperty::Finesse, WeaponProperty::Heavy,
        WeaponProperty::Light, WeaponProperty::Loading, WeaponProperty::Reach,
        WeaponProperty::Thrown, WeaponProperty::TwoHanded, WeaponProperty::Versatile
    };
    for (auto p : props) {
        const char* n = weaponPropertyName(p);
        EXPECT_EQ(weaponPropertyFromString(n), p) << "Failed for: " << n;
    }
}

TEST(RpgItemTest, ArmorTypeRoundTrip) {
    const RpgArmorType types[] = {
        RpgArmorType::Light, RpgArmorType::Medium,
        RpgArmorType::Heavy, RpgArmorType::Shield
    };
    for (auto t : types) {
        const char* n = rpgArmorTypeName(t);
        EXPECT_EQ(rpgArmorTypeFromString(n), t) << "Failed for: " << n;
    }
}

// ---------------------------------------------------------------------------
// RpgItemDefinition — weapon queries
// ---------------------------------------------------------------------------

static RpgItemDefinition makeLongsword() {
    RpgItemDefinition d;
    d.id             = "longsword";
    d.name           = "Longsword";
    d.type           = RpgItemType::Weapon;
    d.rarity         = ItemRarity::Common;
    d.weightLbs      = 3.0f;
    d.valueCp        = 1500;
    d.isWeapon       = true;
    d.weaponCategory = WeaponCategory::Martial;
    d.damageDice     = DiceExpression::parse("1d8");
    d.weaponDamageType = DamageType::Slashing;
    d.weaponProperties = {WeaponProperty::Versatile};
    d.versatileDamage = DiceExpression::parse("1d10");
    return d;
}

TEST(RpgItemTest, WeaponQueriesLongsword) {
    auto item = makeLongsword();
    EXPECT_TRUE(item.isWeapon);
    EXPECT_TRUE(item.hasWeaponDamage());
    EXPECT_TRUE(item.hasVersatile());
    EXPECT_FALSE(item.hasFinesse());
    EXPECT_FALSE(item.isTwoHanded());
    EXPECT_FALSE(item.isRangedWeapon());
    EXPECT_FALSE(item.isMagic());
    EXPECT_EQ(item.attackBonus, 0);
}

TEST(RpgItemTest, MagicWeaponQueries) {
    auto item = makeLongsword();
    item.rarity      = ItemRarity::Uncommon;
    item.attackBonus = 1;
    item.damageBonus = 1;
    EXPECT_TRUE(item.isMagic());
    EXPECT_EQ(item.attackBonus, 1);
    EXPECT_EQ(item.damageBonus, 1);
}

TEST(RpgItemTest, DaggerIsFinesseThrownLight) {
    RpgItemDefinition d;
    d.isWeapon = true;
    d.damageDice = DiceExpression::parse("1d4");
    d.weaponProperties = {
        WeaponProperty::Finesse, WeaponProperty::Light, WeaponProperty::Thrown
    };
    d.rangeNormal = 20;
    EXPECT_TRUE(d.hasFinesse());
    EXPECT_TRUE(d.isRangedWeapon());
    EXPECT_FALSE(d.isTwoHanded());
}

TEST(RpgItemTest, GreatswordIsTwoHanded) {
    RpgItemDefinition d;
    d.isWeapon = true;
    d.damageDice = DiceExpression::parse("2d6");
    d.weaponProperties = {WeaponProperty::Heavy, WeaponProperty::TwoHanded};
    EXPECT_TRUE(d.isTwoHanded());
    EXPECT_FALSE(d.hasFinesse());
    EXPECT_FALSE(d.hasVersatile());
}

// ---------------------------------------------------------------------------
// RpgItemDefinition — armor queries
// ---------------------------------------------------------------------------

TEST(RpgItemTest, ArmorQueries) {
    RpgItemDefinition d;
    d.id               = "plate_armor";
    d.isArmor          = true;
    d.armorType        = RpgArmorType::Heavy;
    d.baseAC           = 18;
    d.maxDexBonus      = 0;
    d.stealthDisadvantage = true;
    d.strengthRequirement = 15;

    EXPECT_TRUE(d.isArmor);
    EXPECT_EQ(d.baseAC, 18);
    EXPECT_EQ(d.maxDexBonus, 0);
    EXPECT_TRUE(d.stealthDisadvantage);
    EXPECT_EQ(d.strengthRequirement, 15);
}

// ---------------------------------------------------------------------------
// RpgItemDefinition — JSON round-trip
// ---------------------------------------------------------------------------

TEST(RpgItemTest, WeaponJsonRoundTrip) {
    auto original = makeLongsword();
    original.attackBonus = 1;
    original.damageBonus = 1;
    original.rarity = ItemRarity::Uncommon;

    auto j = original.toJson();
    auto restored = RpgItemDefinition::fromJson(j);

    EXPECT_EQ(restored.id,         "longsword");
    EXPECT_EQ(restored.type,       RpgItemType::Weapon);
    EXPECT_EQ(restored.rarity,     ItemRarity::Uncommon);
    EXPECT_FLOAT_EQ(restored.weightLbs, 3.0f);
    EXPECT_EQ(restored.valueCp,    1500);
    EXPECT_TRUE(restored.isWeapon);
    EXPECT_EQ(restored.weaponCategory, WeaponCategory::Martial);
    EXPECT_EQ(restored.damageDice.count, 1);
    EXPECT_EQ(static_cast<int>(restored.damageDice.die), 8);
    EXPECT_EQ(restored.weaponDamageType, DamageType::Slashing);
    EXPECT_TRUE(restored.weaponProperties.count(WeaponProperty::Versatile) > 0);
    EXPECT_EQ(restored.versatileDamage.count, 1);
    EXPECT_EQ(static_cast<int>(restored.versatileDamage.die), 10);
    EXPECT_EQ(restored.attackBonus, 1);
    EXPECT_EQ(restored.damageBonus, 1);
}

TEST(RpgItemTest, ArmorJsonRoundTrip) {
    RpgItemDefinition original;
    original.id               = "chain_mail";
    original.name             = "Chain Mail";
    original.type             = RpgItemType::Armor;
    original.rarity           = ItemRarity::Common;
    original.weightLbs        = 55.0f;
    original.valueCp          = 7500;
    original.isArmor          = true;
    original.armorType        = RpgArmorType::Heavy;
    original.baseAC           = 16;
    original.maxDexBonus      = 0;
    original.stealthDisadvantage = true;
    original.strengthRequirement = 13;

    auto j = original.toJson();
    auto restored = RpgItemDefinition::fromJson(j);

    EXPECT_EQ(restored.id,         "chain_mail");
    EXPECT_TRUE(restored.isArmor);
    EXPECT_EQ(restored.armorType,  RpgArmorType::Heavy);
    EXPECT_EQ(restored.baseAC,     16);
    EXPECT_EQ(restored.maxDexBonus, 0);
    EXPECT_TRUE(restored.stealthDisadvantage);
    EXPECT_EQ(restored.strengthRequirement, 13);
}

// ---------------------------------------------------------------------------
// RpgItemRegistry
// ---------------------------------------------------------------------------

class RpgItemRegistryTest : public ::testing::Test {
protected:
    void SetUp()    override { RpgItemRegistry::instance().clear(); }
    void TearDown() override { RpgItemRegistry::instance().clear(); }
};

TEST_F(RpgItemRegistryTest, RegisterAndRetrieve) {
    auto& reg = RpgItemRegistry::instance();
    reg.registerItem(makeLongsword());
    EXPECT_NE(reg.getItem("longsword"), nullptr);
    EXPECT_EQ(reg.getItem("dagger"),    nullptr);
}

TEST_F(RpgItemRegistryTest, GetItemsOfType) {
    auto& reg = RpgItemRegistry::instance();

    auto sword = makeLongsword();
    reg.registerItem(sword);

    RpgItemDefinition potion;
    potion.id   = "potion_healing";
    potion.type = RpgItemType::Potion;
    reg.registerItem(potion);

    EXPECT_EQ(reg.getItemsOfType(RpgItemType::Weapon).size(), 1u);
    EXPECT_EQ(reg.getItemsOfType(RpgItemType::Potion).size(), 1u);
    EXPECT_EQ(reg.getItemsOfType(RpgItemType::Armor).size(),  0u);
}

TEST_F(RpgItemRegistryTest, GetItemsOfRarity) {
    auto& reg = RpgItemRegistry::instance();

    auto common = makeLongsword();  // Common
    common.id = "sword_common";
    reg.registerItem(common);

    auto rare = makeLongsword();
    rare.id     = "sword_rare";
    rare.rarity = ItemRarity::Rare;
    reg.registerItem(rare);

    EXPECT_EQ(reg.getItemsOfRarity(ItemRarity::Common).size(), 1u);
    EXPECT_EQ(reg.getItemsOfRarity(ItemRarity::Rare).size(),   1u);
    EXPECT_EQ(reg.getItemsOfRarity(ItemRarity::Legendary).size(), 0u);
}

TEST_F(RpgItemRegistryTest, GetWeaponsByCategory) {
    auto& reg = RpgItemRegistry::instance();

    auto martial = makeLongsword();
    reg.registerItem(martial);

    RpgItemDefinition simple;
    simple.id = "quarterstaff";
    simple.isWeapon = true;
    simple.weaponCategory = WeaponCategory::Simple;
    reg.registerItem(simple);

    EXPECT_EQ(reg.getWeapons(WeaponCategory::Martial).size(), 1u);
    EXPECT_EQ(reg.getWeapons(WeaponCategory::Simple).size(),  1u);
}

TEST_F(RpgItemRegistryTest, GetArmor) {
    auto& reg = RpgItemRegistry::instance();

    RpgItemDefinition armor;
    armor.id      = "chain_mail";
    armor.isArmor = true;
    reg.registerItem(armor);

    reg.registerItem(makeLongsword());  // not armor

    EXPECT_EQ(reg.getArmor().size(), 1u);
}

TEST_F(RpgItemRegistryTest, LoadFromFileSimpleWeapons) {
    auto& reg = RpgItemRegistry::instance();
    EXPECT_TRUE(reg.loadFromFile("resources/rpg_items/weapons_simple.json"));
    EXPECT_GE(reg.count(), 5u);
    EXPECT_NE(reg.getItem("dagger"),      nullptr);
    EXPECT_NE(reg.getItem("quarterstaff"),nullptr);
    const auto* dagger = reg.getItem("dagger");
    ASSERT_NE(dagger, nullptr);
    EXPECT_TRUE(dagger->hasFinesse());
    EXPECT_TRUE(dagger->isRangedWeapon());
    EXPECT_EQ(dagger->damageDice.count, 1);
    EXPECT_EQ(static_cast<int>(dagger->damageDice.die), 4);
}

TEST_F(RpgItemRegistryTest, LoadFromFileMartialWeapons) {
    auto& reg = RpgItemRegistry::instance();
    EXPECT_TRUE(reg.loadFromFile("resources/rpg_items/weapons_martial.json"));
    const auto* gs = reg.getItem("greatsword");
    ASSERT_NE(gs, nullptr);
    EXPECT_TRUE(gs->isTwoHanded());
    EXPECT_EQ(gs->damageDice.count, 2);
    EXPECT_EQ(static_cast<int>(gs->damageDice.die), 6);
    EXPECT_EQ(gs->weaponCategory, WeaponCategory::Martial);
}

TEST_F(RpgItemRegistryTest, LoadFromFileArmor) {
    auto& reg = RpgItemRegistry::instance();
    EXPECT_TRUE(reg.loadFromFile("resources/rpg_items/armor.json"));
    const auto* plate = reg.getItem("plate_armor");
    ASSERT_NE(plate, nullptr);
    EXPECT_EQ(plate->baseAC,           18);
    EXPECT_EQ(plate->maxDexBonus,       0);
    EXPECT_TRUE(plate->stealthDisadvantage);
    EXPECT_EQ(plate->strengthRequirement, 15);
    EXPECT_EQ(plate->armorType, RpgArmorType::Heavy);
}

TEST_F(RpgItemRegistryTest, LoadFromFileMagicItems) {
    auto& reg = RpgItemRegistry::instance();
    EXPECT_TRUE(reg.loadFromFile("resources/rpg_items/magic_items.json"));

    const auto* ring = reg.getItem("ring_of_protection");
    ASSERT_NE(ring, nullptr);
    EXPECT_TRUE(ring->requiresAttunement);
    EXPECT_EQ(ring->acBonus,   1);
    EXPECT_EQ(ring->saveBonus, 1);
    EXPECT_TRUE(ring->isMagic());

    const auto* plus2 = reg.getItem("longsword_plus2");
    ASSERT_NE(plus2, nullptr);
    EXPECT_EQ(plus2->attackBonus, 2);
    EXPECT_EQ(plus2->damageBonus, 2);
    EXPECT_EQ(plus2->rarity, ItemRarity::Rare);
    EXPECT_FALSE(plus2->requiresAttunement);
}

TEST_F(RpgItemRegistryTest, LoadFromDirectory) {
    auto& reg = RpgItemRegistry::instance();
    int files = reg.loadFromDirectory("resources/rpg_items");
    EXPECT_GE(files, 3);
    EXPECT_GE(reg.count(), 20u);
}
