#pragma once

/// D&D-style RPG item layer — distinct from the engine's voxel ItemDefinition.
/// This covers weapons, armor, magic items, consumables with D&D 5e stats.

#include "core/DiceSystem.h"
#include "core/DamageTypes.h"

#include <string>
#include <vector>
#include <set>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

// ============================================================================
// Enums
// ============================================================================

enum class RpgItemType {
    Weapon, Armor, Shield, Tool, Consumable,
    Ring, Wand, Rod, Staff, Potion, Scroll, MagicItem, Misc
};

enum class ItemRarity {
    Common, Uncommon, Rare, VeryRare, Legendary, Artifact
};

enum class WeaponCategory { Simple, Martial };

enum class WeaponProperty {
    Ammunition, Finesse, Heavy, Light, Loading,
    Reach, Thrown, TwoHanded, Versatile
};

enum class RpgArmorType { Light, Medium, Heavy, Shield };

// Name helpers
const char*    rpgItemTypeName(RpgItemType t);
RpgItemType    rpgItemTypeFromString(const char* s);
const char*    itemRarityName(ItemRarity r);
ItemRarity     itemRarityFromString(const char* s);
const char*    weaponPropertyName(WeaponProperty p);
WeaponProperty weaponPropertyFromString(const char* s);
const char*    rpgArmorTypeName(RpgArmorType t);
RpgArmorType   rpgArmorTypeFromString(const char* s);

// ============================================================================
// RpgItemDefinition — D&D 5e item data
// ============================================================================
struct RpgItemDefinition {
    std::string id;
    std::string name;
    RpgItemType type   = RpgItemType::Misc;
    ItemRarity  rarity = ItemRarity::Common;
    float       weightLbs = 0.0f;
    int         valueCp   = 0;         // value in copper pieces
    std::string description;
    bool        requiresAttunement = false;
    bool        isConsumable       = false;
    int         maxCharges         = 0;   // 0 = not a charged item

    // -----------------------------------------------------------------------
    // Weapon data (isWeapon == true when this item is a weapon or magic weapon)
    // -----------------------------------------------------------------------
    bool                     isWeapon         = false;
    WeaponCategory           weaponCategory   = WeaponCategory::Simple;
    DiceExpression           damageDice       = {0, DieType::D6, 0};
    DamageType               weaponDamageType = DamageType::Slashing;
    std::set<WeaponProperty> weaponProperties;
    int                      rangeNormal      = 0;    // feet (0 = melee)
    int                      rangeLong        = 0;
    DiceExpression           versatileDamage  = {0, DieType::D8, 0};

    // -----------------------------------------------------------------------
    // Armor data (isArmor == true for armor/shields)
    // -----------------------------------------------------------------------
    bool        isArmor             = false;
    RpgArmorType armorType          = RpgArmorType::Light;
    int         baseAC              = 0;
    int         maxDexBonus         = -1;   // -1 = unlimited, 0 = no dex bonus
    bool        stealthDisadvantage = false;
    int         strengthRequirement = 0;    // min STR or speed penalty

    // -----------------------------------------------------------------------
    // Magic bonuses
    // -----------------------------------------------------------------------
    int attackBonus = 0;
    int damageBonus = 0;
    int acBonus     = 0;
    int saveBonus   = 0;

    // -----------------------------------------------------------------------
    // Queries
    // -----------------------------------------------------------------------
    bool hasWeaponDamage() const { return damageDice.count > 0; }
    bool hasFinesse()      const { return weaponProperties.count(WeaponProperty::Finesse)    > 0; }
    bool hasVersatile()    const { return weaponProperties.count(WeaponProperty::Versatile)  > 0; }
    bool isTwoHanded()     const { return weaponProperties.count(WeaponProperty::TwoHanded) > 0; }
    bool isRangedWeapon()  const { return rangeNormal > 0; }
    bool isMagic()         const { return attackBonus > 0 || damageBonus > 0 ||
                                          acBonus > 0 || saveBonus > 0 ||
                                          rarity != ItemRarity::Common; }

    // -----------------------------------------------------------------------
    // Serialization
    // -----------------------------------------------------------------------
    nlohmann::json toJson() const;
    static RpgItemDefinition fromJson(const nlohmann::json& j);
};

// ============================================================================
// RpgItemRegistry — singleton; loads from resources/rpg_items/*.json
// ============================================================================
class RpgItemRegistry {
public:
    static RpgItemRegistry& instance();

    void registerItem(RpgItemDefinition def);
    const RpgItemDefinition* getItem(const std::string& id) const;

    std::vector<const RpgItemDefinition*> getItemsOfType(RpgItemType type) const;
    std::vector<const RpgItemDefinition*> getItemsOfRarity(ItemRarity rarity) const;
    std::vector<const RpgItemDefinition*> getWeapons(WeaponCategory category) const;
    std::vector<const RpgItemDefinition*> getArmor() const;
    std::vector<const RpgItemDefinition*> getAllItems() const;

    int  loadFromDirectory(const std::string& dirPath);
    bool loadFromFile(const std::string& filePath);

    size_t count() const { return m_items.size(); }
    void   clear()       { m_items.clear(); }

private:
    RpgItemRegistry() = default;
    std::unordered_map<std::string, RpgItemDefinition> m_items;
};

} // namespace Core
} // namespace Phyxel
