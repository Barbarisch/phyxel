#pragma once

#include "scene/NPCBehavior.h"

namespace Phyxel {
namespace Scene {

/// NPC stands in place and plays the idle animation.
/// Occasionally faces the nearest entity within a scan radius.
class IdleBehavior : public NPCBehavior {
public:
    explicit IdleBehavior(float lookRadius = 10.0f)
        : m_lookRadius(lookRadius) {}

    void update(float dt, NPCContext& ctx) override;
    void onInteract(Entity* interactor) override;
    void onEvent(const std::string& eventType, const nlohmann::json& data) override;
    std::string getBehaviorName() const override { return "Idle"; }

private:
    float m_lookRadius;
    float m_lookTimer = 0.0f;
    static constexpr float LOOK_INTERVAL = 3.0f;
};

} // namespace Scene
} // namespace Phyxel
