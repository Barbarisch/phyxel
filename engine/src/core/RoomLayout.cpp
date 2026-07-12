#include "core/RoomLayout.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <queue>
#include <string>

#include "core/RoomProgram.h"

namespace Phyxel {
namespace Core {

namespace {

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
            const int len = ed.hi - ed.lo;
            if (len < spec.width) continue;
            const int nWin = (int)std::lround((len / bayLength) * spec.perBay);
            for (int k = 0; k < nWin; ++k) {
                const int ideal = ed.lo + (int)std::lround((k + 1) * (double)len / (nWin + 1));
                // A blocked slot (the door +-1) shifts along the wall instead of dropping the
                // window (the croft's mid-wall door sits exactly on the single window's slot).
                for (int off : {0, 2, -2, 3, -3, 4, -4}) {
                    const int at = std::min(std::max(ideal + off, ed.lo), ed.hi - spec.width);
                    const int px = (ed.axis == 'z') ? at : ed.coord;
                    const int pz = (ed.axis == 'z') ? ed.coord : at;
                    if (blocked(px, pz)) continue;
                    ProgPortal w; w.a = "exterior"; w.b = room.id;
                    w.kind = "window"; w.width = spec.width; w.height = spec.height;
                    w.infill = spec.infill;   // grounded reveal fill (shuttered default / glass)
                    w.px = px; w.pz = pz;
                    rl.portals.push_back(w);
                    break;
                }
            }
        }
    }
}

// Upper floors of a multi-story typology = linear guest chambers (full-width slices), so the FIRST
// slice (room 0, low end of the longer axis) is the stair-landing the flight emerges into — kept big
// enough to hold the well clear of any interior wall. minBays caps the chamber count to the footprint.
static RoomLayout generateUpperChambers(int W, int D, const std::string& purpose) {
    const int length = std::max(W, D);
    // Wider slices (length/5, 2..4) so room 0 — the stair landing — has floor BESIDE the shaft (the
    // switchback cuts its whole well from the upper slab), not an all-hole room.
    const int chambers = std::max(2, std::min(length / 5, 4));
    RoomProgram up;
    for (int c = 0; c < chambers; ++c)
        up.rooms.push_back({"chamber", purpose.empty() ? "bedchamber" : purpose, 1.0});
    return generateRoomLayoutFromProgram(W, D, up);
}

// The missing circulation: a switchback stair connecting every consecutive story. The well is a
// compact 2x(<=6) cube shaft at the LOW end of the longer axis — fully inside room 0 of BOTH the
// ground typology plan (taproom) and the upper chamber plan (the landing), so the emergence hole is
// never crossed by an interior wall. 2x6 is the harness-proven switchback footprint for a ~3 m story.
static void generateStoryStairs(BuildingProgram& program) {
    const int n = static_cast<int>(program.stories.size());
    if (n < 2) return;
    const bool lengthIsX = (program.footprintW >= program.footprintD);
    const int wellRun = std::min(6, lengthIsX ? program.footprintD : program.footprintW);  // across-width
    Rect well;
    if (lengthIsX) well = {1, 0, 2, wellRun};   // x in [1,3] (clear of x=0 wall + the first interior wall)
    else           well = {0, 1, wellRun, 2};
    for (int s = 0; s + 1 < n; ++s) {
        bool authored = false;
        for (const auto& st : program.stories[s].stairs)
            if (st.fromStory == s && st.toStory == s + 1) authored = true;
        if (authored) continue;
        ProgStair stair;
        stair.fromStory = s; stair.toStory = s + 1; stair.rect = well;
        stair.kind = "straight"; stair.form = "switchback";
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
        // ground floor: a winged (non-rect) plan if requested, else the typology's linear plan.
        if (i == 0 && !program.footprintShape.empty() && program.footprintShape != "rect") {
            rl = generateWingedLayout(W, D, program.footprintShape, seed);
            if (!rl.rooms.empty()) typologyApplied = true;   // fit; else fall through
        }
        if (rl.rooms.empty() && typology && i == 0) {        // ground floor = the typology's plan
            rl = generateRoomLayoutFromProgram(W, D, *typology, 2, program.front);
            if (!rl.rooms.empty()) typologyApplied = true;   // fit; else fall through to generic
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
