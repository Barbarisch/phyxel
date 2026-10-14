#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <array>
#include <string>

namespace Phyxel {
namespace Physics { class VoxelRigidBody; }


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
    Cube(const glm::ivec3& pos);
    Cube(const glm::ivec3& pos, const std::string& material);
    
    // Destructor
    ~Cube() = default;
    
    // Copy and move constructors/assignment
    Cube(const Cube&) = default;
    Cube& operator=(const Cube&) = default;
    Cube(Cube&&) = default;
    Cube& operator=(Cube&&) = default;
    
    // Accessors
    const glm::ivec3& getPosition() const { return position; }
    bool isBroken() const { return broken; }
    bool isVisible() const { return visible; }
    Physics::VoxelRigidBody* getVoxelBody() const { return voxelBody; }

    // Physics accessors (for dynamic cubes)
    const glm::vec3& getPhysicsPosition() const { return physicsPosition; }
    const glm::vec4& getPhysicsRotation() const { return physicsRotation; }
    const glm::vec3& getDynamicScale() const { return dynamicScale; }
    bool isDynamic() const { return voxelBody != nullptr; }
    
    // Lifetime accessors (for dynamic cubes)
    float getLifetime() const { return lifetime; }
    bool hasExpired() const { return isDynamic() && lifetime <= 0.0f; }
    
    // Material accessors (for dynamic cubes)
    const std::string& getMaterialName() const { return materialName; }

    // Destruction damage accumulation (DamageSystem). Sub-threshold hits add up
    // here until they exceed material toughness, then the voxel breaks. 0 = pristine.
    float getAccumulatedDamage() const { return accumulatedDamage; }
    void  addDamage(float amount) { accumulatedDamage += amount; }
    void  resetDamage() { accumulatedDamage = 0.0f; }

    // Mutators
    void setPosition(const glm::ivec3& pos) { position = pos; }
    void setBroken(bool isBroken) { broken = isBroken; }
    void setVisible(bool vis) { visible = vis; }
    void setVoxelBody(Physics::VoxelRigidBody* body) { voxelBody = body; }
    
    // Physics mutators (for dynamic cubes)
    void setPhysicsPosition(const glm::vec3& pos) { physicsPosition = pos; }
    void setPhysicsRotation(const glm::vec4& rot) { physicsRotation = rot; }
    void setDynamicScale(const glm::vec3& scale) { dynamicScale = scale; }
    
    // Lifetime mutators (for dynamic cubes)
    void setLifetime(float time) { lifetime = time; }
    void updateLifetime(float deltaTime) { lifetime -= deltaTime; }
    
    // Material mutators (for dynamic cubes)
    void setMaterial(const std::string& material);
    
    // Utility methods
    void hide() { visible = false; }
    void show() { visible = true; }
    void breakApart() { broken = true; }
    void repair() { broken = false; }
    
    // Material utility methods (for dynamic cubes)
    void applyMaterialProperties();
    void applyMaterialProperties(const std::string& newMaterialName);
    glm::vec3 getEffectiveColor() const;
    glm::vec3 getWorldPosition() const;
    
    // Static utility methods
    static float getScale() { return CUBE_SCALE; }
    
private:
    glm::ivec3 position;        // World position in grid coordinates
    bool broken = false;        // Whether the cube is broken/damaged
    bool visible = true;        // Whether the cube should be rendered
    
    Physics::VoxelRigidBody* voxelBody = nullptr;
    
    // Physics position/rotation (for dynamic cubes - bypasses integer grid)
    glm::vec3 physicsPosition = glm::vec3(0.0f);
    glm::vec4 physicsRotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); // Identity quaternion
    glm::vec3 dynamicScale = glm::vec3(1.0f); // Scale for non-uniform dynamic objects
    
    // Material system (for dynamic cubes)
    std::string materialName = "Default";
    float lifetime = 30.0f;     // Lifetime in seconds (auto-cleanup after 30 seconds)
    
    // Destruction: accumulated damage from sub-threshold hits (0 = pristine).
    float accumulatedDamage = 0.0f;

    static constexpr float CUBE_SCALE = 1.0f; // Scale of each cube unit
};

} // namespace Phyxel
