#pragma once

#include "scene/NPCBehavior.h"

#include <functional>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace Phyxel {
namespace Scene {

class AnimatedVoxelCharacter;

/// REAL-TIME spellcaster — the ranged counterpart to CombatBehavior.
///
/// Not the turn-based CombatAISystem cast branch: there are no turns, no
/// initiative and no spell slots here. A caster in a live battle holds its
/// preferred range, throws a spell whenever its cooldown is up, and backs off
/// when something closes on it. Cooldowns replace slots because a slot economy
/// is meaningless without turns — a 20v20 skirmish needs sustained fire, not
/// two nova rounds and then a rock.
///
/// Faction-aware through Entity::hostileTo, so it holds a line in team battles.
/// Damage and the cast visual are delegated to the host (the same
/// SpellRegistry / VfxDirector path the player and the turn-based AI use), so
/// spells look and hit identically no matter who casts them.
class RangedCasterBehavior : public NPCBehavior {
public:
    void update(float dt, NPCContext& ctx) override;
    void onInteract(Entity* /*interactor*/) override {}
    void onEvent(const std::string& /*eventType*/, const nlohmann::json& /*data*/) override {}
    std::string getBehaviorName() const override { return "RangedCaster"; }

    // ── Loadout ─────────────────────────────────────────────────
    /// Spells cycled through, in order. Ids resolve in the SpellRegistry.
    void setSpells(std::vector<std::string> spells) { m_spells = std::move(spells); }
    const std::vector<std::string>& spells() const { return m_spells; }

    /// Team tag; mirrored onto the owning entity so others can read it.
    void setFaction(const std::string& f) { m_faction = f; }

    // ── Tuning ──────────────────────────────────────────────────
    void setAggroRange(float r)     { m_aggroRange = r; }
    void setPreferredRange(float r) { m_preferredRange = r; }
    void setCastCooldown(float s)   { m_castCooldown = s; }
    void setMoveSpeed(float s)      { m_moveSpeed = s; }
    void setDamage(float d)         { m_damage = d; }

    /// Host hook: play the cast animation + VFX and apply damage at the
    /// release frame. (casterId, spellId, targetId, targetPos, damage).
    /// Without it the behavior still fights — damage applies immediately.
    using CastHook = std::function<void(const std::string& casterId,
                                        const std::string& spellId,
                                        const std::string& targetId,
                                        const glm::vec3& targetPos,
                                        float damage)>;
    void setCastHook(CastHook hook) { m_castHook = std::move(hook); }

    const std::string& targetId() const { return m_targetId; }
    const char* stateName() const;

private:
    enum class State { Seek, Close, Hold, Retreat };

    std::string acquireTarget(NPCContext& ctx, const glm::vec3& selfPos) const;

    std::vector<std::string> m_spells;
    std::string m_faction;
    std::string m_targetId;
    CastHook    m_castHook;

    State m_state = State::Seek;
    float m_aggroRange     = 30.0f;  ///< will engage anything this close
    float m_preferredRange = 12.0f;  ///< stands off at this distance
    float m_castCooldown   = 2.5f;   ///< seconds between casts
    float m_moveSpeed      = 0.8f;   ///< control-input magnitude
    float m_damage         = 6.0f;   ///< per cast (host may override per spell)
    float m_cooldownTimer  = 0.0f;
    size_t m_nextSpell     = 0;
    bool  m_publishedFaction = false;
};

} // namespace Scene
} // namespace Phyxel
