#include "core/SpawnGate.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace Phyxel {
namespace Core {

namespace {
/// Search step = one SUBCUBE (1/3 m). A cube-sized step would overshoot: a character
/// wedged in a 2-micro partition only needs to move ~0.2 m to be clear, and jumping a
/// full metre would push it through the wall into the room on the far side.
constexpr float kStep = 1.0f / 3.0f;

/// Slight inset so a body standing FLUSH against a wall face doesn't read as embedded.
/// Voxel faces are exact planes; without this, every character backed against a wall
/// would be "inside" it and the gate would relocate people who are perfectly fine.
constexpr float kSkin = 0.01f;
}  // namespace

bool spawnIsEmbedded(const SolidAABBFn& solid, const glm::vec3& feet,
                     const CharacterBounds& body) {
    if (!solid) return false;
    const glm::vec3 lo(feet.x - body.halfWidth + kSkin, feet.y + kSkin,
                       feet.z - body.halfWidth + kSkin);
    const glm::vec3 hi(feet.x + body.halfWidth - kSkin, feet.y + body.height - kSkin,
                       feet.z + body.halfWidth - kSkin);
    return solid(lo, hi);
}

bool spawnIsSupported(const SolidAABBFn& solid, const glm::vec3& feet,
                      const CharacterBounds& body, float probeDepth) {
    if (!solid) return false;
    const glm::vec3 lo(feet.x - body.halfWidth + kSkin, feet.y - probeDepth,
                       feet.z - body.halfWidth + kSkin);
    const glm::vec3 hi(feet.x + body.halfWidth - kSkin, feet.y - kSkin,
                       feet.z + body.halfWidth - kSkin);
    return solid(lo, hi);
}

SpawnResult resolveSpawn(const SolidAABBFn& solid, const glm::vec3& requested,
                         const CharacterBounds& body, float searchRadius) {
    SpawnResult res;
    res.requested = requested;
    res.position = requested;

    // No solidity query = no claim. Returning Clear here is deliberate: a gate that
    // cannot see the world must not start refusing spawns on ignorance.
    if (!solid) {
        res.reason = "no solidity query available - spawn gate inactive";
        res.supported = false;
        return res;
    }

    if (!spawnIsEmbedded(solid, requested, body)) {
        res.outcome = SpawnOutcome::Clear;
        res.supported = spawnIsSupported(solid, requested, body);
        return res;
    }

    // Embedded. Search outward on the subcube grid. Vertical offsets are tried
    // SAME-LEVEL FIRST across the whole radius before climbing: stepping sideways out
    // of a wall into the room beats being lifted onto the roof above it.
    const int rings = std::max(1, static_cast<int>(std::ceil(searchRadius / kStep)));
    static constexpr float kDy[] = {0.0f, 1.0f / 3.0f, -1.0f / 3.0f, 2.0f / 3.0f,
                                    -2.0f / 3.0f, 1.0f, -1.0f, 2.0f, -2.0f};

    bool haveFallback = false;
    glm::vec3 fallback(0.0f);

    for (float dy : kDy) {
        for (int r = 1; r <= rings; ++r) {
            for (int ix = -r; ix <= r; ++ix) {
                for (int iz = -r; iz <= r; ++iz) {
                    if (std::max(std::abs(ix), std::abs(iz)) != r) continue;  // ring shell only
                    const glm::vec3 cand(requested.x + ix * kStep, requested.y + dy,
                                         requested.z + iz * kStep);
                    if (spawnIsEmbedded(solid, cand, body)) continue;

                    if (spawnIsSupported(solid, cand, body)) {
                        res.outcome = SpawnOutcome::Relocated;
                        res.position = cand;
                        res.supported = true;
                        res.movedDistance = glm::length(cand - requested);
                        std::ostringstream os;
                        os << "spawn was embedded in static geometry; relocated "
                           << res.movedDistance << " m to clear standing ground";
                        res.reason = os.str();
                        return res;
                    }
                    if (!haveFallback) { haveFallback = true; fallback = cand; }
                }
            }
        }
    }

    // Clear but unsupported: better than embedded (the character falls and lands)
    // but say so rather than pretending it is a good spawn.
    if (haveFallback) {
        res.outcome = SpawnOutcome::Relocated;
        res.position = fallback;
        res.supported = false;
        res.movedDistance = glm::length(fallback - requested);
        std::ostringstream os;
        os << "spawn was embedded in static geometry; relocated " << res.movedDistance
           << " m to clear but UNSUPPORTED air (the character will fall)";
        res.reason = os.str();
        return res;
    }

    res.outcome = SpawnOutcome::Refused;
    res.position = requested;
    res.supported = false;
    std::ostringstream os;
    os << "spawn REFUSED: the character body at (" << requested.x << ", " << requested.y
       << ", " << requested.z << ") is inside static geometry and no clear position was "
          "found within " << searchRadius << " m";
    res.reason = os.str();
    return res;
}

SpawnResult resolveSpawnWithClimb(const SolidAABBFn& solid, const glm::vec3& requested,
                                  const CharacterBounds& body, float searchRadius,
                                  float maxClimb) {
    SpawnResult res = resolveSpawn(solid, requested, body, searchRadius);
    if (res.ok() || !solid) return res;

    // The lateral search failed. Climb: find the first height where the body is clear,
    // preferring one that is also supported. Cheap per step by design (see header).
    const int steps = std::max(1, static_cast<int>(maxClimb / kStep));
    bool haveClear = false;
    glm::vec3 firstClear(0.0f);
    for (int i = 1; i <= steps; ++i) {
        const glm::vec3 up(requested.x, requested.y + i * kStep, requested.z);
        if (spawnIsEmbedded(solid, up, body)) continue;
        if (!haveClear) { haveClear = true; firstClear = up; }
        if (spawnIsSupported(solid, up, body)) {
            res.outcome = SpawnOutcome::Relocated;
            res.position = up;
            res.supported = true;
            res.movedDistance = glm::length(up - requested);
            res.reason = "lifted out of the solid column onto clear standing ground";
            return res;
        }
    }
    if (haveClear) {
        res.outcome = SpawnOutcome::Relocated;
        res.position = firstClear;
        res.supported = false;
        res.movedDistance = glm::length(firstClear - requested);
        res.reason = "lifted out of the solid column into clear but UNSUPPORTED air "
                     "(the character will fall)";
        return res;
    }
    // res is still the Refused from resolveSpawn -- position unchanged, reason intact.
    return res;
}

}  // namespace Core
}  // namespace Phyxel
