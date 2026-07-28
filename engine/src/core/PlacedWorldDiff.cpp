#include "core/PlacedWorldDiff.h"

#include <algorithm>
#include <sstream>

namespace Phyxel {
namespace Core {

SolidMicroFn microSolidityFromAABB(
    const std::function<bool(const glm::vec3&, const glm::vec3&)>& solidAABB) {
    if (!solidAABB) return {};
    return [solidAABB](int mx, int my, int mz) {
        // A micro cell spans 1/9 m. Inset by a tenth of a cell so a neighbour's face
        // lying exactly on this cell's boundary does not read as filling it -- voxel
        // faces are exact planes and every stamped cell touches its neighbours.
        constexpr float kCell = 1.0f / 9.0f;
        constexpr float kInset = kCell * 0.1f;
        const glm::vec3 lo(mx * kCell + kInset, my * kCell + kInset, mz * kCell + kInset);
        const glm::vec3 hi((mx + 1) * kCell - kInset, (my + 1) * kCell - kInset,
                           (mz + 1) * kCell - kInset);
        return solidAABB(lo, hi);
    };
}

PlacedDiff diffCanvasAgainstWorld(const MicroCanvas& canvas, const glm::ivec3& originCubes,
                                  const SolidMicroFn& solidMicroAt, size_t maxReported,
                                  bool checkExtra) {
    PlacedDiff d;
    if (!solidMicroAt) return d;   // no world to compare against: no claim

    const glm::ivec3 originMicro = originCubes * 9;

    // ---- planned -> world (the DROP direction, the one that matters) ----
    const std::vector<glm::ivec3> cells = canvas.occupiedCells();
    d.plannedCells = static_cast<long>(cells.size());
    for (const auto& lc : cells) {
        const glm::ivec3 wc = originMicro + lc;
        if (solidMicroAt(wc.x, wc.y, wc.z)) {
            ++d.matchedCells;
            continue;
        }
        if (d.missing.size() < maxReported) d.missing.push_back({wc, lc});
        else d.truncated = true;
    }

    if (!checkExtra) return d;

    // ---- world -> planned (spill), bounded to the canvas's own micro AABB ----
    glm::ivec3 lo(0), hi(0);
    if (!canvas.microBounds(lo, hi)) return d;
    for (int x = lo.x; x <= hi.x; ++x)
        for (int y = lo.y; y <= hi.y; ++y)
            for (int z = lo.z; z <= hi.z; ++z) {
                if (canvas.occupiedMicro(x, y, z)) continue;
                const glm::ivec3 wc = originMicro + glm::ivec3(x, y, z);
                if (!solidMicroAt(wc.x, wc.y, wc.z)) continue;
                if (d.extra.size() < maxReported) d.extra.push_back({wc, {x, y, z}});
                else { d.truncated = true; return d; }
            }
    return d;
}

std::string PlacedDiff::summary() const {
    std::ostringstream os;
    os << plannedCells << " planned, " << matchedCells << " stamped (fidelity "
       << fidelity() << "); " << missing.size() << " MISSING";
    if (!extra.empty()) os << ", " << extra.size() << " extra";
    if (truncated) os << " [TRUNCATED - counts are partial]";
    for (size_t i = 0; i < missing.size() && i < 5; ++i) {
        const auto& m = missing[i];
        os << "\n  dropped: world micro (" << m.world.x << "," << m.world.y << "," << m.world.z
           << ") = cube (" << (m.world.x / 9) << "," << (m.world.y / 9) << "," << (m.world.z / 9)
           << "), canvas-local (" << m.local.x << "," << m.local.y << "," << m.local.z << ")";
    }
    if (missing.size() > 5) os << "\n  ... and " << (missing.size() - 5) << " more";
    return os.str();
}

}  // namespace Core
}  // namespace Phyxel
