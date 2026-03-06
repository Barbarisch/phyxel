#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <chrono>
#include "core/Cube.h"

namespace Phyxel {

// Forward declarations
class Chunk;
class ChunkManager;

/**
 * @brief Force calculation and propagation system for cube breaking
 */
class ForceSystem {
public:
    ForceSystem();
    ~ForceSystem() = default;

    /**
     * @brief Configuration parameters for the force system
     */
    struct ForceConfig {
        float baseForce = 250.0f;                    // Base force from click
        float velocityMultiplier = 100.0f;           // How much mouse velocity affects force
        float maxForce = 1000.0f;                    // Maximum force that can be applied
        float forceFalloffRate = 0.7f;               // How much force diminishes with distance (0.7 = 30% reduction per step)
        int maxPropagationDistance = 4;              // Maximum distance force can propagate
        float propagationThreshold = 50.0f;          // Minimum force needed to propagate to next cube
        float bondBreakingThreshold = 100.0f;        // Default bond strength for standard cubes
    };

    /**
     * @brief Calculate force based on mouse click velocity and position
     */
    struct ClickForce {
        glm::vec3 direction;    // Direction of force (normalized)
        float magnitude;        // Force magnitude
        glm::vec3 impactPoint;  // World position where force is applied
    };

    /**
     * @brief Result of force propagation through the chunk system
     */
    struct PropagationResult {
        std::vector<glm::ivec3> brokenCubes;         // World positions of cubes that should break
        std::vector<glm::ivec3> damagedCubes;        // World positions of cubes that took damage but didn't break
        float totalEnergyDissipated = 0.0f;          // Total force energy that was absorbed
    };

    // Configuration accessors
    const ForceConfig& getConfig() const { return config; }
    void setConfig(const ForceConfig& newConfig) { config = newConfig; }

    // Force calculation
    ClickForce calculateClickForce(const glm::vec2& mouseVelocity, 
                                  const glm::vec3& rayOrigin, 
                                  const glm::vec3& rayDirection,
                                  const glm::vec3& hitPoint) const;

    // Force propagation through chunk system
    PropagationResult propagateForce(const ClickForce& force, 
                                   const glm::ivec3& startWorldPos,
                                   ChunkManager* chunkManager);

    // Utility methods
    static glm::vec3 worldPositionToDirection(const glm::ivec3& from, const glm::ivec3& to);
    static BondDirection getDirectionBetweenCubes(const glm::ivec3& from, const glm::ivec3& to);
    static float calculateDistanceFalloff(int distance, float falloffRate);

    // Debug methods
    void debugPrintPropagation(const PropagationResult& result) const;

private:
    ForceConfig config;

    // Internal propagation helpers
    void propagateForceRecursive(const glm::ivec3& currentPos,
                               const glm::vec3& forceDirection,
                               float remainingForce,
                               int currentDistance,
                               ChunkManager* chunkManager,
                               std::vector<glm::ivec3>& visited,
                               PropagationResult& result);

    // Bond damage calculation
    float calculateBondDamage(float appliedForce, float bondStrength) const;
    bool shouldCubeBreak(Cube* cube, float totalDamageReceived) const;

    // Neighbor finding
    std::vector<glm::ivec3> getNeighborPositions(const glm::ivec3& worldPos) const;
    Cube* getCubeAtWorldPosition(const glm::ivec3& worldPos, ChunkManager* chunkManager) const;
};

/**
 * @brief Mouse velocity tracker for force calculation
 */
class MouseVelocityTracker {
public:
    MouseVelocityTracker();

    // Update mouse position and calculate velocity
    void updatePosition(double x, double y);
    
    // Get current velocity (pixels per second)
    glm::vec2 getVelocity() const { return currentVelocity; }
    
    // Get velocity magnitude
    float getSpeed() const { return glm::length(currentVelocity); }
    
    // Reset tracking (call when mouse button is pressed)
    void reset();

private:
    struct MouseSample {
        glm::vec2 position;
        std::chrono::high_resolution_clock::time_point timestamp;
    };

    static constexpr int MAX_SAMPLES = 5;
    static constexpr double VELOCITY_SMOOTHING = 0.3; // How much to smooth velocity calculations

    std::vector<MouseSample> samples;
    glm::vec2 currentVelocity;
    
    void calculateVelocity();
    void addSample(const glm::vec2& position);
};

} // namespace Phyxel