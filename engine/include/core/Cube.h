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
 * @brief Direction enumeration for cube bonds
 * Used to index bond strengths in each cardinal direction
 */
enum class BondDirection : int {
    POSITIVE_X = 0,  // +X direction
    NEGATIVE_X = 1,  // -X direction  
    POSITIVE_Y = 2,  // +Y direction
    NEGATIVE_Y = 3,  // -Y direction
    POSITIVE_Z = 4,  // +Z direction
    NEGATIVE_Z = 5,  // -Z direction
    COUNT = 6
};

/**
 * @brief Bond properties for cube connections
 */
struct Bond {
    float strength = 100.0f;        // Force required to break this bond
    float accumulatedForce = 0.0f;  // Current accumulated force on this bond
    bool isBroken = false;          // Whether this bond is broken
    
    Bond() = default;
    Bond(float str) : strength(str) {}
    
    // Reset accumulated force (called each frame)
    void resetForce() { accumulatedForce = 0.0f; }
    
    // Add force to this bond
    void addForce(float force) { accumulatedForce += force; }
    
    // Check if bond should break
    bool shouldBreak() const { return !isBroken && accumulatedForce >= strength; }
    
    // Break the bond
    void breakBond() { isBroken = true; }
    
    // Repair the bond
    void repair() { isBroken = false; accumulatedForce = 0.0f; }
};

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
    
    // Bond system accessors
    const std::array<Bond, 6>& getBonds() const { return bonds; }
    const Bond& getBond(BondDirection direction) const { return bonds[static_cast<int>(direction)]; }
    float getBondStrength(BondDirection direction) const { return bonds[static_cast<int>(direction)].strength; }
    float getAccumulatedForce(BondDirection direction) const { return bonds[static_cast<int>(direction)].accumulatedForce; }
    bool isBondBroken(BondDirection direction) const { return bonds[static_cast<int>(direction)].isBroken; }
    
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
    
    // Bond system mutators
    void setBondStrength(BondDirection direction, float strength) { bonds[static_cast<int>(direction)].strength = strength; }
    void addForceToDirection(BondDirection direction, float force) { bonds[static_cast<int>(direction)].addForce(force); }
    void breakBond(BondDirection direction) { bonds[static_cast<int>(direction)].breakBond(); }
    void repairBond(BondDirection direction) { bonds[static_cast<int>(direction)].repair(); }
    void resetBondForces() { for (auto& bond : bonds) bond.resetForce(); }
    std::array<Bond, 6>& getBondsRef() { return bonds; }  // Non-const access for modifications
    
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
    
    // Bond utility methods
    void initializeBonds(float defaultStrength = 100.0f);
    bool hasAnyBrokenBonds() const;
    int getNumberOfBrokenBonds() const;
    std::vector<BondDirection> getBrokenBondDirections() const;
    
    // Static utility methods
    static float getScale() { return CUBE_SCALE; }
    static BondDirection getOppositeDirection(BondDirection direction);
    static glm::ivec3 getDirectionVector(BondDirection direction);
    static BondDirection vectorToDirection(const glm::ivec3& vector);
    
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
    
    // Bond system - stores connection strength to neighbors in each direction
    std::array<Bond, 6> bonds;  // Indexed by BondDirection enum
    
    static constexpr float CUBE_SCALE = 1.0f; // Scale of each cube unit
};

} // namespace Phyxel
