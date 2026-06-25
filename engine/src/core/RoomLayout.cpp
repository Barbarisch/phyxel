#include "core/RoomLayout.h"

#include <algorithm>
#include <queue>
#include <string>

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

void autofillRoomLayout(BuildingProgram& program, unsigned seed) {
    const int W = program.footprintW, D = program.footprintD;
    if (W <= 0 || D <= 0) return;                            // no footprint -> nothing to fill
    for (size_t i = 0; i < program.stories.size(); ++i) {
        ProgStory& st = program.stories[i];
        if (!st.rooms.empty()) continue;                     // respect authored room layouts
        // Layout DENSITY knob (a tunable design default, NOT a grounded clearance): ~1 room per
        // ~16 m^2 (a 4x4 m room), at least 1. Room *sizes* stay grounded — generateRoomLayout
        // enforces minDim (the validator's min usable room dimension).
        const int targetRooms = std::max(1, (W * D) / 16);
        RoomLayout rl = generateRoomLayout(W, D, targetRooms, seed + static_cast<unsigned>(i));
        st.rooms = rl.rooms;
        for (const auto& p : rl.portals) {
            const bool ext = (p.a == "exterior" || p.b == "exterior");
            if (ext && i != 0) continue;                     // exterior entrance: ground story only
            st.portals.push_back(p);                         // keep any authored stairs/portals
        }
    }
}

}  // namespace Core
}  // namespace Phyxel
