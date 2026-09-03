#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>

namespace Phyxel {

namespace Core { class HealthComponent; }

namespace Graphics {
    class RenderCoordinator;
}

namespace Scene {

class Entity {
public:
    virtual ~Entity() = default;

    virtual void update(float deltaTime) = 0;
    virtual void render(Graphics::RenderCoordinator* renderer) = 0;

    virtual void setPosition(const glm::vec3& pos) { position = pos; }
    virtual glm::vec3 getPosition() const { return position; }

    virtual void setRotation(const glm::quat& rot) { rotation = rot; }
    virtual glm::quat getRotation() const { return rotation; }

    virtual void setScale(const glm::vec3& s) { scale = s; }
    virtual glm::vec3 getScale() const { return scale; }

    /// Set horizontal move velocity (XZ), preserving vertical velocity (gravity).
    /// Default is a no-op; physics-backed entities override this.
    virtual void setMoveVelocity(const glm::vec3& /*velocity*/) {}

    /// Trigger a jump impulse. Default is a no-op; physics-backed entities override this.
    virtual void jump() {}

    glm::mat4 getModelMatrix() const {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, position);
        model = model * glm::mat4_cast(rotation);
        model = glm::scale(model, scale);
        return model;
    }

    /// Debug visualization color (subclasses set this for renderer identification)
    glm::vec4 debugColor = glm::vec4(1.0f); // default white

    /// Health component access (nullptr if entity has no health)
    virtual Core::HealthComponent* getHealthComponent() { return nullptr; }
    virtual const Core::HealthComponent* getHealthComponent() const { return nullptr; }

    // ── Faction / team ───────────────────────────────────────────
    // Which side this entity fights for. Lives on the ENTITY (not on a
    // behavior) precisely so one combatant can read ANOTHER's allegiance
    // during target selection — that is the whole point of teams. Empty =
    // unaligned, hostile to everyone (the historical free-for-all default).
    void setFaction(const std::string& f) { faction_ = f; }
    const std::string& faction() const { return faction_; }
    /// The reserved tag for NON-COMBATANTS: nobody attacks a "neutral", and a
    /// neutral attacks nobody. Spectators, shopkeepers and quest-givers need
    /// this — without it an observer standing near a battle is just a target,
    /// because "unaligned" (empty) means hostile to EVERYONE, which is the
    /// opposite of harmless. (Measured: both armies broke off to kill the
    /// camera operator.)
    static constexpr const char* kNeutralFaction = "neutral";

    /// True when these two should fight: different NAMED factions are hostile;
    /// an unaligned side (empty tag) is hostile to all, including its own kind;
    /// "neutral" is hostile to nobody and attacked by nobody.
    bool hostileTo(const Entity& other) const {
        if (faction_ == kNeutralFaction || other.faction_ == kNeutralFaction) return false;
        return faction_.empty() || other.faction_.empty() || faction_ != other.faction_;
    }

protected:
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 scale = glm::vec3(1.0f);
    std::string faction_;   ///< team tag; empty = unaligned (hostile to all)
};

} // namespace Scene
} // namespace Phyxel
