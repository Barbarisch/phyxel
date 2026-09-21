#include "scene/motion/CombatTransitionMap.h"

#include <algorithm>
#include <tuple>

namespace Phyxel::Scene::Motion {

CombatPhase combatPhaseAt(float normalizedTime, const CombatClipTiming& timing,
                          bool guarding) {
    if (guarding) return CombatPhase::Guard;
    const float time = std::clamp(normalizedTime, 0.0f, 1.0f);
    if (time < timing.activeStartFraction) return CombatPhase::Windup;
    if (time <= timing.activeEndFraction) return CombatPhase::Active;
    return CombatPhase::Recovery;
}

CombatTransitionMap::CombatTransitionMap(std::vector<CombatTransitionRule> rules)
    : m_rules(std::move(rules)) {
    for (const auto& rule : m_rules) {
        if (rule.toClip.empty()) {
            m_error = "combat transition target clip may not be empty";
            return;
        }
    }
    for (std::size_t i = 0; i < m_rules.size(); ++i) {
        for (std::size_t j = i + 1; j < m_rules.size(); ++j) {
            if (std::tie(m_rules[i].fromClip, m_rules[i].input, m_rules[i].earliestPhase) ==
                std::tie(m_rules[j].fromClip, m_rules[j].input, m_rules[j].earliestPhase)) {
                m_error = "ambiguous combat transition rule";
                return;
            }
        }
    }
}

const CombatTransitionRule* CombatTransitionMap::resolve(
    const std::string& fromClip, CombatTransitionInput input, CombatPhase phase) const {
    if (!valid()) return nullptr;
    const CombatTransitionRule* wildcard = nullptr;
    for (const auto& rule : m_rules) {
        if (rule.input != input || static_cast<int>(phase) < static_cast<int>(rule.earliestPhase))
            continue;
        if (rule.fromClip == fromClip) return &rule;
        if (rule.fromClip.empty()) wildcard = &rule;
    }
    return wildcard;
}

} // namespace Phyxel::Scene::Motion
