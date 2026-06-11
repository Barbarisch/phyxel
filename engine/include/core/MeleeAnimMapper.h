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
class MeleeAnimMapper {
public:
    static MeleeAnimMapper& instance();

    /// Load (or reload) the family config. Returns false on missing/bad file.
    bool loadConfig(const std::string& jsonPath);
    bool isLoaded() const { return m_loaded; }

    /// Family for a held item (nullptr = unarmed).
    std::string resolveFamily(const ItemDefinition* item) const;

    /// Attack clip cycle for a family (empty if unknown family/config).
    std::vector<std::string> familyAttacks(const std::string& family) const;

    /// Block/guard clip for a family ("" if none).
    std::string familyBlock(const std::string& family) const;

private:
    MeleeAnimMapper() = default;

    nlohmann::json m_cfg;
    bool m_loaded = false;
};

} // namespace Core
} // namespace Phyxel
