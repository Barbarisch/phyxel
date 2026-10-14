#pragma once

// DebrisContact.h — the CPU mirror of voxel_contact.glsl's voxelPointContact, line for line.
// DebrisInteractionPlan 1c (step 3).
//
// GPU debris samples each body at 26 surface points; each point gets a signed distance to the
// static world from a 26-neighbour escape search. Until 1c that search ran over the debris
// solver's own CUBE bitfield, so debris could not see 1/3 slabs, fences or 2-micro walls. It now
// runs over the shared micro-resolution occupancy (VoxelLightOccupancy's packed pool), which is
// what this mirror is written against:
//
//   1. The cube state of the sample's cell and its 26 neighbours: empty / mixed / solid /
//      UNKNOWN. Any unknown (outside the pool's box, chunk not resident) -> the sample is
//      unknown, and the solver HOLDS the body this tick rather than let it fall through a world
//      it has no data for.
//   2. No mixed cube nearby -> exactly the old cube-level search, solid = full cube. A world of
//      full cubes therefore collides exactly as before (the settle bench is the regression check).
//   3. A mixed cube nearby -> the same search at MICRO resolution (1/9). If the sample is inside
//      solid deeper than that search can see (no empty micro neighbour), fall back to the cube
//      search with mixed cubes counted as solid, which escapes to the nearest empty CUBE.
//
// Exists for the same reason packedPoolSolidAt does: a contact model that is wrong inside a shader
// shows up as debris hovering or sinking, which nobody can attribute; wrong here, it fails a test.
// If you change one, change BOTH.

#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>

