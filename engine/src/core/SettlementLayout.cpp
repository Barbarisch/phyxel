#include "core/SettlementLayout.h"

#include <algorithm>
#include <climits>
#include <cmath>
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

std::string drawTypology(const std::map<std::string, int>& weights, int plotIndex, unsigned seed,
                         unsigned salt) {
    long total = 0;
    for (const auto& [_, w] : weights) total += std::max(0, w);
    if (total <= 0) return "hall_house";
    // Same avalanche mix as pickBuildingVariant, salted so a caller can redraw deterministically.
    unsigned x = static_cast<unsigned>(plotIndex) * 2654435761u + seed * 2246822519u
               + (salt + 7u) * 40503u;
    x ^= x >> 16; x *= 2246822519u; x ^= x >> 13; x *= 3266489917u; x ^= x >> 16;
    long pick = static_cast<long>(x % static_cast<unsigned>(total));
    for (const auto& [name, w] : weights) {           // std::map iteration order is stable (sorted)
        pick -= std::max(0, w);
        if (pick < 0) return name;
    }
    return weights.begin()->first;                    // unreachable
}

MainStreetLayout planMainStreetLayout(const SettlementTierPreset& tier, int W, int D,
                                      const RoomProgramRegistry& rooms, unsigned seed,
                                      char axis, int crossOffset) {
    MainStreetLayout out;
    if (W <= 0 || D <= 0 || tier.street.mainWidth <= 0) return out;
    // Work in street-local coords: U = along the spine (length L), V = across it (extent C);
    // mkRect maps back to settlement X/Z. Keeps the allocator single-axis.
    const bool alongX = axis ? (axis == 'X') : (W >= D);
    const int L = alongX ? W : D, C = alongX ? D : W;
    const int sw = tier.street.mainWidth;
    const int v0 = (crossOffset >= 0) ? crossOffset : (C - sw) / 2;
    if (v0 < 0 || v0 + sw > C) return out;

    auto mkRect = [&](int u, int v, int du, int dv) {
        return alongX ? Rect{u, v, du, dv} : Rect{v, u, dv, du};
    };
    out.mainStreet = mkRect(0, v0, L, sw);
    out.base.streets.push_back(out.mainStreet);

    // Per-plot deterministic scalar draw (same avalanche family as drawTypology, distinct salts).
    auto drawIn = [&](int idx, unsigned salt, int lo, int hi) {
        if (hi <= lo) return lo;
        unsigned x = static_cast<unsigned>(idx) * 2654435761u + seed * 2246822519u
                   + (salt + 3u) * 40503u;
        x ^= x >> 16; x *= 2246822519u; x ^= x >> 13; x *= 3266489917u; x ^= x >> 16;
        return lo + static_cast<int>(x % static_cast<unsigned>(hi - lo + 1));
    };

    const int endMargin = std::max(1, tier.street.laneWidth);   // clear run-off at the street ends

    // MARKET SQUARE (town tier): the main street WIDENS at mid-length into a building-free paved
    // place — the common English market form (a widened street, not a detached plaza). Reserved
    // BEFORE plot allocation; the allocator jumps its run on both sides. Pushed into base.streets
    // so the paver paves it as a plaza and the L3 walk can prove it.
    int sqU0 = 0, sqU1 = 0;
    if (tier.pub.marketW > 0 && tier.pub.marketD > 0 && L > 2 * endMargin + 4) {
        const int sqW = std::min(tier.pub.marketW, L - 2 * endMargin);
        const int over = std::max(0, tier.pub.marketD - sw);   // bulge past the street band
        const int vLo = std::max(0, v0 - over / 2);
        const int vHi = std::min(C, v0 + sw + (over - over / 2));
        sqU0 = (L - sqW) / 2;
        sqU1 = sqU0 + sqW;
        out.marketSquare = mkRect(sqU0, vLo, sqW, vHi - vLo);
        out.hasSquare = true;
        out.base.streets.push_back(out.marketSquare);
    }

    // Allocate frontage-by-frontage, ALTERNATING sides (side 0 = across the street at +v, side 1
    // at -v) so both rows fill evenly. Each plot: draw the typology FIRST, size the plot FROM it
    // (the burgage principle — frontage = building frontage + 2*setback), orient per the
    // typology's entrance rule: "long_wall" dwellings present the LONG wall to the street,
    // gable/shop typologies the GABLE (narrow burgage frontage). A draw whose depth can't fit
    // its side is redrawn (salted) up to the palette size, then the side stops.
    int cursors[2] = {endMargin, endMargin};
    bool open[2] = {true, true};
    int count = 0;
    while ((open[0] || open[1]) && count < tier.buildingsMax) {
        bool placedThisRound = false;
        for (int side = 0; side < 2 && count < tier.buildingsMax; ++side) {
            if (!open[side]) continue;
            // back-lane tiers RESERVE the lane band behind the row (the burgage form PLANS the
            // lane; it doesn't squeeze it in after) — plots may not fill the whole cross extent.
            const int laneReserve = (tier.street.backLanes && tier.street.laneWidth > 0)
                                        ? tier.street.laneWidth : 0;
            const int availDepth = ((side == 0) ? (C - (v0 + sw)) : v0) - laneReserve;
            const int redraws = static_cast<int>(std::max<size_t>(1, tier.typologyWeights.size()));
            bool fit = false;
            for (int t = 0; t < redraws && !fit; ++t) {
                const std::string typ = drawTypology(tier.typologyWeights, count, seed,
                                                     static_cast<unsigned>(t));
                const RoomProgram* rp = rooms.get(typ);
                if (!rp) continue;                              // unknown typology: skip this draw
                const int natLong  = std::max(1, (int)std::lround(rp->bays * rp->bayLength));
                const int natShort = std::max(1, (int)std::lround(rp->widthMax > 0 ? rp->widthMax
                                                                                   : rp->widthMin));
                const bool longToStreet = (rp->entrance == "long_wall");
                const int fDim = longToStreet ? natLong : natShort;   // along the street
                const int dDim = longToStreet ? natShort : natLong;   // back from the street
                const int setb = drawIn(count, 11, tier.setback.min, tier.setback.max);
                const int frontage = fDim + 2 * setb;
                const int minDepth = dDim + setb + 1;           // front yard + >=1 cube rear toft
                if (minDepth > availDepth) continue;            // too deep for this side — redraw
                // jump the market square's reserved run (both sides keep clear of the place)
                if (out.hasSquare && cursors[side] < sqU1 + tier.plot.sideGap &&
                    cursors[side] + frontage > sqU0 - tier.plot.sideGap)
                    cursors[side] = sqU1 + tier.plot.sideGap;
                if (cursors[side] + frontage > L - endMargin) continue;  // run full — redraw smaller
                const int depth = std::clamp(drawIn(count, 12, tier.plot.depthMin, tier.plot.depthMax),
                                             minDepth, availDepth);
                const int u = cursors[side];
                const int v = (side == 0) ? v0 + sw : v0 - depth;
                AssignedPlot ap;
                ap.plot.rect = mkRect(u, v, frontage, depth);
                ap.plot.row = side;
                ap.plot.col = count;
                ap.typology = typ;
                ap.streetSide = alongX ? (side == 0 ? 'S' : 'N') : (side == 0 ? 'W' : 'E');
                ap.setback = setb;
                // Footprint: NATURAL size, front wall `setb` in from the street edge, side margins
                // exactly `setb` (frontage = fDim + 2*setb by construction); the rest = rear toft.
                const int fu = u + setb;
                const int fv = (side == 0) ? v + setb : v + depth - setb - dDim;
                ap.footprint = mkRect(fu, fv, fDim, dDim);
                out.base.plots.push_back(ap.plot);
                out.assigned.push_back(ap);
                cursors[side] = u + frontage + tier.plot.sideGap;
                ++count;
                fit = true;
                placedThisRound = true;
            }
            if (!fit) open[side] = false;                       // nothing in the palette fits — stop this row
        }
        if (!placedThisRound) break;
    }

    // BACK LANES (town tier): a service lane parallel to the spine BEHIND each plot row, joined
    // to the main street by end connectors that run THROUGH the lane band — the burgage back-lane
    // circuit (Tait plan-form). Lanes/connectors are street rects: the paver paves them and the
    // L3 walk proves them; rows/sides without room skip honestly.
    if (tier.street.backLanes && tier.street.laneWidth > 0 && !out.assigned.empty()) {
        const int lw = tier.street.laneWidth;
        for (int side = 0; side < 2; ++side) {
            int maxDepth = 0, uLo = INT_MAX, uHi = INT_MIN;
            for (const auto& ap : out.assigned) {
                if (ap.plot.row != side) continue;
                const Rect& r = ap.plot.rect;
                maxDepth = std::max(maxDepth, alongX ? r.d : r.w);
                uLo = std::min(uLo, alongX ? r.x : r.z);
                uHi = std::max(uHi, alongX ? r.x1() : r.z1());
            }
            if (maxDepth == 0) continue;                       // empty row
            const int vLane = (side == 0) ? v0 + sw + maxDepth : v0 - maxDepth - lw;
            if (vLane < 0 || vLane + lw > C) continue;         // no room behind this row (honest)
            out.base.streets.push_back(mkRect(uLo, vLane, uHi - uLo, lw));
            // end connectors OUTSIDE the plot runs (uLo-lw / uHi), spanning street edge -> through
            // the lane band so the rects share an EDGE with both (one connected network).
            const int vc0 = (side == 0) ? v0 + sw : vLane;
            const int vcd = maxDepth + lw;
            if (uLo - lw >= 0)     out.base.streets.push_back(mkRect(uLo - lw, vc0, lw, vcd));
            if (uHi + lw <= L)     out.base.streets.push_back(mkRect(uHi, vc0, lw, vcd));
        }
    }

    out.ok = !out.assigned.empty();
    return out;
}

