#include "core/ForceSystem.h"
#include "core/ChunkManager.h"
#include "core/Chunk.h"
#include "utils/Logger.h"
#include "utils/CoordinateUtils.h"
#include <algorithm>
#include <iostream>
#include <cmath>

namespace VulkanCube {

ForceSystem::ForceSystem() {
    // Initialize with default configuration
}

ForceSystem::ClickForce ForceSystem::calculateClickForce(const glm::vec2& mouseVelocity, 
                                                        const glm::vec3& rayOrigin, 
                                                        const glm::vec3& rayDirection,
                                                        const glm::vec3& hitPoint) const {
    ClickForce force;
    
    // Calculate base force direction (from camera to hit point)
    force.direction = glm::normalize(rayDirection);
    force.impactPoint = hitPoint;
    
    // Calculate velocity-based force magnitude
    float mouseSpeed = glm::length(mouseVelocity);
    float velocityForce = mouseSpeed * config.velocityMultiplier;
    
    // Combine base force with velocity component
    force.magnitude = config.baseForce + velocityForce;
    
    // Clamp to maximum force
    force.magnitude = std::min(force.magnitude, config.maxForce);
    
    LOG_DEBUG_FMT("ForceSystem", "[FORCE SYSTEM] Calculated click force: magnitude=" << force.magnitude 
              << ", mouse_speed=" << mouseSpeed << ", direction=(" 
              << force.direction.x << "," << force.direction.y << "," << force.direction.z << ")");
    
    return force;
}

ForceSystem::PropagationResult ForceSystem::propagateForce(const ClickForce& force, 
                                                          const glm::ivec3& startWorldPos,
                                                          ChunkManager* chunkManager) {
    PropagationResult result;
    std::vector<glm::ivec3> visited;
    
    LOG_DEBUG_FMT("ForceSystem", "[FORCE PROPAGATION] Starting propagation from (" 
              << startWorldPos.x << "," << startWorldPos.y << "," << startWorldPos.z 
              << ") with force magnitude " << force.magnitude);
    
    // Start recursive propagation
    propagateForceRecursive(startWorldPos, force.direction, force.magnitude, 0, 
                           chunkManager, visited, result);
    
    LOG_DEBUG_FMT("ForceSystem", "[FORCE PROPAGATION] Complete: " << result.brokenCubes.size() << " cubes broken, " 
              << result.damagedCubes.size() << " cubes damaged");
    
    return result;
}

void ForceSystem::propagateForceRecursive(const glm::ivec3& currentPos,
                                         const glm::vec3& forceDirection,
                                         float remainingForce,
                                         int currentDistance,
                                         ChunkManager* chunkManager,
                                         std::vector<glm::ivec3>& visited,
                                         PropagationResult& result) {
    
    // Check termination conditions
    if (currentDistance > config.maxPropagationDistance || 
        remainingForce < config.propagationThreshold) {
        return;
    }
    
    // Check if already visited
    if (std::find(visited.begin(), visited.end(), currentPos) != visited.end()) {
        return;
    }
    
    visited.push_back(currentPos);
    
    // Get cube at current position
    Cube* cube = getCubeAtWorldPosition(currentPos, chunkManager);
    if (!cube) {
        return; // No cube at this position
    }
    
    // Reset bond forces for this frame
    cube->resetBondForces();
    
    // Apply force to all bonds based on direction
    float totalDamageToThisCube = 0.0f;
    
    // Calculate force components for each bond direction
    for (int dir = 0; dir < 6; ++dir) {
        BondDirection bondDir = static_cast<BondDirection>(dir);
        glm::vec3 bondDirVector = Cube::getDirectionVector(bondDir);
        
        // Calculate how much of the total force affects this bond
        // Force affects bonds more when aligned with their direction
        float alignment = std::max(0.0f, glm::dot(forceDirection, bondDirVector));
        float bondForce = remainingForce * alignment;
        
        if (bondForce > 0.0f) {
            cube->addForceToDirection(bondDir, bondForce);
            totalDamageToThisCube += bondForce;
            
            // Check if bond should break
            if (cube->getBond(bondDir).shouldBreak()) {
                cube->breakBond(bondDir);
                LOG_DEBUG_FMT("ForceSystem", "[BOND BREAK] Bond broken at (" << currentPos.x << "," << currentPos.y << "," << currentPos.z << ") direction " << dir);
            }
        }
    }
    
    // Determine if entire cube should break
    bool cubeBreaks = shouldCubeBreak(cube, totalDamageToThisCube);
    
    if (cubeBreaks) {
        result.brokenCubes.push_back(currentPos);
        LOG_DEBUG_FMT("ForceSystem", "[CUBE BREAK] Cube breaks at (" << currentPos.x << "," << currentPos.y << "," << currentPos.z << ") from " << totalDamageToThisCube << " damage");
    } else if (totalDamageToThisCube > 0.0f) {
        result.damagedCubes.push_back(currentPos);
    }
    
    // Calculate remaining force for propagation
    float energyAbsorbed = totalDamageToThisCube * 0.3f; // Cube absorbs 30% of applied force
    float propagationForce = remainingForce - energyAbsorbed;
    propagationForce *= config.forceFalloffRate; // Apply distance falloff
    
    result.totalEnergyDissipated += energyAbsorbed;
    
    // Propagate to neighbors if force remains
    if (propagationForce >= config.propagationThreshold) {
        std::vector<glm::ivec3> neighbors = getNeighborPositions(currentPos);
        
        for (const glm::ivec3& neighborPos : neighbors) {
            // Calculate direction to neighbor for directional propagation
            glm::vec3 toNeighbor = worldPositionToDirection(currentPos, neighborPos);
            float directionAlignment = glm::dot(forceDirection, toNeighbor);
            
            // Force propagates more in the original direction
            if (directionAlignment > 0.1f) { // Only propagate somewhat in force direction
                float neighborForce = propagationForce * directionAlignment;
                propagateForceRecursive(neighborPos, forceDirection, neighborForce, 
                                      currentDistance + 1, chunkManager, visited, result);
            }
        }
    }
}

float ForceSystem::calculateBondDamage(float appliedForce, float bondStrength) const {
    return appliedForce; // Simple 1:1 damage for now
}

bool ForceSystem::shouldCubeBreak(Cube* cube, float totalDamageReceived) const {
    // Cube breaks if it received significant damage or has multiple broken bonds
    int brokenBonds = cube->getNumberOfBrokenBonds();
    
    // Break if:
    // 1. Received damage above breaking threshold
    // 2. Has 3 or more broken bonds (structurally compromised)
    // 3. Has 2 broken bonds and received moderate damage
    
    if (totalDamageReceived >= config.bondBreakingThreshold * 2.0f) {
        return true; // High damage
    }
    
    if (brokenBonds >= 3) {
        return true; // Structurally compromised
    }
    
    if (brokenBonds >= 2 && totalDamageReceived >= config.bondBreakingThreshold * 0.5f) {
        return true; // Moderate damage + structural weakness
    }
    
    return false;
}

std::vector<glm::ivec3> ForceSystem::getNeighborPositions(const glm::ivec3& worldPos) const {
    std::vector<glm::ivec3> neighbors;
    neighbors.reserve(6);
    
    // Add all 6 cardinal neighbors
    neighbors.push_back(worldPos + glm::ivec3(1, 0, 0));   // +X
    neighbors.push_back(worldPos + glm::ivec3(-1, 0, 0));  // -X
    neighbors.push_back(worldPos + glm::ivec3(0, 1, 0));   // +Y
    neighbors.push_back(worldPos + glm::ivec3(0, -1, 0));  // -Y
    neighbors.push_back(worldPos + glm::ivec3(0, 0, 1));   // +Z
    neighbors.push_back(worldPos + glm::ivec3(0, 0, -1));  // -Z
    
    return neighbors;
}

Cube* ForceSystem::getCubeAtWorldPosition(const glm::ivec3& worldPos, ChunkManager* chunkManager) const {
    if (!chunkManager) return nullptr;
    
    Chunk* chunk = chunkManager->getChunkAt(worldPos);
    if (!chunk) return nullptr;
    
    glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(worldPos);
    return chunk->getCubeAt(localPos);
}

glm::vec3 ForceSystem::worldPositionToDirection(const glm::ivec3& from, const glm::ivec3& to) {
    glm::vec3 direction = glm::vec3(to - from);
    float length = glm::length(direction);
    return length > 0.0f ? direction / length : glm::vec3(0.0f);
}

BondDirection ForceSystem::getDirectionBetweenCubes(const glm::ivec3& from, const glm::ivec3& to) {
    glm::ivec3 diff = to - from;
    return Cube::vectorToDirection(diff);
}

float ForceSystem::calculateDistanceFalloff(int distance, float falloffRate) {
    return std::pow(falloffRate, static_cast<float>(distance));
}

void ForceSystem::debugPrintPropagation(const PropagationResult& result) const {
    LOG_DEBUG("ForceSystem", "[FORCE DEBUG] Propagation Results:");
    LOG_DEBUG_FMT("ForceSystem", "  Broken cubes: " << result.brokenCubes.size());
    for (const auto& pos : result.brokenCubes) {
        LOG_DEBUG_FMT("ForceSystem", "    (" << pos.x << "," << pos.y << "," << pos.z << ")");
    }
    LOG_DEBUG_FMT("ForceSystem", "  Damaged cubes: " << result.damagedCubes.size());
    LOG_DEBUG_FMT("ForceSystem", "  Total energy dissipated: " << result.totalEnergyDissipated);
}

// MouseVelocityTracker implementation

MouseVelocityTracker::MouseVelocityTracker() : currentVelocity(0.0f) {
    samples.reserve(MAX_SAMPLES);
}

void MouseVelocityTracker::updatePosition(double x, double y) {
    glm::vec2 position(static_cast<float>(x), static_cast<float>(y));
    addSample(position);
    calculateVelocity();
}

void MouseVelocityTracker::reset() {
    samples.clear();
    currentVelocity = glm::vec2(0.0f);
}

void MouseVelocityTracker::addSample(const glm::vec2& position) {
    MouseSample sample;
    sample.position = position;
    sample.timestamp = std::chrono::high_resolution_clock::now();
    
    samples.push_back(sample);
    
    // Keep only recent samples
    if (samples.size() > MAX_SAMPLES) {
        samples.erase(samples.begin());
    }
}

void MouseVelocityTracker::calculateVelocity() {
    if (samples.size() < 2) {
        currentVelocity = glm::vec2(0.0f);
        return;
    }
    
    // Calculate velocity using most recent samples
    auto& newest = samples.back();
    auto& previous = samples[samples.size() - 2];
    
    auto timeDiff = std::chrono::duration_cast<std::chrono::microseconds>(
        newest.timestamp - previous.timestamp).count();
    
    if (timeDiff <= 0) {
        return; // Avoid division by zero
    }
    
    float deltaTimeSeconds = timeDiff / 1000000.0f;
    glm::vec2 positionDelta = newest.position - previous.position;
    glm::vec2 instantVelocity = positionDelta / deltaTimeSeconds;
    
    // Smooth the velocity with previous value
    currentVelocity = glm::mix(currentVelocity, instantVelocity, static_cast<float>(VELOCITY_SMOOTHING));
}

} // namespace VulkanCube