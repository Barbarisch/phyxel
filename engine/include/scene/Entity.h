#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

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

protected:
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 scale = glm::vec3(1.0f);
};

} // namespace Scene
} // namespace Phyxel
