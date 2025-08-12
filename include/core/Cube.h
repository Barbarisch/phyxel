#pragma once

#include <glm/glm.hpp>

// Forward declarations
class btRigidBody;

namespace VulkanCube {

/**
 * @brief Cube class for voxel-based world representation
 * 
 * Represents a single cube/voxel in the 3D world. Each cube has a position,
 * color, visibility state, and can optionally have physics simulation.
 */
class Cube {
public:
    // Constructors
    Cube();
    Cube(const glm::ivec3& pos, const glm::vec3& col);
    
    // Destructor
    ~Cube() = default;
    
    // Copy and move constructors/assignment
    Cube(const Cube&) = default;
    Cube& operator=(const Cube&) = default;
    Cube(Cube&&) = default;
    Cube& operator=(Cube&&) = default;
    
    // Accessors
    const glm::ivec3& getPosition() const { return position; }
    const glm::vec3& getColor() const { return color; }
    bool isBroken() const { return broken; }
    bool isVisible() const { return visible; }
    btRigidBody* getRigidBody() const { return rigidBody; }
    
    // Mutators
    void setPosition(const glm::ivec3& pos) { position = pos; }
    void setColor(const glm::vec3& col) { color = col; }
    void setBroken(bool isBroken) { broken = isBroken; }
    void setVisible(bool vis) { visible = vis; }
    void setRigidBody(btRigidBody* body) { rigidBody = body; }
    
    // Utility methods
    void hide() { visible = false; }
    void show() { visible = true; }
    void breakApart() { broken = true; }
    void repair() { broken = false; }
    
    // Static utility methods
    static float getSize() { return CUBE_SIZE; }
    
private:
    glm::ivec3 position;        // World position in grid coordinates
    glm::vec3 color;            // RGB color (0.0 - 1.0)
    bool broken = false;        // Whether the cube is broken/damaged
    bool visible = true;        // Whether the cube should be rendered
    
    // Physics body for dynamic cubes
    btRigidBody* rigidBody = nullptr;
    
    static constexpr float CUBE_SIZE = 1.0f; // Size of each cube unit
};

} // namespace VulkanCube
