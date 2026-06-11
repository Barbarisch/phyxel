#pragma once

#include <string>
#include <vector>
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
// ItemEffectDef — a declarative effect on an item (particles and/or light),
// active in BOTH item states (world prop and held-in-hand) unless filtered.
// ============================================================================
// Authored per-item in items.json under "effects": [...]. Examples:
//   torch flame:      vfx + light, no condition (always on)
//   enchanted blade:  blue aura vfx + light, when {"nearby": {"radius": 8}}
// anchor is template-local (the held/prop transform already carries scale).
struct ItemEffectDef {
    std::string id;                     // unique within the item
    glm::vec3 anchor{0.5f, 0.5f, 0.5f}; // template-local emit point

    // -- particle bursts (VfxSystem::spawnBurst on a rate timer) --
    bool  hasVfx = false;
    glm::vec3 vfxColor{1.0f, 0.55f, 0.15f};
    float vfxRate      = 8.0f;   // bursts per second
    int   vfxCount     = 3;      // particles per burst
    float vfxSize      = 0.05f;
    float vfxSpeed     = 0.5f;
    float vfxUpBias    = 1.0f;   // 1 = straight up (flame), 0 = sphere
    float vfxGravity   = 0.5f;   // positive = buoyant rise (flame); negative = fall
    float vfxLifetime  = 0.5f;
    float vfxIntensity = 2.5f;   // emissive boost
    glm::vec3 vfxJitter{0.05f, 0.02f, 0.05f};

    // -- point light --
    bool  hasLight = false;
    glm::vec3 lightColor{1.0f, 0.8f, 0.5f};
    float lightIntensity = 2.0f;
    float lightRadius    = 8.0f;

    // -- condition ("when"); all absent = always active --
    // state: 0 = any, 1 = held only, 2 = prop only
    int   whenState = 0;
    bool  hasNearby = false;     // active only when a matching entity is near
    std::string nearbyName;      // substring match on entity id/name ("" = any)
    std::string nearbyType = "npc"; // entity type tag for the registry query
    float nearbyRadius = 8.0f;

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["id"] = id;
        j["anchor"] = {anchor.x, anchor.y, anchor.z};
        if (hasVfx) {
            j["vfx"] = {
                {"color", {vfxColor.r, vfxColor.g, vfxColor.b}},
                {"rate", vfxRate}, {"count", vfxCount}, {"size", vfxSize},
                {"speed", vfxSpeed}, {"upBias", vfxUpBias}, {"gravity", vfxGravity},
                {"lifetime", vfxLifetime}, {"intensity", vfxIntensity},
                {"posJitter", {vfxJitter.x, vfxJitter.y, vfxJitter.z}},
            };
        }
        if (hasLight) {
            j["light"] = {
                {"color", {lightColor.r, lightColor.g, lightColor.b}},
                {"intensity", lightIntensity}, {"radius", lightRadius},
            };
        }
        nlohmann::json when = nlohmann::json::object();
        if (whenState == 1) when["state"] = "held";
        if (whenState == 2) when["state"] = "prop";
        if (hasNearby) {
            when["nearby"] = {{"radius", nearbyRadius}, {"type", nearbyType}};
            if (!nearbyName.empty()) when["nearby"]["name"] = nearbyName;
        }
        if (!when.empty()) j["when"] = when;
        return j;
    }

    static ItemEffectDef fromJson(const nlohmann::json& j) {
        ItemEffectDef e;
        e.id = j.value("id", "effect");
        auto vec3 = [](const nlohmann::json& src, const char* key, glm::vec3 def) {
            if (src.contains(key) && src[key].is_array() && src[key].size() == 3)
                return glm::vec3(src[key][0].get<float>(), src[key][1].get<float>(), src[key][2].get<float>());
            return def;
        };
        e.anchor = vec3(j, "anchor", e.anchor);
        if (j.contains("vfx")) {
            const auto& v = j["vfx"];
            e.hasVfx = true;
            e.vfxColor     = vec3(v, "color", e.vfxColor);
            e.vfxRate      = v.value("rate", e.vfxRate);
            e.vfxCount     = v.value("count", e.vfxCount);
            e.vfxSize      = v.value("size", e.vfxSize);
            e.vfxSpeed     = v.value("speed", e.vfxSpeed);
            e.vfxUpBias    = v.value("upBias", e.vfxUpBias);
            e.vfxGravity   = v.value("gravity", e.vfxGravity);
            e.vfxLifetime  = v.value("lifetime", e.vfxLifetime);
            e.vfxIntensity = v.value("intensity", e.vfxIntensity);
            e.vfxJitter    = vec3(v, "posJitter", e.vfxJitter);
        }
        if (j.contains("light")) {
            const auto& l = j["light"];
            e.hasLight = true;
            e.lightColor     = vec3(l, "color", e.lightColor);
            e.lightIntensity = l.value("intensity", e.lightIntensity);
            e.lightRadius    = l.value("radius", e.lightRadius);
        }
        if (j.contains("when")) {
            const auto& w = j["when"];
            std::string state = w.value("state", "any");
            e.whenState = (state == "held") ? 1 : (state == "prop") ? 2 : 0;
            if (w.contains("nearby")) {
                const auto& n = w["nearby"];
                e.hasNearby = true;
                e.nearbyRadius = n.value("radius", e.nearbyRadius);
                e.nearbyType = n.value("type", e.nearbyType);
                e.nearbyName = n.value("name", e.nearbyName);
            }
        }
        return e;
    }
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
    std::string weaponFamily;          // Explicit melee animation family ("slash_1h", ...).
                                       // Empty = resolve via rpg item rules / toolType heuristic.

    // Holdable: the item can exist as a world prop and be held in a hand.
    // Defaults true when a templateFile is present (see fromJson).
    bool holdable = false;
    HeldItemInfo held;

    // Declarative effects (particles/lights), active in both item states.
    std::vector<ItemEffectDef> effects;

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
        if (!weaponFamily.empty()) j["weaponFamily"] = weaponFamily;
        j["holdable"] = holdable;
        if (holdable) j["held"] = held.toJson();
        if (!effects.empty()) {
            j["effects"] = nlohmann::json::array();
            for (const auto& e : effects) j["effects"].push_back(e.toJson());
        }
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
        def.weaponFamily = j.value("weaponFamily", "");

        // Holdable defaults to "has a voxel model"; authors can override.
        def.holdable = j.value("holdable", !def.templateFile.empty());
        if (j.contains("held")) def.held = HeldItemInfo::fromJson(j["held"]);

        if (j.contains("effects") && j["effects"].is_array()) {
            for (const auto& ej : j["effects"])
                def.effects.push_back(ItemEffectDef::fromJson(ej));
        }
        // Legacy migration: held.light (pre-effects) becomes an always-on
        // light effect so there is a single runtime path for item lights.
        if (def.effects.empty() && def.held.hasLight()) {
            ItemEffectDef e;
            e.id = "held_light_legacy";
            e.hasLight = true;
            e.lightColor = def.held.lightColor;
            e.lightIntensity = def.held.lightIntensity;
            e.lightRadius = def.held.lightRadius;
            def.effects.push_back(e);
        }

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
