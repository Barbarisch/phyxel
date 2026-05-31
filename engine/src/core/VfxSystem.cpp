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

std::string VfxSystem::nextId(const char* prefix) {
    return std::string(prefix) + std::to_string(++m_instanceCounter);
}

void VfxSystem::emitEvent(VfxEvent::Type type, const std::string& id, const glm::vec3& pos) {
    // Cap the queue so events don't accumulate if nothing is draining them
    // (e.g. effects fired directly without the VfxDirector). Drop oldest.
    if (m_events.size() > 512) {
        m_events.erase(m_events.begin(), m_events.begin() + 256);
    }
    m_events.push_back(VfxEvent{type, id, pos});
}

std::vector<VfxEvent> VfxSystem::drainEvents() {
    std::vector<VfxEvent> out;
    out.swap(m_events);
    return out;
}

bool VfxSystem::dismiss(const std::string& instanceId) {
    for (auto& b : m_beams) {
        if (b.active && b.id == instanceId) {
            if (b.lightId >= 0 && m_removeLight) m_removeLight(b.lightId);
            b.lightId = -1; b.active = false;
            emitEvent(VfxEvent::Type::Expired, b.id, b.target);
            return true;
        }
    }
    for (auto& f : m_fields) {
        if (f.active && f.id == instanceId) {
            if (f.lightId >= 0 && m_removeLight) m_removeLight(f.lightId);
            f.lightId = -1; f.active = false;
            emitEvent(VfxEvent::Type::Expired, f.id, f.center);
            return true;
        }
    }
    return false;
}

glm::vec3 VfxSystem::randomUnitVector() {
    glm::vec3 v(frand(-1.0f, 1.0f), frand(-1.0f, 1.0f), frand(-1.0f, 1.0f));
    float len = glm::length(v);
    return (len > 1e-4f) ? v / len : glm::vec3(0.0f, 1.0f, 0.0f);
}

