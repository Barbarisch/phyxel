#include "core/MeleeAnimMapper.h"

#include "core/RpgItem.h"
#include "utils/Logger.h"

#include <fstream>

namespace Phyxel {
namespace Core {

MeleeAnimMapper& MeleeAnimMapper::instance() {
    static MeleeAnimMapper inst;
    return inst;
}

bool MeleeAnimMapper::loadConfig(const std::string& jsonPath) {
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        LOG_WARN("MeleeAnimMapper", "Could not open config: {}", jsonPath);
        m_loaded = false;
        return false;
    }
    try {
        m_cfg = nlohmann::json::parse(file);
        if (!m_cfg.contains("families")) {
            LOG_WARN("MeleeAnimMapper", "Config missing 'families': {}", jsonPath);
            m_loaded = false;
            return false;
        }
        m_loaded = true;
        LOG_INFO("MeleeAnimMapper", "Loaded {} families from {}",
                 m_cfg["families"].size(), jsonPath);
        return true;
    } catch (const std::exception& e) {
        LOG_WARN("MeleeAnimMapper", "JSON parse error in '{}': {}", jsonPath, e.what());
        m_loaded = false;
        return false;
    }
}

std::string MeleeAnimMapper::resolveFamily(const ItemDefinition* item) const {
    const std::string unarmed = m_loaded ? m_cfg.value("unarmedFamily", "unarmed") : "unarmed";
    if (!item) return unarmed;

    // 1. Explicit per-item override.
    if (!item->weaponFamily.empty()) return item->weaponFamily;

    if (m_loaded) {
        // 2. D&D weapon entry with the same id -> config overrides + rules.
        if (m_cfg.contains("weaponOverrides")) {
            auto it = m_cfg["weaponOverrides"].find(item->id);
            if (it != m_cfg["weaponOverrides"].end()) return it->get<std::string>();
        }
        if (const auto* rpg = RpgItemRegistry::instance().getItem(item->id)) {
            if (rpg->isWeapon && m_cfg.contains("rules")) {
                auto hasProp = [&](WeaponProperty p) {
                    return rpg->weaponProperties.count(p) > 0;
                };
                for (const auto& rule : m_cfg["rules"]) {
                    const auto& cond = rule.value("if", nlohmann::json::object());
                    if (cond.contains("hasProperty")) {
                        const std::string p = cond["hasProperty"].get<std::string>();
                        bool match =
                            (p == "Ammunition" && hasProp(WeaponProperty::Ammunition)) ||
                            (p == "Reach"      && hasProp(WeaponProperty::Reach)) ||
                            (p == "TwoHanded"  && hasProp(WeaponProperty::TwoHanded)) ||
                            (p == "Heavy"      && hasProp(WeaponProperty::Heavy));
                        if (!match) continue;
                    }
                    if (cond.contains("damageType") &&
                        cond["damageType"].get<std::string>() !=
                            damageTypeToString(rpg->weaponDamageType))
                        continue;
                    return rule.value("family", "slash_1h");
                }
            }
        }
    }

    // 3. ToolType heuristic for gameplay items without D&D data.
    switch (item->toolType) {
        case ToolType::Sword:
        case ToolType::Axe:
        case ToolType::Pickaxe:
        case ToolType::Hoe:
        case ToolType::Shovel:
            return "slash_1h";
        default:
            break;
    }
    if (item->type == ItemType::Weapon) return "slash_1h";

    return unarmed;
}

std::vector<std::string> MeleeAnimMapper::familyAttacks(const std::string& family) const {
    std::vector<std::string> out;
    if (!m_loaded) return out;
    auto it = m_cfg["families"].find(family);
    if (it == m_cfg["families"].end()) return out;
    for (const auto& clip : it->value("attacks", nlohmann::json::array()))
        out.push_back(clip.get<std::string>());
    return out;
}

std::string MeleeAnimMapper::familyBlock(const std::string& family) const {
    if (!m_loaded) return "";
    auto it = m_cfg["families"].find(family);
    if (it == m_cfg["families"].end()) return "";
    return it->value("block", "");
}

MeleeMovesetDef MeleeAnimMapper::resolveMovesetDef(const ItemDefinition* item) const {
    MeleeMovesetDef def;
    def.family = resolveFamily(item);
    if (!m_loaded) return def;

    auto famIt = m_cfg["families"].find(def.family);
    if (famIt == m_cfg["families"].end()) return def;
    const nlohmann::json& fam = *famIt;

    for (const auto& clip : fam.value("attacks", nlohmann::json::array()))
        def.lightChain.push_back(clip.get<std::string>());
    def.heavy = fam.value("heavy", "");
    def.block = fam.value("block", "");
    def.blockHoldFrac = fam.value("blockHold", 0.5f);

    const std::string speedClass = fam.value("speedClass", "standard");
    if (m_cfg.contains("speedClasses")) {
        auto scIt = m_cfg["speedClasses"].find(speedClass);
        if (scIt != m_cfg["speedClasses"].end()) {
            def.attackRate      = scIt->value("rate", 1.0f);
            def.chainWindowFrac = scIt->value("chainWindow", 0.35f);
        }
    }
    return def;
}

} // namespace Core
} // namespace Phyxel
