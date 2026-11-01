#pragma once
// docs/WaterCore.md 48 (plan A4) - wading. Pure functions shared by the character (AnimatedVoxelCharacter's wading step), the
// editor's water feed and WadeDragTest; no engine state.
//
// The law (48.1 item 2): the legs push the body toward the wanted horizontal velocity with at most `driveAcc` (grip through the
// feet); the water pulls it toward the water's own velocity u with quadratic form drag on the submerged front,
//     a_drag = -k |v - u| (v - u),   k = 0.5 rho_w C_d A / m,   A = width x depth,   m = kgPerM3 x the controller box volume.
// Both terms are taken implicitly in one solve, so the terminal speed is exactly sqrt(driveAcc / k) at ANY step length: a
// character's wading speed never depends on the frame rate or the kinematic sub-step count (the table this replaces was applied
// once per sub-step and compounded within a long frame - WadeDragTest.TheOldTableCompoundsWithSubSteps is that red, kept).

#include "core/water/MovingSolids.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace Phyxel::Core::Water {

/// 48.2: characters enter the water core (displacement, stride rings, the slept-pond wake) - `water_coupling {characters}`.
/// Pinned by WadeDragTest.TheDefaultsArePinned.
inline constexpr bool kWadeCharactersByDefault = true;

struct WadeParams {
    float driveAcc = 2.5f;     ///< m/s^2: the most the legs push horizontally (~0.25 g of grip; a feel knob for the owner)
    float dragCd   = 1.05f;    ///< form-drag coefficient - VoxelDynamicsWorld's WATER_FORM_CD, the same law the CPU bodies use
    float rhoWater = 1000.0f;  ///< kg/m^3
    float kgPerM3  = 147.0f;   ///< body mass per m^3 of controller box: a 70 kg adult in the default 0.5 x 1.9 x 0.5 m box
    float minDepth = 0.05f;    ///< m: shallower than this is dry (the old code's threshold)
};

struct WadeResult {
    glm::vec2 velocity{0.0f};  ///< m/s, the new horizontal velocity
    float dragAcc = 0.0f;      ///< m/s^2: the drag's magnitude at the new velocity (echoed by water_coupling)
    bool wet = false;          ///< depth >= minDepth: the law acted
};

/// k (1/m) of the law for a controller of half-width `halfW`, half-height `halfH` standing in `depth` m of water.
inline float wadeDragK(float depth, float halfW, float halfH, const WadeParams& p = {}) {
    const float d = std::clamp(depth, 0.0f, 2.0f * halfH);   // past the head the front stops growing (no swimming in A4)
    const float w = 2.0f * std::max(halfW, 1e-3f);
    const float mass = p.kgPerM3 * w * w * 2.0f * std::max(halfH, 1e-3f);
    return 0.5f * p.rhoWater * p.dragCd * w * d / mass;
}

/// The terminal wading speed in still water (driving at full grip): sqrt(driveAcc / k).
inline float wadeTerminalSpeed(float depth, float halfW, float halfH, const WadeParams& p = {}) {
    const float k = wadeDragK(depth, halfW, halfH, p);
    return k > 0.0f ? std::sqrt(p.driveAcc / k) : INFINITY;
}

/// One step of the law. `v` the current horizontal velocity, `want` the input's (what the character would do on land), `flow` the
/// water's horizontal velocity, `grounded` - no drive in the air (drag only). Dry (depth < minDepth): the velocity is `want`.
inline WadeResult wadeStep(glm::vec2 v, glm::vec2 want, glm::vec2 flow, float depth, float halfW, float halfH, bool grounded,
                           float dt, const WadeParams& p = {}) {
    WadeResult r;
    if (!(depth >= p.minDepth) || !(dt > 0.0f)) { r.velocity = want; return r; }
    r.wet = true;
    const float k = wadeDragK(depth, halfW, halfH, p);
    const float drive = grounded ? p.driveAcc : 0.0f;
    // the drive that lands exactly on `want` with the drag at `want`: within the grip, the character walks at `want`
    const glm::vec2 rw = want - flow;
    const glm::vec2 need = (want - v) / dt + k * glm::length(rw) * rw;
    const float needLen = glm::length(need);
    glm::vec2 out;
    if (grounded && needLen <= drive) {
        out = want;
    } else {
        const glm::vec2 a = needLen > 1e-9f ? need * (drive / needLen) : glm::vec2(0.0f);
        // implicit: w + k dt |w| w = b, w = v' - u, b = v + a dt - u  ->  w parallel to b, |w| = s, s + k dt s^2 = |b|
        const glm::vec2 b = v + a * dt - flow;
        const float bl = glm::length(b), kd = k * dt;
        const float s = (kd > 1e-12f) ? (std::sqrt(1.0f + 4.0f * kd * bl) - 1.0f) / (2.0f * kd) : bl;
        out = flow + (bl > 1e-12f ? b * (s / bl) : glm::vec2(0.0f));
    }
    r.velocity = out;
    const glm::vec2 rel = out - flow;
    r.dragAcc = k * glm::dot(rel, rel);
    return r;
}

