#include "core/RpgItem.h"
#include "utils/Logger.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <stdexcept>

namespace Phyxel {
namespace Core {

// ---------------------------------------------------------------------------
// Name helpers — RpgItemType
// ---------------------------------------------------------------------------

const char* rpgItemTypeName(RpgItemType t) {
    switch (t) {
        case RpgItemType::Weapon:    return "Weapon";
        case RpgItemType::Armor:     return "Armor";
        case RpgItemType::Shield:    return "Shield";
        case RpgItemType::Tool:      return "Tool";
        case RpgItemType::Consumable:return "Consumable";
        case RpgItemType::Ring:      return "Ring";
        case RpgItemType::Wand:      return "Wand";
        case RpgItemType::Rod:       return "Rod";
        case RpgItemType::Staff:     return "Staff";
        case RpgItemType::Potion:    return "Potion";
        case RpgItemType::Scroll:    return "Scroll";
        case RpgItemType::MagicItem: return "MagicItem";
        default:                     return "Misc";
    }
}

RpgItemType rpgItemTypeFromString(const char* s) {
    if (!s) return RpgItemType::Misc;
    auto eq = [&](const char* c) { return _stricmp(s, c) == 0; };
    if (eq("Weapon"))     return RpgItemType::Weapon;
    if (eq("Armor"))      return RpgItemType::Armor;
    if (eq("Shield"))     return RpgItemType::Shield;
    if (eq("Tool"))       return RpgItemType::Tool;
    if (eq("Consumable")) return RpgItemType::Consumable;
    if (eq("Ring"))       return RpgItemType::Ring;
    if (eq("Wand"))       return RpgItemType::Wand;
    if (eq("Rod"))        return RpgItemType::Rod;
    if (eq("Staff"))      return RpgItemType::Staff;
    if (eq("Potion"))     return RpgItemType::Potion;
    if (eq("Scroll"))     return RpgItemType::Scroll;
    if (eq("MagicItem"))  return RpgItemType::MagicItem;
    return RpgItemType::Misc;
}

// ---------------------------------------------------------------------------
// Name helpers — ItemRarity
// ---------------------------------------------------------------------------

const char* itemRarityName(ItemRarity r) {
    switch (r) {
        case ItemRarity::Common:    return "Common";
        case ItemRarity::Uncommon:  return "Uncommon";
        case ItemRarity::Rare:      return "Rare";
        case ItemRarity::VeryRare:  return "VeryRare";
        case ItemRarity::Legendary: return "Legendary";
        case ItemRarity::Artifact:  return "Artifact";
        default:                    return "Common";
    }
}

ItemRarity itemRarityFromString(const char* s) {
    if (!s) return ItemRarity::Common;
    auto eq = [&](const char* c) { return _stricmp(s, c) == 0; };
    if (eq("Uncommon"))              return ItemRarity::Uncommon;
    if (eq("Rare"))                  return ItemRarity::Rare;
    if (eq("VeryRare") || eq("Very Rare")) return ItemRarity::VeryRare;
    if (eq("Legendary"))             return ItemRarity::Legendary;
    if (eq("Artifact"))              return ItemRarity::Artifact;
    return ItemRarity::Common;
}

// ---------------------------------------------------------------------------
// Name helpers — WeaponProperty
// ---------------------------------------------------------------------------

const char* weaponPropertyName(WeaponProperty p) {
    switch (p) {
        case WeaponProperty::Ammunition: return "Ammunition";
        case WeaponProperty::Finesse:    return "Finesse";
        case WeaponProperty::Heavy:      return "Heavy";
        case WeaponProperty::Light:      return "Light";
        case WeaponProperty::Loading:    return "Loading";
        case WeaponProperty::Reach:      return "Reach";
        case WeaponProperty::Thrown:     return "Thrown";
        case WeaponProperty::TwoHanded:  return "TwoHanded";
        case WeaponProperty::Versatile:  return "Versatile";
        default:                         return "Unknown";
    }
}

WeaponProperty weaponPropertyFromString(const char* s) {
    if (!s) throw std::invalid_argument("null property string");
    auto eq = [&](const char* c) { return _stricmp(s, c) == 0; };
    if (eq("Ammunition"))             return WeaponProperty::Ammunition;
    if (eq("Finesse"))                return WeaponProperty::Finesse;
    if (eq("Heavy"))                  return WeaponProperty::Heavy;
    if (eq("Light"))                  return WeaponProperty::Light;
    if (eq("Loading"))                return WeaponProperty::Loading;
    if (eq("Reach"))                  return WeaponProperty::Reach;
    if (eq("Thrown"))                 return WeaponProperty::Thrown;
    if (eq("TwoHanded") || eq("Two-Handed")) return WeaponProperty::TwoHanded;
    if (eq("Versatile"))              return WeaponProperty::Versatile;
    throw std::invalid_argument(std::string("Unknown weapon property: ") + s);
}

// ---------------------------------------------------------------------------
// Name helpers — RpgArmorType
// ---------------------------------------------------------------------------

const char* rpgArmorTypeName(RpgArmorType t) {
    switch (t) {
        case RpgArmorType::Light:  return "Light";
        case RpgArmorType::Medium: return "Medium";
        case RpgArmorType::Heavy:  return "Heavy";
        case RpgArmorType::Shield: return "Shield";
        default:                   return "Light";
    }
}

RpgArmorType rpgArmorTypeFromString(const char* s) {
    if (!s) return RpgArmorType::Light;
    auto eq = [&](const char* c) { return _stricmp(s, c) == 0; };
    if (eq("Medium")) return RpgArmorType::Medium;
    if (eq("Heavy"))  return RpgArmorType::Heavy;
    if (eq("Shield")) return RpgArmorType::Shield;
    return RpgArmorType::Light;
}

// ---------------------------------------------------------------------------
// RpgItemDefinition — serialization
// ---------------------------------------------------------------------------

static std::vector<std::string> propertySetToStrings(const std::set<WeaponProperty>& props) {
    std::vector<std::string> v;
    for (auto p : props) v.push_back(weaponPropertyName(p));
    return v;
}

static std::set<WeaponProperty> propertySetFromJson(const nlohmann::json& arr) {
    std::set<WeaponProperty> props;
    if (!arr.is_array()) return props;
    for (const auto& p : arr) {
        if (p.is_string()) {
            try { props.insert(weaponPropertyFromString(p.get<std::string>().c_str())); }
            catch (...) {}
        }
    }
    return props;
}

nlohmann::json RpgItemDefinition::toJson() const {
    nlohmann::json j;
    j["id"]                 = id;
    j["name"]               = name;
    j["type"]               = rpgItemTypeName(type);
    j["rarity"]             = itemRarityName(rarity);
    j["weightLbs"]          = weightLbs;
    j["valueCp"]            = valueCp;
    j["description"]        = description;
    j["requiresAttunement"] = requiresAttunement;
    j["isConsumable"]       = isConsumable;
    j["maxCharges"]         = maxCharges;
    j["attackBonus"]        = attackBonus;
    j["damageBonus"]        = damageBonus;
    j["acBonus"]            = acBonus;
    j["saveBonus"]          = saveBonus;

    if (isWeapon) {
        nlohmann::json w;
        w["category"]    = (weaponCategory == WeaponCategory::Simple) ? "Simple" : "Martial";
        w["damage"]      = damageDice.toString();
        w["damageType"]  = damageTypeToString(weaponDamageType);
        w["properties"]  = propertySetToStrings(weaponProperties);
        w["rangeNormal"] = rangeNormal;
        w["rangeLong"]   = rangeLong;
        if (hasVersatile()) w["versatileDamage"] = versatileDamage.toString();
        j["weapon"] = w;
    }

    if (isArmor) {
        nlohmann::json a;
        a["armorType"]           = rpgArmorTypeName(armorType);
        a["baseAC"]              = baseAC;
        a["maxDexBonus"]         = maxDexBonus;
        a["stealthDisadvantage"] = stealthDisadvantage;
        a["strengthRequirement"] = strengthRequirement;
        j["armor"] = a;
    }

    return j;
}

RpgItemDefinition RpgItemDefinition::fromJson(const nlohmann::json& j) {
    RpgItemDefinition d;
    d.id          = j.value("id",   "");
    d.name        = j.value("name", d.id);

    d.type   = rpgItemTypeFromString(j.value("type",   "Misc").c_str());
    d.rarity = itemRarityFromString( j.value("rarity", "Common").c_str());

    d.weightLbs          = j.value("weightLbs",          0.0f);
    d.valueCp            = j.value("valueCp",             0);
    d.description        = j.value("description",        "");
    d.requiresAttunement = j.value("requiresAttunement", false);
    d.isConsumable       = j.value("isConsumable",       false);
    d.maxCharges         = j.value("maxCharges",          0);
    d.attackBonus        = j.value("attackBonus",         0);
    d.damageBonus        = j.value("damageBonus",         0);
    d.acBonus            = j.value("acBonus",             0);
    d.saveBonus          = j.value("saveBonus",           0);

    if (j.contains("weapon") && j["weapon"].is_object()) {
        d.isWeapon = true;
        const auto& w = j["weapon"];
        std::string cat = w.value("category", "Simple");
        d.weaponCategory  = (_stricmp(cat.c_str(), "Martial") == 0)
                            ? WeaponCategory::Martial : WeaponCategory::Simple;

        std::string dmg = w.value("damage", "");
        if (!dmg.empty()) { try { d.damageDice = DiceExpression::parse(dmg); } catch (...) {} }

        std::string dt = w.value("damageType", "Slashing");
        try { d.weaponDamageType = damageTypeFromString(dt.c_str()); }
        catch (...) { d.weaponDamageType = DamageType::Slashing; }

        d.weaponProperties = propertySetFromJson(w.value("properties", nlohmann::json::array()));
        d.rangeNormal      = w.value("rangeNormal", 0);
        d.rangeLong        = w.value("rangeLong",   0);

        std::string vd = w.value("versatileDamage", "");
        if (!vd.empty()) { try { d.versatileDamage = DiceExpression::parse(vd); } catch (...) {} }
    }

    if (j.contains("armor") && j["armor"].is_object()) {
        d.isArmor = true;
        const auto& a = j["armor"];
        d.armorType           = rpgArmorTypeFromString(a.value("armorType", "Light").c_str());
        d.baseAC              = a.value("baseAC",              0);
        d.maxDexBonus         = a.value("maxDexBonus",         -1);
        d.stealthDisadvantage = a.value("stealthDisadvantage", false);
        d.strengthRequirement = a.value("strengthRequirement",  0);
    }

    return d;
}

// ---------------------------------------------------------------------------
// RpgItemRegistry
// ---------------------------------------------------------------------------

RpgItemRegistry& RpgItemRegistry::instance() {
    static RpgItemRegistry s_instance;
    return s_instance;
}

void RpgItemRegistry::registerItem(RpgItemDefinition def) {
    if (def.id.empty()) {
        LOG_WARN("RpgItemRegistry", "Cannot register item with empty ID");
        return;
    }
    m_items.emplace(def.id, std::move(def));
}

const RpgItemDefinition* RpgItemRegistry::getItem(const std::string& id) const {
    auto it = m_items.find(id);
    return (it != m_items.end()) ? &it->second : nullptr;
}

std::vector<const RpgItemDefinition*> RpgItemRegistry::getItemsOfType(RpgItemType type) const {
    std::vector<const RpgItemDefinition*> result;
    for (const auto& [id, def] : m_items)
        if (def.type == type) result.push_back(&def);
    return result;
}

std::vector<const RpgItemDefinition*> RpgItemRegistry::getItemsOfRarity(ItemRarity rarity) const {
    std::vector<const RpgItemDefinition*> result;
    for (const auto& [id, def] : m_items)
        if (def.rarity == rarity) result.push_back(&def);
    return result;
}

std::vector<const RpgItemDefinition*> RpgItemRegistry::getWeapons(WeaponCategory category) const {
    std::vector<const RpgItemDefinition*> result;
    for (const auto& [id, def] : m_items)
        if (def.isWeapon && def.weaponCategory == category) result.push_back(&def);
    return result;
}

std::vector<const RpgItemDefinition*> RpgItemRegistry::getArmor() const {
    std::vector<const RpgItemDefinition*> result;
    for (const auto& [id, def] : m_items)
        if (def.isArmor) result.push_back(&def);
    return result;
}

std::vector<const RpgItemDefinition*> RpgItemRegistry::getAllItems() const {
    std::vector<const RpgItemDefinition*> result;
    result.reserve(m_items.size());
    for (const auto& [id, def] : m_items) result.push_back(&def);
    return result;
}

int RpgItemRegistry::loadFromDirectory(const std::string& dirPath) {
    int loaded = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dirPath, ec)) {
        if (entry.path().extension() == ".json")
            if (loadFromFile(entry.path().string())) ++loaded;
    }
    if (ec) LOG_WARN("RpgItemRegistry", "Error iterating '{}': {}", dirPath, ec.message());
    return loaded;
}

bool RpgItemRegistry::loadFromFile(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        LOG_WARN("RpgItemRegistry", "Could not open file: {}", filePath);
        return false;
    }
    try {
        nlohmann::json j = nlohmann::json::parse(file);
        if (j.is_array()) {
            for (const auto& item : j) {
                try {
                    auto def = RpgItemDefinition::fromJson(item);
                    if (!def.id.empty()) registerItem(std::move(def));
                } catch (const std::exception& e) {
                    LOG_WARN("RpgItemRegistry", "Failed to parse item in '{}': {}", filePath, e.what());
                }
            }
        } else if (j.is_object()) {
            auto def = RpgItemDefinition::fromJson(j);
            if (!def.id.empty()) registerItem(std::move(def));
        }
        return true;
    } catch (const std::exception& e) {
        LOG_WARN("RpgItemRegistry", "JSON parse error in '{}': {}", filePath, e.what());
        return false;
    }
}

} // namespace Core
} // namespace Phyxel
