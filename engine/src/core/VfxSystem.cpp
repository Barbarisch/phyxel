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

glm::vec3 VfxSystem::randomUnitVector() {
    glm::vec3 v(frand(-1.0f, 1.0f), frand(-1.0f, 1.0f), frand(-1.0f, 1.0f));
    float len = glm::length(v);
    return (len > 1e-4f) ? v / len : glm::vec3(0.0f, 1.0f, 0.0f);
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

    // Move projectiles + beams + fields first — they emit particles into the
    // pool, which the integration loop below then ticks/counts.
    updateProjectiles(dt);
    updateBeams(dt);
    updateFields(dt);

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

// ---------------------------------------------------------------------------
// Projectiles (Phase 2)
// ---------------------------------------------------------------------------

int VfxSystem::emitParticle(const glm::vec3& pos, const glm::vec3& vel, float size,
                            const glm::vec4& color, float lifetime, float gravity, float drag) {
    int idx = allocParticle();
    if (idx < 0) return -1;
    VfxParticle& p = m_particles[idx];
    p.position    = pos;
    p.velocity    = vel;
    p.scale       = glm::vec3(size);
    p.color       = color;
    p.lifetime    = lifetime;
    p.maxLifetime = lifetime;
    p.gravity     = gravity;
    p.drag        = drag;
    p.active      = true;
    ++m_activeCount; // kept live; update()'s loop recomputes anyway
    return idx;
}

int VfxSystem::allocProjectile() {
    for (size_t i = 0; i < m_projectiles.size(); ++i) {
        if (!m_projectiles[i].active) return static_cast<int>(i);
    }
    if (m_projectiles.size() < MAX_PROJECTILES) {
        m_projectiles.emplace_back();
        return static_cast<int>(m_projectiles.size() - 1);
    }
    return -1;
}

void VfxSystem::spawnProjectile(const glm::vec3& origin, const VfxProjectileParams& params) {
    int idx = allocProjectile();
    if (idx < 0) return;
    VfxProjectile& pr = m_projectiles[idx];

    // Initial direction: toward target, or the explicit direction.
    glm::vec3 dir = params.useTarget ? (params.target - origin) : params.direction;
    float len = glm::length(dir);
    dir = (len > 1e-4f) ? dir / len : glm::vec3(0.0f, 0.0f, -1.0f);

    // Optional spread jitter (for missile fans).
    if (params.spread > 0.0f) {
        glm::vec3 jitter(frand(-1.0f, 1.0f), frand(-1.0f, 1.0f), frand(-1.0f, 1.0f));
        dir = glm::normalize(dir + jitter * params.spread);
    }

    pr.position       = origin;
    pr.velocity       = dir * params.speed;
    pr.target         = params.target;
    pr.useTarget      = params.useTarget;
    pr.homing         = params.homing;
    pr.homingStrength = params.homingStrength;
    pr.speed          = params.speed;
    pr.maxDistance    = params.maxDistance;
    pr.traveled       = 0.0f;
    pr.coreSize       = params.coreSize;
    pr.coreColor      = glm::vec4(params.color * params.intensity, 1.0f);
    pr.trailRate      = params.trailRate;
    pr.trailAccum     = 0.0f;
    pr.trailLifetime  = params.trailLifetime;
    pr.trailSize      = params.trailSize;
    pr.trailColor     = params.color * params.intensity;
    pr.arrivalEffect  = params.arrivalEffect;
    pr.active         = true;

    // Transient point light (follows the projectile).
    pr.lightId = -1;
    if (params.light && m_addLight) {
        pr.lightId = m_addLight(origin, params.color, params.lightIntensity, params.lightRadius);
    }

    ++m_activeProjectiles;
}

void VfxSystem::updateProjectiles(float dt) {
    if (m_projectiles.empty()) return;

    size_t active = 0;
    for (auto& pr : m_projectiles) {
        if (!pr.active) continue;

        // Homing: steer the velocity toward the target, preserving speed.
        if (pr.homing && pr.useTarget) {
            glm::vec3 toTarget = pr.target - pr.position;
            float d = glm::length(toTarget);
            if (d > 1e-4f) {
                glm::vec3 desired = (toTarget / d) * pr.speed;
                float t = glm::clamp(pr.homingStrength * dt, 0.0f, 1.0f);
                pr.velocity = pr.velocity * (1.0f - t) + desired * t;
                float sp = glm::length(pr.velocity);
                if (sp > 1e-4f) pr.velocity = pr.velocity / sp * pr.speed;
            }
        }

        glm::vec3 step = pr.velocity * dt;
        pr.position += step;
        pr.traveled += glm::length(step);

        // Bright leading core (short life so it rides with the projectile).
        emitParticle(pr.position, glm::vec3(0.0f), pr.coreSize, pr.coreColor,
                     0.10f, 0.0f, 2.0f);

        // Trail: drop fading particles along the path at a fixed rate.
        pr.trailAccum += pr.trailRate * dt;
        while (pr.trailAccum >= 1.0f) {
            pr.trailAccum -= 1.0f;
            glm::vec3 jitter(frand(-1.0f, 1.0f), frand(-1.0f, 1.0f), frand(-1.0f, 1.0f));
            glm::vec3 tpos = pr.position + jitter * (pr.trailSize * 0.6f);
            float life = pr.trailLifetime * frand(0.7f, 1.0f);
            float size = pr.trailSize * frand(0.7f, 1.1f);
            emitParticle(tpos, glm::vec3(0.0f), size,
                         glm::vec4(pr.trailColor * frand(0.8f, 1.0f), 1.0f),
                         life, 0.5f, 2.5f);
        }

        // Move the transient light with the projectile.
        if (pr.lightId >= 0 && m_moveLight) m_moveLight(pr.lightId, pr.position);

        // Arrival: reached the target, or travelled the max distance.
        float arriveDist = std::max(pr.arrivalRadius, glm::length(step) * 1.5f);
        bool arrived = pr.traveled >= pr.maxDistance ||
                       (pr.useTarget && glm::distance(pr.position, pr.target) <= arriveDist);

        if (arrived) {
            if (!pr.arrivalEffect.empty()) spawnEffect(pr.arrivalEffect, pr.position);
            if (pr.lightId >= 0 && m_removeLight) m_removeLight(pr.lightId);
            pr.lightId = -1;
            pr.active = false;
        } else {
            ++active;
        }
    }
    m_activeProjectiles = active;
}

int VfxSystem::castProjectile(const std::string& effect, const glm::vec3& origin, const glm::vec3& target) {
    VfxProjectileParams p;
    p.target = target;
    p.useTarget = true;

    if (effect == "magic_missile") {
        // A fan of homing violet darts that curve back to the target.
        int n = 4;
        for (int i = 0; i < n; ++i) {
            VfxProjectileParams mm;
            mm.target = target;
            mm.speed = 17.0f;
            mm.homing = true;
            mm.homingStrength = 7.0f;
            mm.spread = 0.45f;             // initial scatter, homing reels them in
            mm.coreSize = 0.18f;
            mm.trailRate = 110.0f;
            mm.trailLifetime = 0.35f;
            mm.trailSize = 0.12f;
            mm.intensity = 1.8f;
            mm.color = glm::vec3(0.5f, 0.25f, 1.0f);
            mm.light = false;              // 4 darts → skip per-dart lights
            mm.arrivalEffect = "magic_missile";
            spawnProjectile(origin, mm);
        }
        return n;
    }

    // Default / "fireball": one fiery projectile, room-lighting glow, AoE burst.
    p.speed = 14.0f;
    p.homing = false;
    p.coreSize = 0.36f;
    p.trailRate = 150.0f;
    p.trailLifetime = 0.5f;
    p.trailSize = 0.2f;
    p.intensity = 2.0f;
    p.color = glm::vec3(1.0f, 0.4f, 0.08f);
    p.light = true;
    p.lightRadius = 9.0f;
    p.lightIntensity = 2.8f;
    p.arrivalEffect = "fireball";
    spawnProjectile(origin, p);
    return 1;
}

// ---------------------------------------------------------------------------
// Beams (Phase 2)
// ---------------------------------------------------------------------------

int VfxSystem::allocBeam() {
    for (size_t i = 0; i < m_beams.size(); ++i) {
        if (!m_beams[i].active) return static_cast<int>(i);
    }
    if (m_beams.size() < MAX_BEAMS) {
        m_beams.emplace_back();
        return static_cast<int>(m_beams.size() - 1);
    }
    return -1;
}

void VfxSystem::spawnBeam(const glm::vec3& origin, const glm::vec3& target, const VfxBeamParams& params) {
    int idx = allocBeam();
    if (idx < 0) return;
    VfxBeam& b = m_beams[idx];
    b.origin           = origin;
    b.target           = target;
    b.duration         = params.duration;
    b.elapsed          = 0.0f;
    b.thickness        = params.thickness;
    b.density          = params.density;
    b.intensity        = params.intensity;
    b.particleLifetime = params.particleLifetime;
    b.color            = params.color;
    b.impactEffect     = params.impactEffect;

    b.lightId = -1;
    if (params.light && m_addLight) {
        b.lightId = m_addLight(target, params.color, params.lightIntensity, params.lightRadius);
    }
    b.active = true;
    ++m_activeBeams;
}

void VfxSystem::updateBeams(float dt) {
    if (m_beams.empty()) return;

    size_t active = 0;
    for (auto& b : m_beams) {
        if (!b.active) continue;
        b.elapsed += dt;

        glm::vec3 axis = b.target - b.origin;
        float length = glm::length(axis);
        glm::vec4 emissive(b.color * b.intensity, 1.0f);

        // Lay a flickering line of glowing cubes along the beam each frame.
        int count = std::max(1, static_cast<int>(length * b.density));
        for (int i = 0; i < count; ++i) {
            float t = (static_cast<float>(i) + frand(0.0f, 1.0f)) / static_cast<float>(count);
            glm::vec3 base = b.origin + axis * t;
            glm::vec3 jitter(frand(-1.0f, 1.0f), frand(-1.0f, 1.0f), frand(-1.0f, 1.0f));
            glm::vec3 pos = base + jitter * (b.thickness * 0.5f);
            float flicker = frand(0.6f, 1.0f);
            float size = b.thickness * frand(0.6f, 1.1f);
            emitParticle(pos, glm::vec3(0.0f), size,
                         glm::vec4(glm::vec3(emissive) * flicker, 1.0f),
                         b.particleLifetime, 0.0f, 0.0f);
        }
        // Brighter impact node at the target end.
        emitParticle(b.target, glm::vec3(0.0f), b.thickness * 1.5f,
                     glm::vec4(glm::vec3(emissive) * 1.3f, 1.0f),
                     b.particleLifetime, 0.0f, 0.0f);

        if (b.elapsed >= b.duration) {
            if (!b.impactEffect.empty()) spawnEffect(b.impactEffect, b.target);
            if (b.lightId >= 0 && m_removeLight) m_removeLight(b.lightId);
            b.lightId = -1;
            b.active = false;
        } else {
            ++active;
        }
    }
    m_activeBeams = active;
}

int VfxSystem::castBeam(const std::string& effect, const glm::vec3& origin, const glm::vec3& target) {
    VfxBeamParams p;
    if (effect == "eldritch_blast") {
        p.duration = 0.4f;
        p.thickness = 0.2f;
        p.density = 3.5f;
        p.intensity = 2.1f;
        p.color = glm::vec3(0.25f, 0.95f, 0.5f); // warlock green
        p.light = true;
        p.lightRadius = 7.0f;
        p.lightIntensity = 2.4f;
    }
    // (unknown effects use the default beam params)
    spawnBeam(origin, target, p);
    return 1;
}

// ---------------------------------------------------------------------------
// Fields / shells (Phase 2) — e.g. Shield
// ---------------------------------------------------------------------------

int VfxSystem::allocField() {
    for (size_t i = 0; i < m_fields.size(); ++i) {
        if (!m_fields[i].active) return static_cast<int>(i);
    }
    if (m_fields.size() < MAX_FIELDS) {
        m_fields.emplace_back();
        return static_cast<int>(m_fields.size() - 1);
    }
    return -1;
}

void VfxSystem::spawnField(const glm::vec3& center, const VfxFieldParams& params) {
    int idx = allocField();
    if (idx < 0) return;
    VfxField& f = m_fields[idx];
    f.center           = center;
    f.radius           = params.radius;
    f.duration         = params.duration;
    f.elapsed          = 0.0f;
    f.thickness        = params.thickness;
    f.density          = params.density;
    f.intensity        = params.intensity;
    f.particleLifetime = params.particleLifetime;
    f.pulseSpeed       = params.pulseSpeed;
    f.hemisphere       = params.hemisphere;
    f.color            = params.color;

    f.lightId = -1;
    if (params.light && m_addLight) {
        f.lightId = m_addLight(center, params.color, params.lightIntensity, params.lightRadius);
    }
    f.active = true;
    ++m_activeFields;
}

void VfxSystem::updateFields(float dt) {
    if (m_fields.empty()) return;

    size_t active = 0;
    for (auto& f : m_fields) {
        if (!f.active) continue;
        f.elapsed += dt;

        // Gentle "breathe": brightness shimmer + a small radius pulse.
        float pulse = 0.85f + 0.15f * std::sin(f.elapsed * f.pulseSpeed);
        float r = f.radius * (1.0f + 0.03f * std::sin(f.elapsed * f.pulseSpeed * 1.3f));
        glm::vec4 emissive(f.color * f.intensity, 1.0f);

        // Fade the shell out over the last 25% of its life.
        float fade = 1.0f;
        if (f.duration > 0.0f) {
            float lifeT = f.elapsed / f.duration; // 0..1
            if (lifeT > 0.75f) fade = std::max(0.0f, (1.0f - lifeT) / 0.25f);
        }

        int count = std::max(1, static_cast<int>(f.density));
        for (int i = 0; i < count; ++i) {
            glm::vec3 dir = randomUnitVector();
            if (f.hemisphere) dir.y = std::abs(dir.y);
            glm::vec3 pos = f.center + dir * (r + frand(-1.0f, 1.0f) * f.thickness * 0.5f);
            float flicker = frand(0.6f, 1.0f) * pulse * fade;
            float size = f.thickness * frand(0.7f, 1.1f);
            emitParticle(pos, glm::vec3(0.0f), size,
                         glm::vec4(glm::vec3(emissive) * flicker, 1.0f),
                         f.particleLifetime, 0.0f, 0.0f);
        }

        if (f.elapsed >= f.duration) {
            if (f.lightId >= 0 && m_removeLight) m_removeLight(f.lightId);
            f.lightId = -1;
            f.active = false;
        } else {
            ++active;
        }
    }
    m_activeFields = active;
}

int VfxSystem::castField(const std::string& effect, const glm::vec3& center) {
    VfxFieldParams p;
    if (effect == "shield") {
        p.radius = 2.6f;
        p.duration = 3.0f;
        p.thickness = 0.13f;
        p.density = 85.0f;
        p.intensity = 1.7f;
        p.pulseSpeed = 3.2f;
        p.hemisphere = false;            // full protective bubble
        p.color = glm::vec3(0.3f, 0.6f, 1.0f); // protective blue
        p.light = true;
        p.lightRadius = 6.0f;
        p.lightIntensity = 1.8f;
    }
    // (unknown effects use the default field params)
    spawnField(center, p);
    return 1;
}

} // namespace Phyxel
