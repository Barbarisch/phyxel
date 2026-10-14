#pragma once

#include "core/ItemDefinition.h"

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

// ============================================================================
// MeleeAnimMapper — held weapon -> melee animation family -> attack clips.
//
// Data-driven from resources/rpg_items/anim/melee_anim_families.json (rules
// over D&D weapon damageType/properties + per-weapon overrides). For gameplay
// items (items.json) the family resolves through a chain:
//   1. explicit ItemDefinition::weaponFamily ("slash_1h", ...)
//   2. RpgItemRegistry entry with the same id -> config rules
//   3. ToolType heuristic (Sword/Axe/Pickaxe/... -> slash_1h)
//   4. unarmed
// The clip-side metadata (meleeFamily/meleeRole/hitFrameFraction) lives in
// humanoid.anim clip_meta; this class only picks WHICH clips to cycle.
// ============================================================================
/// A fully-resolved moveset: what the character FSM needs for souls-style
/// melee (light chain + heavy + held block + speed-class timing).
struct MeleeMovesetDef {
    std::string family;
    std::vector<std::string> lightChain;
    std::string heavy;
    std::string block;
    float attackRate      = 1.0f;
    float chainWindowFrac = 0.35f;
    float blockHoldFrac   = 0.5f;
};

class MeleeAnimMapper {
public:
    static MeleeAnimMapper& instance();

    /// Load (or reload) the family config. Returns false on missing/bad file.
    bool loadConfig(const std::string& jsonPath);
    bool isLoaded() const { return m_loaded; }

    /// Family for a held item (nullptr = unarmed).
    std::string resolveFamily(const ItemDefinition* item) const;

    /// A3 composition factors DERIVED from the equipment — never hand-authored per item
    /// (docs/AnimationSystemV3Plan.md §4 A3 item 3). Values are clip_meta schema enums:
    ///   grip: empty | 1h | 1h_shield | 2h_light | 2h_heavy | bow | staff | torch
    ///         torch = held light or id contains "torch"; bow = Ammunition; staff = id contains "staff";
    ///         Heavy+TwoHanded → 2h_heavy, TwoHanded → 2h_light, Heavy alone → 2h_heavy;
    ///         else 1h, and 1h_shield when the off-hand holds a Shield (RpgArmorType::Shield).
    ///   load: none | light | heavy | bulky from the main hand's D&D weightLbs:
    ///         < 2 none, < 6 light, < 15 heavy, else bulky (no RPG entry → none).
    struct GripFactors {
        std::string grip = "empty";
        std::string load = "none";
    };
    GripFactors resolveGripFactors(const ItemDefinition* mainHand,
                                   const ItemDefinition* offHand = nullptr) const;

    /// Attack clip cycle for a family (empty if unknown family/config).
    std::vector<std::string> familyAttacks(const std::string& family) const;

    /// Block/guard clip for a family ("" if none).
    std::string familyBlock(const std::string& family) const;

    /// Full moveset for a held item: family clips + speed-class timing
    /// (rate / chain window) from the config's "speedClasses" table.
    MeleeMovesetDef resolveMovesetDef(const ItemDefinition* item) const;

private:
    MeleeAnimMapper() = default;

    nlohmann::json m_cfg;
    bool m_loaded = false;
};

} // namespace Core
} // namespace Phyxel
