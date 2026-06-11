#include "core/ItemEffectSystem.h"

#include "core/VfxSystem.h"
#include "utils/Logger.h"

#include <algorithm>

namespace Phyxel {
namespace Core {

void ItemEffectSystem::registerInstance(const std::string& instanceId, const ItemDefinition* def,
                                        bool held, TransformProvider transform) {
    if (!def || !transform) return;
    unregisterInstance(instanceId);  // tear down any previous lights first
    if (def->effects.empty()) return;

    Instance inst;
    inst.def = def;
    inst.held = held;
    inst.transform = std::move(transform);
    inst.states.resize(def->effects.size());
    // Stagger condition checks so many props don't all query on the same frame.
    inst.conditionTimer = CONDITION_INTERVAL * float(m_instances.size() % 4) * 0.25f;
    m_instances[instanceId] = std::move(inst);
    LOG_DEBUG("ItemEffects", "Registered '{}' ({} effect(s), {})",
              instanceId, def->effects.size(), held ? "held" : "prop");
}

void ItemEffectSystem::unregisterInstance(const std::string& instanceId) {
    auto it = m_instances.find(instanceId);
    if (it == m_instances.end()) return;
    for (auto& state : it->second.states) deactivate(state);
    m_instances.erase(it);
}

void ItemEffectSystem::clear() {
    for (auto& [id, inst] : m_instances)
        for (auto& state : inst.states) deactivate(state);
    m_instances.clear();
}

void ItemEffectSystem::deactivate(EffectState& state) {
    if (state.lightId >= 0 && m_lightRemove) m_lightRemove(state.lightId);
    state.lightId = -1;
    state.active = false;
    state.burstAccum = 0.0f;
}

bool ItemEffectSystem::evaluateCondition(const ItemEffectDef& e, const Instance& inst,
                                         const glm::vec3& anchorWorld) const {
    if (e.whenState == 1 && !inst.held) return false;
    if (e.whenState == 2 && inst.held) return false;
    if (e.hasNearby) {
        if (!m_nearbyQuery) return false;
        if (m_nearbyQuery(anchorWorld, e.nearbyRadius, e.nearbyType, e.nearbyName) <= 0)
            return false;
    }
    return true;
}

void ItemEffectSystem::update(float dt) {
    for (auto& [id, inst] : m_instances) {
        if (!inst.def || inst.states.size() != inst.def->effects.size()) continue;

        const glm::mat4 transform = inst.transform();
        inst.conditionTimer -= dt;
        const bool checkConditions = inst.conditionTimer <= 0.0f;
        if (checkConditions) inst.conditionTimer = CONDITION_INTERVAL;

        for (size_t i = 0; i < inst.def->effects.size(); ++i) {
            const ItemEffectDef& e = inst.def->effects[i];
            EffectState& state = inst.states[i];

            const glm::vec3 anchorWorld = glm::vec3(transform * glm::vec4(e.anchor, 1.0f));

            if (checkConditions) {
                const bool nowActive = evaluateCondition(e, inst, anchorWorld);
                if (nowActive && !state.active) {
                    state.active = true;
                    if (e.hasLight && m_lightCreate && state.lightId < 0)
                        state.lightId = m_lightCreate(anchorWorld, e.lightColor,
                                                      e.lightIntensity, e.lightRadius);
                } else if (!nowActive && state.active) {
                    deactivate(state);
                }
            }
            if (!state.active) continue;

            // Keep the light glued to the (possibly moving) anchor.
            if (state.lightId >= 0 && m_lightMove)
                m_lightMove(state.lightId, anchorWorld);

            // Periodic particle bursts.
            if (e.hasVfx && m_vfx && e.vfxRate > 0.0f) {
                state.burstAccum += dt * e.vfxRate;
                int bursts = (int)state.burstAccum;
                if (bursts > 0) {
                    state.burstAccum -= (float)bursts;
                    bursts = std::min(bursts, 4);  // catch-up clamp after hitches
                    VfxBurstParams p;
                    p.count       = e.vfxCount;
                    p.color       = e.vfxColor;
                    p.size        = e.vfxSize;
                    p.sizeVar     = e.vfxSize * 0.4f;
                    p.speed       = e.vfxSpeed;
                    p.speedVar    = e.vfxSpeed * 0.5f;
                    p.upBias      = e.vfxUpBias;
                    p.gravity     = e.vfxGravity;
                    p.drag        = 1.0f;
                    p.lifetime    = e.vfxLifetime;
                    p.lifetimeVar = e.vfxLifetime * 0.4f;
                    p.intensity   = e.vfxIntensity;
                    p.posJitter   = e.vfxJitter;
                    for (int b = 0; b < bursts; ++b)
                        m_vfx->spawnBurst(anchorWorld, p);
                }
            }
        }
    }
}

} // namespace Core
} // namespace Phyxel
