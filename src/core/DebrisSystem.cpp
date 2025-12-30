#include "core/DebrisSystem.h"
#include "core/ChunkVoxelQuerySystem.h"
#include "utils/CoordinateUtils.h"
#include "utils/Logger.h"
#include <algorithm>
#include <cmath>

namespace VulkanCube {

DebrisSystem::DebrisSystem(ChunkVoxelQuerySystem* voxelQuerySystem)
    : m_voxelQuerySystem(voxelQuerySystem) {
    m_particles.resize(MAX_PARTICLES);
    for (auto& p : m_particles) {
        p.active = false;
    }
}

DebrisSystem::~DebrisSystem() = default;

void DebrisSystem::spawnDebris(const glm::vec3& position, const glm::vec3& velocity, const glm::vec3& scale, const glm::vec4& color, float lifetime) {
    // Ring buffer approach to overwrite oldest particles
    static size_t nextIdx = 0;
    
    DebrisParticle& p = m_particles[nextIdx];
    p.position = position;
    // Initialize prevPosition based on velocity
    // velocity = (pos - prevPos) / dt  =>  prevPos = pos - velocity * dt
    // Assuming 60 FPS (16ms) for initialization
    p.prevPosition = position - velocity * 0.016f; 
    p.scale = scale;
    p.color = color;
    p.lifetime = lifetime;
    p.maxLifetime = lifetime;
    p.active = true;
    
    nextIdx = (nextIdx + 1) % MAX_PARTICLES;
    if (m_activeCount < MAX_PARTICLES) {
        m_activeCount++;
    }
}

void DebrisSystem::update(float dt) {
    // Clamp dt to avoid instability with large time steps
    float safeDt = std::min(dt, 0.05f);
    float dtSq = safeDt * safeDt;

    for (auto& p : m_particles) {
        if (!p.active) continue;

        p.lifetime -= safeDt;
        if (p.lifetime <= 0.0f) {
            p.active = false;
            if (m_activeCount > 0) m_activeCount--;
            continue;
        }

        // Verlet Integration
        // pos = pos + (pos - prevPos) * friction + gravity * dt * dt
        glm::vec3 velocity = p.position - p.prevPosition;
        p.prevPosition = p.position;
        
        // Apply gravity
        glm::vec3 gravity(0.0f, GRAVITY, 0.0f);
        p.position += velocity * FRICTION + gravity * dtSq;

        // Collision
        resolveCollision(p, safeDt);
    }
}

void DebrisSystem::resolveCollision(DebrisParticle& p, float dt) {
    // 1. Floor Collision
    if (p.position.y < 0.0f) {
        p.position.y = 0.0f;
        
        // Reflect velocity for bounce
        glm::vec3 vel = p.position - p.prevPosition;
        
        // Simple reflection: invert Y component
        // New Prev Pos = Current Pos - (Reflected Vel * Bounce)
        glm::vec3 reflectedVel = vel;
        reflectedVel.y = -reflectedVel.y;
        
        p.prevPosition = p.position - (reflectedVel * BOUNCE);
        
        // Apply ground friction to X/Z
        glm::vec3 frictionVel = p.position - p.prevPosition;
        frictionVel.x *= 0.8f;
        frictionVel.z *= 0.8f;
        p.prevPosition.x = p.position.x - frictionVel.x;
        p.prevPosition.z = p.position.z - frictionVel.z;
    }

    // 2. Voxel Collision
    if (m_voxelQuerySystem) {
        glm::ivec3 voxelPos = glm::floor(p.position);
        
        // Check if we are inside a solid voxel
        if (m_voxelQuerySystem->hasVoxelAt(voxelPos)) {
            // Simple resolution: push back to previous position
            // This is a "continuous collision" approximation
            glm::vec3 penetration = p.position - p.prevPosition;
            
            // If moving fast, we might have skipped through. 
            // But for simple debris, just reverting to prev pos is often enough
            // or pushing out to the nearest face.
            
            // For now, just revert and bounce
            p.position = p.prevPosition;
            
            // Dampen energy significantly on voxel hit
            glm::vec3 vel = p.position - p.prevPosition;
            p.prevPosition = p.position - (vel * 0.5f);
        }
    }
}

} // namespace VulkanCube