namespace Phyxel {
namespace DebrisContact {

enum class CubeState : uint8_t { Empty = 0, Mixed = 1, Solid = 2, Unknown = 3 };

struct PointContact {
    bool      hit     = false;   ///< a contact (penetrating or within margin) was found
    bool      unknown = false;   ///< some cell needed was unknown: hold the body, do not guess
    bool      micro   = false;   ///< resolved at micro resolution (a mixed cube was nearby)
    glm::vec3 n{0.0f};           ///< unit normal, out of the solid toward the point
    float     pen     = 0.0f;    ///< > 0 penetrating, < 0 separated (down to -margin), world units
    int       dirIdx  = -1;      ///< neighbour index 0..25 (warm-start feature)
};

/// Neighbour offset index d (0..25) over the 26-neighbourhood, centre excluded.
inline glm::ivec3 neighbourOffset(int d) {
    const int k = d < 13 ? d : d + 1;
    return glm::ivec3(k % 3, (k / 3) % 3, k / 9) - glm::ivec3(1);
}

/// How far (in micro cells) an INSIDE micro sample looks for the way out along each of the 26
/// directions. One cell is not enough: a body sunk 0.13 u into a 1/3 slab is 1.8 micro deep, and a
/// radius-1 search would see only solid, fall back to the cube search and kick it SIDEWAYS out of
/// the whole cube — the bubbling class of defect. 4 micro (0.44 u) covers a fast landing's
/// overshoot into any sub-voxel feature; deeper than that falls back to the cube search.
constexpr int kMicroEscapeSteps = 4;

/// Distance vector from fractional f to the nearest point of the cell k steps along o.
inline glm::vec3 toCellAlong(const glm::vec3& f, const glm::ivec3& o, int k) {
    glm::vec3 v(0.0f);
    for (int a = 0; a < 3; ++a) {
        if (o[a] > 0)      v[a] =  (static_cast<float>(k - 1) + (1.0f - f[a]));
        else if (o[a] < 0) v[a] = -(static_cast<float>(k - 1) + f[a]);
    }
    return v;
}

/// The escape search on a unit grid (cell size 1 in whatever units `x` is in).
/// `occupied(cell)` says whether a grid cell is solid. `margin` is in the same units.
/// INSIDE solid, each of the 26 directions is walked up to `maxSteps` cells to the first empty
/// one; OUTSIDE, only the adjacent cells matter (the margin is far below one cell). With
/// maxSteps = 1 this is exactly the original cube-level search.
template <class OccFn>
PointContact escapeSearch(const glm::vec3& x, float margin, OccFn occupied, int maxSteps = 1) {
    PointContact r;
    const glm::ivec3 c(static_cast<int>(std::floor(x.x)), static_cast<int>(std::floor(x.y)),
                       static_cast<int>(std::floor(x.z)));
    const glm::vec3 f = x - glm::vec3(c);
    const bool inside = occupied(c);
    const int steps = inside ? maxSteps : 1;
    float best = inside ? 1e30f : margin;
    glm::vec3 bestV(0.0f);
    for (int d = 0; d < 26; ++d) {
        const glm::ivec3 o = neighbourOffset(d);
        for (int k = 1; k <= steps; ++k) {
            // inside: toward EMPTY; outside: toward SOLID
            if (occupied(c + o * k) == inside) continue;
            const glm::vec3 v = toCellAlong(f, o, k);
            const float dist = glm::length(v);
            if (dist < best) { best = dist; bestV = v; r.dirIdx = d; }
            break;   // the first cell that differs along this direction is the way out
        }
    }
    if (r.dirIdx < 0) return r;                    // interior (inside) or nothing within margin
    r.hit = true;
    if (inside) {
        r.n   = (best > 1e-6f) ? bestV / best : glm::vec3(neighbourOffset(r.dirIdx));
        r.n   = glm::normalize(r.n);
        r.pen = best;
    } else {
        r.n   = (best > 1e-6f) ? -bestV / best : -glm::normalize(glm::vec3(neighbourOffset(r.dirIdx)));
        r.pen = -best;
    }
    return r;
}

/// THE debris point contact. `cubeState(worldCube)` -> CubeState; `microSolid(worldMicro)` -> bool
/// (world micro = world unit * 9). `x` and `margin` are in world units.
template <class CubeFn, class MicroFn>
PointContact pointContact(const glm::vec3& x, float margin, CubeFn cubeState, MicroFn microSolid) {
    const glm::ivec3 c(static_cast<int>(std::floor(x.x)), static_cast<int>(std::floor(x.y)),
                       static_cast<int>(std::floor(x.z)));
    CubeState st[27];
    bool anyMixed = false;
    for (int k = 0; k < 27; ++k) {
        const glm::ivec3 o = glm::ivec3(k % 3, (k / 3) % 3, k / 9) - glm::ivec3(1);
        st[k] = cubeState(c + o);
        if (st[k] == CubeState::Unknown) { PointContact u; u.unknown = true; return u; }
        if (st[k] == CubeState::Mixed) anyMixed = true;
    }
    auto stateAt = [&](const glm::ivec3& cube) {   // cube is within c's 27-neighbourhood
        const glm::ivec3 o = cube - c + glm::ivec3(1);
        return st[o.x + o.y * 3 + o.z * 9];
    };

    bool microInside = false;
    if (anyMixed) {
        const glm::vec3 xm = x * 9.0f;
        PointContact m = escapeSearch(xm, margin * 9.0f, microSolid, kMicroEscapeSteps);
        if (m.hit) {
            m.pen  /= 9.0f;
            m.micro = true;
            return m;
        }
        const glm::ivec3 cm(static_cast<int>(std::floor(xm.x)), static_cast<int>(std::floor(xm.y)),
                            static_cast<int>(std::floor(xm.z)));
        microInside = microSolid(cm);
        if (!microInside) return PointContact{};   // open air, nothing within margin
        // Deep inside sub-voxel solid: fall through to the cube search, mixed counted as solid.
    }
    return escapeSearch(x, margin, [&](const glm::ivec3& cube) {
        const CubeState s = stateAt(cube);
        return s == CubeState::Solid || (anyMixed && s == CubeState::Mixed);
    });
}

}  // namespace DebrisContact
}  // namespace Phyxel
