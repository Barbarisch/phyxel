#pragma once

#include <glm/glm.hpp>
#include <memory>

// Forward declarations
class btRigidBody;

namespace VulkanCube {

/**
 * @brief DynamicCube class for physics-enabled full-size cubes
 * 
 * Represents a full-size cube that has been broken from the static grid and is now
 * physics-enabled. These cubes are rendered using the dynamic pipeline with scale = 1.0
 * and managed globally by ChunkManager.
 */
class DynamicCube {
public:
    DynamicCube();
    DynamicCube(const glm::vec3& pos, const glm::vec3& col);
    ~DynamicCube() = default;

    // Copy constructor and assignment for transferring between systems
    DynamicCube(const DynamicCube& other);
    DynamicCube& operator=(const DynamicCube& other);

    // Accessors
    const glm::vec3& getPosition() const { return position; }
    const glm::vec3& getColor() const { return color; }
    float getScale() const { return CUBE_SCALE; }
    bool isBroken() const { return broken; }
    bool isVisible() const { return visible; }
    btRigidBody* getRigidBody() const { return rigidBody; }
    float getLifetime() const { return lifetime; }
    bool hasExpired() const { return lifetime <= 0.0f; }
    const glm::vec3& getPhysicsPosition() const { return physicsPosition; }
    const glm::vec4& getPhysicsRotation() const { return physicsRotation; }
    bool isDynamic() const { return rigidBody != nullptr; }

    // Mutators
    void setPosition(const glm::vec3& pos) { position = pos; }
    void setColor(const glm::vec3& col) { color = col; }
    void setBroken(bool isBroken) { broken = isBroken; }
    void setVisible(bool vis) { visible = vis; }
    void setRigidBody(btRigidBody* body) { rigidBody = body; }
    void setPhysicsPosition(const glm::vec3& pos) { physicsPosition = pos; }
    void setPhysicsRotation(const glm::vec4& rot) { physicsRotation = rot; }
    void setLifetime(float time) { lifetime = time; }
    void updateLifetime(float deltaTime) { lifetime -= deltaTime; }

    // Utility methods
    glm::vec3 getWorldPosition() const; // Calculate actual world position (same as physics position for dynamic cubes)
    void hide() { visible = false; }
    void show() { visible = true; }
    void breakApart() { broken = true; }
    void repair() { broken = false; }

private:
    glm::vec3 position;         // World position of the cube (for reference)
    glm::vec3 color;            // Color of the cube
    float lifetime = 30.0f;     // Lifetime in seconds (auto-cleanup after 30 seconds)
    bool broken = false;
    bool visible = true;
    
    // Physics body for dynamic cubes
    btRigidBody* rigidBody = nullptr;
    
    // Smooth floating-point position and rotation for dynamic cubes (bypasses integer grid)
    glm::vec3 physicsPosition = glm::vec3(0.0f);
    glm::vec4 physicsRotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); // Identity quaternion
    
    static constexpr float CUBE_SCALE = 1.0f; // Full size cube
};

} // namespace VulkanCube
