#pragma once

#include "core/ItemDefinition.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>

namespace Phyxel {

class VfxSystem;

namespace Core {

// ============================================================================
// ItemEffectSystem — drives declarative item effects (ItemDefinition::effects)
// for every live item instance: world props AND held-in-hand items.
//
// Each instance supplies a transform provider (typically
// KinematicVoxelManager::getTransform under the hood), so effects follow a
// held item's hand bone and sit still on a dropped prop with the same code.
//
// Per tick: conditions re-evaluate at CONDITION_HZ (cheap, throttled); active
// effects emit VfxSystem bursts on their rate timer at the transformed anchor
// and keep a point light positioned there. Lights are managed through injected
// callbacks so core stays decoupled from the graphics LightManager.
// ============================================================================
class ItemEffectSystem {
public:
    static constexpr float CONDITION_INTERVAL = 0.25f;  // seconds between condition checks

    using TransformProvider = std::function<glm::mat4()>;
    /// Count entities of `typeTag` whose id/name contains `nameFilter` ("" = any)
    /// within `radius` of `pos`.
    using NearbyQuery = std::function<int(const glm::vec3& pos, float radius,
                                          const std::string& typeTag,
                                          const std::string& nameFilter)>;
    using LightCreate = std::function<int(const glm::vec3& pos, const glm::vec3& color,
                                          float intensity, float radius)>;
    using LightMove   = std::function<void(int lightId, const glm::vec3& pos)>;
    using LightRemove = std::function<void(int lightId)>;

    void setVfxSystem(VfxSystem* vfx) { m_vfx = vfx; }
    void setNearbyQuery(NearbyQuery q) { m_nearbyQuery = std::move(q); }
    void setLightCallbacks(LightCreate create, LightMove move, LightRemove remove) {
        m_lightCreate = std::move(create);
        m_lightMove = std::move(move);
        m_lightRemove = std::move(remove);
    }

    /// Register a live item instance. `held` selects the "state" condition
    /// branch. Re-registering an existing id replaces it (effects restart).
    void registerInstance(const std::string& instanceId, const ItemDefinition* def,
                          bool held, TransformProvider transform);

    /// Remove an instance and tear down its lights.
    void unregisterInstance(const std::string& instanceId);

    void clear();
    size_t instanceCount() const { return m_instances.size(); }

    /// Tick all instances. Call once per frame.
    void update(float dt);

private:
    struct EffectState {
        bool  active = false;
        int   lightId = -1;
        float burstAccum = 0.0f;
    };
    struct Instance {
        const ItemDefinition* def = nullptr;
        bool held = false;
        TransformProvider transform;
        std::vector<EffectState> states;   // parallel to def->effects
        float conditionTimer = 0.0f;       // staggered by registration
    };

    void deactivate(EffectState& state);
    bool evaluateCondition(const ItemEffectDef& e, const Instance& inst,
                           const glm::vec3& anchorWorld) const;

    VfxSystem* m_vfx = nullptr;
    NearbyQuery m_nearbyQuery;
    LightCreate m_lightCreate;
    LightMove   m_lightMove;
    LightRemove m_lightRemove;

    std::unordered_map<std::string, Instance> m_instances;
};

} // namespace Core
} // namespace Phyxel
