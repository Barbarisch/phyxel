#include "core/AssetValidator.h"

#include <cmath>
#include <deque>
#include <sstream>
#include <unordered_set>

namespace Phyxel {
namespace Core {

namespace {

struct IVec3Hash {
    size_t operator()(const glm::ivec3& v) const {
        uint64_t x = static_cast<uint64_t>(v.x + (1 << 20)) & 0x1fffff;
        uint64_t y = static_cast<uint64_t>(v.y + (1 << 20)) & 0x1fffff;
        uint64_t z = static_cast<uint64_t>(v.z + (1 << 20)) & 0x1fffff;
        return std::hash<uint64_t>()(x | (y << 21) | (z << 42));
    }
};

std::string fmt(double v) {
    std::ostringstream os;
    os.setf(std::ios::fixed);
    os.precision(3);
    os << v;
    return os.str();
}

// Check one overall-size dimension against the canon, if the archetype declares it.
void checkDim(ValidationReport& r, const ArchetypeDims& arch, const std::string& key,
              double actualCubes) {
    if (!arch.has(key)) return;
    double canon = arch.value(key);
    double tol = arch.tolerance;
    if (std::fabs(actualCubes - canon) > tol) {
        r.addError("dim_out_of_range",
                   key + " " + fmt(actualCubes) + " cubes is outside " + fmt(canon)
                       + " +/- " + fmt(tol),
                   arch.name);
    }
}

} // namespace

int AssetValidator::connectedComponents(const MicroCanvas& canvas) {
    std::unordered_set<glm::ivec3, IVec3Hash> remaining;
    for (const auto& c : canvas.occupiedCells()) remaining.insert(c);
    if (remaining.empty()) return 0;

    const glm::ivec3 dirs[6] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    int components = 0;
    while (!remaining.empty()) {
        ++components;
        std::deque<glm::ivec3> q;
        glm::ivec3 seed = *remaining.begin();
        remaining.erase(remaining.begin());
        q.push_back(seed);
        while (!q.empty()) {
            glm::ivec3 p = q.front(); q.pop_front();
            for (const auto& d : dirs) {
                glm::ivec3 n = p + d;
                auto it = remaining.find(n);
                if (it != remaining.end()) { remaining.erase(it); q.push_back(n); }
            }
        }
    }
    return components;
}

ValidationReport AssetValidator::validate(const MicroCanvas& canvas,
                                          const ArchetypeDims& arch,
                                          const std::vector<std::string>& anchorIds,
                                          const Options& opts) {
    ValidationReport r;

    glm::ivec3 lo, hi;
    if (!canvas.microBounds(lo, hi)) {
        r.addError("empty_asset", "asset has no voxels", arch.name);
        return r;
    }

    // ---- dimensional (overall bounding size, in cubes) ----
    const double wCubes = (hi.x - lo.x + 1) / 9.0;
    const double hCubes = (hi.y - lo.y + 1) / 9.0;
    const double dCubes = (hi.z - lo.z + 1) / 9.0;
    const double horiz  = std::max(wCubes, dCubes);
    checkDim(r, arch, "height",   hCubes);
    checkDim(r, arch, "width",    wCubes);
    checkDim(r, arch, "depth",    dCubes);
    checkDim(r, arch, "length",   horiz);
    checkDim(r, arch, "diameter", horiz);

    // ---- structural: base rests on the floor plane (no float) ----
    if (lo.y > 0) {
        r.addError("not_on_floor",
                   "asset base floats " + fmt(lo.y / 9.0) + " cubes above the floor plane",
                   arch.name);
    } else if (lo.y < 0) {
        r.addError("below_floor", "asset extends below the floor plane (y<0)", arch.name);
    }

    // ---- structural: single connected component (no floating parts) ----
    int expected = arch.has("components") ? static_cast<int>(arch.value("components"))
                                          : opts.expectedComponents;
    int components = connectedComponents(canvas);
    if (components > expected) {
        r.addError("disconnected",
                   "asset has " + std::to_string(components) + " disconnected parts (expected "
                       + std::to_string(expected) + ") — floating geometry",
                   arch.name);
    }

    // ---- structural: voxel budget (advisory) ----
    int total = canvas.report().total();
    if (total > opts.maxVoxelBudget) {
        r.addWarning("over_budget",
                     "asset uses " + std::to_string(total) + " voxels (> budget "
                         + std::to_string(opts.maxVoxelBudget) + ")",
                     arch.name);
    }

    // ---- functional: required anchors present ----
    for (const auto& need : arch.anchors) {
        bool present = false;
        for (const auto& a : anchorIds) if (a == need) { present = true; break; }
        if (!present)
            r.addError("missing_anchor", "required interaction point '" + need + "' is absent",
                       arch.name);
    }

    // ---- symmetry (opt-in, advisory) ----
    if (arch.flag("symmetric")) {
        bool sym = true;
        int sumX = lo.x + hi.x;     // mirror axis: x -> sumX - x
        for (const auto& c : canvas.occupiedCells()) {
            if (!canvas.occupiedMicro(sumX - c.x, c.y, c.z)) { sym = false; break; }
        }
        if (!sym)
            r.addWarning("asymmetric", "asset is not mirror-symmetric across its X center",
                         arch.name);
    }

    return r;
}

} // namespace Core
} // namespace Phyxel
