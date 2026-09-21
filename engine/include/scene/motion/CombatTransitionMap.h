#pragma once

#include <string>
#include <vector>

namespace Phyxel::Scene::Motion {

enum class CombatPhase { Windup, Active, Recovery, Guard };
enum class CombatTransitionInput { LightAttack, HeavyAttack, Block, ReleaseBlock };

struct CombatTransitionRule {
    std::string fromClip; // empty means wildcard
    CombatTransitionInput input = CombatTransitionInput::LightAttack;
    CombatPhase earliestPhase = CombatPhase::Recovery;
    std::string toClip;
};

struct CombatClipTiming {
    float activeStartFraction = 0.45f;
    float activeEndFraction = 0.60f;
};

CombatPhase combatPhaseAt(float normalizedTime, const CombatClipTiming& timing,
                          bool guarding = false);

/// Deterministic authored transition graph. Motion providers may help bridge
/// poses in future, but this graph remains authoritative for hit/block timing.
class CombatTransitionMap {
public:
    explicit CombatTransitionMap(std::vector<CombatTransitionRule> rules = {});
    bool valid() const { return m_error.empty(); }
    const std::string& error() const { return m_error; }
    const CombatTransitionRule* resolve(const std::string& fromClip,
                                        CombatTransitionInput input,
                                        CombatPhase phase) const;

private:
    std::vector<CombatTransitionRule> m_rules;
    std::string m_error;
};

} // namespace Phyxel::Scene::Motion
