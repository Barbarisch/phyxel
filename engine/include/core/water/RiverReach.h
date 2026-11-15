#pragma once
// WaterCore §54.2 (W5.2): a near REACH of a generated river simulated, fed and drained at its edges from the record.
// The pure part: which grid columns on a reach box's outermost ring are river water in the record, their level and current,
// and whether each is an INFLOW edge (the record's current points into the box: level and velocity prescribed) or not (level
// only, the velocity left to the solver - amendment A1: a prescribed outflow forces the record's discharge out whatever
// arrives). A pure function of the grid's spec, the record's runs and the current - the same list whatever order the runs come
// in or however they were read (per chunk or whole: the design check's equality test, RiverReachTest).
#include "core/water/WaterCore.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace Phyxel { namespace Core { namespace Water {

/// The record's current at world column (wx, wz) for water [bottom, top] (world m) - m/s along x and z (W5.1: recordCurrentAt).
using RecordCurrentFn = std::function<glm::vec2(int wx, int wz, float top, float bottom)>;

struct RingColumn {
    glm::ivec2 local{0};      ///< grid column (x, z)
    glm::ivec2 world{0};      ///< its world voxel column
    float topY = 0.0f;        ///< the record's surface there (world m) - the level held
    float bottomY = 0.0f;     ///< the record's bed under it (world m)
    glm::vec2 vel{0.0f};      ///< the record's current there (m/s)
    glm::vec2 inward{0.0f};   ///< the ring's inward normal (a corner: the two sides', normalised)
    bool inflow = false;      ///< the current points into the box: level AND velocity prescribed
};

inline int riverFloorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

/// Runs read whole or chunk by chunk (pieces of one column split at a vertical chunk border, any order) joined per column: the
/// highest top and the lowest bottom - order-free (max / min). ringBoundaries' join; 54.3's test reads the record through it.
inline std::map<std::pair<int, int>, std::pair<float, float>> joinColumnRuns(const std::vector<ColumnRuns>& record, float yLo, float yHi) {
    std::map<std::pair<int, int>, std::pair<float, float>> col;   // (top, bottom)
    for (const ColumnRuns& c : record) for (const ColumnRun& r : c.runs) {
        if (r.topY <= yLo || r.bottomY >= yHi || r.topY <= r.bottomY) continue;
        auto it = col.find({c.x, c.z});
        if (it == col.end()) { col[{c.x, c.z}] = {r.topY, r.bottomY}; continue; }
        it->second.first = std::max(it->second.first, r.topY);
        it->second.second = std::min(it->second.second, r.bottomY);
    }
    return col;
}

/// `inflowCos`: the current's angle to the inward normal must have a cosine above this to be an inflow edge (0.2: within ~78
/// degrees; a current running along the edge is level-only).
inline std::vector<RingColumn> ringBoundaries(const GridSpec& spec, const std::vector<ColumnRuns>& record, const RecordCurrentFn& current,
                                              float inflowCos = 0.2f) {
    std::vector<RingColumn> out;
    const int per = std::max(1, static_cast<int>(std::lround(1.0f / spec.h)));
    const float yLo = static_cast<float>(spec.origin.y) * spec.h, yHi = static_cast<float>(spec.origin.y + spec.dims.y) * spec.h;
    // per world column: the highest run inside the grid's height (its top) and the base of that run - order-free (max / min);
    // runs of one column read in pieces (a vertical chunk border) join
    const auto col = joinColumnRuns(record, yLo, yHi);
    const int nx = spec.dims.x, nz = spec.dims.z;
    auto visit = [&](int x, int z) {
        glm::vec2 n(0.0f);
        if (x == 0) n.x += 1.0f; if (x == nx - 1) n.x -= 1.0f;
        if (z == 0) n.y += 1.0f; if (z == nz - 1) n.y -= 1.0f;
        if (n.x == 0.0f && n.y == 0.0f) return;
        n = glm::normalize(n);
        const int wx = riverFloorDiv(spec.origin.x + x, per), wz = riverFloorDiv(spec.origin.z + z, per);
        const auto it = col.find({wx, wz});
        if (it == col.end()) return;
        RingColumn rc; rc.local = {x, z}; rc.world = {wx, wz}; rc.topY = it->second.first; rc.bottomY = it->second.second; rc.inward = n;
        rc.vel = current ? current(wx, wz, rc.topY, rc.bottomY) : glm::vec2(0.0f);
        const float sp = glm::length(rc.vel);
        rc.inflow = sp > 1e-6f && glm::dot(rc.vel, n) > inflowCos * sp;
        out.push_back(rc);
    };
    // the ring in a fixed order: z = 0 row, z = nz-1 row, then the x = 0 and x = nx-1 columns between them
    for (int x = 0; x < nx; ++x) visit(x, 0);
    if (nz > 1) for (int x = 0; x < nx; ++x) visit(x, nz - 1);
    for (int z = 1; z < nz - 1; ++z) { visit(0, z); if (nx > 1) visit(nx - 1, z); }
    return out;
}

