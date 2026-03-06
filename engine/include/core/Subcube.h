#pragma once

#include <glm/glm.hpp>

// Forward declarations
class btRigidBody;

namespace Phyxel {

/**
 * @brief Subcube class for voxel subdivision system
 * 
 * Represents a subdivided portion of a cube for detailed voxel manipulation.
 * Each subcube is 1/3 the size of a regular cube. When a cube is subdivided,
 * it creates 27 subcubes (3x3x3 grid) that fit in the same space as the original cube.
 */
class Subcube {
public:
    // Constructors
    Subcube();
    Subcube(const glm::ivec3& pos);
    Subcube(const glm::ivec3& pos, const glm::ivec3& localPos);
    
    // Destructor
    ~Subcube() = default;
    
    // Copy and move constructors/assignment
    Subcube(const Subcube&) = default;
    Subcube& operator=(const Subcube&) = default;
    Subcube(Subcube&&) = default;
    Subcube& operator=(Subcube&&) = default;
    
    // Accessors
    const glm::ivec3& getPosition() const { return position; }
    const glm::ivec3& getLocalPosition() const { return localPosition; }
    float getScale() const { return scale; }
    bool isBroken() const { return broken; }
    bool isVisible() const { return visible; }
    btRigidBody* getRigidBody() const { return rigidBody; }
    float getLifetime() const { return lifetime; }
    bool hasExpired() const { return lifetime <= 0.0f; }
    const glm::vec3& getPhysicsPosition() const { return physicsPosition; }
    bool isDynamic() const { return rigidBody != nullptr; }
    
    // Mutators
    void setPosition(const glm::ivec3& pos) { position = pos; }
    void setLocalPosition(const glm::ivec3& localPos) { localPosition = localPos; }
    void setBroken(bool isBroken) { broken = isBroken; }
    void setVisible(bool vis) { visible = vis; }
    void setRigidBody(btRigidBody* body) { rigidBody = body; }
    void setPhysicsPosition(const glm::vec3& pos) { physicsPosition = pos; }
    void setPhysicsRotation(const glm::vec4& rot) { physicsRotation = rot; }
    void setLifetime(float time) { lifetime = time; }
    void updateLifetime(float deltaTime) { lifetime -= deltaTime; }
    
    // Utility methods
    glm::vec3 getWorldPosition() const; // Calculate actual world position including local offset
    glm::vec4 getPhysicsRotation() const { return physicsRotation; }
    void hide() { visible = false; }
    void show() { visible = true; }
    void breakApart() { broken = true; }
    void repair() { broken = false; }
    
private:
    glm::ivec3 position;        // World position of the parent cube (for static subcubes)
    glm::ivec3 localPosition;   // Local position within parent cube (0-2 for each axis)
    float scale;                // Scale factor (1/3 of regular cube)
    float lifetime = 30.0f;     // Lifetime in seconds (auto-cleanup after 30 seconds)
    bool broken = false;
    bool visible = true;
    
    // Physics body for dynamic subcubes
    btRigidBody* rigidBody = nullptr;
    
    // Smooth floating-point position and rotation for dynamic subcubes (bypasses integer grid)
    glm::vec3 physicsPosition = glm::vec3(0.0f);
    glm::vec4 physicsRotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); // Identity quaternion
    
    static constexpr float SUBCUBE_SCALE = 1.0f / 3.0f; // 1/3 the size of a regular cube
};

} // namespace Phyxel
