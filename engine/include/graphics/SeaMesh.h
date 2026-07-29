#pragma once
//
// SeaMesh — the sea surface's geometry, as a camera-relative CARTESIAN CLIPMAP.
//
// WHY NOT THE OLD POLAR MESH. The sea used to be a camera-centred ring/sector grid. Its RADIAL
// spacing was uniform, which is what an earlier fix measured and (wrongly) concluded was enough —
// but on a polar mesh the ANGULAR spacing grows with radius, `arc = 2*pi*r / sectors`. Measured at
// 96 sectors against the 14-unit swell:
//
//     r =  60   arc =  3.9   0.28 wavelengths/segment   ok
//     r = 120   arc =  7.9   0.56                       aliased
//     r = 250   arc = 16.4   1.17                       aliased
//     r = 691   arc = 45.2   3.23                       aliased
//
// Nyquist needs <= 0.5, so everything past ~107 units aliased azimuthally. Aliasing inherits the
// symmetry of the sampling pattern, so a radial mesh turns it into radial spokes converging on the
// viewer — the reported "waves emanate from a point at the camera" and the vortex from above. It
// could not be tuned out: Nyquist at r=250 alone needs 224 sectors (~79k triangles), r=691 ~620.
//
// A clipmap fixes the SYMMETRY as well as the density. Nested uniform square levels, each 2x
// coarser and 2x wider than the one inside it, so:
//   * there is no centre singularity and no radial structure for an artifact to organise around;
//   * cost is per-level constant instead of growing with view distance — this mesh is ~27k
//     triangles reaching 1024 units, against the polar mesh's ~34k reaching 700;
//   * the outer levels are still coarser than the swell's Nyquist limit, and that is fine BECAUSE
//     water.vert fades each wave component out where the local spacing cannot sample it. Fading a
//     wave you cannot represent is correct; drawing it anyway is what produced the spokes.
//
// CRACKS. Levels of differing spacing meet along their boundaries, and a T-junction there shows as
// a pinhole straight through to the sky. This builder cannot produce one by construction: every
// vertex is deduplicated on the FINEST lattice, levels are emitted fine-to-coarse, and a coarse
// quad subdivides any edge whose midpoint a finer level already created. SeaMeshTest asserts it.
//
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace Phyxel {
namespace Graphics {

struct SeaMesh {
    // xz = world-space offset from the camera, in ABSOLUTE world units (the vertex shader adds the
    // camera position, so the wave field itself stays anchored to the world and does not follow the
    // viewer). y is unused and always 0 — the shader derives the local spacing from the radius, see
    // SEA_CORE_SPACING / SEA_CORE_HALF below, because a PER-VERTEX spacing would step by a factor of
    // two across a level boundary and crease the surface along that one row.
    std::vector<glm::vec3> vertices;
    std::vector<uint32_t>  indices;

    int   levels      = 0;   // 1 core + N ring levels
    float outerExtent = 0.0f;// half-extent of the outermost clipmap level (before the skirt)
    int   triangles() const { return static_cast<int>(indices.size() / 3); }
};

// The core level's spacing and half-extent. water.vert reconstructs the local grid spacing from a
// vertex's radius as `SEA_CORE_SPACING * max(1, r / SEA_CORE_HALF)`, which tracks the geometric
// level progression smoothly instead of stepping at each boundary.
// ⚑These two are consumed by the shader through push constants (NOT duplicated as shader literals) —
// this project has already been bitten by a pair of hand-synced struct definitions drifting apart.
constexpr float SEA_CORE_SPACING = 4.0f;    // 0.29 wavelengths at the 14-unit default swell
constexpr int   SEA_CELLS        = 64;      // cells per side, per level (must be a multiple of 4)
constexpr float SEA_CORE_HALF    = SEA_CORE_SPACING * SEA_CELLS * 0.5f;   // 128 units
constexpr int   SEA_MAX_LEVELS   = 7;       // cap: 128 * 2^6 = 8192 units of coverage
constexpr float SEA_SKIRT_RADIUS = 6000.0f; // flat coverage past any render distance

// Builds the clipmap. `waveRadius` is how far the surface must reach (from the render distance);
// levels are added until the outermost one covers it, up to SEA_MAX_LEVELS.
SeaMesh buildSeaClipmap(float waveRadius);

}  // namespace Graphics
}  // namespace Phyxel
