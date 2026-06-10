#pragma once

#include "core/SpellDefinition.h"

#include <functional>
#include <string>
#include <vector>

namespace Phyxel {
namespace Core {

// ============================================================================
// SpellAnimMapper — SpellDefinition -> casting-animation plan.
//
// Maps a spell to an animation FAMILY (bolt / thrust / call_down / touch /
// ward / ritual) and produces the concrete clip segments, playback speeds,
// and loop counts that realize the spell's casting time (plus a caster-skill
// rate modifier). Data-driven from
//   resources/spells/anim/spell_anim_families.json
// This is the C++ port of tools/anim_pipeline/spell_anim_resolver.py — keep
// the two implementations in sync.
//
// The release moment (when VFX/projectile fires) is NOT in the plan: it is
// authored per-clip as "releaseFrame"/"hitFrameFraction" clip_meta in the
// .anim file and read from the final clip at cast time
// (AnimatedVoxelCharacter::castSpell).
// ============================================================================

struct CastAnimSegment {
    std::string clip;
    float speed = 1.0f;   // playback-rate multiplier (1 = authored speed)
    int   loops = 1;      // consecutive plays of this clip (ritual channel)
};

struct CastAnimPlan {
    std::string family;
    std::vector<CastAnimSegment> segments;
    float totalSeconds = 0.0f;   // estimated on-screen cast duration
    bool  valid = false;
};

class SpellAnimMapper {
public:
    static SpellAnimMapper& instance();

    /// Load (or reload) the family config. Returns false on missing file /
    /// parse error (mapper stays unloaded; resolve() returns invalid plans).
    bool loadConfig(const std::string& jsonPath);
    bool isLoaded() const { return m_loaded; }

    /// Family id for a spell: spellOverrides first, then top-down first
    /// matching rule. Returns "thrust" if nothing matches.
    std::string resolveFamily(const SpellDefinition& spell) const;

    /// Full plan. clipDuration(name) returns the clip's authored duration in
    /// seconds (<= 0 if unknown; such segments assume 1s for the rate math).
    CastAnimPlan resolve(const SpellDefinition& spell,
                         int proficiencyBonus,
                         const std::function<float(const std::string&)>& clipDuration) const;

private:
    SpellAnimMapper() = default;
    bool ruleMatches(const nlohmann::json& cond, const SpellDefinition& spell) const;

    nlohmann::json m_cfg;
    bool m_loaded = false;
};

} // namespace Core
} // namespace Phyxel
