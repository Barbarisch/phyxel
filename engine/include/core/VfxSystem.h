#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <cstdint>

namespace Phyxel {

// One glowing voxel particle. Pure CPU data — integrated each frame and
// uploaded to the GPU as instanced cubes by Graphics::VfxRenderPipeline.
struct VfxParticle {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 scale{0.1f};
    glm::vec4 color{1.0f};   // rgb = emissive color, a = current brightness (fades to 0)
    float lifetime = 0.0f;   // remaining seconds
    float maxLifetime = 1.0f;
    float gravity = 0.0f;    // m/s^2 applied to velocity.y
    float drag = 0.0f;       // velocity damping per second
    bool active = false;
};

// Parameters for a radial burst emitter (Phase 1's only emitter type).
struct VfxBurstParams {
    int   count       = 40;
    float speed       = 6.0f;     // base outward speed (m/s)
    float speedVar    = 2.5f;     // +/- random speed
    float upBias      = 0.35f;    // 0 = full sphere, 1 = straight up; biases the burst upward
    float gravity     = -5.0f;    // gravity applied to particles (m/s^2)
    float drag        = 1.2f;     // velocity damping per second
    float lifetime    = 0.9f;     // seconds
    float lifetimeVar = 0.35f;
    float size        = 0.18f;    // cube edge length
    float sizeVar     = 0.08f;
    float intensity   = 1.6f;     // emissive boost baked into color
    glm::vec3 color{1.0f, 0.45f, 0.12f}; // fire orange by default
};

// Lightweight CPU particle VFX system for spell/effect bursts.
// Deliberately cheap: per-frame integrate + a single instanced draw, no
// physics collision and no GPU compute. See [[project-spell-vfx]].
class VfxSystem {
public:
    VfxSystem();

    // Integrate all active particles, fade them out, and recycle the dead.
    void update(float dt);

    // Spawn a radial burst of glowing voxel particles at a world position.
    void spawnBurst(const glm::vec3& position, const VfxBurstParams& params);

    // Trigger a named preset ("fireball", "magic_missile", "eldritch_blast",
    // "shield", "heal"). Unknown names fall back to a generic spark burst.
    // Returns the number of particles actually spawned.
    int spawnEffect(const std::string& effect, const glm::vec3& position);

    // Rendering access (mirrors DebrisSystem's interface).
    const std::vector<VfxParticle>& getParticles() const { return m_particles; }
    size_t getActiveCount() const { return m_activeCount; }

    static const char* effectList() {
        return "fireball, magic_missile, eldritch_blast, shield, heal, spark";
    }

private:
    int allocParticle();          // find/grow a free slot; -1 if at capacity
    float frand();                // [0, 1)
    float frand(float lo, float hi);

    std::vector<VfxParticle> m_particles;
    size_t m_activeCount = 0;
    size_t m_searchHint = 0;      // rotating start index for slot allocation
    uint32_t m_rngState = 0x9E3779B9u;

    static const size_t MAX_PARTICLES = 10000;
};

} // namespace Phyxel
