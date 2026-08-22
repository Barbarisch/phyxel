#include "graphics/WindSystem.h"

#include <cmath>
#include <cstdint>

namespace Phyxel {
namespace Graphics {

float WindSystem::hash1(int i) {
    // Integer avalanche hash (murmur-style finalizer) — platform-deterministic, unlike
    // the sin()-based GLSL idiom.
    uint32_t h = static_cast<uint32_t>(i) * 0x9E3779B9u;
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0x1000000);
}

float WindSystem::noise1(float x) {
    float fl = std::floor(x);
    int   i  = static_cast<int>(fl);
    float f  = x - fl;
    float u  = f * f * (3.0f - 2.0f * f);
    float a  = hash1(i);
    return a + (hash1(i + 1) - a) * u;
}

void WindSystem::tick(float t) {
    // dt for integrating the scroll. CLAMPED: a hitch, a breakpoint or the first frame must not
    // teleport the field — teleporting the field is precisely the bug being fixed.
    const float dt = (m_lastT < 0.0f) ? 0.0f : glm::clamp(t - m_lastT, 0.0f, 0.1f);
    m_lastT = t;

    const float speed = glm::clamp(m_settings.speed, 0.0f, 2.0f);
    const float gusty = glm::clamp(m_settings.gustiness, 0.0f, 1.0f);

    // ⚑THE PUBLISHED DIRECTION MUST NOT WANDER. State::dir reaches the shaders, where it both
    //  rotates the anisotropic gust transform and sets the blade sway axis. The rotation is about
    //  the WORLD ORIGIN, so a heading change displaces every sample by |q| * dTheta — and |q|
    //  reaches ~174 at the far end of the 2048 hash domain. Measured: 3.0 noise cells per DEGREE
    //  at that distance, so the old +/-18 degree wander swung the field by ~54 cells. That is the
    //  same time-amplified teleport the scroll integration fixed, relocated into the rotation.
    //  (Found by the user watching a plane that happens to sit near the wrap boundary, 2026-08-05.)
    const float theta = glm::radians(m_settings.dirDegrees);
    m_state.dir       = glm::vec2(std::cos(theta), std::sin(theta));

    // The wander survives where it is HARMLESS: as the heading the scroll is integrated along.
    // Integration means a heading change bends the field's future path instead of transforming
    // space, so fronts still drift naturally without anything teleporting. CPU-only — it is never
    // published, so it can never reach a position-dependent transform.
    const float wanderDeg = (noise1(t * 0.02f) - 0.5f) * 2.0f * (8.0f + 22.0f * gusty);
    const float thetaW    = glm::radians(m_settings.dirDegrees + wanderDeg);
    const glm::vec2 driftDir(std::cos(thetaW), std::sin(thetaW));

    // Base strength breathes over long periods; gusts ride on top of it. Both scale with
    // speed so speed = 0 zeroes EVERY term — vegetation must be perfectly still in dead calm.
    float breathe   = 0.85f + 0.3f * noise1(t * 0.05f + 7.31f);
    // BASE IS THE STEADY LEAN - the part that never lets go. Cut 0.55 -> 0.10 on 2026-08-05:
    // at 0.55 the grass sat permanently bent and gusts only nudged it, so the motion was invisible
    // even in the wind debug view (user: blades should be blue when still and green only when
    // pushed). Steady wind really does hold grass over, but for the look wanted here the GUSTS
    // must own most of the amplitude, not the constant term.
    m_state.base    = 0.10f * speed * breathe;
    m_state.gustAmp = 1.5f * speed * (0.25f + 0.75f * gusty);

    // Gust field shape: stronger wind drives faster-travelling fronts; gustier weather forms
    // larger coherent fronts (lower spatial frequency) that read as waves sweeping the field.
    // Calibrated so the default speed 0.35 gives 2.5 u/s — the approved travel rate. The old
    // 2.0 + 10.0*speed put the default at 5.5, which read as hurried.
    m_state.gustSpeed = 1.0f + 4.3f * speed;
    // Calibrated so the default gustiness 0.45 gives 0.045 — fronts ~22u deep, and ~110u
    // crosswind once State::aniso stretches them. Gustier weather still means longer fronts.
    // (History, so nobody relitigates it from half the story: an earlier pass lowered this to
    // 0.024-0.010*gusty for 42-71u fronts, then it was tuned BACK by eye — at that size a front
    // was larger than anything you could see moving. This curve is the surviving verdict.)
    // NOTE this OVERWRITES the header default every update — setting State in the header alone
    // does nothing, which is a good way to convince yourself a knob is broken.
    m_state.gustScale = 0.055f - 0.022f * gusty;

    // Tuning overrides last, so they win over the derivation instead of being erased by it.
    if (m_settings.gustScaleOverride > 0.0f) m_state.gustScale = m_settings.gustScaleOverride;
    if (m_settings.gustSpeedOverride > 0.0f) m_state.gustSpeed = m_settings.gustSpeedOverride;

    // INTEGRATE the field offset (see State::scroll for why this must not be dir*gustSpeed*t).
    // Uses the heading and speed decided THIS tick, so a direction change bends the field's future
    // path instead of retroactively rewriting where it has already travelled.
    // Wrapped to the hash domain's own 2048 period so a long session never loses float precision;
    // the wrap shifts gust phase only, which is imperceptible.
    m_state.scroll += driftDir * (m_state.gustSpeed * dt);
    m_state.scroll = glm::mod(m_state.scroll + glm::vec2(2048.0f), glm::vec2(2048.0f));
}

} // namespace Graphics
} // namespace Phyxel
