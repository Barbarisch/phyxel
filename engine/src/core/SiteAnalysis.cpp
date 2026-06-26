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

BuildabilityMap analyzeSite(int W, int D, int maxBuildableRelief,
                            const std::function<int(int, int)>& heightOf,
                            const std::function<bool(int, int)>& waterAt,
                            int flatRelief, int window) {
    BuildabilityMap m;
    if (W <= 0 || D <= 0 || !heightOf) return m;
    if (window < 0) window = 0;
    m.W = W; m.D = D; m.maxBuildableRelief = maxBuildableRelief; m.window = window;
    m.cells.resize(static_cast<size_t>(W) * D);

    // Precompute the height grid once (heightOf may be expensive — a column scan).
    std::vector<int> h(static_cast<size_t>(W) * D);
    for (int z = 0; z < D; ++z)
        for (int x = 0; x < W; ++x) h[static_cast<size_t>(z) * W + x] = heightOf(x, z);

    for (int z = 0; z < D; ++z)
        for (int x = 0; x < W; ++x) {
            SiteCell c;
            c.height = h[static_cast<size_t>(z) * W + x];
            c.water = waterAt ? waterAt(x, z) : false;
            // relief = max-min ground height over the footprint window (in-bounds) — the cut/fill a
            // building of that footprint would need. A cliff/peak inside the window -> large relief.
            int hi = c.height, lo = c.height;
            for (int dz = -window; dz <= window; ++dz)
                for (int dx = -window; dx <= window; ++dx) {
                    const int nx = x + dx, nz = z + dz;
                    if (nx < 0 || nx >= W || nz < 0 || nz >= D) continue;
                    const int v = h[static_cast<size_t>(nz) * W + nx];
                    hi = std::max(hi, v); lo = std::min(lo, v);
                }
            c.relief = hi - lo;
            if (c.water)                              c.cls = Buildability::Water;
            else if (c.relief <= flatRelief)          c.cls = Buildability::Flat;
            else if (c.relief <= maxBuildableRelief)  c.cls = Buildability::SlopeOk;
            else                                      c.cls = Buildability::TooSteep;
            m.cells[static_cast<size_t>(z) * W + x] = c;
        }
    return m;
}

} // namespace Core
} // namespace Phyxel
