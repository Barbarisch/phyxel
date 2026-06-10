#include "core/SpellAnimMapper.h"
#include "utils/Logger.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace Phyxel {
namespace Core {

SpellAnimMapper& SpellAnimMapper::instance() {
    static SpellAnimMapper inst;
    return inst;
}

bool SpellAnimMapper::loadConfig(const std::string& jsonPath) {
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        LOG_WARN("SpellAnimMapper", "Could not open config: {}", jsonPath);
        m_loaded = false;
        return false;
    }
    try {
        m_cfg = nlohmann::json::parse(file);
        if (!m_cfg.contains("families") || !m_cfg.contains("rules")) {
            LOG_WARN("SpellAnimMapper", "Config missing 'families'/'rules': {}", jsonPath);
            m_loaded = false;
            return false;
        }
        m_loaded = true;
        LOG_INFO("SpellAnimMapper", "Loaded {} families, {} rules from {}",
                 m_cfg["families"].size(), m_cfg["rules"].size(), jsonPath);
        return true;
    } catch (const std::exception& e) {
        LOG_WARN("SpellAnimMapper", "JSON parse error in '{}': {}", jsonPath, e.what());
        m_loaded = false;
        return false;
    }
}

bool SpellAnimMapper::ruleMatches(const nlohmann::json& cond, const SpellDefinition& spell) const {
    const std::string ct = castingTimeName(spell.castingTime);
    if (cond.contains("castingTimeIn")) {
        bool found = false;
        for (const auto& v : cond["castingTimeIn"])
            if (v.get<std::string>() == ct) { found = true; break; }
        if (!found) return false;
    }
    if (cond.contains("castingTime") && cond["castingTime"].get<std::string>() != ct)
        return false;
    if (cond.contains("isTouch") && cond["isTouch"].get<bool>() != spell.isTouch)
        return false;
    if (cond.contains("isSelf") && cond["isSelf"].get<bool>() != spell.isSelf)
        return false;
    if (cond.contains("resolutionType") &&
        cond["resolutionType"].get<std::string>() != spellResolutionTypeName(spell.resolutionType))
        return false;
    if (cond.contains("minRangeFeet") && spell.rangeInFeet < cond["minRangeFeet"].get<int>())
        return false;
    if (cond.contains("heals") && cond["heals"].get<bool>() != spell.hasHeal())
        return false;
    return true;
}

std::string SpellAnimMapper::resolveFamily(const SpellDefinition& spell) const {
    if (!m_loaded) return "thrust";
    if (m_cfg.contains("spellOverrides")) {
        auto it = m_cfg["spellOverrides"].find(spell.id);
        if (it != m_cfg["spellOverrides"].end()) return it->get<std::string>();
    }
    for (const auto& rule : m_cfg["rules"]) {
        if (ruleMatches(rule.value("if", nlohmann::json::object()), spell))
            return rule.value("family", "thrust");
    }
    return "thrust";
}

CastAnimPlan SpellAnimMapper::resolve(
        const SpellDefinition& spell,
        int proficiencyBonus,
        const std::function<float(const std::string&)>& clipDuration) const {
    CastAnimPlan plan;
    if (!m_loaded) return plan;

    plan.family = resolveFamily(spell);
    auto famIt = m_cfg["families"].find(plan.family);
    if (famIt == m_cfg["families"].end()) {
        LOG_WARN("SpellAnimMapper", "Unknown family '{}' for spell '{}'", plan.family, spell.id);
        return plan;
    }
    const nlohmann::json& fam = *famIt;

    float target = 1.6f;
    if (m_cfg.contains("castingTimeTargets")) {
        target = m_cfg["castingTimeTargets"].value(
            std::string(castingTimeName(spell.castingTime)), 1.6f);
    }
    float lo = 0.7f, hi = 1.4f;
    if (m_cfg.contains("playbackRateRange") && m_cfg["playbackRateRange"].size() == 2) {
        lo = m_cfg["playbackRateRange"][0].get<float>();
        hi = m_cfg["playbackRateRange"][1].get<float>();
    }
    const float skill = 1.0f + m_cfg.value("skillRatePerProficiency", 0.0f)
                              * float(proficiencyBonus - 2);

    auto dur = [&](const std::string& clip) {
        float d = clipDuration ? clipDuration(clip) : 0.0f;
        return d > 0.0f ? d : 1.0f;
    };

    if (fam.contains("loop")) {
        // Ritual: fixed-speed bookends, loop count fills the target duration.
        const std::string windup  = fam.value("windup", "");
        const std::string loop    = fam.value("loop", "");
        const std::string release = fam.value("release", "");
        const float windupD = dur(windup), loopD = dur(loop), releaseD = dur(release);
        const float speed = std::clamp(skill, lo, hi);
        int loops = (int)std::lround((target - (windupD + releaseD) / speed) / (loopD / speed));
        loops = std::max(1, loops);
        plan.segments = {
            {windup, speed, 1},
            {loop, speed, loops},
            {release, speed, 1},
        };
        plan.totalSeconds = (windupD + loops * loopD + releaseD) / speed;
    } else {
        const std::string clip = fam.value("cast", "");
        const float d = dur(clip);
        const float speed = std::clamp((d / target) * skill, lo, hi);
        plan.segments = {{clip, speed, 1}};
        plan.totalSeconds = d / speed;
    }
    plan.valid = !plan.segments.empty() && !plan.segments.front().clip.empty();
    return plan;
}

} // namespace Core
} // namespace Phyxel
