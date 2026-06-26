#include "core/SiteAnalysis.h"

#include <algorithm>
#include <cstdlib>

namespace Phyxel {
namespace Core {

double BuildabilityMap::buildableFraction() const {
    if (cells.empty()) return 0.0;
    int ok = 0;
    for (const auto& c : cells)
        if (c.cls == Buildability::Flat || c.cls == Buildability::SlopeOk) ++ok;
    return static_cast<double>(ok) / static_cast<double>(cells.size());
}

BuildabilityMap analyzeSite(int W, int D, int maxBuildableSlope,
                            const std::function<int(int, int)>& heightOf,
                            const std::function<bool(int, int)>& waterAt,
                            int flatSlope) {
    BuildabilityMap m;
    if (W <= 0 || D <= 0 || !heightOf) return m;
    m.W = W; m.D = D; m.maxBuildableSlope = maxBuildableSlope;
    m.cells.resize(static_cast<size_t>(W) * D);

    for (int z = 0; z < D; ++z)
        for (int x = 0; x < W; ++x) {
            SiteCell c;
            c.height = heightOf(x, z);
            c.water = waterAt ? waterAt(x, z) : false;
            // slope = the max ground-height delta to an in-bounds 4-neighbour (a cliff -> large slope).
            int slope = 0;
            const int nb[4][2] = {{x - 1, z}, {x + 1, z}, {x, z - 1}, {x, z + 1}};
            for (const auto& n : nb) {
                if (n[0] < 0 || n[0] >= W || n[1] < 0 || n[1] >= D) continue;
                slope = std::max(slope, std::abs(c.height - heightOf(n[0], n[1])));
            }
            c.slope = slope;
            if (c.water)                            c.cls = Buildability::Water;
            else if (c.slope <= flatSlope)          c.cls = Buildability::Flat;
            else if (c.slope <= maxBuildableSlope)  c.cls = Buildability::SlopeOk;
            else                                    c.cls = Buildability::TooSteep;
            m.cells[static_cast<size_t>(z) * W + x] = c;
        }
    return m;
}

} // namespace Core
} // namespace Phyxel
