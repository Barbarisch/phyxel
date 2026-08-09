#include "core/RoomLayout.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <queue>
#include <string>

#include "core/CornerPolicy.h"
#include "core/RoomProgram.h"
#include "utils/Logger.h"

namespace Phyxel {
namespace Core {

namespace {

// M6: width (cubes) of the circulation strip that carries the stair — the upper
// floor's GALLERY and the stair WELL share it, so the flight always emerges onto
// circulation instead of into someone's chamber. 2 cubes ≈ 2 m: wide enough for the
// switchback's two lanes (StairPlanner requires well width >= 2) and for two people
// to pass, which is what a gallery is for.
constexpr int kStairStripCubes = 2;

// THE stair well rect — ONE definition, used by generateStoryStairs (to build the
// flight) and generateUpperChambers (so chamber doors don't open into the shaft).
// It runs ALONG the long axis inside the circulation strip, at the low corner.
// Deriving it twice is how the doors ended up opening onto the stairwell void.
Rect stairWellRect(int W, int D) {
    const bool lengthIsX = (W >= D);
    const int longAxis = lengthIsX ? W : D;
    // INSET the foot of the flight one cube from the gable wall. The exterior wall
    // band eats the first ~3 micro of the footprint, so a well flush at 0 buries its
    // bottom treads in the wall and there is no way to reach the foot of the stair —
    // the storey above becomes unreachable even though the flight is built correctly.
    // (The pre-M6 well was inset for exactly this reason; dropping it cost a debug
    // cycle, so the reason is written down here.)
    const int foot = 1;
    const int wellRun = std::min(6, std::max(2, longAxis - foot));
    return lengthIsX ? Rect{foot, 0, wellRun, kStairStripCubes}
                     : Rect{0, foot, kStairStripCubes, wellRun};
}

// Shared wall between two rects (axis + coord + overlap span). Mirrors the realizer's sharedWall so
// the doors land exactly where the realizer carves.
struct Wall { char axis = 0; int coord = 0, lo = 0, hi = 0; bool ok = false; };
Wall shared(const Rect& a, const Rect& b) {
    Wall w;
    if (a.x1() == b.x || b.x1() == a.x) {
        int lo = std::max(a.z, b.z), hi = std::min(a.z1(), b.z1());
        if (hi - lo > 0) { w = {'x', (a.x1() == b.x) ? a.x1() : b.x1(), lo, hi, true}; return w; }
    }
    if (a.z1() == b.z || b.z1() == a.z) {
        int lo = std::max(a.x, b.x), hi = std::min(a.x1(), b.x1());
        if (hi - lo > 0) { w = {'z', (a.z1() == b.z) ? a.z1() : b.z1(), lo, hi, true}; return w; }
    }
    return w;
}

struct RNG {
    unsigned s;
    unsigned next() { s = s * 1664525u + 1013904223u; return s; }
    int range(int lo, int hi) { return hi <= lo ? lo : lo + (int)(next() % (unsigned)(hi - lo)); }
};

}  // namespace

RoomLayout generateRoomLayout(int W, int D, int targetRooms, unsigned seed, int minDim) {
    RoomLayout out;
    if (W < minDim || D < minDim) return out;
    RNG rng{seed ? seed : 1u};

    // --- BSP: split the largest splittable room along its longer axis until targetRooms ---
    std::vector<Rect> rooms = {{0, 0, W, D}};
    while ((int)rooms.size() < targetRooms) {
        int best = -1, bestArea = -1;
        for (int i = 0; i < (int)rooms.size(); ++i) {
            const Rect& r = rooms[i];
            if (r.w < 2 * minDim && r.d < 2 * minDim) continue;   // can't split either axis
            if (r.w * r.d > bestArea) { bestArea = r.w * r.d; best = i; }
        }
        if (best < 0) break;
        Rect r = rooms[best];
        const bool canX = r.w >= 2 * minDim, canZ = r.d >= 2 * minDim;
        const bool splitX = canX && (!canZ || r.w >= r.d);       // split the longer axis
        if (splitX) {
            const int sx = rng.range(r.x + minDim, r.x + r.w - minDim + 1);
            rooms[best]   = {r.x, r.z, sx - r.x, r.d};
            rooms.push_back({sx, r.z, r.x + r.w - sx, r.d});
        } else {
            const int sz = rng.range(r.z + minDim, r.z + r.d - minDim + 1);
            rooms[best]   = {r.x, r.z, r.w, sz - r.z};
            rooms.push_back({r.x, sz, r.w, r.z + r.d - sz});
        }
    }

    const int n = (int)rooms.size();
    for (int i = 0; i < n; ++i) {
        ProgRoom pr; pr.id = "r" + std::to_string(i); pr.rect = rooms[i]; pr.purpose = "living";
        out.rooms.push_back(pr);
    }

    // --- spanning tree of interior doors (BFS from room 0) => every room reachable ---
    std::vector<std::vector<int>> adj(n);
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            if (shared(rooms[i], rooms[j]).ok) { adj[i].push_back(j); adj[j].push_back(i); }
    std::vector<bool> vis(n, false);
    std::queue<int> q;
    q.push(0); vis[0] = true;
    while (!q.empty()) {
        const int u = q.front(); q.pop();
        for (int v : adj[u]) {
            if (vis[v]) continue;
            vis[v] = true;
            const Wall w = shared(rooms[u], rooms[v]);
            int mid = (w.lo + w.hi) / 2;
            if (mid >= w.hi) mid = w.hi - 1;                     // keep the width-1 door in-span
            ProgPortal p; p.a = out.rooms[u].id; p.b = out.rooms[v].id;
            p.width = 1; p.height = 2; p.kind = "door";
            if (w.axis == 'x') { p.px = w.coord; p.pz = mid; }
            else               { p.px = mid;     p.pz = w.coord; }
            out.portals.push_back(p);
            q.push(v);
        }
    }

    // --- one exterior entrance on the first perimeter-touching room ---
    for (int i = 0; i < n; ++i) {
        const Rect& r = rooms[i];
        ProgPortal e; e.a = "exterior"; e.b = out.rooms[i].id; e.width = 1; e.height = 2; e.kind = "door";
        if (r.x == 0)        { e.px = 0; e.pz = r.z + r.d / 2; out.portals.push_back(e); break; }
        if (r.z == 0)        { e.px = r.x + r.w / 2; e.pz = 0; out.portals.push_back(e); break; }
        if (r.x1() == W)     { e.px = W; e.pz = r.z + r.d / 2; out.portals.push_back(e); break; }
        if (r.z1() == D)     { e.px = r.x + r.w / 2; e.pz = D; out.portals.push_back(e); break; }
    }
    return out;
}

RoomLayout generateRoomLayoutFromProgram(int W, int D, const RoomProgram& typology, int minDim,
                                         const std::string& front) {
    RoomLayout out;
    const auto& specs = typology.rooms;
    const int n = static_cast<int>(specs.size());
    if (n == 0 || W < minDim || D < minDim) return out;

    // Partition along the LONGER axis (medieval houses are linear). Rooms span the full width.
    const bool lengthIsX = (W >= D);
    const int length = lengthIsX ? W : D;
    const int width  = lengthIsX ? D : W;
    if (length < minDim * n) return out;                     // can't fit every room -> caller falls back

    double totalBays = 0.0;
    for (const auto& rs : specs) totalBays += std::max(0.0, rs.bays);
    if (totalBays <= 0.0) return out;

    // Largest-remainder allocation: each room gets minDim, then the (length - minDim*n) leftover
    // cubes are split proportional to bay weight (so the slices sum to EXACTLY length -> tiles).
    const int extra = length - minDim * n;
    std::vector<double> want(n);
    std::vector<int> give(n, 0);
    int given = 0;
    for (int i = 0; i < n; ++i) {
        want[i] = extra * (std::max(0.0, specs[i].bays) / totalBays);
        give[i] = static_cast<int>(std::floor(want[i]));
        given += give[i];
    }
    std::vector<int> order(n);
    for (int i = 0; i < n; ++i) order[i] = i;
    std::sort(order.begin(), order.end(),
              [&](int a, int b) { return (want[a] - give[a]) > (want[b] - give[b]); });
    for (int k = 0, leftover = extra - given; k < leftover; ++k) give[order[k % n]]++;

    std::map<std::string, int> idSeen;
    int pos = 0;
    for (int i = 0; i < n; ++i) {
        const int slice = minDim + give[i];
        Rect r;
        if (lengthIsX) { r.x = pos; r.z = 0; r.w = slice; r.d = width; }
        else           { r.x = 0; r.z = pos; r.w = width; r.d = slice; }
        ProgRoom pr;
        std::string id = specs[i].id.empty() ? ("r" + std::to_string(i)) : specs[i].id;
        if (idSeen[id]++ > 0) id += "_" + std::to_string(idSeen[id] - 1);   // disambiguate dup ids
        pr.id = id;
        pr.purpose = specs[i].purpose;  // the point: real grounded purposes (service/hall/solar/...)
        pr.rect = r;
        out.rooms.push_back(pr);
        pos += slice;
    }

    // Linear chain of doors between consecutive rooms.
    for (int i = 0; i + 1 < n; ++i) {
        const Wall w = shared(out.rooms[i].rect, out.rooms[i + 1].rect);
        if (!w.ok) continue;
        int mid = (w.lo + w.hi) / 2;
        if (mid >= w.hi) mid = w.hi - 1;
        ProgPortal p; p.a = out.rooms[i].id; p.b = out.rooms[i + 1].id;
        p.width = 1; p.height = 2; p.kind = "door";
        if (w.axis == 'x') { p.px = w.coord; p.pz = mid; }
        else               { p.px = mid;     p.pz = w.coord; }
        out.portals.push_back(p);
    }
    // Exterior entrance(s). The wall is a TYPOLOGY fact (OpeningsLayoutTest, grounded
    // 2026-07-06): "long_wall" = the cross-passage/screens-passage doorway of dwelling forms —
    // on the LONG elevation at the bay boundary named by entrance_between (longhouse: between
    // hall and byre, the 4-bay midpoint; hall house: between service and hall — Wikipedia
    // Dartmoor longhouse / Byre-dwelling / Hall house; EH Wharram Percy). entrance_opposed
    // adds the attested second door on the opposite long wall ("opposed doorways on the front
    // and back walls"). Single-room plans (croft) take the wall middle — long wall by family
    // analogy; the position along the wall is NEEDS-RESEARCH (GroundingGaps). ""/"gable" = the
    // legacy perimeter-first pick (correct for narrow street-frontage burgage shops).
    {
        ProgPortal e; e.a = "exterior"; e.b = out.rooms[0].id; e.width = 1; e.height = 2; e.kind = "door";
        const Rect& r0 = out.rooms[0].rect;
        if (typology.entrance == "long_wall") {
            // The passage room = the first of entrance_between (door opens into it, hard
            // against its boundary with the second); fallback: room 0's far edge / middle.
            const ProgRoom* into = &out.rooms[0];
            if (typology.entranceBetween.size() == 2)
                for (const auto& room : out.rooms)
                    if (room.id == typology.entranceBetween[0]) { into = &room; break; }
            const Rect& ri = into->rect;
            e.b = into->id;
            // The street-facing hint flips the PRIMARY door to the far long wall when it names
            // one; a gable hint is ignored (the grounded cross-passage stays on the long side).
            const bool flip = lengthIsX ? (front == "z1") : (front == "x1");
            int at;
            if (lengthIsX) { at = (n > 1) ? std::max(ri.x, ri.x1() - 1) : ri.x + ri.w / 2; e.px = at; e.pz = flip ? D : 0; }
            else           { at = (n > 1) ? std::max(ri.z, ri.z1() - 1) : ri.z + ri.d / 2; e.pz = at; e.px = flip ? W : 0; }
            out.portals.push_back(e);
            if (typology.entranceOpposed) {
                ProgPortal b = e;                       // the opposed back door of the cross-passage
                if (lengthIsX) b.pz = flip ? 0 : D; else b.px = flip ? 0 : W;
                out.portals.push_back(b);
            }
        } else {
            if (r0.x == 0)         { e.px = 0; e.pz = r0.z + r0.d / 2; out.portals.push_back(e); }
            else if (r0.z == 0)    { e.px = r0.x + r0.w / 2; e.pz = 0; out.portals.push_back(e); }
            else if (r0.x1() == W) { e.px = W; e.pz = r0.z + r0.d / 2; out.portals.push_back(e); }
            else if (r0.z1() == D) { e.px = r0.x + r0.w / 2; e.pz = D; out.portals.push_back(e); }
        }
    }
    return out;
}

RoomLayout generateWingedLayout(int W, int D, const std::string& shape, unsigned /*seed*/, int minDim) {
    RoomLayout out;
    if (shape != "L") return out;                              // only the L-plan for now
    const int halfW = W / 2, rangeD = D / 2;
    // need: a 2-room main range (each >= minDim wide) + a wing (>= minDim deep), notch >= 1 wide.
    if (halfW < minDim || (W - halfW) < minDim || rangeD < minDim || (D - rangeD) < minDim || (W - halfW) < 1)
        return out;                                           // too small -> caller falls back to a rect

    // L-plan cross-wing: a 2-room main range across z[0,rangeD), + a wing under the LEFT room into
    // z[rangeD,D). The bottom-right block x[halfW,W) x z[rangeD,D) stays EMPTY (the notch) -> the union
    // is an L, which the realizer walls/roofs along its outline.
    auto add = [&](const char* id, const char* purpose, int x, int z, int w, int d) {
        ProgRoom r; r.id = id; r.purpose = purpose; r.rect = {x, z, w, d}; out.rooms.push_back(r);
    };
    add("hall",    "hall",    0,     0,      halfW,     rangeD);       // main range, left
    add("service", "service", halfW, 0,      W - halfW, rangeD);       // main range, right
    add("solar",   "solar",   0,     rangeD, halfW,     D - rangeD);   // wing (under hall)

    // doors: spanning tree through the hall hub (hall-service, hall-solar).
    auto connect = [&](size_t i, size_t j) {
        const Wall w = shared(out.rooms[i].rect, out.rooms[j].rect);
        if (!w.ok) return;
        int mid = (w.lo + w.hi) / 2; if (mid >= w.hi) mid = w.hi - 1;
        ProgPortal p; p.a = out.rooms[i].id; p.b = out.rooms[j].id; p.width = 1; p.height = 2; p.kind = "door";
        if (w.axis == 'x') { p.px = w.coord; p.pz = mid; } else { p.px = mid; p.pz = w.coord; }
        out.portals.push_back(p);
    };
    connect(0, 1);   // hall - service
    connect(0, 2);   // hall - solar (wing)

    // one exterior entrance into the service end on the front (z=0) wall.
    {
        const Rect& r = out.rooms[1].rect;
        ProgPortal e; e.a = "exterior"; e.b = out.rooms[1].id; e.width = 1; e.height = 2; e.kind = "door";
        e.px = r.x + r.w / 2; e.pz = 0;
        out.portals.push_back(e);
    }
    return out;
}

// Window openings per the typology's GROUNDED rule (room_program.json "windows" — the engine
// invents no sizes; a typology without a declared spec generates none). For each room edge lying
// on a qualifying exterior wall of the bounding footprint, place lround(edge_bays * per_bay)
// evenly-spaced width×height openings, skipping any cell already claimed by a portal (the
// entrance) or its immediate neighbour (frame clearance). L-plan notch walls (exterior but not
// on the bounding box) are not yet windowed — bounding-perimeter edges only.
static void addTypologyWindows(RoomLayout& rl, int W, int D, const WindowSpec& spec, double bayLength,
                               char frontAxis, int frontCoord) {
    if (!spec.valid() || bayLength <= 0.0) return;
    auto blocked = [&](int px, int pz) {
        for (const auto& p : rl.portals)
            if (std::abs(p.px - px) <= 1 && std::abs(p.pz - pz) <= 1) return true;
        return false;
    };
    const bool lengthIsX = (W >= D);
    for (const auto& room : rl.rooms) {
        // Byre (livestock) rooms get no windows — reasoned from the byre record (dung aperture +
        // drain only, no windows mentioned), NOT directly attested; labeled in room_program sources.
        if (room.purpose == "byre") continue;
        const Rect& r = room.rect;
        // Candidate exterior edges: {axis, wall coord, span lo, span hi}. "front" = the entrance's
        // wall only (the surveyed humble pattern: door + windows share the sun-facing long wall).
        struct Edge { char axis; int coord, lo, hi; };
        std::vector<Edge> edges;
        const bool longWallsAreZ = lengthIsX;   // long walls run along X => they are z=const walls
        auto wants = [&](char axis, int coord, bool isLong) {
            if (spec.walls == "all")   return true;
            if (spec.walls == "front") return axis == frontAxis && coord == frontCoord;
            return isLong;                                   // "long"
        };
        if (r.z == 0     && wants('z', 0, longWallsAreZ))  edges.push_back({'z', 0, r.x, r.x1()});
        if (r.z1() == D  && wants('z', D, longWallsAreZ))  edges.push_back({'z', D, r.x, r.x1()});
        if (r.x == 0     && wants('x', 0, !longWallsAreZ)) edges.push_back({'x', 0, r.z, r.z1()});
        if (r.x1() == W  && wants('x', W, !longWallsAreZ)) edges.push_back({'x', W, r.z, r.z1()});
        for (const auto& ed : edges) {
            // KI-5a corner margin: a window must never intrude into the footprint-corner
            // cube (the quoin/corner-post zone). The rule is QUERIED from CornerPolicy —
            // the one definition shared with the realize-time CornerZone claims (Claims
            // Ledger increment 4) — not re-derived here; only edge ends that ARE
            // footprint corners get the margin (mid-wall room boundaries keep full span).
            const int axisMax = (ed.axis == 'z') ? W : D;
            int sLo = 0, sHi = 0;
            CornerPolicy::windowSafeBand(ed.lo, ed.hi, axisMax, sLo, sHi);
            const int len = sHi - sLo;               // placeable band (corner-safe)
            if (len < spec.width) continue;
            // Window COUNT comes from the wall's full architectural length (the
            // grounded windows-per-bay rule); only PLACEMENT is confined to the
            // corner-safe band. Deriving the count from the shrunk band rounded
            // corner rooms down to zero windows (auditor-caught silent loss).
            const int fullLen = ed.hi - ed.lo;
            const int nWin = (int)std::lround((fullLen / bayLength) * spec.perBay);
            for (int k = 0; k < nWin; ++k) {
                const int ideal = sLo + (int)std::lround((k + 1) * (double)len / (nWin + 1));
                // A blocked slot (the door +-1) shifts along the wall instead of dropping
                // the window; on narrow corner-shrunk spans the fixed offsets can ALL
                // collapse onto blocked cells (auditor-caught: 18 footprints silently
                // lost their only window), so fall back to an exhaustive scan of every
                // valid slot. A window is dropped ONLY when the wall genuinely has no
                // unblocked slot (short wall + centred door + corner margins).
                auto tryAt = [&](int at) -> bool {
                    const int px = (ed.axis == 'z') ? at : ed.coord;
                    const int pz = (ed.axis == 'z') ? ed.coord : at;
                    if (blocked(px, pz)) return false;
                    ProgPortal w; w.a = "exterior"; w.b = room.id;
                    w.kind = "window"; w.width = spec.width; w.height = spec.height;
                    w.infill = spec.infill;   // grounded reveal fill (shuttered default / glass)
                    w.px = px; w.pz = pz;
                    rl.portals.push_back(w);
                    return true;
                };
                bool placedWin = false;
                for (int off : {0, 2, -2, 3, -3, 4, -4}) {
                    const int at = std::min(std::max(ideal + off, sLo), sHi - spec.width);
                    if (tryAt(at)) { placedWin = true; break; }
                }
                if (!placedWin)
                    for (int at = sLo; at <= sHi - spec.width && !placedWin; ++at)
                        placedWin = tryAt(at);
            }
        }
    }
}

// Upper floors of a multi-story typology = linear guest chambers (full-width slices), so the FIRST
// slice (room 0, low end of the longer axis) is the stair-landing the flight emerges into — kept big
// enough to hold the well clear of any interior wall. minBays caps the chamber count to the footprint.
// M6 CIRCULATION GRAMMAR: the upper floor is a GALLERY serving chambers off it —
// the galleried-inn arrangement (The New Inn, Gloucester; already cited by the
// tavern room program), not a row of chambers chained door-to-door.
//
// Before M6 this produced a linear slice with a door chain, so reaching the far
// guest chamber meant walking THROUGH another guest's room — connected, but not
// livable, and undetected because the only check was "is every room linked".
//
// The gallery runs the full LENGTH along one side and holds the stair emergence
// (kStairStripCubes wide, matching the well siting in generateStoryStairs); every
// chamber opens off it, so no chamber is ever transited to reach another.
static RoomLayout generateUpperChambers(int W, int D, const std::string& purpose) {
    RoomLayout out;
    const bool lengthIsX = (W >= D);
    const int length = lengthIsX ? W : D;
    const int width  = lengthIsX ? D : W;
    const std::string chamberPurpose = purpose.empty() ? "bedchamber" : purpose;

    // A gallery needs its own strip PLUS a habitable chamber depth beside it. Too
    // narrow => fall back to the linear plan (one chamber, so no through-traffic
    // is possible and the grammar is satisfied trivially).
    // The gallery must be DEEPER than the stair well it carries, or the shaft spans
    // its full depth and severs it: the far chambers become unreachable even though
    // their doors are fine (measured — that is exactly what a 2-deep gallery with a
    // 2-deep well did). One extra cube leaves a continuous walkway past the shaft,
    // which is what a gallery beside a stairwell actually is.
    const int galleryDepth = kStairStripCubes + 1;
    if (width < galleryDepth + 2 || length < 4) {
        RoomProgram up;
        up.rooms.push_back({"chamber", chamberPurpose, 1.0});
        return generateRoomLayoutFromProgram(W, D, up);
    }

    auto rectOf = [&](int along, int alongLen, int across, int acrossLen) {
        return lengthIsX ? Rect{along, across, alongLen, acrossLen}
                         : Rect{across, along, acrossLen, alongLen};
    };
    const Rect well = stairWellRect(W, D);   // shared with generateStoryStairs

    ProgRoom gallery;
    gallery.id = "landing";
    gallery.purpose = "landing";            // AccessClass::Circulation
    gallery.rect = rectOf(0, length, 0, galleryDepth);
    out.rooms.push_back(gallery);

    // Chambers fill the strip beside the gallery, split along the length.
    const int chamberDepth = width - galleryDepth;
    const int n = std::max(2, std::min(length / 5, 4));
    const int base = length / n, extra = length % n;
    int pos = 0;
    for (int i = 0; i < n; ++i) {
        const int slice = base + (i < extra ? 1 : 0);
        ProgRoom ch;
        ch.id = (i == 0) ? "chamber" : ("chamber_" + std::to_string(i));
        ch.purpose = chamberPurpose;
        ch.rect = rectOf(pos, slice, galleryDepth, chamberDepth);
        out.rooms.push_back(ch);
        // Door from the GALLERY into this chamber. Derive it through shared() —
        // the same helper the linear chain uses — so the portal lands exactly
        // where the realizer carves (hand-computed coordinates put the doors in
        // the wrong plane and left the chambers sealed).
        const Wall w = shared(gallery.rect, ch.rect);
        if (w.ok) {
            // Two constraints, and getting either wrong is visible from outside.
            //
            // (1) NEVER on the shell. The realizer widens a 1-cube door to a
            //     2-cube carve, so a door on the first or last cell of the
            //     footprint reaches past the edge and removes the gable — a
            //     2x2 hole open to the sky. An interior door must keep a clear
            //     cube of wall on both sides.
            // (2) The door must open onto FLOOR, not the stairwell shaft. But
            //     the well only blocks a door if it actually REACHES the wall:
            //     the gallery is one cube deeper than the well (galleryDepth =
            //     kStairStripCubes + 1), so the strip adjacent to the chamber
            //     wall is clear for the well's whole length. Testing the well's
            //     span WITHOUT that adjacency check rejected every safe cell and
            //     drove the door onto the gable — which is exactly how the hole
            //     got there.
            const int alongExtent = (w.axis == 'x') ? D : W;   // footprint size along the wall
            // The landing cell: the gallery cube on the far side of the wall.
            const bool galleryBelow = (w.axis == 'x') ? (gallery.rect.x < w.coord)
                                                      : (gallery.rect.z < w.coord);
            const int landingCross = galleryBelow ? w.coord - 1 : w.coord;
            const bool wellReachesWall =
                (w.axis == 'x') ? (landingCross >= well.x && landingCross < well.x1())
                                : (landingCross >= well.z && landingCross < well.z1());
            const int wellLo = (w.axis == 'x') ? well.z : well.x;
            const int wellHi = (w.axis == 'x') ? well.z1() : well.x1();
            auto usable = [&](int c) {
                if (c < w.lo || c >= w.hi) return false;
                if (c < 1 || c > alongExtent - 2) return false;          // (1) off the shell
                if (wellReachesWall && c >= wellLo && c < wellHi) return false;  // (2) over the void
                return true;
            };
            int mid = (w.lo + w.hi) / 2;
            if (mid >= w.hi) mid = w.hi - 1;
            if (!usable(mid)) {   // walk outward from the middle for the nearest usable cell
                int best = -1, bestDist = -1;
                for (int c = w.lo; c < w.hi; ++c) {
                    if (!usable(c)) continue;
                    const int dist = std::abs(c - mid);
                    if (bestDist < 0 || dist < bestDist) { bestDist = dist; best = c; }
                }
                if (best < 0) { pos += slice; continue; }   // no legal door: leave it to the
                                                            // traversal gate rather than punch
                                                            // a hole in the shell
                mid = best;
            }
            ProgPortal d;
            d.a = gallery.id; d.b = ch.id;
            d.kind = "door"; d.width = 1; d.height = 2;
            if (w.axis == 'x') { d.px = w.coord; d.pz = mid; }
            else               { d.px = mid;     d.pz = w.coord; }
            out.portals.push_back(d);
        }
        pos += slice;
    }
    return out;
}

// The missing circulation: a switchback stair connecting every consecutive story. The well is a
// compact 2x(<=6) cube shaft at the LOW end of the longer axis — fully inside room 0 of BOTH the
// ground typology plan (taproom) and the upper chamber plan (the landing), so the emergence hole is
// never crossed by an interior wall. 2x6 is the harness-proven switchback footprint for a ~3 m story.
static void generateStoryStairs(BuildingProgram& program) {
    const int n = static_cast<int>(program.stories.size());
    if (n < 2) return;
    // M6: the well runs ALONG the long axis inside the kStairStripCubes-wide strip
    // that the upper floor's GALLERY occupies (generateUpperChambers), so the flight
    // emerges onto circulation — never into a guest's chamber. Before M6 the well ran
    // ACROSS the width, which only worked because room 0 happened to span the full
    // width; with a gallery that would have put the stair head inside a bedroom.
    const Rect well = stairWellRect(program.footprintW, program.footprintD);
    for (int s = 0; s + 1 < n; ++s) {
        bool authored = false;
        for (const auto& st : program.stories[s].stairs)
            if (st.fromStory == s && st.toStory == s + 1) authored = true;
        if (authored) continue;
        ProgStair stair;
        stair.fromStory = s; stair.toStory = s + 1; stair.rect = well;
        // FORM FOLLOWS THE SHAFT. StairPlanner always runs its flights along the
        // well's Z extent and splits switchback lanes along X, so a LONG NARROW
        // shaft (the one that fits inside a gallery) gets almost no run from a
        // switchback — it "fits" by the planner's arithmetic and is miserable in
        // practice (measured: the climb from the taproom failed). A long narrow
        // shaft takes a STRAIGHT flight; a compact shaft takes a switchback,
        // which folds to fit and keeps its headroom when stacked.
        const int lo = std::min(well.w, well.d), hi = std::max(well.w, well.d);
        stair.form = (lo > 0 && hi >= lo * 2) ? "straight" : "switchback";
        stair.kind = stair.form;
        program.stories[s].stairs.push_back(stair);
    }
}

bool autofillRoomLayout(BuildingProgram& program, unsigned seed, const RoomProgram* typology) {
    const int W = program.footprintW, D = program.footprintD;
    if (W <= 0 || D <= 0) return false;                      // no footprint -> nothing to fill

    // Grow to the typology's story count (e.g. an inn = 2: ground + lodging). Only when the caller
    // gave a single EMPTY story -> respect any authored multi-story program.
    if (typology && typology->stories > 1 &&
        program.stories.size() == 1 && program.stories[0].rooms.empty()) {
        const ProgStory proto = program.stories[0];          // copy height/etc. for each added floor
        while (static_cast<int>(program.stories.size()) < typology->stories)
            program.stories.push_back(proto);
    }

    bool typologyApplied = false;
    char frontAxis = 'z'; int frontCoord = 0;                // the entrance's wall (set on story 0)
    for (size_t i = 0; i < program.stories.size(); ++i) {
        ProgStory& st = program.stories[i];
        if (!st.rooms.empty()) continue;                     // respect authored room layouts
        RoomLayout rl;
        // Ground floor: the TYPOLOGY's grounded plan comes FIRST. The winged (L) layout
        // used to take precedence and REPLACED the typology with generic hall/service/
        // solar — an L-shaped TAVERN had no taproom (found live 2026-07-23, L-plan hunt).
        // Until winged variants of the typology programs exist, a requested non-rect
        // shape applies only when no typology plan fits; the skip is surfaced.
        if (typology && i == 0) {                            // ground floor = the typology's plan
            rl = generateRoomLayoutFromProgram(W, D, *typology, 2, program.front);
            if (!rl.rooms.empty()) {
                typologyApplied = true;                      // fit; else fall through
                if (!program.footprintShape.empty() && program.footprintShape != "rect")
                    LOG_INFO_FMT("RoomLayout", "footprintShape '" << program.footprintShape
                                 << "' requested but the typology has no winged variant"
                                 " — building RECT (surfaced, not silent)");
            }
        }
        if (rl.rooms.empty() && i == 0 && !program.footprintShape.empty() &&
            program.footprintShape != "rect") {
            rl = generateWingedLayout(W, D, program.footprintShape, seed);
            if (!rl.rooms.empty()) {
                typologyApplied = true;                      // fit; else fall through
                // A winged GROUND floor caps the building at ONE story: upper layouts
                // (chambers or generic BSP) span the full RECT, so their rooms — and
                // their furniture — would hover over the empty L-notch (found live:
                // an L-tavern's upstairs wardrobe floated in mid-air at the notch,
                // KI-5g). Truncate + surface until winged upper stories exist.
                if (program.stories.size() > 1) {
                    LOG_WARN_FMT("RoomLayout", "winged ground floor: truncating "
                                 << program.stories.size() << " stories to 1 (upper "
                                 "layouts would overhang the notch — surfaced, not silent)");
                    program.stories.resize(1);
                }
            }
        }
        // upper floor of a multi-story typology: grounded guest chambers (linear, landing = room 0).
        if (rl.rooms.empty() && i != 0 && typology && typology->stories > 1) {
            rl = generateUpperChambers(W, D, typology->upperPurpose);
            if (!rl.rooms.empty()) typologyApplied = true;
        }
        if (rl.rooms.empty()) {
            // Fallback (no typology / doesn't fit / generic upper story): generic BSP. DENSITY knob (a
            // tunable design default, NOT a grounded clearance): ~1 room per ~16 m^2, at least 1.
            const int targetRooms = std::max(1, (W * D) / 16);
            rl = generateRoomLayout(W, D, targetRooms, seed + static_cast<unsigned>(i));
        }
        // Windows per the typology's grounded rule — every story (chambers upstairs too). The
        // "front" wall (entrance's wall, the surveyed door+windows-share-a-wall pattern) is
        // derived from the ground story's exterior door and reused for upper stories.
        if (typology && typology->windows.valid()) {
            if (i == 0) {
                frontAxis = (W >= D) ? 'z' : 'x';            // default: a long wall
                frontCoord = 0;
                for (const auto& p : rl.portals) {
                    if (p.kind != "door" || (p.a != "exterior" && p.b != "exterior")) continue;
                    if (p.pz == 0) { frontAxis = 'z'; frontCoord = 0; }
                    else if (p.pz == D) { frontAxis = 'z'; frontCoord = D; }
                    else if (p.px == 0) { frontAxis = 'x'; frontCoord = 0; }
                    else if (p.px == W) { frontAxis = 'x'; frontCoord = W; }
                    break;                                   // the first exterior door = the front
                }
            }
            addTypologyWindows(rl, W, D, typology->windows, typology->bayLength, frontAxis, frontCoord);
        }
        st.rooms = rl.rooms;
        for (const auto& p : rl.portals) {
            const bool ext = (p.a == "exterior" || p.b == "exterior");
            if (ext && i != 0 && p.kind == "door") continue;  // exterior DOORS: ground story only
            st.portals.push_back(p);                          // keep any authored stairs/portals
        }
    }

    // Generate the connecting stair(s) for any multi-story building (the missing circulation).
    generateStoryStairs(program);
    return typologyApplied;
}

}  // namespace Core
}  // namespace Phyxel