/// 54.3 (W5.2b): the record's water at one world column - its top run's surface and bed (world m); false = dry. What the follow
/// search reads (the manager: recordColumnRuns, the cached record; a test: a map of runs).
using RecordColumnFn = std::function<bool(int wx, int wz, float& top, float& bottom)>;

struct RiverPoint { bool found = false; int wx = 0, wz = 0; float top = 0.0f, bottom = 0.0f; float distM = 0.0f; };

/// 54.3 (W5.2b): the nearest RIVER column to the viewer within `radiusM` (horizontal) - a column the record holds water in whose
/// record current is not zero - sampled on a world-aligned `latticeM` lattice (columns wx, wz multiples of it: a pure function of
/// world position and the record, not of chunks or of where the viewer stands within a lattice cell). Ties: the lower z, then x
/// (lattice order). The follow trigger's search.
inline RiverPoint nearestRiverPoint(const glm::vec2& viewer, float radiusM, const RecordColumnFn& column, const RecordCurrentFn& current,
                                    int latticeM = 2) {
    RiverPoint best;
    if (!column || !current || radiusM <= 0.0f || latticeM < 1) return best;
    const int L = latticeM;
    const int i0 = riverFloorDiv(static_cast<int>(std::floor(viewer.x - radiusM)), L), i1 = riverFloorDiv(static_cast<int>(std::ceil(viewer.x + radiusM)), L) + 1;
    const int j0 = riverFloorDiv(static_cast<int>(std::floor(viewer.y - radiusM)), L), j1 = riverFloorDiv(static_cast<int>(std::ceil(viewer.y + radiusM)), L) + 1;
    float bestD2 = radiusM * radiusM;
    for (int j = j0; j <= j1; ++j) for (int i = i0; i <= i1; ++i) {
        const int wx = i * L, wz = j * L;
        const float dx = static_cast<float>(wx) + 0.5f - viewer.x, dz = static_cast<float>(wz) + 0.5f - viewer.y;
        const float d2 = dx * dx + dz * dz;
        if (d2 > bestD2 || (best.found && d2 == bestD2)) continue;   // the scan runs in lattice order: an equal one later loses the tie
        float top = 0.0f, bottom = 0.0f;
        if (!column(wx, wz, top, bottom) || top <= bottom) continue;
        const glm::vec2 v = current(wx, wz, top, bottom);
        if (v.x == 0.0f && v.y == 0.0f) continue;
        best.found = true; best.wx = wx; best.wz = wz; best.top = top; best.bottom = bottom; bestD2 = d2;
    }
    if (best.found) best.distM = std::sqrt(bestD2);
    return best;
}

/// The record's discharge across the ring (m^3/s): in = over the inflow columns, out = over the others whose current leaves the
/// box (each grid column is h wide; the depth is the record's).
inline void ringDischarge(const std::vector<RingColumn>& ring, float h, double& qIn, double& qOut) {
    qIn = 0.0; qOut = 0.0;
    for (const RingColumn& c : ring) {
        const double q = static_cast<double>(glm::dot(c.vel, c.inward)) * (c.topY - c.bottomY) * h;
        if (c.inflow) qIn += q; else if (q < 0.0) qOut -= q;
    }
}

}}} // namespace Phyxel::Core::Water