MainStreetLayout planCityLayout(const SettlementTierPreset& tier, int W, int D,
                                const RoomProgramRegistry& rooms, unsigned seed) {
    MainStreetLayout out;
    if (W <= 0 || D <= 0 || tier.street.mainWidth <= 0) return out;
    const bool alongX = (W >= D);
    const int L = alongX ? W : D, C = alongX ? D : W;
    const int sw = tier.street.mainWidth;
    const int lw = std::max(1, tier.street.laneWidth);
    const int v0 = (C - sw) / 2;
    if (v0 <= 0 || v0 + sw >= C) return out;

    auto mkRect = [&](int u, int v, int du, int dv) {
        return alongX ? Rect{u, v, du, dv} : Rect{v, u, dv, du};
    };
    auto drawIn = [&](int idx, unsigned salt, int lo, int hi) {
        if (hi <= lo) return lo;
        unsigned x = static_cast<unsigned>(idx) * 2654435761u + seed * 2246822519u
                   + (salt + 3u) * 40503u;
        x ^= x >> 16; x *= 2246822519u; x ^= x >> 13;
        return lo + static_cast<int>(x % static_cast<unsigned>(hi - lo + 1));
    };

    // ---- AXES: the main street + a full cross street of MAIN width, crossing at mid-length.
    out.mainStreet = mkRect(0, v0, L, sw);
    out.base.streets.push_back(out.mainStreet);
    const int uc0 = (L - sw) / 2, uc1 = uc0 + sw;          // the cross axis (u-range)
    out.base.streets.push_back(mkRect(uc0, 0, sw, C));

    // ---- MARKET SQUARE over the crossing (the crossroads growth seed).
    const int sqW = std::min(tier.pub.marketW > 0 ? tier.pub.marketW : 12, L / 3);
    const int sqD = std::min(tier.pub.marketD > 0 ? tier.pub.marketD : 12, C / 3);
    const int squ = (L - sqW) / 2;
    const int sqv = std::max(0, v0 + sw / 2 - sqD / 2);
    out.marketSquare = mkRect(squ, sqv, sqW, sqD);
    out.hasSquare = true;
    out.base.streets.push_back(out.marketSquare);

    // ---- SECONDARY streets (lane width) perpendicular to the main street at seeded-JITTERED
    // block intervals — the bounded chaos. Walk from both edges toward the centre; keep clear
    // of the cross axis + square.
    const int endMargin = std::max(2, lw);
    // exclusion zone: the square/axis plus a full block-min margin either side, so no secondary
    // lands a degenerate sliver from the crossroads (block gaps stay within the jitter band)
    const int exLo = std::min(squ, uc0) - tier.blocksMin;
    const int exHi = std::max(squ + sqW, uc1) + tier.blocksMin;
    std::vector<std::pair<int, int>> vertBands;            // (u0, u1) of perpendicular streets
    vertBands.push_back({uc0, uc1});
    {
        int u = endMargin, k = 0;
        while (true) {
            u += drawIn(k++, 71, tier.blocksMin, tier.blocksMax);
            if (u + lw >= L - endMargin) break;
            if (u + lw >= exLo && u <= exHi) { u = exHi; continue; }
            out.base.streets.push_back(mkRect(u, 0, lw, C));
            vertBands.push_back({u, u + lw});
        }
    }
    std::sort(vertBands.begin(), vertBands.end());
    // main-row allocation cuts on the perpendicular streets AND the square's full u-extent
    // (the square is wider than the cross axis — rows must not run into the market place)
    std::vector<std::pair<int, int>> mainCuts = vertBands;
    for (auto& b : mainCuts)
        if (b.first == uc0 && b.second == uc1) b = {std::min(uc0, squ), std::max(uc1, squ + sqW)};
    std::sort(mainCuts.begin(), mainCuts.end());

    // ---- shared burgage-row allocator (both axes use it). Runs along a street edge in (run,
    // depth) coords; `alongU` = the run follows the U axis (fronting the MAIN street) or the V
    // axis (fronting the CROSS street). District ring: a plot whose frontage midpoint lies within
    // `coreRing` (Chebyshev, cubes) of the square centre draws from the shop-heavy core weights
    // and keeps tight setbacks; the fringe draws the base palette at +1 setback (looser edges).
    const int sqCu = squ + sqW / 2, sqCv = sqv + sqD / 2;  // square centre (u/v frame)
    int count = 0;
    const int depthCap = tier.plot.depthMax + tier.setback.max + 2;
    auto allocRow = [&](int runFrom, int runTo, int streetEdge, bool plusDepth, bool alongU,
                        int availDepth) {
        if (availDepth < 4) return;
        int cursor = runFrom;
        int guard = 0;
        while (count < tier.buildingsMax && ++guard < 64) {
            bool fit = false;
            const int redraws = static_cast<int>(std::max<size_t>(1, tier.typologyWeights.size()));
            for (int t = 0; t < redraws && !fit; ++t) {
                // ring membership from the FRONTAGE MIDPOINT (market adjacency is about frontage).
                // Chicken-and-egg: the midpoint needs the frontage, the frontage needs the palette
                // — so guess at cursor+8 (a typical half-frontage), size, re-check at the exact
                // midpoint, and re-draw ONCE with the corrected palette if membership flipped.
                auto ringAt = [&](int runPos) {
                    if (tier.coreRing <= 0 || tier.coreTypologyWeights.empty()) return false;
                    const int fu = alongU ? runPos : streetEdge;
                    const int fv = alongU ? streetEdge : runPos;
                    return std::max(std::abs(fu - sqCu), std::abs(fv - sqCv)) <= tier.coreRing;
                };
                bool core = ringAt(cursor + 8);
                std::string typ;
                const RoomProgram* rp = nullptr;
                int fDim = 0, dDim = 0, setb = 0, frontage = 0;
                for (int pass = 0; pass < 2; ++pass) {
                    const auto& weights = core ? tier.coreTypologyWeights : tier.typologyWeights;
                    typ = drawTypology(weights, count, seed, static_cast<unsigned>(t));
                    rp = rooms.get(typ);
                    if (!rp) break;
                    const int natLong  = std::max(1, (int)std::lround(rp->bays * rp->bayLength));
                    const int natShort = std::max(1, (int)std::lround(rp->widthMax > 0 ? rp->widthMax
                                                                                       : rp->widthMin));
                    const bool longToStreet = (rp->entrance == "long_wall");
                    fDim = longToStreet ? natLong : natShort;
                    dDim = longToStreet ? natShort : natLong;
                    setb = drawIn(count, 11, tier.setback.min, tier.setback.max) + (core ? 0 : 1);
                    frontage = fDim + 2 * setb;
                    const bool actual = ringAt(cursor + frontage / 2);
                    if (actual == core) break;
                    core = actual;
                }
                if (!rp) continue;
                const int minDepth = dDim + setb + 1;
                if (minDepth > availDepth) continue;
                if (cursor + frontage > runTo) continue;
                const int depth = std::clamp(drawIn(count, 12, tier.plot.depthMin,
                                                    tier.plot.depthMax),
                                             minDepth, availDepth);
                const int vPlot = plusDepth ? streetEdge : streetEdge - depth;
                AssignedPlot ap;
                ap.plot.rect = alongU ? mkRect(cursor, vPlot, frontage, depth)
                                      : mkRect(vPlot, cursor, depth, frontage);
                ap.plot.row = plusDepth ? 0 : 1;
                ap.plot.col = count;
                ap.typology = typ;
                ap.setback = setb;
                if (alongU) ap.streetSide = plusDepth ? (alongX ? 'S' : 'W') : (alongX ? 'N' : 'E');
                else        ap.streetSide = plusDepth ? (alongX ? 'W' : 'S') : (alongX ? 'E' : 'N');
                // footprint: natural size, front wall `setb` off the street edge, centred on the run
                const int fpRun = cursor + setb;
                const int fpV = plusDepth ? streetEdge + setb : streetEdge - setb - dDim;
                ap.footprint = alongU ? mkRect(fpRun, fpV, fDim, dDim)
                                      : mkRect(fpV, fpRun, dDim, fDim);
                out.base.plots.push_back(ap.plot);
                out.assigned.push_back(ap);
                cursor += frontage + tier.plot.sideGap;
                ++count;
                fit = true;
            }
            if (!fit) break;
        }
    };

    // ---- MAIN-STREET rows: one burgage run per BLOCK (between consecutive perpendicular
    // streets), both sides, capped so cross-street rows keep their own band.
    const int rowBand = std::min(depthCap, (C - sw) / 2 - 2);
    for (size_t b = 0; b <= mainCuts.size(); ++b) {
        const int a = (b == 0) ? endMargin : mainCuts[b - 1].second + 1;
        const int e = (b < mainCuts.size()) ? mainCuts[b].first - 1 : L - endMargin;
        if (e - a < 6) continue;
        allocRow(a, e, v0 + sw, true, true, rowBand);       // north row of this block
        allocRow(a, e, v0, false, true, rowBand);           // south row
    }

    // ---- CROSS-STREET rows: front the cross axis BEYOND the main rows' band (the second
    // burgage axis of the crossroads). Depth extends toward the nearest secondary street.
    {
        int leftGap = uc0 - endMargin, rightGap = L - endMargin - uc1;
        for (const auto& vb : vertBands) {
            if (vb.second <= uc0) leftGap = std::min(leftGap, uc0 - vb.second - 1);
            if (vb.first >= uc1)  rightGap = std::min(rightGap, vb.first - uc1 - 1);
        }
        const int nLo = v0 + sw + rowBand + 1, nHi = C - 2;   // north segment (v-range)
        const int sLo = 2, sHi = v0 - rowBand - 1;            // south segment
        if (nHi - nLo >= 6) {
            allocRow(nLo, nHi, uc1, true, false, std::min(depthCap, rightGap));   // east of the axis
            allocRow(nLo, nHi, uc0, false, false, std::min(depthCap, leftGap));   // west of the axis
        }
        if (sHi - sLo >= 6) {
            allocRow(sLo, sHi, uc1, true, false, std::min(depthCap, rightGap));
            allocRow(sLo, sHi, uc0, false, false, std::min(depthCap, leftGap));
        }
    }

    out.ok = !out.assigned.empty();
    return out;
}

