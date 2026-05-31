#include "core/VfxDirector.h"
#include "utils/Logger.h"
#include <algorithm>

namespace Phyxel {

std::string VfxDirector::cast(const VfxComposition& comp, const VfxCastContext& ctx) {
    auto inst = std::make_unique<SpellInstance>();
    inst->id = "spell_" + std::to_string(++m_counter);
    inst->comp = comp;
    inst->ctx = ctx;
    inst->live.resize(comp.emissions.size());

    for (size_t i = 0; i < comp.emissions.size(); ++i) {
        const VfxEmissionSpec& e = comp.emissions[i];
        inst->live[i].delayRemaining = e.delay;
        if (e.trigger == VfxTriggerKind::OnTick) inst->persistent = true;
        if ((e.archetype == VfxArchetypeKind::Field && e.field.untilDismissed) ||
            (e.archetype == VfxArchetypeKind::Beam  && e.beam.untilDismissed)) {
            inst->persistent = true;
        }
    }

    SpellInstance* s = inst.get();
    m_instances.push_back(std::move(inst));

    // Fire all OnCast emissions immediately.
    for (size_t i = 0; i < s->comp.emissions.size(); ++i) {
        if (s->comp.emissions[i].trigger == VfxTriggerKind::OnCast) {
            fireEmission(*s, static_cast<int>(i), s->ctx.caster);
        }
    }

    LOG_INFO("VfxDirector", "Cast '{}' as {} ({} emissions)", comp.name, s->id, comp.emissions.size());
    return s->id;
}

glm::vec3 VfxDirector::resolveAnchor(SpellInstance& s, VfxAnchorKind kind, int targetIdx,
                                     const glm::vec3& fixed, const glm::vec3& impactPos) const {
    switch (kind) {
        case VfxAnchorKind::Caster:
            return s.ctx.caster;
        case VfxAnchorKind::Target:
            if (!s.ctx.targets.empty()) {
                int idx = std::min(std::max(0, targetIdx), static_cast<int>(s.ctx.targets.size()) - 1);
                return s.ctx.targets[idx];
            }
            return s.ctx.caster;
        case VfxAnchorKind::ImpactOf:
            return impactPos;
        case VfxAnchorKind::Fixed:
        default:
            return fixed;
    }
}

void VfxDirector::fireEmission(SpellInstance& s, int emissionIdx, const glm::vec3& impactPos) {
    const VfxEmissionSpec& e = s.comp.emissions[emissionIdx];

    // Determine how many copies and which target index each uses.
    int iterations = e.perTarget ? std::max<int>(1, static_cast<int>(s.ctx.targets.size())) : std::max(1, e.count);

    for (int k = 0; k < iterations; ++k) {
        int tIdx = e.perTarget ? k : e.targetIndex;
        glm::vec3 origin = resolveAnchor(s, e.originAnchor, tIdx, e.fixedOrigin, impactPos);
        glm::vec3 target = resolveAnchor(s, e.targetAnchor, tIdx, e.fixedTarget, impactPos);

        std::string id;
        switch (e.archetype) {
            case VfxArchetypeKind::Burst: {
                VfxBurstParams p = e.burst;
                // Aim directional burst shapes (cone/ring/dome) from origin toward target.
                if (p.shape == VfxShape::Cone || p.shape == VfxShape::Ring || p.shape == VfxShape::Dome) {
                    glm::vec3 d = target - origin;
                    if (glm::length(d) > 1e-4f) p.direction = glm::normalize(d);
                }
                m_vfx->spawnBurst(origin, p);
                break;
            }
            case VfxArchetypeKind::Projectile: {
                VfxProjectileParams p = e.projectile;
                p.useTarget = true;
                p.target = target;
                id = m_vfx->spawnProjectile(origin, p);
                break;
            }
            case VfxArchetypeKind::Beam: {
                VfxBeamParams p = e.beam;
                // Follow-anchor a tethering beam's target end to a moving creature.
                if (e.targetAnchor == VfxAnchorKind::Target && tIdx < static_cast<int>(s.ctx.targetProviders.size())
                    && s.ctx.targetProviders[tIdx]) {
                    p.anchorTarget = s.ctx.targetProviders[tIdx];
                }
                id = m_vfx->spawnBeam(origin, target, p);
                break;
            }
            case VfxArchetypeKind::Field: {
                VfxFieldParams p = e.field;
                // Follow-anchor an aura's center to the caster/target if a provider exists.
                if (e.originAnchor == VfxAnchorKind::Caster && s.ctx.casterProvider) {
                    p.anchor = s.ctx.casterProvider;
                } else if (e.originAnchor == VfxAnchorKind::Target && tIdx < static_cast<int>(s.ctx.targetProviders.size())
                           && s.ctx.targetProviders[tIdx]) {
                    p.anchor = s.ctx.targetProviders[tIdx];
                }
                id = m_vfx->spawnField(origin, p);
                break;
            }
        }

        if (!id.empty()) {
            m_owner[id] = {&s, emissionIdx};
            s.spawnedIds.push_back(id);
        }
    }

    s.live[emissionIdx].fired = true;
    // OnTick emissions can fire repeatedly; everything else is one-shot.
    if (e.trigger != VfxTriggerKind::OnTick) s.live[emissionIdx].pending = false;
}

void VfxDirector::update(float dt) {
    if (m_instances.empty()) return;

    // 1) Advance OnDelay timers.
    for (auto& up : m_instances) {
        SpellInstance& s = *up;
        s.age += dt;
        for (size_t i = 0; i < s.comp.emissions.size(); ++i) {
            const VfxEmissionSpec& e = s.comp.emissions[i];
            if (e.trigger != VfxTriggerKind::OnDelay || !s.live[i].pending) continue;
            s.live[i].delayRemaining -= dt;
            if (s.live[i].delayRemaining <= 0.0f) {
                fireEmission(s, static_cast<int>(i), s.ctx.caster);
            }
        }
    }

    // 2) Drain VfxSystem lifecycle events and fire matching triggered emissions.
    std::vector<VfxEvent> events = m_vfx->drainEvents();
    for (const VfxEvent& ev : events) {
        auto it = m_owner.find(ev.instanceId);
        if (it == m_owner.end()) continue;
        SpellInstance* s = it->second.first;
        int srcEmission = it->second.second;

        VfxTriggerKind want;
        switch (ev.type) {
            case VfxEvent::Type::Impact:  want = VfxTriggerKind::OnImpact; break;
            case VfxEvent::Type::Expired: want = VfxTriggerKind::OnExpire; break;
            case VfxEvent::Type::Tick:    want = VfxTriggerKind::OnTick;   break;
            default: continue; // Spawned: nothing chains off it
        }
        for (size_t i = 0; i < s->comp.emissions.size(); ++i) {
            const VfxEmissionSpec& e = s->comp.emissions[i];
            if (e.trigger == want && e.triggerRef == srcEmission && s->live[i].pending) {
                fireEmission(*s, static_cast<int>(i), ev.position);
            }
        }
    }

    // 3) Clean up finished instances (and their owner-map entries).
    for (auto& up : m_instances) {
        if (instanceDone(*up)) {
            for (const std::string& eid : up->spawnedIds) m_owner.erase(eid);
            up->live.clear(); // mark for removal below (age check keeps it simple)
        }
    }
    m_instances.erase(
        std::remove_if(m_instances.begin(), m_instances.end(),
                       [](const std::unique_ptr<SpellInstance>& s) { return s->live.empty(); }),
        m_instances.end());
}

bool VfxDirector::instanceDone(const SpellInstance& s) const {
    // Persistent spells (ticks / until-dismissed) live until explicitly dismissed
    // or a safety cap; one-shot spells finish once all emissions fired + a grace
    // window (so chained on_impact/on_expire emissions have time to resolve).
    if (s.persistent) return s.age > 60.0f;
    bool allFired = true;
    for (const auto& le : s.live) {
        if (le.pending) { allFired = false; break; }
    }
    return (allFired && s.age > 3.0f) || s.age > 20.0f;
}

// ============================================================================
// Test-spell factory — code-defined compositions, one per dimension.
// ============================================================================
VfxComposition buildTestSpell(const std::string& name) {
    VfxComposition c;
    c.name = name;

    if (name == "t_conecold") {
        // SHAPE(cone): a single cone burst aimed caster->target.
        VfxEmissionSpec e;
        e.archetype = VfxArchetypeKind::Burst;
        e.trigger = VfxTriggerKind::OnCast;
        e.originAnchor = VfxAnchorKind::Caster;
        e.targetAnchor = VfxAnchorKind::Target;
        e.burst.shape = VfxShape::Cone;
        e.burst.coneAngleDeg = 32.0f;
        e.burst.count = 140; e.burst.speed = 11.0f; e.burst.speedVar = 3.0f;
        e.burst.gravity = -1.0f; e.burst.drag = 1.0f; e.burst.lifetime = 0.7f;
        e.burst.size = 0.16f; e.burst.intensity = 1.7f;
        e.burst.color = glm::vec3(0.6f, 0.85f, 1.0f); // frost
        c.emissions.push_back(e);
        return c;
    }
    if (name == "t_thunderwave") {
        // SHAPE(cube): a cube-shaped concussive burst around the caster.
        VfxEmissionSpec e;
        e.archetype = VfxArchetypeKind::Burst;
        e.originAnchor = VfxAnchorKind::Caster;
        e.burst.shape = VfxShape::Cube;
        e.burst.extent = glm::vec3(3.0f);
        e.burst.count = 160; e.burst.speed = 2.0f; e.burst.lifetime = 0.5f;
        e.burst.size = 0.18f; e.burst.intensity = 1.5f;
        e.burst.color = glm::vec3(0.8f, 0.85f, 1.0f);
        c.emissions.push_back(e);
        return c;
    }
    if (name == "t_firebolt") {
        // COMPOSITION + on_impact + impact-anchor: projectile -> explosive burst.
        VfxEmissionSpec proj;
        proj.archetype = VfxArchetypeKind::Projectile;
        proj.trigger = VfxTriggerKind::OnCast;
        proj.originAnchor = VfxAnchorKind::Caster;
        proj.targetAnchor = VfxAnchorKind::Target;
        proj.projectile.speed = 16.0f; proj.projectile.coreSize = 0.34f;
        proj.projectile.trailRate = 150.0f; proj.projectile.intensity = 2.0f;
        proj.projectile.color = glm::vec3(1.0f, 0.4f, 0.08f);
        proj.projectile.light = true; proj.projectile.arrivalEffect = ""; // director handles arrival
        c.emissions.push_back(proj);

        VfxEmissionSpec boom;
        boom.archetype = VfxArchetypeKind::Burst;
        boom.trigger = VfxTriggerKind::OnImpact; boom.triggerRef = 0;
        boom.originAnchor = VfxAnchorKind::ImpactOf;
        boom.burst.shape = VfxShape::Sphere;
        boom.burst.count = 110; boom.burst.speed = 7.0f; boom.burst.lifetime = 1.0f;
        boom.burst.size = 0.22f; boom.burst.intensity = 1.9f;
        boom.burst.color = glm::vec3(1.0f, 0.45f, 0.1f);
        c.emissions.push_back(boom);
        return c;
    }
    if (name == "t_magicmissile") {
        // COUNT + MULTI-TARGET: one homing dart per target, each -> small burst.
        VfxEmissionSpec proj;
        proj.archetype = VfxArchetypeKind::Projectile;
        proj.trigger = VfxTriggerKind::OnCast;
        proj.perTarget = true;                  // one per context target
        proj.originAnchor = VfxAnchorKind::Caster;
        proj.targetAnchor = VfxAnchorKind::Target;
        proj.projectile.speed = 17.0f; proj.projectile.homing = true; proj.projectile.homingStrength = 7.0f;
        proj.projectile.spread = 0.5f; proj.projectile.coreSize = 0.16f; proj.projectile.trailRate = 110.0f;
        proj.projectile.intensity = 1.8f; proj.projectile.color = glm::vec3(0.5f, 0.25f, 1.0f);
        proj.projectile.light = false;
        c.emissions.push_back(proj);

        VfxEmissionSpec pop;
        pop.archetype = VfxArchetypeKind::Burst;
        pop.trigger = VfxTriggerKind::OnImpact; pop.triggerRef = 0;
        pop.originAnchor = VfxAnchorKind::ImpactOf;
        pop.burst.count = 30; pop.burst.speed = 4.0f; pop.burst.lifetime = 0.5f;
        pop.burst.size = 0.13f; pop.burst.intensity = 1.7f;
        pop.burst.color = glm::vec3(0.6f, 0.35f, 1.0f);
        c.emissions.push_back(pop);
        return c;
    }
    if (name == "t_lightningbolt") {
        // SHAPE(line/beam): a thick instant bolt caster->target.
        VfxEmissionSpec e;
        e.archetype = VfxArchetypeKind::Beam;
        e.originAnchor = VfxAnchorKind::Caster; e.targetAnchor = VfxAnchorKind::Target;
        e.beam.duration = 0.18f; e.beam.thickness = 0.28f; e.beam.density = 5.0f;
        e.beam.intensity = 2.4f; e.beam.color = glm::vec3(0.8f, 0.9f, 1.0f);
        e.beam.light = true; e.beam.lightIntensity = 2.6f;
        c.emissions.push_back(e);
        return c;
    }
    if (name == "t_walloffire") {
        // SHAPE(wall) + LIFETIME(until-dismissed): a persistent fiery wall.
        VfxEmissionSpec e;
        e.archetype = VfxArchetypeKind::Field;
        e.originAnchor = VfxAnchorKind::Caster;
        e.field.shape = VfxShape::Wall;
        e.field.direction = glm::vec3(0.0f, 0.0f, 1.0f); // facing +Z (spans X x Y)
        e.field.extent = glm::vec3(7.0f, 4.0f, 0.0f);
        e.field.untilDismissed = true;
        e.field.density = 120.0f; e.field.thickness = 0.16f; e.field.intensity = 1.9f;
        e.field.color = glm::vec3(1.0f, 0.4f, 0.08f);
        e.field.light = true; e.field.lightRadius = 9.0f;
        c.emissions.push_back(e);
        return c;
    }
    if (name == "t_moonbeam") {
        // SHAPE(cylinder) + on_tick: a moonlit column that pulses each second.
        VfxEmissionSpec col;
        col.archetype = VfxArchetypeKind::Field;
        col.originAnchor = VfxAnchorKind::Target;
        col.field.shape = VfxShape::Cylinder;
        col.field.radius = 2.0f; col.field.extent = glm::vec3(0.0f, 6.0f, 0.0f);
        col.field.duration = 5.0f; col.field.tickInterval = 1.0f;
        col.field.density = 90.0f; col.field.thickness = 0.13f; col.field.intensity = 1.6f;
        col.field.color = glm::vec3(0.7f, 0.8f, 1.0f);
        col.field.light = true;
        c.emissions.push_back(col);

        VfxEmissionSpec pulse;
        pulse.archetype = VfxArchetypeKind::Burst;
        pulse.trigger = VfxTriggerKind::OnTick; pulse.triggerRef = 0;
        pulse.originAnchor = VfxAnchorKind::ImpactOf; // tick reports the field center
        pulse.burst.shape = VfxShape::Sphere;
        pulse.burst.count = 40; pulse.burst.speed = 5.0f; pulse.burst.upBias = 0.0f;
        pulse.burst.lifetime = 0.6f; pulse.burst.size = 0.15f; pulse.burst.intensity = 1.8f;
        pulse.burst.color = glm::vec3(0.8f, 0.9f, 1.0f);
        c.emissions.push_back(pulse);
        return c;
    }
    if (name == "t_spiritguardians") {
        // ANCHOR(entity-follow) + until-dismissed + on_tick: an aura that follows the caster.
        VfxEmissionSpec aura;
        aura.archetype = VfxArchetypeKind::Field;
        aura.originAnchor = VfxAnchorKind::Caster; // director attaches casterProvider for follow
        aura.field.shape = VfxShape::Sphere;
        aura.field.radius = 2.8f;
        aura.field.untilDismissed = true; aura.field.tickInterval = 0.8f;
        aura.field.density = 70.0f; aura.field.thickness = 0.14f; aura.field.intensity = 1.6f;
        aura.field.pulseSpeed = 4.0f;
        aura.field.color = glm::vec3(0.9f, 0.85f, 0.5f); // spectral gold
        aura.field.light = true;
        c.emissions.push_back(aura);

        VfxEmissionSpec pulse;
        pulse.archetype = VfxArchetypeKind::Burst;
        pulse.trigger = VfxTriggerKind::OnTick; pulse.triggerRef = 0;
        pulse.originAnchor = VfxAnchorKind::ImpactOf;
        pulse.burst.count = 25; pulse.burst.speed = 3.0f; pulse.burst.lifetime = 0.5f;
        pulse.burst.size = 0.12f; pulse.burst.intensity = 1.6f;
        pulse.burst.color = glm::vec3(0.95f, 0.9f, 0.55f);
        c.emissions.push_back(pulse);
        return c;
    }
    if (name == "t_delayedblast") {
        // TRIGGER(on_delay): an ember charges at the target, then detonates.
        VfxEmissionSpec charge;
        charge.archetype = VfxArchetypeKind::Field;
        charge.originAnchor = VfxAnchorKind::Target;
        charge.field.shape = VfxShape::Sphere; charge.field.radius = 0.6f;
        charge.field.duration = 1.5f; charge.field.density = 50.0f; charge.field.thickness = 0.1f;
        charge.field.intensity = 1.8f; charge.field.pulseSpeed = 10.0f;
        charge.field.color = glm::vec3(1.0f, 0.5f, 0.1f); charge.field.light = true;
        c.emissions.push_back(charge);

        VfxEmissionSpec boom;
        boom.archetype = VfxArchetypeKind::Burst;
        boom.trigger = VfxTriggerKind::OnDelay; boom.delay = 1.5f;
        boom.originAnchor = VfxAnchorKind::Target;
        boom.burst.shape = VfxShape::Sphere;
        boom.burst.count = 150; boom.burst.speed = 9.0f; boom.burst.lifetime = 1.1f;
        boom.burst.size = 0.24f; boom.burst.intensity = 2.0f;
        boom.burst.color = glm::vec3(1.0f, 0.45f, 0.1f);
        c.emissions.push_back(boom);
        return c;
    }
    if (name == "t_chainlightning") {
        // MULTI-HOP chain: beam caster->t0, then on_expire hop t0->t1->t2 (ImpactOf anchor).
        VfxEmissionSpec b0;
        b0.archetype = VfxArchetypeKind::Beam;
        b0.originAnchor = VfxAnchorKind::Caster; b0.targetAnchor = VfxAnchorKind::Target; b0.targetIndex = 0;
        b0.beam.duration = 0.22f; b0.beam.thickness = 0.18f; b0.beam.density = 4.5f;
        b0.beam.intensity = 2.3f; b0.beam.color = glm::vec3(0.7f, 0.85f, 1.0f);
        c.emissions.push_back(b0);

        VfxEmissionSpec b1;
        b1.archetype = VfxArchetypeKind::Beam;
        b1.trigger = VfxTriggerKind::OnExpire; b1.triggerRef = 0;
        b1.originAnchor = VfxAnchorKind::ImpactOf; b1.targetAnchor = VfxAnchorKind::Target; b1.targetIndex = 1;
        b1.beam = b0.beam;
        c.emissions.push_back(b1);

        VfxEmissionSpec b2;
        b2.archetype = VfxArchetypeKind::Beam;
        b2.trigger = VfxTriggerKind::OnExpire; b2.triggerRef = 1;
        b2.originAnchor = VfxAnchorKind::ImpactOf; b2.targetAnchor = VfxAnchorKind::Target; b2.targetIndex = 2;
        b2.beam = b0.beam;
        c.emissions.push_back(b2);
        return c;
    }
    if (name == "t_thunderstep") {
        // ANCHOR (two points): burst at caster, then burst at destination after a beat.
        VfxEmissionSpec a;
        a.archetype = VfxArchetypeKind::Burst;
        a.originAnchor = VfxAnchorKind::Caster;
        a.burst.count = 70; a.burst.speed = 6.0f; a.burst.lifetime = 0.7f;
        a.burst.size = 0.18f; a.burst.intensity = 1.7f;
        a.burst.color = glm::vec3(0.7f, 0.8f, 1.0f);
        c.emissions.push_back(a);

        VfxEmissionSpec b;
        b.archetype = VfxArchetypeKind::Burst;
        b.trigger = VfxTriggerKind::OnDelay; b.delay = 0.18f;
        b.originAnchor = VfxAnchorKind::Target;
        b.burst.count = 90; b.burst.speed = 7.0f; b.burst.lifetime = 0.8f;
        b.burst.size = 0.2f; b.burst.intensity = 1.8f;
        b.burst.color = glm::vec3(0.8f, 0.85f, 1.0f);
        c.emissions.push_back(b);
        return c;
    }

    // Default: a simple sphere burst at the caster.
    VfxEmissionSpec e;
    e.archetype = VfxArchetypeKind::Burst;
    e.originAnchor = VfxAnchorKind::Caster;
    c.emissions.push_back(e);
    return c;
}

void VfxDirector::dismiss(const std::string& spellInstanceId) {
    for (auto& up : m_instances) {
        if (up->id != spellInstanceId) continue;
        for (const std::string& eid : up->spawnedIds) {
            m_vfx->dismiss(eid);
            m_owner.erase(eid);
        }
        up->live.clear(); // removed next update
        return;
    }
}

} // namespace Phyxel
