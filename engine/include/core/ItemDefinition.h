#pragma once

#include <string>
#include <glm/glm.hpp>
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
// HeldItemInfo — how an item sits in a character's hand
// ============================================================================
// Authored per-item in items.json under "held". gripOffset/gripEulerDeg place
// the item's template origin relative to the grip bone (hand-tuned; world
// units / degrees, intrinsic XYZ). light turns the held item into a light
// source (torch): radius <= 0 means no light.
struct HeldItemInfo {
    std::string gripBone = "RightHand";  // short bone name; resolved with mixamorig: aliasing
    glm::vec3 gripOffset{0.0f};
    glm::vec3 gripEulerDeg{0.0f};
    float scale = 1.0f;                  // uniform scale applied to the held template

    // Optional held light (torch, lantern)
    glm::vec3 lightColor{1.0f, 0.8f, 0.5f};
    float lightIntensity = 0.0f;         // 0 = no light
    float lightRadius = 0.0f;

    bool hasLight() const { return lightIntensity > 0.0f && lightRadius > 0.0f; }

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["gripBone"] = gripBone;
        j["gripOffset"] = {gripOffset.x, gripOffset.y, gripOffset.z};
        j["gripEulerDeg"] = {gripEulerDeg.x, gripEulerDeg.y, gripEulerDeg.z};
        j["scale"] = scale;
        if (hasLight()) {
            j["light"] = {
                {"color", {lightColor.r, lightColor.g, lightColor.b}},
                {"intensity", lightIntensity},
                {"radius", lightRadius},
            };
        }
        return j;
    }

    static HeldItemInfo fromJson(const nlohmann::json& j) {
        HeldItemInfo h;
        h.gripBone = j.value("gripBone", std::string("RightHand"));
        auto vec3 = [&](const char* key, glm::vec3 def) {
            if (j.contains(key) && j[key].is_array() && j[key].size() == 3)
                return glm::vec3(j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>());
            return def;
        };
        h.gripOffset = vec3("gripOffset", h.gripOffset);
        h.gripEulerDeg = vec3("gripEulerDeg", h.gripEulerDeg);
        h.scale = j.value("scale", 1.0f);
        if (j.contains("light")) {
            const auto& l = j["light"];
            if (l.contains("color") && l["color"].is_array() && l["color"].size() == 3)
                h.lightColor = glm::vec3(l["color"][0].get<float>(), l["color"][1].get<float>(), l["color"][2].get<float>());
            h.lightIntensity = l.value("intensity", 1.0f);
            h.lightRadius = l.value("radius", 8.0f);
        }
        return h;
    }
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

    // Holdable: the item can exist as a world prop and be held in a hand.
    // Defaults true when a templateFile is present (see fromJson).
    bool holdable = false;
    HeldItemInfo held;

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
        j["holdable"] = holdable;
        if (holdable) j["held"] = held.toJson();
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

        // Holdable defaults to "has a voxel model"; authors can override.
        def.holdable = j.value("holdable", !def.templateFile.empty());
        if (j.contains("held")) def.held = HeldItemInfo::fromJson(j["held"]);

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