std::vector<YardProp> planYardProps(const AssignedPlot& ap, unsigned seed) {
    std::vector<YardProp> out;
    const Rect& pl = ap.plot.rect;
    const Rect& fp = ap.footprint;
    // Usable parcel interior: inset 1 cube from every edge (clear of the fence line).
    const int ix0 = pl.x + 1, iz0 = pl.z + 1, ix1 = pl.x1() - 1, iz1 = pl.z1() - 1;
    // The REAR TOFT: the interior strip behind the building, opposite the street.
    int rx0 = ix0, rz0 = iz0, rx1 = ix1, rz1 = iz1;
    switch (ap.streetSide) {
        case 'S': rz0 = std::max(iz0, fp.z1()); break;
        case 'N': rz1 = std::min(iz1, fp.z);    break;
        case 'W': rx0 = std::max(ix0, fp.x1()); break;
        default:  rx1 = std::min(ix1, fp.x);    break;   // 'E'
    }
    if (rx1 - rx0 <= 0 || rz1 - rz0 <= 0) return out;    // no rear room: place nothing (honest)
    const bool rearIsZ = (ap.streetSide == 'S' || ap.streetSide == 'N');

    auto draw = [&](unsigned salt, int lo, int hi) {     // [lo, hi] inclusive, deterministic
        if (hi <= lo) return lo;
        unsigned x = static_cast<unsigned>(pl.x * 31 + pl.z) * 2654435761u
                   + seed * 2246822519u + (salt + 5u) * 40503u;
        x ^= x >> 16; x *= 2246822519u; x ^= x >> 13;
        return lo + static_cast<int>(x % static_cast<unsigned>(hi - lo + 1));
    };
    auto fits = [&](int cx, int cz, int w, int d) {
        if (cx < rx0 || cz < rz0 || cx + w > rx1 || cz + d > rz1) return false;
        for (const auto& q : out)
            if (cx < q.cx + q.w && q.cx < cx + w && cz < q.cz + q.d && q.cz < cz + d) return false;
        return true;
    };
    // Place one prop: `nearBuilding` hugs the rear wall side (the woodpile — fuel by the door);
    // otherwise it sits deeper in the open toft (the garden). Salted retries slide it laterally.
    auto tryPlace = [&](const char* type, int w, int d, bool nearBuilding, unsigned salt) {
        YardProp p;
        p.type = type;
        p.rotDeg = rearIsZ ? 0 : 90;                     // long side parallel to the rear wall
        p.w = rearIsZ ? w : d;
        p.d = rearIsZ ? d : w;
        for (int t = 0; t < 6; ++t) {
            int cx, cz;
            if (rearIsZ) {
                cx = draw(salt + t, rx0, rx1 - p.w);
                if (nearBuilding) cz = (ap.streetSide == 'S') ? rz0 : rz1 - p.d;
                else              cz = (ap.streetSide == 'S')
                                        ? draw(salt + 17 + t, rz0 + 2, rz1 - p.d)
                                        : draw(salt + 17 + t, rz0, rz1 - p.d - 2);
            } else {
                cz = draw(salt + t, rz0, rz1 - p.d);
                if (nearBuilding) cx = (ap.streetSide == 'W') ? rx0 : rx1 - p.w;
                else              cx = (ap.streetSide == 'W')
                                        ? draw(salt + 17 + t, rx0 + 2, rx1 - p.w)
                                        : draw(salt + 17 + t, rx0, rx1 - p.w - 2);
            }
            if (fits(cx, cz, p.w, p.d)) { p.cx = cx; p.cz = cz; out.push_back(p); return; }
        }
    };
    tryPlace("woodpile",   2, 1, true,  31);             // fuel stack against the rear wall side
    tryPlace("garden_bed", 2, 1, false, 67);             // kitchen garden in the open toft
    return out;
}

