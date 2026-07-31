#pragma once

// ============================================================================
// SpawnGate — a character must never be created INSIDE the world.
//
// USER DIRECTIVE (2026-07-28): "it should be impossible (by default) to generate
// a character inside a wall/object/static voxel."
//
// This is the character-side analog of the settlement grounding gate (structures
// refuse to build over air unless allow_ungrounded is passed): a spawn request
// whose body volume intersects static geometry is RESOLVED to the nearest clear
// standing position, and REFUSED outright if no clear position exists nearby.
// Silently spawning an embedded character is never an outcome.
//
// WHY A NEW MODULE rather than reusing StructureBuildService::snapToStandable:
// that helper is CUBE-granular (it takes ivec3 and asks solidAt(cell)), so it is
// blind to exactly the geometry characters get stuck in — interior partitions are
// ~2 MICRO thick and live inside a cube that reads "not solid". It also returns
// the requested cell unchanged when it finds nothing, which is an honest no-op for
// a location anchor but is precisely the silent-embed failure here.
//
// The solidity predicate is therefore an AABB query in WORLD FLOAT space, and it
// must be RESOLUTION-COMPLETE (cube + subcube + microcube) — i.e. the same query
// the character actually collides against
// (Physics::VoxelDynamicsWorld::anyStaticSolidInAABB). Injected, so this stays
// pure and unit-testable with no engine.
// ============================================================================

#include <functional>
#include <string>

#include <glm/glm.hpp>

namespace Phyxel {
namespace Core {

/// The character's collision volume, as a box standing on its feet position.
/// Defaults match AnimatedVoxelCharacter (m_originalHalfWidth 0.25 m, ~1.75 m tall).
struct CharacterBounds {
    float halfWidth = 0.25f;
    float height    = 1.75f;
};

enum class SpawnOutcome {
    Clear,      ///< the requested position was already free — spawn there
    Relocated,  ///< it was embedded; a nearby clear position was found
    Refused     ///< it was embedded and nothing clear was found — DO NOT SPAWN
};

struct SpawnResult {
    SpawnOutcome outcome = SpawnOutcome::Clear;
    glm::vec3 position{0.0f};    ///< where to actually spawn (== requested when Clear)
    glm::vec3 requested{0.0f};
    float movedDistance = 0.0f;  ///< how far the resolution had to move it (m)
    bool supported = true;       ///< something solid under the feet (else it will fall)
    std::string reason;          ///< human-readable; surfaced in logs / API responses

    bool ok() const { return outcome != SpawnOutcome::Refused; }
};

/// solid(lo, hi) -> is ANY static solid inside this world-space AABB?
/// Must be resolution-complete; a cube-only query will miss thin walls and this
/// whole gate becomes decorative.
using SolidAABBFn = std::function<bool(const glm::vec3& lo, const glm::vec3& hi)>;

/// Does the character's body at `feet` intersect static geometry?
bool spawnIsEmbedded(const SolidAABBFn& solid, const glm::vec3& feet,
                     const CharacterBounds& body = {});

/// Is there something solid under the feet within `probeDepth`?
bool spawnIsSupported(const SolidAABBFn& solid, const glm::vec3& feet,
                      const CharacterBounds& body = {}, float probeDepth = 0.34f);

/// Resolve a spawn request. Already clear -> Clear, unchanged. Embedded -> search
/// outward on the SUBCUBE grid (1/3 m — fine enough to step out of a thin wall
/// instead of teleporting a whole cube) for a clear position, preferring same-level
/// and supported candidates; -> Relocated. Nothing clear within `searchRadius`
/// -> Refused, and the caller must not spawn.
SpawnResult resolveSpawn(const SolidAABBFn& solid, const glm::vec3& requested,
                         const CharacterBounds& body = {}, float searchRadius = 4.0f);

/// resolveSpawn, then -- if that bounded lateral search finds nothing -- CLIMB the column
/// for the first height at which the body is clear, preferring one that is also supported.
/// Refused only if neither succeeds within `maxClimb`.
///
/// Two hard-won properties, both regression-tested:
///   * The climb must be BODY-aware. An earlier version delegated to a FEET-CUBE helper
///     (groundSpawnYIfInsideSolid), which early-returns when the feet cell is empty -- the
///     exact case needing rescue (feet in an air pocket, body in rock). It was inert there.
///   * The climb must NOT re-run the full resolveSpawn per step. Its lateral ring search is
///     meaningless while ascending a column, and doing it anyway cost ~1536 x ~5,000-11,000
///     solidity queries -- MEASURED stalling the engine's main loop 27 SECONDS on a spawn
///     deep inside a tall column. Each step is now a couple of AABB tests.
SpawnResult resolveSpawnWithClimb(const SolidAABBFn& solid, const glm::vec3& requested,
                                  const CharacterBounds& body = {}, float searchRadius = 4.0f,
                                  float maxClimb = 512.0f);

/// SECOND-PASS check for a body that ALREADY EXISTS, once its real size is known.
///
/// The pre-construction gate necessarily assumes a body size, and the only size it can
/// assume is the humanoid default -- a species' real capsule is resolved during
/// construction (resizeController measures the loaded skeleton). BodyPlan clamps
/// half-width to [0.12, 0.60], so for a wolf, horse or dragon that assumption is wrong
/// by up to 2.4x: a 0.9 m gap reads Clear for a humanoid while the creature actually
/// created there is embedded in the walls either side.
///
/// Clear     -> leave it where it is (the common case; near-humanoids short-circuit).
/// Relocated -> move it to `position`.
/// Refused   -> the caller must REMOVE the entity completely, including any registry
///              entry made before this ran -- erasing an owning pointer alone leaves a
///              dangling raw pointer behind (solution-auditor, round 7).
SpawnResult verifyPlacedBody(const SolidAABBFn& solid, const glm::vec3& at,
                             const CharacterBounds& realBody);

}  // namespace Core
}  // namespace Phyxel
