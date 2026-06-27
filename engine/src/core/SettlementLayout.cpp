#include "core/SettlementLayout.h"

#include <algorithm>

namespace Phyxel {
namespace Core {

std::vector<Plot> selectBuildablePlots(const BuildabilityMap& site, int plotSize, int spacing,
                                       int maxPlots) {
    std::vector<Plot> out;
    if (plotSize <= 0 || site.W < plotSize || site.D < plotSize) return out;

    // Candidate = a top-left where a fully-BUILDABLE plotSize x plotSize footprint fits. Score =
    // total relief over the footprint (lower = flatter = better).
    struct Cand { int x, z, score; };
    std::vector<Cand> cands;
    for (int z = 0; z + plotSize <= site.D; ++z)
        for (int x = 0; x + plotSize <= site.W; ++x) {
            int relief = 0; bool buildable = true;
            for (int dz = 0; dz < plotSize && buildable; ++dz)
                for (int dx = 0; dx < plotSize; ++dx) {
                    const SiteCell& c = site.at(x + dx, z + dz);
                    if (c.cls == Buildability::TooSteep || c.cls == Buildability::Water) {
                        buildable = false; break;          // any unbuildable cell disqualifies the plot
                    }
                    relief += c.relief;
                }
            if (buildable) cands.push_back({x, z, relief});
        }
    // Flattest-first; stable tiebreak by position for determinism.
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        if (a.score != b.score) return a.score < b.score;
        if (a.z != b.z) return a.z < b.z;
        return a.x < b.x;
    });
    for (const auto& cd : cands) {
        if (static_cast<int>(out.size()) >= maxPlots) break;
        const Rect r{cd.x, cd.z, plotSize, plotSize};
        bool clash = false;
        for (const auto& p : out) {                       // reject if within `spacing` of a placed plot
            const Rect e{p.rect.x - spacing, p.rect.z - spacing,
                         p.rect.w + 2 * spacing, p.rect.d + 2 * spacing};
            if (r.x < e.x1() && e.x < r.x1() && r.z < e.z1() && e.z < r.z1()) { clash = true; break; }
        }
        if (!clash) { Plot pl; pl.rect = r; out.push_back(pl); }
    }
    return out;
}

SettlementLayout subdividePlots(int W, int D, int cols, int rows, int streetWidth, int minPlot) {
    SettlementLayout out;
    if (cols < 1 || rows < 1 || streetWidth < 0 || W <= 0 || D <= 0) return out;

    // Reserve (cols+1) vertical street bands + (rows+1) horizontal bands; plots fill the rest.
    const int plotW = (W - (cols + 1) * streetWidth) / cols;
    const int plotD = (D - (rows + 1) * streetWidth) / rows;
    if (plotW < minPlot || plotD < minPlot) return out;   // can't fit -> caller reduces density

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            Plot p;
            p.col = c; p.row = r;
            p.rect.x = streetWidth + c * (plotW + streetWidth);   // inset by a street, gap per column
            p.rect.z = streetWidth + r * (plotD + streetWidth);
            p.rect.w = plotW;
            p.rect.d = plotD;
            out.plots.push_back(p);
        }
    }
    // street corridors: vertical bands (full D) at each x-gap, horizontal bands (full W) at each z-gap
    for (int c = 0; c <= cols; ++c) {
        Rect s; s.x = c * (plotW + streetWidth); s.z = 0; s.w = streetWidth; s.d = D;
        out.streets.push_back(s);
    }
    for (int r = 0; r <= rows; ++r) {
        Rect s; s.x = 0; s.z = r * (plotD + streetWidth); s.w = W; s.d = streetWidth;
        out.streets.push_back(s);
    }
    return out;
}

std::vector<PlacedBuilding> populatePlots(const SettlementLayout& layout, int setback,
                                          int minBuilding, const std::string& typology) {
    std::vector<PlacedBuilding> out;
    if (setback < 0) return out;
    for (size_t i = 0; i < layout.plots.size(); ++i) {
        const Rect& plot = layout.plots[i].rect;
        Rect fp;
        fp.x = plot.x + setback;       // inset by the yard on every side
        fp.z = plot.z + setback;
        fp.w = plot.w - 2 * setback;
        fp.d = plot.d - 2 * setback;
        if (fp.w < minBuilding || fp.d < minBuilding) continue;   // too small for a building + yard
        PlacedBuilding b;
        b.plotIndex = static_cast<int>(i);
        b.footprint = fp;
        b.typology = typology;
        out.push_back(b);
    }
    return out;
}

BuildingVariant pickBuildingVariant(int plotIndex, const std::vector<std::string>& typologies,
                                    const std::vector<std::string>& styles, unsigned seed) {
    // Independent hash per dimension (different salts) so typology, style and shape vary INDEPENDENTLY
    // — neighbours can share a typology yet differ in style/shape. Deterministic in (plotIndex, seed).
    auto hash = [&](unsigned salt) {
        unsigned x = static_cast<unsigned>(plotIndex) * 2654435761u + seed * 2246822519u + salt * 40503u;
        x ^= x >> 16; x *= 2246822519u; x ^= x >> 13; x *= 3266489917u; x ^= x >> 16;
        return x;
    };
    BuildingVariant v;
    v.typology = typologies.empty() ? "hall_house" : typologies[hash(1) % typologies.size()];
    v.style    = styles.empty() ? "timber_cottage" : styles[hash(2) % styles.size()];
    v.footprintShape = (hash(3) % 3u == 0u) ? "L" : "rect";   // ~1/3 of buildings get an L-plan
    return v;
}

} // namespace Core
} // namespace Phyxel