void VfxSystem::buildBasis(const glm::vec3& dir, glm::vec3& outU, glm::vec3& outV) {
    glm::vec3 n = glm::length(dir) > 1e-4f ? glm::normalize(dir) : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 ref = (std::abs(n.y) < 0.99f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    outU = glm::normalize(glm::cross(ref, n));
    outV = glm::cross(n, outU);
}

glm::vec3 VfxSystem::randomConeDir(const glm::vec3& axis, float halfAngleRad) {
    glm::vec3 n = glm::length(axis) > 1e-4f ? glm::normalize(axis) : glm::vec3(0.0f, 1.0f, 0.0f);
    // Uniform sample within a spherical cap of half-angle halfAngleRad.
    float cosTheta = 1.0f - frand() * (1.0f - std::cos(halfAngleRad));
    float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    float phi = frand() * 6.2831853f;
    glm::vec3 u, v;
    buildBasis(n, u, v);
    return glm::normalize(n * cosTheta + (u * std::cos(phi) + v * std::sin(phi)) * sinTheta);
}

glm::vec3 VfxSystem::randomShellPoint(VfxShape shape, const glm::vec3& center, float radius,
                                      const glm::vec3& dir, const glm::vec3& extent) {
    glm::vec3 n = glm::length(dir) > 1e-4f ? glm::normalize(dir) : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 u, v;
    buildBasis(n, u, v);
    switch (shape) {
        case VfxShape::Dome: {
            glm::vec3 d = randomUnitVector();
            d.y = std::abs(d.y);
            return center + d * radius;
        }
        case VfxShape::Cylinder: {
            float ang = frand() * 6.2831853f;
            float h = frand(-0.5f, 0.5f) * extent.y;             // height along axis n
            glm::vec3 ring = (u * std::cos(ang) + v * std::sin(ang)) * radius;
            return center + ring + n * h;
        }
        case VfxShape::Wall: {
            // Planar rectangle with normal n, spanning u (width=extent.x) and world-up-ish v (height=extent.y)
            float w = frand(-0.5f, 0.5f) * extent.x;
            float h = frand(-0.5f, 0.5f) * extent.y;
            return center + u * w + v * h;
        }
        case VfxShape::Line: {
            float t = frand(-0.5f, 0.5f) * extent.x;
            return center + n * t;
        }
        case VfxShape::Cube: {
            return center + glm::vec3(frand(-1.0f, 1.0f), frand(-1.0f, 1.0f), frand(-1.0f, 1.0f)) * extent;
        }
        case VfxShape::Sphere:
        default:
            return center + randomUnitVector() * radius;
    }
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

        // Outward velocity direction (and, for Cube, the start position) by shape.
        glm::vec3 dir;
        glm::vec3 startPos = position;
        switch (params.shape) {
            case VfxShape::Cone:
                dir = randomConeDir(params.direction, glm::radians(params.coneAngleDeg));
                break;
            case VfxShape::Dome: {
                dir = randomUnitVector();
                glm::vec3 axis = glm::length(params.direction) > 1e-4f ? glm::normalize(params.direction) : glm::vec3(0,1,0);
                if (glm::dot(dir, axis) < 0.0f) dir = -dir; // fold into hemisphere
                break;
            }
            case VfxShape::Ring: {
                glm::vec3 u, v; buildBasis(params.direction, u, v);
                float ang = frand() * 6.2831853f;
                dir = glm::normalize(u * std::cos(ang) + v * std::sin(ang));
                break;
            }
            case VfxShape::Cube: {
                startPos = position + glm::vec3(frand(-1.0f,1.0f), frand(-1.0f,1.0f), frand(-1.0f,1.0f)) * params.extent;
                glm::vec3 out = startPos - position;
                dir = (glm::length(out) > 1e-4f) ? glm::normalize(out) : glm::vec3(0,1,0);
                break;
            }
            case VfxShape::Sphere:
            default: {
                glm::vec3 d(frand(-1.0f, 1.0f), frand(-1.0f, 1.0f), frand(-1.0f, 1.0f));
                float len = glm::length(d);
                d = (len > 1e-4f) ? d / len : glm::vec3(0.0f, 1.0f, 0.0f);
                d.y = d.y * (1.0f - params.upBias) + params.upBias;
                len = glm::length(d);
                dir = (len > 1e-4f) ? d / len : glm::vec3(0,1,0);
                break;
            }
        }

        float speed = std::max(0.0f, params.speed + frand(-params.speedVar, params.speedVar));
        float life  = std::max(0.05f, params.lifetime + frand(-params.lifetimeVar, params.lifetimeVar));
        float size  = std::max(0.02f, params.size + frand(-params.sizeVar, params.sizeVar));

        p.position    = startPos;
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

std::string VfxSystem::spawnProjectile(const glm::vec3& origin, const VfxProjectileParams& params) {
    int idx = allocProjectile();
    if (idx < 0) return std::string();
    VfxProjectile& pr = m_projectiles[idx];
    pr.id = nextId("proj_");

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
    emitEvent(VfxEvent::Type::Spawned, pr.id, origin);
    return pr.id;
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
            // Decoupling seam: report the impact; the composition layer (VfxDirector)
            // decides what spawns there. arrivalEffect is kept only for legacy presets.
            emitEvent(VfxEvent::Type::Impact, pr.id, pr.position);
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

std::string VfxSystem::spawnBeam(const glm::vec3& origin, const glm::vec3& target, const VfxBeamParams& params) {
    int idx = allocBeam();
    if (idx < 0) return std::string();
    VfxBeam& b = m_beams[idx];
    b.id               = nextId("beam_");
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
    b.untilDismissed   = params.untilDismissed;
    b.tickInterval     = params.tickInterval;
    b.tickAccum        = 0.0f;
    b.anchorTarget     = params.anchorTarget;

    b.lightId = -1;
    if (params.light && m_addLight) {
        b.lightId = m_addLight(target, params.color, params.lightIntensity, params.lightRadius);
    }
    b.active = true;
    ++m_activeBeams;
    emitEvent(VfxEvent::Type::Spawned, b.id, origin);
    emitEvent(VfxEvent::Type::Impact, b.id, target); // beams strike instantly
    return b.id;
}

void VfxSystem::updateBeams(float dt) {
    if (m_beams.empty()) return;

    size_t active = 0;
    for (auto& b : m_beams) {
        if (!b.active) continue;
        b.elapsed += dt;

        // Anchor: track a moving target end (e.g. a tethering beam follows a creature).
        if (b.anchorTarget) {
            glm::vec3 p;
            if (b.anchorTarget(p)) {
                b.target = p;
                if (b.lightId >= 0 && m_moveLight) m_moveLight(b.lightId, p);
            } else {
                // anchor gone — end the beam
                if (b.lightId >= 0 && m_removeLight) m_removeLight(b.lightId);
                b.lightId = -1; b.active = false;
                emitEvent(VfxEvent::Type::Expired, b.id, b.target);
                continue;
            }
        }

        // Periodic tick events (e.g. channeled re-zap).
        if (b.tickInterval > 0.0f) {
            b.tickAccum += dt;
            while (b.tickAccum >= b.tickInterval) {
                b.tickAccum -= b.tickInterval;
                emitEvent(VfxEvent::Type::Tick, b.id, b.target);
            }
        }

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

        if (!b.untilDismissed && b.elapsed >= b.duration) {
            if (!b.impactEffect.empty()) spawnEffect(b.impactEffect, b.target);
            if (b.lightId >= 0 && m_removeLight) m_removeLight(b.lightId);
            b.lightId = -1;
            b.active = false;
            emitEvent(VfxEvent::Type::Expired, b.id, b.target);
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

std::string VfxSystem::spawnField(const glm::vec3& center, const VfxFieldParams& params) {
    int idx = allocField();
    if (idx < 0) return std::string();
    VfxField& f = m_fields[idx];
    f.id               = nextId("field_");
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
    f.shape            = params.hemisphere ? VfxShape::Dome : params.shape; // legacy hemisphere flag
    f.direction        = params.direction;
    f.extent           = params.extent;
    f.untilDismissed   = params.untilDismissed;
    f.tickInterval     = params.tickInterval;
    f.tickAccum        = 0.0f;
    f.anchor           = params.anchor;

    f.lightId = -1;
    if (params.light && m_addLight) {
        f.lightId = m_addLight(center, params.color, params.lightIntensity, params.lightRadius);
    }
    f.active = true;
    ++m_activeFields;
    emitEvent(VfxEvent::Type::Spawned, f.id, center);
    return f.id;
}

void VfxSystem::updateFields(float dt) {
    if (m_fields.empty()) return;

    size_t active = 0;
    for (auto& f : m_fields) {
        if (!f.active) continue;
        f.elapsed += dt;

        // Anchor: track a moving center (e.g. an aura that follows a creature).
        if (f.anchor) {
            glm::vec3 p;
            if (f.anchor(p)) {
                f.center = p;
                if (f.lightId >= 0 && m_moveLight) m_moveLight(f.lightId, p);
            } else {
                if (f.lightId >= 0 && m_removeLight) m_removeLight(f.lightId);
                f.lightId = -1; f.active = false;
                emitEvent(VfxEvent::Type::Expired, f.id, f.center);
                continue;
            }
        }

        // Periodic tick events (e.g. a zone that pulses each interval).
        if (f.tickInterval > 0.0f) {
            f.tickAccum += dt;
            while (f.tickAccum >= f.tickInterval) {
                f.tickAccum -= f.tickInterval;
                emitEvent(VfxEvent::Type::Tick, f.id, f.center);
            }
        }

        // Gentle "breathe": brightness shimmer + a small radius pulse.
        float pulse = 0.85f + 0.15f * std::sin(f.elapsed * f.pulseSpeed);
        float r = f.radius * (1.0f + 0.03f * std::sin(f.elapsed * f.pulseSpeed * 1.3f));
        glm::vec4 emissive(f.color * f.intensity, 1.0f);

        // Fade the shell out over the last 25% of its life (skip when persistent).
        float fade = 1.0f;
        if (!f.untilDismissed && f.duration > 0.0f) {
            float lifeT = f.elapsed / f.duration; // 0..1
            if (lifeT > 0.75f) fade = std::max(0.0f, (1.0f - lifeT) / 0.25f);
        }

        int count = std::max(1, static_cast<int>(f.density));
        for (int i = 0; i < count; ++i) {
            // Place on the shell shape, then jitter outward for shell thickness.
            glm::vec3 pos = randomShellPoint(f.shape, f.center, r, f.direction, f.extent);
            pos += randomUnitVector() * (frand(-1.0f, 1.0f) * f.thickness * 0.5f);
            float flicker = frand(0.6f, 1.0f) * pulse * fade;
            float size = f.thickness * frand(0.7f, 1.1f);
            emitParticle(pos, glm::vec3(0.0f), size,
                         glm::vec4(glm::vec3(emissive) * flicker, 1.0f),
                         f.particleLifetime, 0.0f, 0.0f);
        }

        if (!f.untilDismissed && f.elapsed >= f.duration) {
            if (f.lightId >= 0 && m_removeLight) m_removeLight(f.lightId);
            f.lightId = -1;
            f.active = false;
            emitEvent(VfxEvent::Type::Expired, f.id, f.center);
        } else {
            ++active;
        }
    }
    m_activeFields = active;
}

VfxShape VfxSystem::shapeFromString(const std::string& s) {
    if (s == "dome")     return VfxShape::Dome;
    if (s == "cone")     return VfxShape::Cone;
    if (s == "ring")     return VfxShape::Ring;
    if (s == "cube")     return VfxShape::Cube;
    if (s == "cylinder") return VfxShape::Cylinder;
    if (s == "wall")     return VfxShape::Wall;
    if (s == "line")     return VfxShape::Line;
    return VfxShape::Sphere;
}

int VfxSystem::castField(const std::string& effect, const glm::vec3& center, const std::string& shapeOverride) {
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
    if (!shapeOverride.empty()) {
        p.shape = shapeFromString(shapeOverride);
        if (p.shape == VfxShape::Cylinder) { p.extent = glm::vec3(0.0f, 5.0f, 0.0f); p.direction = glm::vec3(0,1,0); }
        else if (p.shape == VfxShape::Wall) { p.extent = glm::vec3(6.0f, 4.0f, 0.0f); p.direction = glm::vec3(0,0,1); }
        else if (p.shape == VfxShape::Line) { p.extent = glm::vec3(8.0f, 0.0f, 0.0f); p.direction = glm::vec3(1,0,0); }
    }
    spawnField(center, p);
    return 1;
}

} // namespace Phyxel
