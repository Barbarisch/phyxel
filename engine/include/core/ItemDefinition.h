#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

// ============================================================================
// Item type categories
// ============================================================================
enum class ItemType {
    Material,       // Raw material (Stone, Wood, Metal, etc.)
    Tool,           // Tool for interacting with the world
    Weapon,         // Weapon for combat
    Consumable,     // Consumed on use (food, potions)
    Quest,          // Quest item (non-stackable, non-removable)
    Equippable      // Wearable equipment (armor, accessories)
};

// ============================================================================
// Tool sub-types (for Tool and Weapon items)
// ============================================================================
enum class ToolType {
    None,
    Pickaxe,
    Axe,
    Sword,
    Hoe,
    Shovel
};

// ============================================================================
// Equipment slots
// ============================================================================
enum class EquipSlot {
    None,
    MainHand,
    OffHand,
    Head,
    Chest,
    Legs,
    Feet
};

// ============================================================================
// ItemDefinition — static data describing an item type
// ============================================================================
struct ItemDefinition {
    std::string id;                     // Unique identifier (e.g. "iron_sword")
    std::string name;                   // Display name (e.g. "Iron Sword")
    ItemType type = ItemType::Material;
    ToolType toolType = ToolType::None;
    EquipSlot equipSlot = EquipSlot::None;

    std::string description;            // Flavor text
    bool stackable = true;              // Whether items can stack
    int maxStack = 64;                  // Maximum stack size (1 for non-stackable)

    // Combat / tool stats
    float damage = 0.0f;               // Base damage
    float speed = 1.0f;                // Attack/use speed multiplier
    int maxDurability = 0;             // 0 = indestructible
    float reach = 1.5f;               // Interaction reach distance

    // Visual
    std::string templateFile;          // Voxel model template (e.g. "weapons/sword.voxel")
    std::string attackAnimation;       // Animation clip to use (e.g. "melee_attack_horizontal")

    // Serialization
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["id"] = id;
        j["name"] = name;
        j["type"] = static_cast<int>(type);
        j["toolType"] = static_cast<int>(toolType);
        j["equipSlot"] = static_cast<int>(equipSlot);
        j["description"] = description;
        j["stackable"] = stackable;
        j["maxStack"] = maxStack;
        j["damage"] = damage;
        j["speed"] = speed;
        j["maxDurability"] = maxDurability;
        j["reach"] = reach;
        if (!templateFile.empty()) j["templateFile"] = templateFile;
        if (!attackAnimation.empty()) j["attackAnimation"] = attackAnimation;
        return j;
    }

    static ItemDefinition fromJson(const nlohmann::json& j) {
        ItemDefinition def;
        def.id = j.value("id", "");
        def.name = j.value("name", def.id);
        def.type = static_cast<ItemType>(j.value("type", 0));
        def.toolType = static_cast<ToolType>(j.value("toolType", 0));
        def.equipSlot = static_cast<EquipSlot>(j.value("equipSlot", 0));
        def.description = j.value("description", "");
        def.stackable = j.value("stackable", true);
        def.maxStack = j.value("maxStack", 64);
        def.damage = j.value("damage", 0.0f);
        def.speed = j.value("speed", 1.0f);
        def.maxDurability = j.value("maxDurability", 0);
        def.reach = j.value("reach", 1.5f);
        def.templateFile = j.value("templateFile", "");
        def.attackAnimation = j.value("attackAnimation", "");

        // Non-stackable items have maxStack=1
        if (!def.stackable) def.maxStack = 1;

        return def;
    }
};

// String conversions for enums
inline const char* itemTypeToString(ItemType type) {
    switch (type) {
        case ItemType::Material:    return "Material";
        case ItemType::Tool:        return "Tool";
        case ItemType::Weapon:      return "Weapon";
        case ItemType::Consumable:  return "Consumable";
        case ItemType::Quest:       return "Quest";
        case ItemType::Equippable:  return "Equippable";
        default:                    return "Unknown";
    }
}

inline const char* toolTypeToString(ToolType type) {
    switch (type) {
        case ToolType::None:    return "None";
        case ToolType::Pickaxe: return "Pickaxe";
        case ToolType::Axe:     return "Axe";
        case ToolType::Sword:   return "Sword";
        case ToolType::Hoe:     return "Hoe";
        case ToolType::Shovel:  return "Shovel";
        default:                return "Unknown";
    }
}

inline const char* equipSlotToString(EquipSlot slot) {
    switch (slot) {
        case EquipSlot::None:      return "None";
        case EquipSlot::MainHand:  return "MainHand";
        case EquipSlot::OffHand:   return "OffHand";
        case EquipSlot::Head:      return "Head";
        case EquipSlot::Chest:     return "Chest";
        case EquipSlot::Legs:      return "Legs";
        case EquipSlot::Feet:      return "Feet";
        default:                   return "Unknown";
    }
}

inline EquipSlot equipSlotFromString(const std::string& s) {
    if (s == "MainHand") return EquipSlot::MainHand;
    if (s == "OffHand")  return EquipSlot::OffHand;
    if (s == "Head")     return EquipSlot::Head;
    if (s == "Chest")    return EquipSlot::Chest;
    if (s == "Legs")     return EquipSlot::Legs;
    if (s == "Feet")     return EquipSlot::Feet;
    return EquipSlot::None;
}

} // namespace Core
} // namespace Phyxel
