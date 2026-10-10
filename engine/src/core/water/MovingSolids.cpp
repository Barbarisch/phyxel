#include "core/water/MovingSolids.h"
#include "core/water/WaterCore.h"

#include <algorithm>
#include <cmath>

namespace Phyxel::Core::Water {

SolidRaster rasterizeSolids(const WaterGrid& g, const std::vector<MovingSolid>& bodies,
                            std::vector<float>& s, std::vector<uint8_t>& fresh) {
    SolidRaster r;
    const size_t n = g.cellCount();
    s.assign(n, 0.0f);
    fresh.assign(n, 0);
    const float h = g.h();
    const glm::ivec3 o = g.spec().origin;   // in cells
    const double V = static_cast<double>(g.cellVolume());
    for (const MovingSolid& b : bodies) {
        const glm::vec3 he = b.halfExtents;
        if (!std::isfinite(b.centre.x) || !std::isfinite(b.centre.y) || !std::isfinite(b.centre.z) ||
            !(he.x > 0.0f) || !(he.y > 0.0f) || !(he.z > 0.0f) || !std::isfinite(he.x + he.y + he.z)) { ++r.refused; continue; }
        ++r.bodies;
        // Everything in WORLD coordinates and WORLD cell indices: a cell's bounds are (world index) x h, so
        // every volume holding that cell does the same arithmetic and gets the same bits. (Grid-local
        // coordinates - centre minus the volume's origin - rounded differently per volume:
        // WorldAlignedRasterAcrossChunkSeam, red on its first run, 2026-10-09.)
        const glm::vec3 lo = b.centre - he, hi = b.centre + he;
        const int x0 = std::max(o.x, static_cast<int>(std::floor(lo.x / h))), x1 = std::min(o.x + g.nx() - 1, static_cast<int>(std::floor(hi.x / h)));
        const int y0 = std::max(o.y, static_cast<int>(std::floor(lo.y / h))), y1 = std::min(o.y + g.ny() - 1, static_cast<int>(std::floor(hi.y / h)));
        const int z0 = std::max(o.z, static_cast<int>(std::floor(lo.z / h))), z1 = std::min(o.z + g.nz() - 1, static_cast<int>(std::floor(hi.z / h)));
        auto overlap = [h](float a0, float a1, int c) {   // length of [a0, a1] inside world cell c along one axis
            return std::max(0.0f, std::min(a1, static_cast<float>(c + 1) * h) - std::max(a0, static_cast<float>(c) * h));
        };
        for (int wz = z0; wz <= z1; ++wz) {
            const float oz = overlap(lo.z, hi.z, wz);
            if (oz <= 0.0f) continue;
            const int z = wz - o.z;
            for (int wy = y0; wy <= y1; ++wy) {
                const float oy = overlap(lo.y, hi.y, wy);
                if (oy <= 0.0f) continue;
                const int y = wy - o.y;
                for (int wx = x0; wx <= x1; ++wx) {
                    const float ox = overlap(lo.x, hi.x, wx);
                    const int x = wx - o.x;
                    if (ox <= 0.0f) continue;
                    if (g.occ(x, y, z) != Occ::Air) continue;   // inside a static solid: no water to displace
                    const size_t i = g.idx(x, y, z);
                    s[i] = static_cast<float>(s[i] + static_cast<double>(ox) * oy * oz / V);
                    if (b.fresh) fresh[i] = 1;
                }
            }
        }
    }
    for (size_t i = 0; i < n; ++i) {
        if (s[i] <= 0.0f) continue;
        s[i] = std::min(s[i], 1.0f);   // two bodies overlapping one cell cannot fill more than the cell
        ++r.cells;
        r.volumeInside += static_cast<double>(s[i]) * V;
    }
    return r;
}

} // namespace Phyxel::Core::Water