StreetAxisChoice chooseStreetAxis(const BuildabilityMap& site, int mainWidth, int minPlotDepth,
                                  char preferredAxis) {
    // Road-arrival preference: a bounded per-cell handicap on the NON-preferred axis. 1500
    // (= 1.5 cubes of per-cell relief in the x1000 score) tips ties and mild differences
    // toward the arriving road's axis, but never outweighs the water/cliff penalty
    // (100000/cell) or a decisively flatter alternative — the street meets the road only
    // where the ground allows it. REASONED magnitude: row villages grow ALONG their
    // through-road (the settlement_program.json morphology sources), so comparable terrain
    // should defer to the road; hostile terrain should not.
    const long kPreferenceHandicap = 1500;
    StreetAxisChoice best;
    best.score = -1;
    // Score is PER-CELL (x1000 for integer precision) — comparing band TOTALS handed the SHORT
    // axis a systematic cell-count advantage on any noisy terrain (found live: an 80x44 village
    // picked the 44-long axis). Penalty per unbuildable cell dominates relief so the spine avoids
    // water/cliffs first.
    const double PEN = 100000.0;
    auto evalBand = [&](char ax, int off) {
        double s = 0;
        const int len = (ax == 'X') ? site.W : site.D;
        for (int u = 0; u < len; ++u)
            for (int dv = 0; dv < mainWidth; ++dv) {
                const SiteCell& c = (ax == 'X') ? site.at(u, off + dv) : site.at(off + dv, u);
                s += c.relief;
                if (c.cls == Buildability::TooSteep || c.cls == Buildability::Water) s += PEN;
            }
        const long cells = static_cast<long>(len) * mainWidth;
        return static_cast<long>(s * 1000.0 / std::max(1L, cells));
    };
    // Tiebreak: the LONGER axis first (a longer street hosts more plots), then lower offset.
    const char first = (site.W >= site.D) ? 'X' : 'Z';
    for (char ax : {first, first == 'X' ? 'Z' : 'X'}) {
        const int cross = (ax == 'X') ? site.D : site.W;
        // Keep >= minPlotDepth of plot room on BOTH sides of the band (found live: offset 0 pinned
        // the street to the site edge -> a one-sided village). Clamped so small sites still search.
        const int room = std::min(minPlotDepth, std::max(0, (cross - mainWidth) / 2));
        for (int off = room; off + mainWidth <= cross - room; ++off) {
            long s = evalBand(ax, off);
            if (preferredAxis != 0 && ax != preferredAxis) s += kPreferenceHandicap;
            if (best.score < 0 || s < best.score) { best = {ax, off, s}; }
        }
    }
    if (best.score < 0) best.score = 0;   // site too small for any band: default X/0
    return best;
}

} // namespace Core
} // namespace Phyxel
