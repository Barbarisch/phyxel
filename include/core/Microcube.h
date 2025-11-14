#pragma once

#include <glm/glm.hpp>

// Forward declarations
class btRigidBody;

namespace VulkanCube {

/**
 * @brief Microcube class for fine-grained voxel subdivision
 * 
 * Represents the smallest subdivided unit in the three-level hierarchy: Cube → Subcube → Microcube.
 * Each microcube is 1/9 the size of a regular cube (1/3 of a subcube).
 * When a subcube is subdivided, it creates 27 microcubes (3x3x3 grid).
 * 
 * Hierarchy:
 * - Cube: 1.0 scale (full block)
 * - Subcube: 1/3 scale (27 per cube)
 * - Microcube: 1/9 scale (27 per subcube, 729 per cube)
 */
class Microcube {
public:
    // Constructors
    Microcube();
    Microcube(const glm::ivec3& parentCubePos, const glm::vec3& col);
    Microcube(const glm::ivec3& parentCubePos, const glm::vec3& col, 
              const glm::ivec3& subcubeLocalPos, const glm::ivec3& microcubeLocalPos);
    
    // Destructor
    ~Microcube() = default;
    
    // Copy and move constructors/assignment
    Microcube(const Microcube&) = default;
    Microcube& operator=(const Microcube&) = default;
    Microcube(Microcube&&) = default;
    Microcube& operator=(Microcube&&) = default;
    
    // Accessors
    const glm::ivec3& getParentCubePosition() const { return parentCubePosition; }
    const glm::vec3& getColor() const { return color; }
    const glm::vec3& getOriginalColor() const { return originalColor; }
    const glm::ivec3& getSubcubeLocalPosition() const { return subcubeLocalPosition; }
    const glm::ivec3& getMicrocubeLocalPosition() const { return microcubeLocalPosition; }
    float getScale() const { return scale; }
    bool isBroken() const { return broken; }
    bool isVisible() const { return visible; }
    btRigidBody* getRigidBody() const { return rigidBody; }
    float getLifetime() const { return lifetime; }
    bool hasExpired() const { return lifetime <= 0.0f; }
    const glm::vec3& getPhysicsPosition() const { return physicsPosition; }
    bool isDynamic() const { return rigidBody != nullptr; }
    
    // Mutators
    void setParentCubePosition(const glm::ivec3& pos) { parentCubePosition = pos; }
    void setColor(const glm::vec3& col) { color = col; }
    void setOriginalColor(const glm::vec3& col) { originalColor = col; }
    void setSubcubeLocalPosition(const glm::ivec3& localPos) { subcubeLocalPosition = localPos; }
    void setMicrocubeLocalPosition(const glm::ivec3& localPos) { microcubeLocalPosition = localPos; }
    void setBroken(bool isBroken) { broken = isBroken; }
    void setVisible(bool vis) { visible = vis; }
    void setRigidBody(btRigidBody* body) { rigidBody = body; }
    void setPhysicsPosition(const glm::vec3& pos) { physicsPosition = pos; }
    void setPhysicsRotation(const glm::vec4& rot) { physicsRotation = rot; }
    void setLifetime(float time) { lifetime = time; }
    void updateLifetime(float deltaTime) { lifetime -= deltaTime; }
    
    // Utility methods
    glm::vec3 getWorldPosition() const; // Calculate actual world position including both local offsets
    glm::vec4 getPhysicsRotation() const { return physicsRotation; }
    void hide() { visible = false; }
    void show() { visible = true; }
    void breakApart() { broken = true; }
    void repair() { broken = false; }
    
private:
    glm::ivec3 parentCubePosition;      // World position of the parent cube
    glm::vec3 color;                     // Color of the microcube
    glm::vec3 originalColor;             // Original color before hover effects
    glm::ivec3 subcubeLocalPosition;     // Local position of parent subcube within cube (0-2 for each axis)
    glm::ivec3 microcubeLocalPosition;   // Local position within parent subcube (0-2 for each axis)
    float scale;                         // Scale factor (1/9 of regular cube)
    float lifetime = 30.0f;              // Lifetime in seconds (auto-cleanup after 30 seconds)
    bool broken = false;
    bool visible = true;
    
    // Physics body for dynamic microcubes
    btRigidBody* rigidBody = nullptr;
    
    // Smooth floating-point position and rotation for dynamic microcubes (bypasses integer grid)
    glm::vec3 physicsPosition = glm::vec3(0.0f);
    glm::vec4 physicsRotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); // Identity quaternion
    
    static constexpr float MICROCUBE_SCALE = 1.0f / 9.0f; // 1/9 the size of a regular cube
};

} // namespace VulkanCube
