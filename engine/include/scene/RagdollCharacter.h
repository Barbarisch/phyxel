#pragma once

#include "scene/Entity.h"
#include "physics/PhysicsWorld.h"
#include "core/HealthComponent.h"
#include <vector>
#include <string>
#include <memory>
#include <glm/glm.hpp>

namespace Phyxel {
namespace Scene {

enum class Faction {
    Player,
    Enemy,
    Neutral
};

struct RagdollPart {
    btRigidBody* rigidBody;
    glm::vec3 scale;
    glm::vec4 color;
    std::string name;
    glm::vec3 offset = glm::vec3(0.0f); // Offset from rigid body center
};

class RagdollCharacter : public Entity {
public:
    RagdollCharacter(Physics::PhysicsWorld* physicsWorld, const glm::vec3& startPos) 
        : physicsWorld(physicsWorld), faction(Faction::Neutral), m_health(std::make_unique<Core::HealthComponent>(100.0f)) {}
    
    virtual ~RagdollCharacter() {
        // Cleanup constraints
        for (auto* constraint : constraints) {
            physicsWorld->removeConstraint(constraint);
            delete constraint;
        }
        constraints.clear();

        // Cleanup rigid bodies
        for (auto& part : parts) {
            if (part.rigidBody) {
                physicsWorld->removeCube(part.rigidBody);
            }
        }
        parts.clear();
    }

    // Common Entity overrides
    virtual void update(float deltaTime) override = 0;
    virtual void render(Graphics::RenderCoordinator* renderer) override {} // Default empty, RenderCoordinator handles parts

    virtual void setPosition(const glm::vec3& pos) override {
        // Teleport all parts
        glm::vec3 currentPos = getPosition();
        glm::vec3 diff = pos - currentPos;
        
        for (auto& part : parts) {
            if (part.rigidBody) {
                btTransform trans;
                part.rigidBody->getMotionState()->getWorldTransform(trans);
                trans.setOrigin(trans.getOrigin() + btVector3(diff.x, diff.y, diff.z));
                part.rigidBody->setWorldTransform(trans);
                part.rigidBody->setLinearVelocity(btVector3(0,0,0));
                part.rigidBody->setAngularVelocity(btVector3(0,0,0));
            }
        }
    }

    virtual glm::vec3 getPosition() const override {
        // Default to first part (usually torso/body)
        if (!parts.empty() && parts[0].rigidBody) {
            btTransform trans;
            parts[0].rigidBody->getMotionState()->getWorldTransform(trans);
            btVector3 pos = trans.getOrigin();
            return glm::vec3(pos.x(), pos.y(), pos.z());
        }
        return glm::vec3(0.0f);
    }

    // Ragdoll specific
    const std::vector<RagdollPart>& getParts() const { return parts; }
    
    void setFaction(Faction f) { faction = f; }
    Faction getFaction() const { return faction; }

    // Control interface (can be overridden by specific characters)
    virtual void setControlInput(float forward, float turn) {}

    // Health
    Core::HealthComponent* getHealthComponent() override { return m_health.get(); }
    const Core::HealthComponent* getHealthComponent() const override { return m_health.get(); }

protected:
    Physics::PhysicsWorld* physicsWorld;
    std::vector<RagdollPart> parts;
    std::vector<btTypedConstraint*> constraints;
    Faction faction;
    std::unique_ptr<Core::HealthComponent> m_health;
};

} // namespace Scene
} // namespace Phyxel
