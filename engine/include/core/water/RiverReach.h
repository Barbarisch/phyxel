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

/// `inflowCos`: the current's angle to the inward normal must have a cosine above this to be an inflow edge (0.2: within ~78
/// degrees; a current running along the edge is level-only).
inline std::vector<RingColumn> ringBoundaries(const GridSpec& spec, const std::vector<ColumnRuns>& record, const RecordCurrentFn& current,
                                              float inflowCos = 0.2f) {
    std::vector<RingColumn> out;
    const int per = std::max(1, static_cast<int>(std::lround(1.0f / spec.h)));
    const float yLo = static_cast<float>(spec.origin.y) * spec.h, yHi = static_cast<float>(spec.origin.y + spec.dims.y) * spec.h;
    // per world column: the highest run inside the grid's height (its top) and the base of that run - order-free (max / min)
    std::map<std::pair<int, int>, std::pair<float, float>> col;   // (top, bottom)
    for (const ColumnRuns& c : record) for (const ColumnRun& r : c.runs) {
        if (r.topY <= yLo || r.bottomY >= yHi || r.topY <= r.bottomY) continue;
        auto it = col.find({c.x, c.z});
        if (it == col.end()) { col[{c.x, c.z}] = {r.topY, r.bottomY}; continue; }
        // runs of one column read in pieces (a vertical chunk border) join: the top is the highest, the bottom the lowest
        it->second.first = std::max(it->second.first, r.topY);
        it->second.second = std::min(it->second.second, r.bottomY);
    }
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
