#pragma once

#include "core/VfxSystem.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>

namespace Phyxel {

// ============================================================================
// VfxDirector — the spell composition runtime (Layer 2 execution).
//
// A spell is a flat list of "emission specs", each naming a generic VFX
// archetype + its (already gameplay-resolved) params + an anchor + a trigger.
// The director fires on_cast emissions immediately, then drives the rest from
// VfxSystem lifecycle events (impact/expire/tick) and delay timers. VFX stays
// dumb; the director owns sequencing, anchors, and count/multi-target fan-out.
// See [[project-spell-vfx]].
// ============================================================================

enum class VfxArchetypeKind { Burst, Projectile, Beam, Field };

// Where an emission's origin/target position comes from.
enum class VfxAnchorKind {
    Caster,    // the cast context's caster position (or casterProvider, for follow)
    Target,    // context target[targetIndex] (or per-target loop index)
    ImpactOf,  // the position of the event that triggered this emission
    Fixed      // a literal point baked into the spec
};

// When an emission fires.
enum class VfxTriggerKind {
    OnCast,    // immediately when the spell is cast
    OnImpact,  // when emission #triggerRef reports an Impact event
    OnExpire,  // when emission #triggerRef expires
    OnTick,    // each time emission #triggerRef ticks
    OnDelay    // `delay` seconds after cast
};

struct VfxEmissionSpec {
    VfxArchetypeKind archetype = VfxArchetypeKind::Burst;
    VfxTriggerKind   trigger   = VfxTriggerKind::OnCast;
    int   triggerRef = -1;     // emission index whose event triggers this (impact/expire/tick)
    float delay = 0.0f;        // seconds, for OnDelay
    int   count = 1;           // emit N copies (e.g. a fan)
    bool  perTarget = false;   // fire once per context target instead of `count`

    VfxAnchorKind originAnchor = VfxAnchorKind::Caster;
    VfxAnchorKind targetAnchor = VfxAnchorKind::Target;
    int   targetIndex = 0;     // which target for Target anchor (when not perTarget)
    glm::vec3 fixedOrigin{0.0f};
    glm::vec3 fixedTarget{0.0f};

    // Only the param block matching `archetype` is used.
    VfxBurstParams      burst;
    VfxProjectileParams projectile;
    VfxBeamParams       beam;
    VfxFieldParams      field;
};

// A spell template: name + its emission list.
struct VfxComposition {
    std::string name;
    std::vector<VfxEmissionSpec> emissions;
};

// Per-cast inputs resolved by gameplay (Layer 3) before casting.
struct VfxCastContext {
    glm::vec3 caster{0.0f};
    std::vector<glm::vec3> targets;
    // Optional live position providers for follow-anchoring (auras/tethers).
    VfxPositionProvider casterProvider;
    std::vector<VfxPositionProvider> targetProviders;
};

class VfxDirector {
public:
    explicit VfxDirector(VfxSystem* vfx) : m_vfx(vfx) {}

    // Cast a composition; returns a spell-instance id (usable with dismiss()).
    std::string cast(const VfxComposition& comp, const VfxCastContext& ctx);

    // Tick once per frame: advances delay timers and drains VfxSystem events
    // to fire triggered emissions. Call before VfxSystem::update().
    void update(float dt);

    // End a spell instance early — dismisses all its persistent (until-dismissed) effects.
    void dismiss(const std::string& spellInstanceId);

    size_t activeCount() const { return m_instances.size(); }

private:
    struct LiveEmission {
        bool  fired = false;
        bool  pending = true;      // still expected to fire (false once fired, unless OnTick)
        float delayRemaining = 0.0f;
    };

    struct SpellInstance {
        std::string id;
        VfxComposition comp;
        VfxCastContext ctx;
        std::vector<LiveEmission> live;
        std::vector<std::string> spawnedIds; // effect ids we own (for cleanup / dismiss)
        float age = 0.0f;
        bool  persistent = false;  // has OnTick or until-dismissed emissions
    };

    void fireEmission(SpellInstance& s, int emissionIdx, const glm::vec3& impactPos);
    glm::vec3 resolveAnchor(SpellInstance& s, VfxAnchorKind kind, int targetIdx,
                            const glm::vec3& fixed, const glm::vec3& impactPos) const;
    bool instanceDone(const SpellInstance& s) const;

    VfxSystem* m_vfx;
    std::vector<std::unique_ptr<SpellInstance>> m_instances;
    std::unordered_map<std::string, std::pair<SpellInstance*, int>> m_owner; // effectId -> (instance, emissionIdx)
    uint32_t m_counter = 0;
};

// Test-spell factory: returns a composition exercising specific dimensions.
// Names: t_firebolt, t_conecold, t_thunderwave, t_magicmissile, t_lightningbolt,
// t_walloffire, t_moonbeam, t_spiritguardians, t_delayedblast, t_chainlightning,
// t_thunderstep. Unknown name returns a simple sphere-burst spell.
VfxComposition buildTestSpell(const std::string& name);

} // namespace Phyxel
