#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

namespace Phyxel {
namespace Utils {

/// A character's body as a vertical capsule: feet at `base`, `height` tall, `radius` wide.
struct BodyCapsule {
    glm::vec3 base{0.0f};
    float     height = 1.8f;
    float     radius = 0.3f;
};

/// Does the segment a->b pass through the capsule? Used to hide the parts of a
/// ground marker (the target ring's dots, Ravenmere G-138) that lie BEHIND a body
/// from the camera's point of view: the dot is occluded when the camera->dot
/// segment crosses any character's capsule. Exact for a capsule = the set of points
/// within `radius` of the vertical core segment [base + r, base + height - r].
inline bool segmentHitsCapsule(const glm::vec3& a, const glm::vec3& b, const BodyCapsule& c) {
    const float coreLen = std::max(0.0f, c.height - 2.0f * c.radius);
    const glm::vec3 p0 = c.base + glm::vec3(0.0f, c.radius, 0.0f);          // core bottom
    const glm::vec3 p1 = p0 + glm::vec3(0.0f, coreLen, 0.0f);               // core top
    // Closest approach between segment a->b (param s) and the core p0->p1 (param t).
    const glm::vec3 d1 = b - a, d2 = p1 - p0, r = a - p0;
    const float A = glm::dot(d1, d1), E = glm::dot(d2, d2), F = glm::dot(d2, r);
    float s = 0.0f, t = 0.0f;
    if (A <= 1e-9f && E <= 1e-9f) {
        return glm::dot(r, r) <= c.radius * c.radius;
    }
    if (A <= 1e-9f) {
        t = std::clamp(F / E, 0.0f, 1.0f);
    } else {
        const float C = glm::dot(d1, r);
        if (E <= 1e-9f) {
            s = std::clamp(-C / A, 0.0f, 1.0f);
        } else {
            const float B = glm::dot(d1, d2);
            const float denom = A * E - B * B;
            s = denom != 0.0f ? std::clamp((B * F - C * E) / denom, 0.0f, 1.0f) : 0.0f;
            t = (B * s + F) / E;
            if (t < 0.0f)      { t = 0.0f; s = std::clamp(-C / A, 0.0f, 1.0f); }
            else if (t > 1.0f) { t = 1.0f; s = std::clamp((B - C) / A, 0.0f, 1.0f); }
        }
    }
    const glm::vec3 c1 = a + d1 * s, c2 = p0 + d2 * t;
    const glm::vec3 diff = c1 - c2;
    return glm::dot(diff, diff) <= c.radius * c.radius;
}

/// Is `p` inside the capsule? THE first-person clipping test for the camera's own character
/// (Ravenmere G-147): MmoRig switches to an eye view when the boom zooms under 1 u, and the boom
/// is also shortened by wall collision, so either way the camera can end up inside the player's
/// body and the mesh clips through the near plane. The renderer drops the owner from the MAIN
/// pass while this is true, and keeps it in the shadow pass so the player still casts a shadow.
///
/// Exact, and deliberately the same maths as segmentHitsCapsule with a degenerate segment: one
/// implementation to be wrong, and it already handles the zero-length case.
inline bool pointInsideCapsule(const glm::vec3& p, const BodyCapsule& c) {
    return segmentHitsCapsule(p, p, c);
}

} // namespace Utils
} // namespace Phyxel
