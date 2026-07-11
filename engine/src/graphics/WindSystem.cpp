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
    const float speed = glm::clamp(m_settings.speed, 0.0f, 2.0f);
    const float gusty = glm::clamp(m_settings.gustiness, 0.0f, 1.0f);

    // Direction wanders slowly around the mean; gustier weather = wider wander.
    float wanderDeg = (noise1(t * 0.02f) - 0.5f) * 2.0f * (8.0f + 22.0f * gusty);
    float theta     = glm::radians(m_settings.dirDegrees + wanderDeg);
    m_state.dir     = glm::vec2(std::cos(theta), std::sin(theta));

    // Base strength breathes over long periods; gusts ride on top of it. Both scale with
    // speed so speed = 0 zeroes EVERY term — vegetation must be perfectly still in dead calm.
    float breathe   = 0.85f + 0.3f * noise1(t * 0.05f + 7.31f);
    m_state.base    = 0.55f * speed * breathe;
    m_state.gustAmp = 1.5f * speed * (0.25f + 0.75f * gusty);

    // Gust field shape: stronger wind drives faster-travelling fronts; gustier weather forms
    // larger coherent fronts (lower spatial frequency) that read as waves sweeping the field.
    m_state.gustSpeed = 2.0f + 10.0f * speed;
    m_state.gustScale = 0.055f - 0.02f * gusty;
}

} // namespace Graphics
} // namespace Phyxel
