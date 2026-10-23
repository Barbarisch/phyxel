#pragma once
// PlayBounds.h -- the horizontal extent a character may occupy (G-169).
//
// User (2026-09-23, Ravenmere manual play): "characters can walk off the edge of the map". The
// game's scenes are baked, fixed-size worlds (3x3 chunks); past the last chunk there was no
// ground and nothing blocked the step, so the player walked into the void and fell for ever.
//
// The edge is now a WALL, by construction: a kinematic character's position is clamped to the
// play bounds every frame in AnimatedVoxelCharacter::resolveKinematicMovement, so no input,
// path or push can take a body outside. Which bounds:
//   * authored  - a scene definition's `playBounds: {min:{x,z}, max:{x,z}}` (validated, refused
//                 when malformed) - a designer's smaller stage inside a bigger world;
//   * derived   - otherwise, the rectangle of the chunks the world HAS (ChunkManager), which for
//                 a static world is exactly where terrain exists;
//   * none      - a STREAMING world, whose extent grows ahead of the player; the residency hold
//                 (kinematicResidencyHold) covers the not-yet-streamed case there.
// Pure value type: unit-tested without a world.

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>
#include <utility>

namespace Phyxel::Core {

struct PlayBounds {
    bool  valid = false;
    // Half-open voxel-space extent: a position p is inside when minX <= p.x < maxX (same for z).
    float minX = 0.0f, maxX = 0.0f;
    float minZ = 0.0f, maxZ = 0.0f;

    static PlayBounds make(float x0, float z0, float x1, float z1) {
        PlayBounds b; b.valid = true; b.minX = x0; b.minZ = z0; b.maxX = x1; b.maxZ = z1; return b;
    }

    bool contains(const glm::vec3& p) const {
        return valid && p.x >= minX && p.x < maxX && p.z >= minZ && p.z < maxZ;
    }

    /// The nearest position whose whole body (half width `halfW`) is inside. Invalid bounds
    /// return `p` unchanged. A body wider than the extent sits at its centre.
    glm::vec3 clamp(const glm::vec3& p, float halfW) const {
        if (!valid) return p;
        glm::vec3 q = p;
        const float loX = minX + halfW, hiX = maxX - halfW;
        const float loZ = minZ + halfW, hiZ = maxZ - halfW;
        q.x = (loX <= hiX) ? std::clamp(p.x, loX, hiX) : 0.5f * (minX + maxX);
        q.z = (loZ <= hiZ) ? std::clamp(p.z, loZ, hiZ) : 0.5f * (minZ + maxZ);
        return q;
    }

    /// `{min:{x,z}, max:{x,z}}` -> bounds. Refused (false + reason), never clamped: an inverted
    /// or zero-area box is an authoring mistake, and a silent fix-up would hide it as a player
    /// pinned to one spot.
    static std::pair<bool, std::string> validate(const nlohmann::json& j) {
        if (!j.is_object()) return {false, "playBounds must be an object {min:{x,z}, max:{x,z}}"};
        for (const char* k : {"min", "max"}) {
            if (!j.contains(k) || !j[k].is_object()) return {false, std::string("playBounds.") + k + " must be an object {x,z}"};
            for (const char* a : {"x", "z"})
                if (!j[k].contains(a) || !j[k][a].is_number())
                    return {false, std::string("playBounds.") + k + "." + a + " must be a number"};
        }
        const float x0 = j["min"]["x"].get<float>(), x1 = j["max"]["x"].get<float>();
        const float z0 = j["min"]["z"].get<float>(), z1 = j["max"]["z"].get<float>();
        if (!(x1 > x0) || !(z1 > z0)) return {false, "playBounds.max must exceed playBounds.min on x and z"};
        return {true, ""};
    }

    static bool fromJson(const nlohmann::json& j, PlayBounds& out) {
        if (!validate(j).first) return false;
        out = make(j["min"]["x"].get<float>(), j["min"]["z"].get<float>(),
                   j["max"]["x"].get<float>(), j["max"]["z"].get<float>());
        return true;
    }

    nlohmann::json toJson() const {
        if (!valid) return nullptr;
        return {{"min", {{"x", minX}, {"z", minZ}}}, {"max", {{"x", maxX}, {"z", maxZ}}}};
    }
};

}  // namespace Phyxel::Core
