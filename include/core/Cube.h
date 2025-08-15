#pragma once

#include <glm/glm.hpp>
#include <vector>

// Forward declarations
class btRigidBody;

namespace VulkanCube {

// Forward declaration
class Subcube;

/**
 * @brief Cube class for voxel-based world representation
 * 
 * Represents a single cube/voxel in the 3D world. Each cube has a position,
 * color, visibility state, and can optionally have physics simulation.
 * Can be subdivided into 27 subcubes for detailed manipulation.
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
    const glm::vec3& getOriginalColor() const { return originalColor; }
    bool isBroken() const { return broken; }
    bool isVisible() const { return visible; }
    btRigidBody* getRigidBody() const { return rigidBody; }
    
    // Mutators
    void setPosition(const glm::ivec3& pos) { position = pos; }
    void setColor(const glm::vec3& col) { color = col; }
    void setOriginalColor(const glm::vec3& col) { originalColor = col; }
    void setBroken(bool isBroken) { broken = isBroken; }
    void setVisible(bool vis) { visible = vis; }
    void setRigidBody(btRigidBody* body) { rigidBody = body; }
    
    // Utility methods
    void hide() { visible = false; }
    void show() { visible = true; }
    void breakApart() { broken = true; }
    void repair() { broken = false; }
    
    // Subdivision methods
    bool isSubdivided() const { return !subcubes.empty(); }
    void addSubcube(Subcube* subcube) { subcubes.push_back(subcube); }
    void removeSubcube(Subcube* subcube);
    void clearSubcubes();
    Subcube* getSubcubeAt(const glm::ivec3& localPos);
    const Subcube* getSubcubeAt(const glm::ivec3& localPos) const;
    const std::vector<Subcube*>& getSubcubes() const { return subcubes; }
    
    // Static utility methods
    static float getSize() { return CUBE_SIZE; }
    
private:
    glm::ivec3 position;        // World position in grid coordinates
    glm::vec3 color;            // RGB color (0.0 - 1.0) - current color (may be modified by hover, etc.)
    glm::vec3 originalColor;    // RGB color (0.0 - 1.0) - original color before any modifications
    bool broken = false;        // Whether the cube is broken/damaged
    bool visible = true;        // Whether the cube should be rendered
    
    // Subdivision support
    std::vector<Subcube*> subcubes; // Subcubes if this cube is subdivided
    
    // Physics body for dynamic cubes
    btRigidBody* rigidBody = nullptr;
    
    static constexpr float CUBE_SIZE = 1.0f; // Size of each cube unit
};

} // namespace VulkanCube
