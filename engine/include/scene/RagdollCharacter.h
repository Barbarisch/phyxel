#pragma once

#include "scene/Entity.h"
#include "physics/PhysicsWorld.h"
#include "core/HealthComponent.h"
#include "utils/Logger.h"
#include <vector>
#include <string>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Phyxel {
namespace Scene {

enum class Faction {
    Player,
    Enemy,
    Neutral
};

struct RagdollPart {
    glm::vec3 scale;
    glm::vec4 color;
    std::string name;
    glm::vec3 offset = glm::vec3(0.0f);
    bool active = true;
    // Direct-transform path used by AnimatedVoxelCharacter bones.
    bool      useDirectTransform = true;
    int       boneGroupId = -1;
    glm::vec3 worldPos = glm::vec3(0.0f);
    glm::quat worldRot = glm::quat(1, 0, 0, 0);
};

class RagdollCharacter : public Entity {
public:
    RagdollCharacter(Physics::PhysicsWorld* physicsWorld, const glm::vec3& startPos)
        : physicsWorld(physicsWorld), faction(Faction::Neutral),
          m_health(std::make_unique<Core::HealthComponent>(100.0f)) {}

    virtual ~RagdollCharacter() = default;

    virtual void update(float deltaTime) override = 0;
    virtual void render(Graphics::RenderCoordinator* renderer) override {}

    virtual void setPosition(const glm::vec3& pos) override {}

    virtual glm::vec3 getPosition() const override { return glm::vec3(0.0f); }

    const std::vector<RagdollPart>& getParts() const { return parts; }

    void setFaction(Faction f) { faction = f; }
    Faction getFaction() const { return faction; }

    virtual void setControlInput(float forward, float turn) {}

    Core::HealthComponent* getHealthComponent() override { return m_health.get(); }
    const Core::HealthComponent* getHealthComponent() const override { return m_health.get(); }

protected:
    Physics::PhysicsWorld* physicsWorld;
    std::vector<RagdollPart> parts;
    Faction faction;
    std::unique_ptr<Core::HealthComponent> m_health;
};

} // namespace Scene
} // namespace Phyxel
