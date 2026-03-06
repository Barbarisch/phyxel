#pragma once

#include "Types.h"
#include <glm/glm.hpp>
#include <vector>
#include <memory>

namespace Phyxel {

class ChunkVoxelQuerySystem;

struct DebrisParticle {
    glm::vec3 position;
    glm::vec3 prevPosition; // For Verlet integration
    glm::vec3 scale;
    glm::vec4 color;
    float lifetime;
    float maxLifetime;
    bool active;
};

class DebrisSystem {
public:
    DebrisSystem(ChunkVoxelQuerySystem* voxelQuerySystem);
    ~DebrisSystem();

    void update(float dt);
    void spawnDebris(const glm::vec3& position, const glm::vec3& velocity, const glm::vec3& scale, const glm::vec4& color, float lifetime);
    
    // Access to particles for rendering
    const std::vector<DebrisParticle>& getParticles() const { return m_particles; }
    size_t getActiveParticleCount() const { return m_activeCount; }

private:
    void resolveCollision(DebrisParticle& particle, float dt);

    std::vector<DebrisParticle> m_particles;
    size_t m_activeCount = 0;
    ChunkVoxelQuerySystem* m_voxelQuerySystem;
    
    // Physics constants
    const float GRAVITY = -9.81f;
    const float FRICTION = 0.98f;
    const float BOUNCE = 0.5f;
    const size_t MAX_PARTICLES = 10000;
};

} // namespace Phyxel
