#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <array>

// Forward declarations
class btRigidBody;

namespace VulkanCube {

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
    
    // Bond system accessors
    const std::array<Bond, 6>& getBonds() const { return bonds; }
    const Bond& getBond(BondDirection direction) const { return bonds[static_cast<int>(direction)]; }
    float getBondStrength(BondDirection direction) const { return bonds[static_cast<int>(direction)].strength; }
    float getAccumulatedForce(BondDirection direction) const { return bonds[static_cast<int>(direction)].accumulatedForce; }
    bool isBondBroken(BondDirection direction) const { return bonds[static_cast<int>(direction)].isBroken; }
    
    // Mutators
    void setPosition(const glm::ivec3& pos) { position = pos; }
    void setColor(const glm::vec3& col) { color = col; }
    void setOriginalColor(const glm::vec3& col) { originalColor = col; }
    void setBroken(bool isBroken) { broken = isBroken; }
    void setVisible(bool vis) { visible = vis; }
    void setRigidBody(btRigidBody* body) { rigidBody = body; }
    
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
    
    // Bond utility methods
    void initializeBonds(float defaultStrength = 100.0f);
    bool hasAnyBrokenBonds() const;
    int getNumberOfBrokenBonds() const;
    std::vector<BondDirection> getBrokenBondDirections() const;
    
    // Static utility methods
    static float getSize() { return CUBE_SIZE; }
    static BondDirection getOppositeDirection(BondDirection direction);
    static glm::ivec3 getDirectionVector(BondDirection direction);
    static BondDirection vectorToDirection(const glm::ivec3& vector);
    
private:
    glm::ivec3 position;        // World position in grid coordinates
    glm::vec3 color;            // RGB color (0.0 - 1.0) - current color (may be modified by hover, etc.)
    glm::vec3 originalColor;    // RGB color (0.0 - 1.0) - original color before any modifications
    bool broken = false;        // Whether the cube is broken/damaged
    bool visible = true;        // Whether the cube should be rendered
    
    // Physics body for dynamic cubes
    btRigidBody* rigidBody = nullptr;
    
    // Bond system - stores connection strength to neighbors in each direction
    std::array<Bond, 6> bonds;  // Indexed by BondDirection enum
    
    static constexpr float CUBE_SIZE = 1.0f; // Size of each cube unit
};

} // namespace VulkanCube