/// 48.5 item 2: the world voxel column a point stands in - floor, never truncation (truncation reads the column EAST / SOUTH of
/// the true one at x < 0 - A2a's red).
inline int wadeColumn(float x) { return static_cast<int>(std::floor(x)); }

/// 48.1 item 1: the depth over the feet from a column's runs (highest first, as WaterCoreManager::ColumnLevels): the highest run
/// whose bottom is at or below `probeY` (the feet + 0.05 m) - a body in a dry room or a cave under a pond reads the water it is
/// in, never the pond above it (A2e). 0 when the feet are under every run's bottom or above the chosen run's top.
inline float wadeDepthFromRuns(int n, const float* top, const float* bottom, float probeY) {
    const float feet = probeY - 0.05f;
    for (int k = 0; k < n; ++k)
        if (bottom[k] <= probeY) return std::max(0.0f, top[k] - feet);
    return 0.0f;
}

/// 48.6: how far outside the controller box the ring samples the water - past a stride's reach (a foot swings ~0.4 m ahead of the
/// controller's centre; at 1/3 m out the ring still met the legs' crest: 0.88-1.09 m read in 0.8 m of water, measured).
inline constexpr float kWadeRingOutM = 2.0f / 3.0f;

/// 48.6: the depth a wader stands in, from the water ROUND it - the mean of the wet ring samples (the four points kWadeRingOutM
/// outside the controller box along +-x, +-z), never its own column alone, which holds the crest its own legs pile up (measured
/// live: 0.93-1.30 m read in a 0.8 m channel - M4 met the same self-read with debris, 31.7 item 3). The centre decides wet or dry
/// (a wader on the bank with the ring over water stays dry); a ring point on dry land or in rock is skipped; no wet ring point ->
/// the centre's own reading. The ring's ahead and behind see the bow wave's crest and the trough, which cancel.
inline float wadeRingDepth(float centre, const float ring[4], float minDepth = 0.05f) {
    if (!(centre >= minDepth)) return centre;
    float sum = 0.0f; int n = 0;
    for (int i = 0; i < 4; ++i) if (ring[i] >= minDepth) { sum += ring[i]; ++n; }
    return n > 0 ? sum / static_cast<float>(n) : centre;
}

/// 48.5 item 1: a limb's oriented box as the core's axis-aligned moving solid - the AABB of the rotated box, shrunk evenly to the
/// limb's own volume (MovingSolid's rule: rotation ignored, volume exact). The shape follows the limb's lean; the water it
/// displaces is the limb's, not its bounding box's.
inline glm::vec3 limbSolidHalfExtents(const glm::vec3& halfExtents, const glm::quat& rotation) {
    const glm::mat3 R = glm::mat3_cast(rotation);
    glm::vec3 aabb(0.0f);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) aabb[i] += std::abs(R[j][i]) * halfExtents[j];
    const float vObb = halfExtents.x * halfExtents.y * halfExtents.z, vAabb = aabb.x * aabb.y * aabb.z;
    if (!(vAabb > 0.0f) || !(vObb > 0.0f)) return halfExtents;
    return aabb * std::cbrt(vObb / vAabb);
}

/// 48.1 item 5: the box a walking character wakes a slept pond with - its controller box grown along its horizontal velocity by
/// `leadS` seconds, so a pond wakes before the feet reach it. `feet` is the controller's feet point.
inline MovingSolid wadeWakeBox(const glm::vec3& feet, float halfW, float halfH, const glm::vec3& velocity, float leadS = 0.5f) {
    MovingSolid m;
    const glm::vec2 reach(velocity.x * leadS * 0.5f, velocity.z * leadS * 0.5f);
    m.centre = feet + glm::vec3(reach.x, halfH, reach.y);
    m.halfExtents = glm::vec3(halfW + std::abs(reach.x), halfH, halfW + std::abs(reach.y));
    m.velocity = velocity;
    return m;
}

}  // namespace Phyxel::Core::Water
