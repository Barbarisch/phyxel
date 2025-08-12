#pragma once

#include <glm/glm.hpp>

// Forward declarations
class btRigidBody;

namespace VulkanCube {

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
    Subcube(const glm::ivec3& pos, const glm::vec3& col);
    Subcube(const glm::ivec3& pos, const glm::vec3& col, const glm::ivec3& localPos);
    
    // Destructor
    ~Subcube() = default;
    
    // Copy and move constructors/assignment
    Subcube(const Subcube&) = default;
    Subcube& operator=(const Subcube&) = default;
    Subcube(Subcube&&) = default;
    Subcube& operator=(Subcube&&) = default;
    
    // Accessors
    const glm::ivec3& getPosition() const { return position; }
    const glm::vec3& getColor() const { return color; }
    const glm::ivec3& getLocalPosition() const { return localPosition; }
    float getScale() const { return scale; }
    bool isBroken() const { return broken; }
    bool isVisible() const { return visible; }
    btRigidBody* getRigidBody() const { return rigidBody; }
    
    // Mutators
    void setPosition(const glm::ivec3& pos) { position = pos; }
    void setColor(const glm::vec3& col) { color = col; }
    void setLocalPosition(const glm::ivec3& localPos) { localPosition = localPos; }
    void setBroken(bool isBroken) { broken = isBroken; }
    void setVisible(bool vis) { visible = vis; }
    void setRigidBody(btRigidBody* body) { rigidBody = body; }
    
    // Utility methods
    glm::vec3 getWorldPosition() const; // Calculate actual world position including local offset
    void hide() { visible = false; }
    void show() { visible = true; }
    void breakApart() { broken = true; }
    void repair() { broken = false; }
    
private:
    glm::ivec3 position;        // World position of the parent cube
    glm::vec3 color;            // Color of the subcube
    glm::ivec3 localPosition;   // Local position within parent cube (0-2 for each axis)
    float scale;                // Scale factor (1/3 of regular cube)
    bool broken = false;
    bool visible = true;
    
    // Physics body for dynamic subcubes
    btRigidBody* rigidBody = nullptr;
    
    static constexpr float SUBCUBE_SCALE = 1.0f / 3.0f; // 1/3 the size of a regular cube
};

} // namespace VulkanCube
