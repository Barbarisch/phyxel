#include "core/VfxSystem.h"
#include "utils/Logger.h"
#include <algorithm>
#include <cmath>

namespace Phyxel {

VfxSystem::VfxSystem() {
    m_particles.reserve(512); // grow lazily; bursts are small
}

float VfxSystem::frand() {
    // xorshift32 — fast, deterministic, good enough for VFX jitter.
    m_rngState ^= m_rngState << 13;
    m_rngState ^= m_rngState >> 17;
    m_rngState ^= m_rngState << 5;
    return (m_rngState & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

float VfxSystem::frand(float lo, float hi) {
    return lo + (hi - lo) * frand();
}

int VfxSystem::allocParticle() {
    const size_t n = m_particles.size();
    // Scan from a rotating hint so repeated spawns spread across the vector.
    for (size_t i = 0; i < n; ++i) {
        size_t idx = (m_searchHint + i) % n;
        if (!m_particles[idx].active) {
            m_searchHint = (idx + 1) % (n ? n : 1);
            return static_cast<int>(idx);
        }
    }
    // No free slot — grow if we still have headroom.
    if (n < MAX_PARTICLES) {
        m_particles.emplace_back();
        return static_cast<int>(n);
    }
    return -1;
}

void VfxSystem::update(float dt) {
    if (dt <= 0.0f) return;

    size_t active = 0;
    for (auto& p : m_particles) {
        if (!p.active) continue;

        p.lifetime -= dt;
        if (p.lifetime <= 0.0f) {
            p.active = false;
            p.color.a = 0.0f;
            continue;
        }

        // Integrate motion.
        p.velocity.y += p.gravity * dt;
        float damp = std::max(0.0f, 1.0f - p.drag * dt);
        p.velocity *= damp;
        p.position += p.velocity * dt;

        // Fade brightness over remaining life (ease-out for a soft tail).
        float t = p.lifetime / p.maxLifetime; // 1 -> 0
        p.color.a = t * t;

        ++active;
    }
    m_activeCount = active;
}

void VfxSystem::spawnBurst(const glm::vec3& position, const VfxBurstParams& params) {
    glm::vec3 emissive = params.color * params.intensity;
    int spawned = 0;

    for (int i = 0; i < params.count; ++i) {
        int idx = allocParticle();
        if (idx < 0) break; // at capacity

        VfxParticle& p = m_particles[idx];

        // Random unit direction biased upward by upBias.
        glm::vec3 dir(frand(-1.0f, 1.0f), frand(-1.0f, 1.0f), frand(-1.0f, 1.0f));
        float len = glm::length(dir);
        dir = (len > 1e-4f) ? dir / len : glm::vec3(0.0f, 1.0f, 0.0f);
        dir.y = dir.y * (1.0f - params.upBias) + params.upBias;
        len = glm::length(dir);
        if (len > 1e-4f) dir /= len;

        float speed = std::max(0.0f, params.speed + frand(-params.speedVar, params.speedVar));
        float life  = std::max(0.05f, params.lifetime + frand(-params.lifetimeVar, params.lifetimeVar));
        float size  = std::max(0.02f, params.size + frand(-params.sizeVar, params.sizeVar));

        p.position    = position;
        p.velocity    = dir * speed;
        p.scale       = glm::vec3(size);
        p.gravity     = params.gravity;
        p.drag        = params.drag;
        p.lifetime    = life;
        p.maxLifetime = life;
        // Slight per-particle color jitter for a livelier look.
        p.color = glm::vec4(emissive * frand(0.8f, 1.15f), 1.0f);
        p.active = true;
        ++spawned;
        ++m_activeCount; // keep count live between updates
    }

    LOG_INFO("VfxSystem", "Burst spawned {} particles at ({}, {}, {})",
             spawned, position.x, position.y, position.z);
}

int VfxSystem::spawnEffect(const std::string& effect, const glm::vec3& position) {
    size_t before = m_activeCount;
    VfxBurstParams p;

    if (effect == "fireball") {
        p.count = 90;  p.speed = 7.0f;  p.speedVar = 3.0f; p.upBias = 0.2f;
        p.gravity = -3.0f; p.drag = 1.0f; p.lifetime = 1.0f; p.lifetimeVar = 0.4f;
        p.size = 0.22f; p.sizeVar = 0.1f; p.intensity = 1.8f;
        p.color = glm::vec3(1.0f, 0.4f, 0.08f);
    } else if (effect == "magic_missile") {
        p.count = 45; p.speed = 5.0f; p.speedVar = 2.0f; p.upBias = 0.25f;
        p.gravity = -2.0f; p.drag = 1.4f; p.lifetime = 0.8f; p.lifetimeVar = 0.3f;
        p.size = 0.14f; p.sizeVar = 0.06f; p.intensity = 1.7f;
        p.color = glm::vec3(0.5f, 0.25f, 1.0f); // arcane violet
    } else if (effect == "eldritch_blast") {
        p.count = 60; p.speed = 6.5f; p.speedVar = 2.5f; p.upBias = 0.15f;
        p.gravity = -1.0f; p.drag = 1.2f; p.lifetime = 0.85f; p.lifetimeVar = 0.3f;
        p.size = 0.16f; p.sizeVar = 0.07f; p.intensity = 1.9f;
        p.color = glm::vec3(0.2f, 0.9f, 0.6f); // sickly green
    } else if (effect == "shield") {
        p.count = 70; p.speed = 3.0f; p.speedVar = 1.0f; p.upBias = 0.4f;
        p.gravity = 0.5f; p.drag = 2.0f; p.lifetime = 1.1f; p.lifetimeVar = 0.3f;
        p.size = 0.15f; p.sizeVar = 0.05f; p.intensity = 1.5f;
        p.color = glm::vec3(0.3f, 0.6f, 1.0f); // protective blue
    } else if (effect == "heal") {
        p.count = 55; p.speed = 2.5f; p.speedVar = 1.0f; p.upBias = 0.8f;
        p.gravity = 1.5f; p.drag = 1.0f; p.lifetime = 1.3f; p.lifetimeVar = 0.4f;
        p.size = 0.13f; p.sizeVar = 0.05f; p.intensity = 1.6f;
        p.color = glm::vec3(1.0f, 0.95f, 0.4f); // golden
    } else {
        // "spark" / generic fallback.
        p.count = 40; p.color = glm::vec3(1.0f, 0.7f, 0.2f);
    }

    spawnBurst(position, p);
    return static_cast<int>(m_activeCount) - static_cast<int>(before);
}

} // namespace Phyxel
