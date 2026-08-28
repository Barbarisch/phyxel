#include "core/TownWall.h"

#include <algorithm>

namespace Phyxel {
namespace Core {

namespace {

Rect expand(const Rect& r, int by) {
    return Rect{r.x - by, r.z - by, r.w + 2 * by, r.d + 2 * by};
}
/// Midpoint that FLOORS instead of truncating toward zero. Plain `(a+b)/2` shifts the
/// answer by one cube once the coordinates go negative, so a settlement west or north of
/// the origin got its gates one cube off from an identical settlement east of it —
/// caught by ThePlanIsTranslationInvariant, which is exactly what that test is for.
int midFloor(int a, int b) {
    const int s = a + b;
    return (s >= 0) ? (s / 2) : ((s - 1) / 2);
}
bool overlaps(const Rect& a, const Rect& b) {
    return a.x < b.x1() && b.x < a.x1() && a.z < b.z1() && b.z < a.z1();
}

/// One planned gate before it is cut: which side, and the span it must clear along
/// that side's RUN axis.
struct GateWant {
    char side;
    int  lo;          ///< run-axis start of the street
    int  hi;          ///< run-axis end (exclusive)
    int  streetWidth;
};

}  // namespace

TownWallPlan planTownWall(const Rect& site, const std::vector<Rect>& streets,
                          const std::vector<Rect>& footprints, const TownWallSpec& spec) {
    TownWallPlan p;
    if (!spec.enabled) { p.refusal = "walls disabled for this tier"; return p; }
    const int t = std::max(1, spec.thicknessCubes);
    const int margin = std::max(0, spec.marginCubes);
    if (site.w <= 0 || site.d <= 0) { p.refusal = "empty site"; return p; }

    p.innerBound = expand(site, margin);
    p.outerBound = expand(p.innerBound, t);

    // ---- GATES FIRST: a street that reaches the site edge must pass through the wall.
    // Deciding gates before runs means a run is always emitted around a known opening,
    // and an ungateable street is a REFUSAL rather than a silently walled-in road.
    std::vector<GateWant> wants;
    for (const auto& s : streets) {
        if (s.w <= 0 || s.d <= 0) continue;
        const bool alongX = s.w >= s.d;
        if (alongX) {
            // Runs east-west: it can only leave through the WEST or EAST side.
            const int width = s.d;                       // cross-extent = street width
            if (s.x <= site.x)         wants.push_back({'W', s.z, s.z1(), width});
            if (s.x1() >= site.x1())   wants.push_back({'E', s.z, s.z1(), width});
        } else {
            const int width = s.w;
            if (s.z <= site.z)         wants.push_back({'S', s.x, s.x1(), width});
            if (s.z1() >= site.z1())   wants.push_back({'N', s.x, s.x1(), width});
        }
    }

    // Widen each want to the spec minimum, centred on the street, then clamp inside the
    // side's usable run (corner towers own the ends). Refuse if it cannot fit.
    const int towerSize = spec.towers ? std::max(1, spec.towerSize) : 0;
    auto cutGate = [&](const GateWant& w) -> bool {
        const bool onXSide = (w.side == 'W' || w.side == 'E');
        // The run axis is Z for the W/E sides, X for the N/S sides.
        const int runLo = (onXSide ? p.outerBound.z : p.outerBound.x) + towerSize;
        const int runHi = (onXSide ? p.outerBound.z1() : p.outerBound.x1()) - towerSize;
        const int want = std::max(spec.gateWidthCubes, w.streetWidth);
        if (runHi - runLo < want) return false;          // no room between the towers

        const int centre = midFloor(w.lo, w.hi);
        int lo = centre - want / 2;
        lo = std::max(runLo, std::min(lo, runHi - want));

        WallGate g;
        g.side = w.side;
        g.streetWidth = w.streetWidth;
        if (onXSide) {
            const int bandX = (w.side == 'W') ? p.outerBound.x : p.innerBound.x1();
            g.opening = Rect{bandX, lo, t, want};        // full thickness through the band
        } else {
            const int bandZ = (w.side == 'S') ? p.outerBound.z : p.innerBound.z1();
            g.opening = Rect{lo, bandZ, want, t};
        }
        p.gates.push_back(g);
        return true;
    };

    for (const auto& w : wants) {
        if (!cutGate(w)) {
            p.gates.clear();
            p.refusal = std::string("cannot gate the street reaching the ") + w.side +
                        " side (needs " + std::to_string(std::max(spec.gateWidthCubes, w.streetWidth)) +
                        " cubes between the corner towers) — refusing to wall a street in";
            return p;
        }
    }

    // ---- RUNS: the four sides of the band, full length. Gates are reported separately
    // and carved by the realizer, so a run stays one simple rect.
    const Rect& o = p.outerBound;
    const Rect& in = p.innerBound;
    p.runs.push_back({Rect{o.x, o.z, o.w, t}, 'S'});              // south band
    p.runs.push_back({Rect{o.x, in.z1(), o.w, t}, 'N'});          // north band
    p.runs.push_back({Rect{o.x, in.z, t, in.d}, 'W'});            // west band (between them)
    p.runs.push_back({Rect{in.x1(), in.z, t, in.d}, 'E'});        // east band

    // ---- TOWERS at the four corners, sitting on the band's outer corner.
    if (spec.towers) {
        const int ts = towerSize;
        p.towers.push_back(Rect{o.x, o.z, ts, ts});
        p.towers.push_back(Rect{o.x1() - ts, o.z, ts, ts});
        p.towers.push_back(Rect{o.x, o.z1() - ts, ts, ts});
        p.towers.push_back(Rect{o.x1() - ts, o.z1() - ts, ts, ts});
    }

    // ---- The band must never land on a building the allocator already placed.
    for (const auto& f : footprints) {
        for (const auto& r : p.runs) {
            if (!overlaps(r.band, f)) continue;
            p.runs.clear(); p.gates.clear(); p.towers.clear();
            p.refusal = "the wall band would run through a building at (" +
                        std::to_string(f.x) + "," + std::to_string(f.z) +
                        ") — widen the wall margin or shrink the site";
            return p;
        }
        for (const auto& tw : p.towers) {
            if (!overlaps(tw, f)) continue;
            p.runs.clear(); p.gates.clear(); p.towers.clear();
            p.refusal = "a corner tower would stand on a building at (" +
                        std::to_string(f.x) + "," + std::to_string(f.z) + ")";
            return p;
        }
    }

    p.ok = true;
    return p;
}

} // namespace Core
} // namespace Phyxel
