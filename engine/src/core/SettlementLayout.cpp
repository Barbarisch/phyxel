#include "core/SettlementLayout.h"

#include <algorithm>
#include <set>

namespace Phyxel {
namespace Core {

char streetSideForPlot(const SettlementLayout& layout, const Rect& plot) {
    auto touchesStreet = [&](const Rect& strip) {
        for (const auto& s : layout.streets)
            if (strip.x < s.x1() && s.x < strip.x1() && strip.z < s.z1() && s.z < strip.z1())
                return true;
        return false;
    };
    // A side is FACING when another plot lies across the street in that direction — a SHARED
    // street with opposite frontages (the village street), as opposed to the settlement's
    // outer ring road. In a plot grid every side touches some street, so this is what breaks
    // the tie toward houses facing each other (SettlementLayoutTest.PlotsFrontTheSharedStreet…).
    auto facing = [&](char side) {
        for (const auto& q : layout.plots) {
            const Rect& o = q.rect;
            if (o.x == plot.x && o.z == plot.z && o.w == plot.w && o.d == plot.d) continue;  // self
            const bool xOverlap = o.x < plot.x1() && plot.x < o.x1();
            const bool zOverlap = o.z < plot.z1() && plot.z < o.z1();
            if (side == 'N' && xOverlap && o.z >= plot.z1()) return true;
            if (side == 'S' && xOverlap && o.z1() <= plot.z) return true;
            if (side == 'E' && zOverlap && o.x >= plot.x1()) return true;
            if (side == 'W' && zOverlap && o.x1() <= plot.x) return true;
        }
        return false;
    };
    // 1-cube strips just outside each plot edge.
    const Rect south{plot.x, plot.z - 1, plot.w, 1};
    const Rect north{plot.x, plot.z1(), plot.w, 1};
    const Rect west{plot.x - 1, plot.z, 1, plot.d};
    const Rect east{plot.x1(), plot.z, 1, plot.d};
    // Prefer the LONG sides (the building orients along the plot's long axis, so its entrance
    // wall is a long side); within that, prefer a FACING shared street over the outer ring.
    const bool longX = plot.w >= plot.d;
    const char order[4] = {longX ? 'S' : 'W', longX ? 'N' : 'E', longX ? 'W' : 'S', longX ? 'E' : 'N'};
    auto strip = [&](char side) -> const Rect& {
        return side == 'S' ? south : side == 'N' ? north : side == 'W' ? west : east;
    };
    for (char side : order)
        if (touchesStreet(strip(side)) && facing(side)) return side;
    for (char side : order)
        if (touchesStreet(strip(side))) return side;
    return 0;
}

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

FencePlan planParcelFence(const Rect& parcel, char gateSide, int gateWidth) {
    FencePlan f;
    if (parcel.w < 2 || parcel.d < 2 || gateWidth < 1) return f;
    const bool horiz = (gateSide == 'N' || gateSide == 'S');   // gate runs along X on a Z-edge
    const int sideLen = horiz ? parcel.w : parcel.d;
    if (gateWidth > sideLen) return f;                          // gate can't fit on that side

    // full perimeter (cube cells)
    std::set<std::pair<int, int>> perim;
    for (int x = parcel.x; x < parcel.x1(); ++x) { perim.insert({x, parcel.z}); perim.insert({x, parcel.z1() - 1}); }
    for (int z = parcel.z; z < parcel.z1(); ++z) { perim.insert({parcel.x, z}); perim.insert({parcel.x1() - 1, z}); }

    // gate run: gateWidth cells centred on the requested side
    const int start = (horiz ? parcel.x : parcel.z) + (sideLen - gateWidth) / 2;
    std::set<std::pair<int, int>> gate;
    for (int k = 0; k < gateWidth; ++k) {
        if (gateSide == 'S')      gate.insert({start + k, parcel.z});
        else if (gateSide == 'N') gate.insert({start + k, parcel.z1() - 1});
        else if (gateSide == 'W') gate.insert({parcel.x, start + k});
        else                      gate.insert({parcel.x1() - 1, start + k});   // 'E'
    }

    for (const auto& g : gate) f.gate.push_back(g);
    for (const auto& c : perim) if (!gate.count(c)) f.posts.push_back(c);   // fence everything but the gate
    f.ok = true;
    return f;
}

} // namespace Core
} // namespace Phyxel
