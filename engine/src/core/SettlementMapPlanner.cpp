#include "core/SettlementMapPlanner.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <map>
#include <queue>
#include <set>
#include <unordered_map>

#include "core/BuildingProgram.h"
#include "core/PathPlanner.h"
#include "core/RoomLayout.h"      // autofillRoomLayout (predictLotDoor, M5b-1a)
#include "core/RoomProgram.h"
#include "core/SettlementLayout.h"   // pickBuildingVariant (the lot's style, D49)
#include "core/SiteAnalysis.h"
#include "core/StreetPaver.h"        // planWayPaving, padCubeForStreet (the lot's seat, M5b-1a)
#include "core/TowerForge.h"         // planTower, muralTowerSpec (the wall's towers, M5d-3)
#include "core/TownWall.h"           // towerFootprintCells

namespace Phyxel {
namespace Core {

namespace {

// ── The cost model of the town-local connection: the M4 corridor cost (CorridorGraph.cpp
// kSlopeAversion = 15: cost = step x (1 + 15 grade)^2 - DERIVED, the same number) with water
// blocked outright (M0: a path never crosses standing water) and the reuse discount on cells
// already on a way (routeReuseFraction, D27).
constexpr float kPathSlopeAversion = 15.0f;
// the testing hook (SettlementMapTest.WaysAvoidTheSteepBand's control): 0 = the connection ignores slope
float s_pathSlopeAversion = kPathSlopeAversion;

// ── M5a (2026-11-05): step 2 ran SettlementMapTest against the cluster tier as BUILT
// (planSettlementMapAsBuilt: selectBuildablePlots + centred natural footprints and NO ways -
// today's paths are an MST of the realized doors, planned after the build; chain 63: four red,
// m5a_red_green.txt); step 3 is the interest-map growth below. The as-built stays as the control.
constexpr bool kClusterGrowth = true;

struct Footprint { int fDim = 8, dDim = 6, stories = 1; };   ///< along the street / back from it

Footprint naturalFootprint(const RoomProgramRegistry* rooms, const std::string& typ, const std::string& era) {
    Footprint f;
    if (!rooms) return f;   // the stand-in (tests without a room program): an 8 x 6 long-wall house
    const RoomProgram* rp = rooms->get(typ, era);   // M5f-1: the era's own program, never another era's
    if (!rp) return f;
    const NaturalFootprint nf = naturalFootprintOf(*rp);   // M5b-1: the ONE derivation (RoomProgram.cpp)
    const int natLong = nf.longU, natShort = nf.shortU;
    const bool longToStreet = (rp->entrance == "long_wall");
    f.fDim = longToStreet ? natLong : natShort;
    f.dDim = longToStreet ? natShort : natLong;
    f.stories = std::max(1, rp->stories);
    return f;
}

bool overlaps(const Rect& a, const Rect& b, int gap) {
    return a.x < b.x1() + gap && b.x < a.x1() + gap && a.z < b.z1() + gap && b.z < a.z1() + gap;
}

// the lot rect and footprint for a typology at (x,z) facing `orientation`
struct LotShape { Rect rect, footprint; };
LotShape lotShape(int x, int z, int orientation, const Footprint& f, int setback, int depth) {
    LotShape s;
    const int frontage = f.fDim + 2 * setback;
    if (orientation == 0 || orientation == 2) { s.rect = {x, z, frontage, depth}; }
    else { s.rect = {x, z, depth, frontage}; }
    switch (orientation) {
        case 0: s.footprint = {x + (frontage - f.fDim) / 2, z + depth - setback - f.dDim, f.fDim, f.dDim}; break;   // door faces +Z
        case 1: s.footprint = {x + depth - setback - f.dDim, z + (frontage - f.fDim) / 2, f.dDim, f.fDim}; break;   // +X
        case 2: s.footprint = {x + (frontage - f.fDim) / 2, z + setback, f.fDim, f.dDim}; break;                    // -Z
        default: s.footprint = {x + setback, z + (frontage - f.fDim) / 2, f.dDim, f.fDim}; break;                   // -X
    }
    return s;
}

// a chamfer (3,4) distance field over the site from a seed mask, in cubes (two passes)
void chamfer(int W, int D, const std::vector<uint8_t>& seed, std::vector<float>& dist) {
    const float INF = 1e9f;
    dist.assign(static_cast<size_t>(W) * D, INF);
    for (int z = 0; z < D; ++z) for (int x = 0; x < W; ++x) if (seed[static_cast<size_t>(z) * W + x]) dist[static_cast<size_t>(z) * W + x] = 0.0f;
    auto at = [&](int x, int z) -> float& { return dist[static_cast<size_t>(z) * W + x]; };
    for (int z = 0; z < D; ++z) for (int x = 0; x < W; ++x) {
        float d = at(x, z);
        if (x > 0) d = std::min(d, at(x - 1, z) + 1.0f);
        if (z > 0) d = std::min(d, at(x, z - 1) + 1.0f);
        if (x > 0 && z > 0) d = std::min(d, at(x - 1, z - 1) + 1.3333f);
        if (x + 1 < W && z > 0) d = std::min(d, at(x + 1, z - 1) + 1.3333f);
        at(x, z) = d;
    }
    for (int z = D - 1; z >= 0; --z) for (int x = W - 1; x >= 0; --x) {
        float d = at(x, z);
        if (x + 1 < W) d = std::min(d, at(x + 1, z) + 1.0f);
        if (z + 1 < D) d = std::min(d, at(x, z + 1) + 1.0f);
        if (x + 1 < W && z + 1 < D) d = std::min(d, at(x + 1, z + 1) + 1.3333f);
        if (x > 0 && z + 1 < D) d = std::min(d, at(x - 1, z + 1) + 1.3333f);
        at(x, z) = d;
    }
}

struct Grid {
    int W, D;
    std::vector<int> h;            // surface y per cell
    std::vector<uint8_t> blocked;  // water or a seated lot
    std::vector<uint8_t> onNet;    // on a way
    int idx(int x, int z) const { return z * W + x; }
    bool inside(int x, int z) const { return x >= 0 && z >= 0 && x < W && z < D; }
};

// Dijkstra from `start` to the nearest goal cell (a way cell or an entry); the M4 cost model
std::vector<int> connect(const Grid& g, int start, const std::vector<uint8_t>& goal, float reuse) {
    const int N = g.W * g.D;
    std::vector<float> gc(static_cast<size_t>(N), 1e30f);
    std::vector<int> parent(static_cast<size_t>(N), -1);
    using QE = std::pair<float, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
    if (start < 0 || start >= N) return {};
    gc[static_cast<size_t>(start)] = 0.0f; open.push({0.0f, start});
    static const int NB[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    int found = -1;
    while (!open.empty()) {
        const auto [f, cur] = open.top(); open.pop();
        if (f > gc[static_cast<size_t>(cur)] + 1e-4f) continue;
        if (goal[static_cast<size_t>(cur)]) { found = cur; break; }
        const int cx = cur % g.W, cz = cur / g.W;
        for (const auto& nb : NB) {
            const int tx = cx + nb[0], tz = cz + nb[1];
            if (!g.inside(tx, tz)) continue;
            const int ti = g.idx(tx, tz);
            if (g.blocked[static_cast<size_t>(ti)] && !goal[static_cast<size_t>(ti)]) continue;
            const float stepLen = (nb[0] && nb[1]) ? 1.41421356f : 1.0f;
            const float grade = std::fabs(static_cast<float>(g.h[static_cast<size_t>(ti)] - g.h[static_cast<size_t>(cur)])) / stepLen;
            float c = stepLen * (1.0f + s_pathSlopeAversion * grade) * (1.0f + s_pathSlopeAversion * grade);
            if (g.onNet[static_cast<size_t>(ti)]) c *= reuse;
            if (gc[static_cast<size_t>(cur)] + c < gc[static_cast<size_t>(ti)] - 1e-6f) {
                gc[static_cast<size_t>(ti)] = gc[static_cast<size_t>(cur)] + c;
                parent[static_cast<size_t>(ti)] = cur;
                open.push({gc[static_cast<size_t>(ti)], ti});
            }
        }
    }
    if (found < 0) return {};
    std::vector<int> path;
    for (int c = found; c != -1; c = parent[static_cast<size_t>(c)]) path.push_back(c);
    std::reverse(path.begin(), path.end());
    return path;
}

int nodeAt(SettlementMap& map, std::map<std::pair<int, int>, int>& index, int x, int z, int y) {
    const auto key = std::make_pair(x, z);
    auto it = index.find(key);
    if (it != index.end()) return it->second;
    MapNode n; n.id = static_cast<int>(map.nodes.size()); n.x = x; n.z = z; n.y = y;
    map.nodes.push_back(n);
    index[key] = n.id;
    return n.id;
}

// ── the street tiers as built: the main-street scenario emitted as the map
void planStreetTiers(const SettlementMapInput& in, const BuildabilityMap& site, SettlementMap& map, std::map<std::pair<int, int>, int>& nodeIndex) {
    const SettlementTierPreset& tier = *in.tier;
    const bool city = tier.morphology == "semi_organic";
    char prefAxis = 0; int prefOffset = -1;
    if (!in.entries.empty()) {
        const auto& e = in.entries.front();
        const bool onXEdge = (e.x <= 0 || e.x >= in.W - 1);
        prefAxis = onXEdge ? 'X' : 'Z';
        prefOffset = std::max(0, (onXEdge ? e.z : e.x) - tier.street.mainWidth / 2);
    }
    const StreetAxisChoice pick = chooseStreetAxis(site, tier.street.mainWidth, tier.plot.depthMin, prefAxis, prefOffset);
    RoomProgramRegistry empty;
    const RoomProgramRegistry& rooms = in.rooms ? *in.rooms : empty;
    MainStreetLayout msl = city ? planCityLayout(tier, in.W, in.D, rooms, in.seed)
                                : planMainStreetLayout(tier, in.W, in.D, rooms, in.seed, pick.axis, pick.crossOffset);
    if (!msl.ok) { map.refusal = "settlement footprint too small for a main street at tier '" + tier.name + "'"; return; }
    // the buildability drop the build service applied after the layout (now the planner's)
    std::vector<AssignedPlot> keep;
    for (const auto& ap : msl.assigned) {
        bool buildable = true;
        const Rect& pr = ap.plot.rect;
        for (int z = pr.z; z < pr.z1() && buildable; ++z)
            for (int x = pr.x; x < pr.x1(); ++x) {
                if (x < 0 || z < 0 || x >= site.W || z >= site.D) { buildable = false; break; }
                const auto cls = site.at(x, z).cls;
                if (cls == Buildability::TooSteep || cls == Buildability::Water) { buildable = false; break; }
            }
        if (buildable) keep.push_back(ap); else ++map.droppedLots;
    }
    if (keep.empty()) { map.refusal = "terrain too steep - no buildable main-street plots"; return; }
    auto wayFromRect = [&](const Rect& r, const char* highway, int cls) {
        MapWay w; w.id = static_cast<int>(map.ways.size()); w.highway = highway; w.cls = cls;
        const bool alongX = r.w >= r.d;
        w.width = static_cast<float>(alongX ? r.d : r.w);
        const int cz = r.z + r.d / 2, cx = r.x + r.w / 2;
        const int ax = alongX ? r.x : cx, az = alongX ? cz : r.z, bx = alongX ? r.x1() - 1 : cx, bz = alongX ? cz : r.z1() - 1;
        const int ya = in.heightAt(std::clamp(ax, 0, in.W - 1), std::clamp(az, 0, in.D - 1)), yb = in.heightAt(std::clamp(bx, 0, in.W - 1), std::clamp(bz, 0, in.D - 1));
        w.nodes = {nodeAt(map, nodeIndex, ax, az, ya), nodeAt(map, nodeIndex, bx, bz, yb)};
        const float run = static_cast<float>(std::max(1, alongX ? r.w - 1 : r.d - 1));
        w.grade = std::fabs(static_cast<float>(yb - ya)) / run;
        map.ways.push_back(w);
        return w.id;
    };
    const int cls = (tier.name == "town" || tier.name == "city") ? 3 : 2;
    const int primary = wayFromRect(msl.mainStreet, "primary", cls);
    for (const auto& s : msl.base.streets) {
        if (s.x == msl.mainStreet.x && s.z == msl.mainStreet.z && s.w == msl.mainStreet.w && s.d == msl.mainStreet.d) continue;
        wayFromRect(s, "residential", std::max(1, cls - 1));
    }
    if (msl.hasSquare) { MapArea a; a.id = 0; a.landuse = "market"; a.rect = msl.marketSquare; map.areas.push_back(a); }
    // each lot fronts the way nearest its door-approach cell (chain 64: the city's cross-street rows
    // were all assigned to the main street - half of them front a cross or secondary street)
    auto nearestWay = [&](const MapLot& l) {
        const glm::ivec2 d = lotDoorApproach(l);
        const glm::vec2 pd(static_cast<float>(d.x), static_cast<float>(d.y));
        int best = primary; float bestD = 1e30f;
        for (const auto& w : map.ways)
            for (size_t k = 0; k + 1 < w.nodes.size(); ++k) {
                const MapNode* a = map.node(w.nodes[k]); const MapNode* b = map.node(w.nodes[k + 1]);
                if (!a || !b) continue;
                const glm::vec2 pa(static_cast<float>(a->x), static_cast<float>(a->z)), pb(static_cast<float>(b->x), static_cast<float>(b->z)), ab = pb - pa;
                const float t = glm::dot(ab, ab) > 0.0f ? std::clamp(glm::dot(pd - pa, ab) / glm::dot(ab, ab), 0.0f, 1.0f) : 0.0f;
                const float dist = glm::length(pd - (pa + ab * t)) - 0.5f * w.width;
                if (dist < bestD) { bestD = dist; best = w.id; }
            }
        return best;
    };
    for (const auto& ap : keep) {
        MapLot l; l.id = static_cast<int>(map.lots.size()); l.rect = ap.plot.rect; l.footprint = ap.footprint;
        l.typology = ap.typology; l.orientation = SettlementMap::orientationOf(ap.streetSide); l.setback = ap.setback;
        l.stories = naturalFootprint(in.rooms, ap.typology, in.era).stories; l.interest = 1.0f;
        l.way = nearestWay(l);
        map.lots.push_back(l);
    }
    if (!in.entries.empty()) map.entryNode = nodeAt(map, nodeIndex, in.entries.front().x, in.entries.front().z, in.heightAt(std::clamp(in.entries.front().x, 0, in.W - 1), std::clamp(in.entries.front().z, 0, in.D - 1)));
}

// ── the cluster tier as BUILT (step 2, the red): plots on the flattest fully-buildable 12 x 12
// windows with the build service's defaults (plot_size 12, spacing = street_width 4, max_plots
// 25), the natural footprint centred, facing +Z, and no ways.
void planClusterAsBuilt(const SettlementMapInput& in, const BuildabilityMap& site, SettlementMap& map) {
    const std::vector<Plot> plots = selectBuildablePlots(site, 12, 4, 25);
    for (size_t i = 0; i < plots.size(); ++i) {
        const std::string typ = drawTypology(in.tier->typologyWeights, static_cast<int>(i), in.seed);
        const Footprint f = naturalFootprint(in.rooms, typ, in.era);
        MapLot l; l.id = static_cast<int>(i); l.rect = plots[i].rect; l.typology = typ; l.stories = f.stories;
        l.footprint = {l.rect.x + std::max(0, (l.rect.w - f.fDim) / 2), l.rect.z + std::max(0, (l.rect.d - f.dDim) / 2), std::min(f.fDim, l.rect.w), std::min(f.dDim, l.rect.d)};
        l.orientation = 0; l.setback = in.tier->setback.min; l.way = -1;
        map.lots.push_back(l);
    }
    if (map.lots.empty()) map.refusal = "terrain too steep - no buildable plots";
}

// ── INTEREST-MAP GROWTH (M5a step 3 on the cluster tier; M5b-1 on EVERY tier - the design check of
// 2026-11-06): seat the lot of highest interest, connect it, update the maps, repeat to the program's band.
// A STREET tier first ROUTES its primary way on the same cost grid (the M4 corridor cost, water blocked) -
// from the first arriving road to the second, else to the far edge's middle half - at the program's
// main_width; a market area sits on the primary's middle cell when the program has one; a semi_organic
// tier adds a second axis from that cell to the two side edges. Then the growth: a lot whose door-approach
// cell lies on or beside a way's paved BAND fronts that way (no new way - the rows emerge from the road
// bell), a lot off every way connects by a lane of lane_width (the bell's floor keeps it a candidate: the
// infill). The cluster tier's growth is byte-for-byte M5a's (a croft always connects by a path of its own).
void planGrowth(const SettlementMapInput& in, const BuildabilityMap& site, SettlementMap& map, std::map<std::pair<int, int>, int>& nodeIndex, bool streetTier) {
    const SettlementTierPreset& tier = *in.tier;
    const int W = in.W, D = in.D;
    const size_t N = static_cast<size_t>(W) * D;
    Grid g; g.W = W; g.D = D;
    g.h.resize(N); g.blocked.assign(N, 0); g.onNet.assign(N, 0);
    std::vector<uint8_t> waterMask(N, 0), lotMask(N, 0), netMask(N, 0), reserved(N, 0), entryMask(N, 0);
    std::vector<int> wayOf(N, -1);   // the way whose paved band covers the cell (-1 = none)
    bool anyWater = false;
    for (int z = 0; z < D; ++z) for (int x = 0; x < W; ++x) {
        const SiteCell& c = site.at(x, z);
        g.h[static_cast<size_t>(g.idx(x, z))] = c.height;
        if (c.water) { waterMask[static_cast<size_t>(g.idx(x, z))] = 1; g.blocked[static_cast<size_t>(g.idx(x, z))] = 1; anyWater = true; }
    }
    std::vector<float> waterDist, netDist, lotDist;
    chamfer(W, D, waterMask, waterDist);
    // the entry: the arriving road's end, or (no road) the site edge nearest the first lot, set at its connection
    for (const auto& e : in.entries) if (g.inside(e.x, e.z)) entryMask[static_cast<size_t>(g.idx(e.x, e.z))] = 1;
    // register a routed way: nodes at its direction changes, its steepest grade, the centre cells on the network
    // (the reuse discount) and its paved BAND (the cells within half its width, Chebyshev) kept clear of lots
    // and owned by it; the first way that starts or ends on an entry sets the entry node
    auto registerWay = [&](const std::vector<int>& path, const char* highway, float width, int cls) -> int {
        MapWay w; w.id = static_cast<int>(map.ways.size()); w.highway = highway; w.width = width; w.cls = cls;
        const int r = std::max(0, static_cast<int>(std::lround(width)) / 2);
        float worst = 0.0f;
        for (size_t k = 0; k < path.size(); ++k) {
            const int cx = path[k] % W, cz = path[k] / W;
            if (k > 0) {
                const int px = path[k - 1] % W, pz = path[k - 1] / W;
                const float stepLen = (cx != px && cz != pz) ? 1.41421356f : 1.0f;
                worst = std::max(worst, std::fabs(static_cast<float>(g.h[static_cast<size_t>(path[k])] - g.h[static_cast<size_t>(path[k - 1])])) / stepLen);
            }
            bool keepNode = (k == 0 || k + 1 == path.size());
            if (!keepNode) {
                const int px = path[k - 1] % W, pz = path[k - 1] / W, nx = path[k + 1] % W, nz = path[k + 1] / W;
                keepNode = (cx - px != nx - cx) || (cz - pz != nz - cz);   // a direction change
            }
            if (keepNode) w.nodes.push_back(nodeAt(map, nodeIndex, cx, cz, g.h[static_cast<size_t>(path[k])]));
            g.onNet[static_cast<size_t>(path[k])] = 1;
            for (int dz = -r; dz <= r; ++dz) for (int dx = -r; dx <= r; ++dx) {
                if (dx * dx + dz * dz > r * r) continue;   // a disc: every band cell within r of the centre line (chain 91)
                const int bx = cx + dx, bz = cz + dz;
                if (!g.inside(bx, bz)) continue;
                const size_t bi = static_cast<size_t>(g.idx(bx, bz));
                netMask[bi] = 1;
                if (wayOf[bi] < 0) wayOf[bi] = w.id;
            }
        }
        w.grade = worst;
        if (map.entryNode < 0 && !path.empty() && !w.nodes.empty()) {
            if (entryMask[static_cast<size_t>(path.front())]) map.entryNode = w.nodes.front();
            else if (entryMask[static_cast<size_t>(path.back())]) map.entryNode = w.nodes.back();
        }
        map.ways.push_back(w);
        return w.id;
    };
    int marketCx = -1, marketCz = -1;
    if (streetTier) {
        glm::ivec2 entry = in.entries.empty() ? glm::ivec2(0, D / 2)
                                              : glm::ivec2(std::clamp(in.entries.front().x, 0, W - 1), std::clamp(in.entries.front().z, 0, D - 1));
        if (in.entries.empty()) entryMask[static_cast<size_t>(g.idx(entry.x, entry.y))] = 1;
        std::vector<uint8_t> goal(N, 0); bool haveSecond = false;
        for (size_t i = 1; i < in.entries.size(); ++i) {   // a second arriving road: the main street runs through
            const auto& e = in.entries[i];
            if (!g.inside(e.x, e.z) || (e.x == entry.x && e.z == entry.y)) continue;
            goal[static_cast<size_t>(g.idx(e.x, e.z))] = 1; haveSecond = true;
        }
        if (!haveSecond) {   // the far edge's middle half, opposite the edge the entry sits on (nearest edge for an interior one)
            const int dl = entry.x, dr = W - 1 - entry.x, db = entry.y, dt = D - 1 - entry.y;
            const int m = std::min({dl, dr, db, dt});
            if (m == dl)      for (int z = D / 4; z < (3 * D) / 4; ++z) goal[static_cast<size_t>(g.idx(W - 1, z))] = 1;
            else if (m == dr) for (int z = D / 4; z < (3 * D) / 4; ++z) goal[static_cast<size_t>(g.idx(0, z))] = 1;
            else if (m == db) for (int x = W / 4; x < (3 * W) / 4; ++x) goal[static_cast<size_t>(g.idx(x, D - 1))] = 1;
            else              for (int x = W / 4; x < (3 * W) / 4; ++x) goal[static_cast<size_t>(g.idx(x, 0))] = 1;
        }
        for (size_t i = 0; i < N; ++i) if (waterMask[i]) goal[i] = 0;   // a goal under water is no goal
        const std::vector<int> spine = connect(g, g.idx(entry.x, entry.y), goal, in.reuse);
        if (spine.empty()) { map.refusal = "no dry route for the main street from the entry to the far edge"; return; }
        const float mainW = static_cast<float>(std::max(1, tier.street.mainWidth));
        const int cls = (tier.name == "town" || tier.name == "city") ? 3 : 2;
        registerWay(spine, "primary", mainW, cls);
        int mid = spine[spine.size() / 2];
        marketCx = mid % W; marketCz = mid / W;
        if (tier.pub.marketW > 0 && tier.pub.marketD > 0 && tier.pub.marketW <= W && tier.pub.marketD <= D) {
            // the market place on the primary's middle cell (the town's widened street, the city's crossing);
            // reserved from lots, open to ways.
            // M5e-1: a market does not stand in water. The middle cell BY INDEX is tried first (so a dry site plans
            // what it always did); else the place slides along the main street, one cell at a time, nearest the
            // middle first, to the first cell whose rect is dry. No dry place = no market, and the map says so.
            const auto rectAt = [&](int cell) {
                return Rect{std::clamp(cell % W - tier.pub.marketW / 2, 0, W - tier.pub.marketW), std::clamp(cell / W - tier.pub.marketD / 2, 0, D - tier.pub.marketD), tier.pub.marketW, tier.pub.marketD};
            };
            const auto wetIn = [&](const Rect& r) {
                int n = 0;
                for (int z = r.z; z < r.z1(); ++z) for (int x = r.x; x < r.x1(); ++x) n += waterMask[static_cast<size_t>(g.idx(x, z))] ? 1 : 0;
                return n;
            };
            const int m0 = static_cast<int>(spine.size() / 2), sn = static_cast<int>(spine.size());
            int pick = -1;
            for (int k = 0; k < sn && pick < 0; ++k)
                for (const int i : {m0 + k, m0 - k}) {
                    if (i < 0 || i >= sn || (k == 0 && i != m0)) continue;
                    if (wetIn(rectAt(spine[static_cast<size_t>(i)])) == 0) { pick = i; break; }
                }
            if (pick < 0) {
                map.marketNote = "no dry place for the market along the main street (" + std::to_string(wetIn(rectAt(mid))) + " water cells at its middle)";
            } else {
            if (pick != m0) map.marketNote = "the market stands " + std::to_string(std::abs(pick - m0)) + " cells along the main street from its middle: water at the middle";
            mid = spine[static_cast<size_t>(pick)];
            marketCx = mid % W; marketCz = mid / W;
            map.marketLane = std::max(0, tier.street.laneWidth);   // D67 PROPOSED: the lane a way keeps clear across the market
            MapArea a; a.id = static_cast<int>(map.areas.size()); a.landuse = "market";
            a.rect = rectAt(mid);
            map.areas.push_back(a);
            for (int z = a.rect.z; z < a.rect.z1(); ++z) for (int x = a.rect.x; x < a.rect.x1(); ++x) reserved[static_cast<size_t>(g.idx(x, z))] = 1;
            }
        }
        if (tier.morphology == "semi_organic") {   // the crossroads: a second axis through the middle cell to both side edges
            const bool alongX = std::abs(spine.back() % W - spine.front() % W) >= std::abs(spine.back() / W - spine.front() / W);
            for (int side = 0; side < 2; ++side) {
                std::vector<uint8_t> g2(N, 0);
                if (alongX) { const int zz = side == 0 ? 0 : D - 1; for (int x = W / 4; x < (3 * W) / 4; ++x) if (!waterMask[static_cast<size_t>(g.idx(x, zz))]) g2[static_cast<size_t>(g.idx(x, zz))] = 1; }
                else        { const int xx = side == 0 ? 0 : W - 1; for (int z = D / 4; z < (3 * D) / 4; ++z) if (!waterMask[static_cast<size_t>(g.idx(xx, z))]) g2[static_cast<size_t>(g.idx(xx, z))] = 1; }
                const std::vector<int> axis = connect(g, mid, g2, in.reuse);
                if (!axis.empty()) registerWay(axis, "primary", mainW, cls);
            }
        }
    }
    // the target count: the program's band, drawn by seed (today's cluster branch ignored the band - max_plots 25)
    const int band = std::max(1, tier.buildingsMax - tier.buildingsMin + 1);
    const int target = std::min(in.lotCap, tier.buildingsMin + static_cast<int>(in.seed % static_cast<unsigned>(band)));
    map.targetLots = target;
    // every cell is a candidate position (audit E6: a stride of 2 on sites above 4096 cells moved lot positions by site
    // size - a cost bound that touched appearance; the summed-area test keeps a candidate O(1), so stride 1 everywhere)
    const int stride = 1;
    // ── the bells (Emilien 2012's forms): the tier's data, resources/settlement_program.json `bells` with a
    // `sources` entry each (GROUNDED / REASONED, see BellSpec); the defaults there are the shipped hamlet's
    const BellSpec& B = tier.bells;
    const float roadD0 = static_cast<float>(B.roadPreferredU), roadS0 = static_cast<float>(std::max(2, B.roadSpreadU));
    const float roadFloor = std::clamp(B.roadFloor, 0.0f, 1.0f);
    const float reliefR0 = std::max(1.0f, static_cast<float>(B.reliefHalfU));
    const float waterW0 = static_cast<float>(B.waterPreferredU), waterW1 = static_cast<float>(std::max(1, B.waterSpreadU)), waterFloor = std::clamp(B.waterFloor, 0.0f, 1.0f);
    const float socG0 = static_cast<float>(B.socialGapU), socG1 = static_cast<float>(std::max(2, B.socialSpreadU));
    auto bell = [](float v, float v0, float s) { const float t = (v - v0) / s; return std::exp(-t * t); };
    auto doorCell = [&](const LotShape& s, int o) -> glm::ivec2 {
        MapLot tmp; tmp.rect = s.rect; tmp.footprint = s.footprint; tmp.orientation = o;
        return lotDoorApproach(tmp);
    };
    // the city's core ring round the market: its own palette (the shops), and the program's typology caps
    const bool coreRing = streetTier && tier.coreRing > 0 && !tier.coreTypologyWeights.empty() && marketCx >= 0;
    std::map<std::string, int> typCounts;
    auto capped = [&](const std::string& t) { auto it = tier.typologyCaps.find(t); return it != tier.typologyCaps.end() && typCounts[t] >= it->second; };
    static const int NB4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    int seated = 0;
    while (seated < target) {
        bool placed = false;
        const int redraws = static_cast<int>(std::max<size_t>(1, tier.typologyWeights.size()));
        for (int salt = 0; salt < redraws && !placed; ++salt) {
            struct Draw { std::string typ; Footprint f; int setb = 1, depth = 10; bool ok = false; };
            Draw dr[2];   // 0 = the tier's palette, 1 = the core ring's (the city)
            for (int pal = 0; pal < 2; ++pal) {
                if (pal == 1 && !coreRing) break;
                const auto& weights = pal == 1 ? tier.coreTypologyWeights : tier.typologyWeights;
                dr[pal].typ = drawTypology(weights, seated, in.seed, static_cast<unsigned>(salt));
                if (streetTier && capped(dr[pal].typ)) continue;   // the caps bind counts (CityForgePlan M4) - redraw
                dr[pal].f = naturalFootprint(in.rooms, dr[pal].typ, in.era);
                dr[pal].setb = tier.setback.min + static_cast<int>((in.seed / 7u + static_cast<unsigned>(seated) * 13u) % static_cast<unsigned>(std::max(1, tier.setback.max - tier.setback.min + 1)));
                dr[pal].depth = std::clamp(dr[pal].f.dDim + dr[pal].setb + 1, tier.plot.depthMin, std::max(tier.plot.depthMin, tier.plot.depthMax));
                // chain 206 (M5b-2, found by the footprint-in-parcel check): the clamp to plot.depth_max cut the toft
                // SHORTER than its building - the city's manor_hall (13 x 20, setback 1) on an 18-deep toft ran 3 cells
                // out of its back. The toft always holds setback + the footprint (the program's depth_max is its own
                // DESIGN DECISION for the toft; the building's depth is the typology's).
                dr[pal].depth = std::max(dr[pal].depth, dr[pal].f.dDim + dr[pal].setb);
                dr[pal].ok = true;
            }
            if (!dr[0].ok && !dr[1].ok) continue;
            chamfer(W, D, netMask, netDist);
            chamfer(W, D, lotMask, lotDist);
            const bool haveNet = std::any_of(netMask.begin(), netMask.end(), [](uint8_t v) { return v != 0; });
            const bool haveEntry = std::any_of(entryMask.begin(), entryMask.end(), [](uint8_t v) { return v != 0; });
            std::vector<float> accessDist;
            if (haveNet) accessDist = netDist;
            else if (haveEntry) chamfer(W, D, entryMask, accessDist);
            // every candidate (x, z, orientation): fully buildable, off every way's band and the market, clear of seated
            // lots by the side gap. A summed-area table of the unusable cells makes the rect test O(1); the lot's relief
            // is the buildability map's window relief at the lot's centre (the window, 6, is half a lot - the rule
            // selectBuildablePlots sums over; here the centre's max-min).
            std::vector<int> bad(static_cast<size_t>(W + 1) * (D + 1), 0);
            for (int z = 0; z < D; ++z) for (int x = 0; x < W; ++x) {
                const SiteCell& c = site.at(x, z);
                const size_t ci = static_cast<size_t>(g.idx(x, z));
                const int v = (c.cls == Buildability::TooSteep || c.cls == Buildability::Water || lotMask[ci] || netMask[ci] || reserved[ci]) ? 1 : 0;
                bad[static_cast<size_t>(z + 1) * (W + 1) + (x + 1)] = v + bad[static_cast<size_t>(z) * (W + 1) + (x + 1)] + bad[static_cast<size_t>(z + 1) * (W + 1) + x] - bad[static_cast<size_t>(z) * (W + 1) + x];
            }
            auto badIn = [&](const Rect& r) {
                return bad[static_cast<size_t>(r.z1()) * (W + 1) + r.x1()] - bad[static_cast<size_t>(r.z) * (W + 1) + r.x1()] - bad[static_cast<size_t>(r.z1()) * (W + 1) + r.x] + bad[static_cast<size_t>(r.z) * (W + 1) + r.x];
            };
            struct Cand { int x, z, o, pal; float interest; LotShape shape; };
            std::vector<Cand> cands;
            for (int o = 0; o < 4; ++o)
                for (int z = 0; z < D; z += stride)
                    for (int x = 0; x < W; x += stride) {
                        // the palette is a property of the POSITION: inside the core ring (Chebyshev from the market's
                        // centre to the candidate's origin cell) the shops' palette, else the tier's
                        const int pal = (coreRing && std::max(std::abs(x - marketCx), std::abs(z - marketCz)) <= tier.coreRing) ? 1 : 0;
                        if (!dr[pal].ok) continue;
                        const Draw& d = dr[pal];
                        const LotShape s = lotShape(x, z, o, d.f, d.setb, d.depth);
                        if (s.rect.x1() > W || s.rect.z1() > D) continue;
                        if (badIn(s.rect) > 0) continue;
                        bool ok = true;
                        for (const auto& l : map.lots) if (overlaps(s.rect, l.rect, tier.plot.sideGap)) { ok = false; break; }
                        if (!ok) continue;
                        const int relief = site.at(s.rect.x + s.rect.w / 2, s.rect.z + s.rect.d / 2).relief;
                        const glm::ivec2 door = doorCell(s, o);
                        if (!g.inside(door.x, door.y) || g.blocked[static_cast<size_t>(g.idx(door.x, door.y))]) continue;
                        float interest = 1.0f;
                        if (!accessDist.empty()) interest *= roadFloor + (1.0f - roadFloor) * bell(accessDist[static_cast<size_t>(g.idx(door.x, door.y))], roadD0, roadS0);
                        interest *= bell(static_cast<float>(relief), 0.0f, reliefR0);
                        if (anyWater) {
                            const float wd = waterDist[static_cast<size_t>(g.idx(s.rect.x + s.rect.w / 2, s.rect.z + s.rect.d / 2))];
                            interest *= waterFloor + (1.0f - waterFloor) * bell(wd, waterW0, waterW1);
                        }
                        // the distance is measured from the candidate's CENTRE to the nearest seated lot's edge, so the
                        // preferred value is the gap plus half the toft's depth (stated in the bells' sources entry)
                        if (seated > 0) interest *= bell(lotDist[static_cast<size_t>(g.idx(s.rect.x + s.rect.w / 2, s.rect.z + s.rect.d / 2))], socG0 + static_cast<float>(d.depth) * 0.5f, socG1);
                        if (interest > 1e-6f) cands.push_back({x, z, o, pal, interest, s});
                    }
            std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
                if (a.interest != b.interest) return a.interest > b.interest;
                if (a.z != b.z) return a.z < b.z;
                if (a.x != b.x) return a.x < b.x;
                return a.o < b.o;
            });
            for (const auto& c : cands) {
                const Draw& d = dr[c.pal];
                const glm::ivec2 door = doorCell(c.shape, c.o);
                const int di = g.idx(door.x, door.y);
                int wayId = -1;
                // M5b-1: a door-approach cell ON a way fronts that way on every tier (chain 91: a one-cell path became a
                // one-node way on the hamlet); on a street tier a door cell BESIDE a way's paved band fronts it too
                if (haveNet && wayOf[static_cast<size_t>(di)] >= 0) wayId = wayOf[static_cast<size_t>(di)];
                else if (streetTier && haveNet) {
                    for (const auto& nb : NB4) {
                        const int nx = door.x + nb[0], nz = door.y + nb[1];
                        if (g.inside(nx, nz) && wayOf[static_cast<size_t>(g.idx(nx, nz))] >= 0) { wayId = wayOf[static_cast<size_t>(g.idx(nx, nz))]; break; }
                    }
                }
                std::vector<int> path;
                if (wayId < 0) {
                    // connect: from the door approach to the nearest way cell / entry (the first lot with no road: the nearest edge)
                    std::vector<uint8_t> goal = haveNet ? netMask : entryMask;
                    if (!haveNet && !haveEntry) {
                        // the nearest point of the site's boundary to the door becomes the entry
                        const int dl = door.x, dr2 = W - 1 - door.x, db = door.y, dt = D - 1 - door.y;
                        const int m = std::min({dl, dr2, db, dt});
                        const glm::ivec2 e = (m == dl) ? glm::ivec2(0, door.y) : (m == dr2) ? glm::ivec2(W - 1, door.y) : (m == db) ? glm::ivec2(door.x, 0) : glm::ivec2(door.x, D - 1);
                        entryMask[static_cast<size_t>(g.idx(e.x, e.y))] = 1;
                        goal = entryMask;
                    }
                    Grid gg = g;   // the candidate's own lot blocks nothing yet; its door starts outside the rect
                    path = connect(gg, di, goal, in.reuse);
                    if (path.empty()) continue;
                }
                // seat the lot
                MapLot l; l.id = static_cast<int>(map.lots.size()); l.rect = c.shape.rect; l.footprint = c.shape.footprint;
                l.typology = d.typ; l.stories = d.f.stories; l.orientation = c.o; l.setback = d.setb; l.interest = c.interest;
                if (wayId < 0) {
                    // the way: the path's cells, collinear runs merged, from the door to the network - a lane of the
                    // program's lane_width where the tier has one, else the agent's path
                    const bool lane = tier.street.laneWidth > 0;
                    const float wd = lane ? static_cast<float>(tier.street.laneWidth)
                                          : static_cast<float>(std::max(1, static_cast<int>(std::lround((2.0f * in.agent.halfWidthMicro + 1.0f) / 9.0f))));
                    wayId = registerWay(path, lane ? "lane" : "path", wd, lane ? 1 : 0);
                }
                l.way = wayId;
                map.lots.push_back(l);
                for (int zz = l.rect.z; zz < l.rect.z1(); ++zz) for (int xx = l.rect.x; xx < l.rect.x1(); ++xx) {
                    lotMask[static_cast<size_t>(g.idx(xx, zz))] = 1;
                    if (!g.onNet[static_cast<size_t>(g.idx(xx, zz))]) g.blocked[static_cast<size_t>(g.idx(xx, zz))] = 1;   // the toft is fenced; a way already there stays
                }
                ++typCounts[d.typ];
                ++seated; placed = true;
                break;
            }
        }
        if (!placed) break;
    }
    if (seated < tier.buildingsMin)
        map.refusal = "no buildable, connectable lot for the tier's minimum (" + std::to_string(seated) + " of " + std::to_string(tier.buildingsMin) + " seated)";
}

// M5f-1: a typology the tier asks for that the ERA's pack does not hold is refused by name. Before this the planner
// planned it as the 8 x 6 stand-in (or borrowed another era's program of that name) and said nothing.
std::string missingTypology(const SettlementMapInput& in) {
    if (!in.rooms || !in.tier) return {};
    for (const auto* weights : {&in.tier->typologyWeights, &in.tier->coreTypologyWeights})
        for (const auto& [typ, w] : *weights)
            if (w > 0 && !in.rooms->get(typ, in.era))
                return "the era '" + in.era + "' has no typology '" + typ + "' (the " + in.tier->name + " tier asks for it)";
    return {};
}

}  // namespace

void setPathSlopeAversionForTesting(float aversion) { s_pathSlopeAversion = aversion < 0.0f ? kPathSlopeAversion : aversion; }

LotDoor predictLotDoor(const RoomProgramRegistry* rooms, const std::string& typology, const Rect& fp, int orientation,
                       int worldX, int worldZ, const std::string& era) {
    LotDoor out;
    if (!rooms || fp.w <= 0 || fp.d <= 0) return out;
    const RoomProgram* rp = rooms->get(typology, era);
    if (!rp) return out;
    static const char* kFront[4] = {"z1", "x1", "z0", "x0"};   // orientation 0 = +Z ('N') ... 3 = -X ('W')
    const std::string front = kFront[((orientation % 4) + 4) % 4];
    // the program the realizer hands the forge for a lot (makeBp): a slab, one story of height 3, the front hint
    BuildingProgram pp;
    pp.name = typology; pp.typology = typology; pp.footprintW = fp.w; pp.footprintD = fp.d;
    pp.substructure = "slab"; pp.front = front;
    ProgStory st; st.height = 3; pp.stories.push_back(st);
    const unsigned seed = (static_cast<unsigned>(worldX) * 73856093u) ^ (static_cast<unsigned>(worldZ) * 19349663u) ^ 0x9e3779b9u;
    if (!autofillRoomLayout(pp, seed ? seed : 1u, rp) || pp.stories.empty()) return out;
    const ProgPortal* door = nullptr; bool onFront = false;
    for (const auto& po : pp.stories[0].portals) {
        if (po.kind != "door" || (po.a != "exterior" && po.b != "exterior")) continue;
        const bool f = (front == "x0" && po.px == 0) || (front == "x1" && po.px == fp.w) || (front == "z0" && po.pz == 0) || (front == "z1" && po.pz == fp.d);
        if (!door || (f && !onFront)) { door = &po; onFront = f; }
    }
    if (!door) return out;
    if (door->px == 0)         { out.wall = {fp.x, fp.z + door->pz}; out.normal = {-1, 0}; }
    else if (door->px == fp.w) { out.wall = {fp.x + fp.w - 1, fp.z + door->pz}; out.normal = {1, 0}; }
    else if (door->pz == 0)    { out.wall = {fp.x + door->px, fp.z}; out.normal = {0, -1}; }
    else                       { out.wall = {fp.x + door->px, fp.z + fp.d - 1}; out.normal = {0, 1}; }
    out.found = true; out.onFront = onFront;
    return out;
}

glm::ivec2 lotDoorApproach(const MapLot& lot) {
    const Rect& f = lot.footprint;
    switch (((lot.orientation % 4) + 4) % 4) {
        case 0: return {f.x + f.w / 2, lot.rect.z1()};        // door faces +Z: the strip beyond the lot's far edge
        case 1: return {lot.rect.x1(), f.z + f.d / 2};        // +X
        case 2: return {f.x + f.w / 2, lot.rect.z - 1};       // -Z
        default: return {lot.rect.x - 1, f.z + f.d / 2};      // -X
    }
}

// M5b-2 (D44 a; D50-D52 PROPOSED): the lot's PARCEL - the conquered subset of its rect. The FORM is Emilien et al.
// 2012 s5.3 (anisotropic conquest: sources spread on the grid until the summed cost reaches a fund; the slope cost
// quadratic in the directional gradient). Ours, labelled: D50 the front block (the frontage edge through the
// footprint's back row, the rect's full width) is in the parcel unconditionally and is the source - the building is
// seated before its parcel is conquered, so a conquest from the way could stop short of it; the fund is the back
// yard's depth + 0.5 (D52, DESIGN: without the half cell any slope at all loses the last row); the step cost is
// 1 + lambda * g^2 with g the gradient over a 13-cube window along the step (D52: adjacent integer cubes differ by 0
// or 1 - one cube step would cost 5.7 cells); lambda REASONED 4.7 (SettlementMapInput::parcelSlopeWeight). D51: the
// reached set is regularized to a depth profile - per column behind the front block, the cells reached contiguously.
// Inside the rect only (the rects are disjoint, so lots never contend); water is never conquered.
static void conquerParcel(MapLot& l, const SettlementMapInput& in, float lambda) {
    const Rect r = l.rect, f = l.footprint;
    l.parcel.clear();
    if (r.w <= 0 || r.d <= 0) return;
    int bx = 0, bz = 0, B = 0;   // the direction to the back, the back yard's depth
    const int orient = ((l.orientation % 4) + 4) % 4;   // the audit's F8: as lotDoorApproach reads it
    switch (orient) {
        case 0: bz = -1; B = f.z - r.z; break;
        case 2: bz = 1; B = r.z1() - f.z1(); break;
        case 1: bx = -1; B = f.x - r.x; break;
        default: bx = 1; B = r.x1() - f.x1(); break;
    }
    const auto front = [&](int x, int z) {
        switch (orient) { case 0: return z >= f.z; case 2: return z < f.z1(); case 1: return x >= f.x; default: return x < f.x1(); }
    };
    const int w = r.w, d = r.d;
    const auto idx = [&](int x, int z) { return static_cast<size_t>(z - r.z) * w + static_cast<size_t>(x - r.x); };
    std::vector<uint8_t> reached(static_cast<size_t>(w) * d, 0), mask(static_cast<size_t>(w) * d, 0);
    if (B > 0) {
        std::vector<float> cost(static_cast<size_t>(w) * d, 1e9f);
        using QE = std::pair<float, int>;
        std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
        for (int z = r.z; z < r.z1(); ++z) for (int x = r.x; x < r.x1(); ++x)
            if (front(x, z)) { cost[idx(x, z)] = 0.0f; pq.push({0.0f, static_cast<int>(idx(x, z))}); }
        const float fund = static_cast<float>(B) + 0.5f;
        static const int DX[4] = {1, -1, 0, 0}, DZ[4] = {0, 0, 1, -1};
        while (!pq.empty()) {
            const QE top = pq.top(); pq.pop();
            if (top.first > cost[static_cast<size_t>(top.second)]) continue;
            const int x = r.x + top.second % w, z = r.z + top.second / w;
            for (int k = 0; k < 4; ++k) {
                const int nx = x + DX[k], nz = z + DZ[k];
                if (nx < r.x || nx >= r.x1() || nz < r.z || nz >= r.z1() || front(nx, nz)) continue;
                if (in.waterAt && in.waterAt(nx, nz)) continue;
                // the audit's F5: the window is CLIPPED to the site and divided by its real span (the height cache
                // clamps at the edge - a yard at the site's edge read half its gradient over the fixed 12)
                const int ax = std::clamp(nx + 6 * DX[k], 0, in.W - 1), az = std::clamp(nz + 6 * DZ[k], 0, in.D - 1);
                const int qx = std::clamp(nx - 6 * DX[k], 0, in.W - 1), qz = std::clamp(nz - 6 * DZ[k], 0, in.D - 1);
                const int span = std::abs(ax - qx) + std::abs(az - qz);
                const float g = span > 0 ? static_cast<float>(in.heightAt(ax, az) - in.heightAt(qx, qz)) / static_cast<float>(span) : 0.0f;
                const float nc = top.first + 1.0f + lambda * g * g;
                if (nc > fund || nc >= cost[idx(nx, nz)]) continue;
                cost[idx(nx, nz)] = nc; reached[idx(nx, nz)] = 1;
                pq.push({nc, static_cast<int>(idx(nx, nz))});
            }
        }
    }
    for (int z = r.z; z < r.z1(); ++z) for (int x = r.x; x < r.x1(); ++x) if (front(x, z)) mask[idx(x, z)] = 1;
    // D51 as REVISED (pm_chain195.txt): ONE rect - the back rows EVERY column reached (the least column reach). The
    // fence's corner contract is four planes of a rect (FenceBuilder.h KI-5f); a stepped parcel waits for M5c.
    const int cols = bx != 0 ? d : w;
    int depth = B;
    for (int i = 0; i < cols; ++i) {
        int k = 1;
        for (; k <= B; ++k) {
            const int x = bx != 0 ? (bx < 0 ? f.x - k : f.x1() - 1 + k) : r.x + i;
            const int z = bx != 0 ? r.z + i : (bz < 0 ? f.z - k : f.z1() - 1 + k);
            if (!reached[idx(x, z)]) break;
        }
        depth = std::min(depth, k - 1);
    }
    for (int i = 0; i < cols; ++i)
        for (int k = 1; k <= depth; ++k) {
            const int x = bx != 0 ? (bx < 0 ? f.x - k : f.x1() - 1 + k) : r.x + i;
            const int z = bx != 0 ? r.z + i : (bz < 0 ? f.z - k : f.z1() - 1 + k);
            mask[idx(x, z)] = 1;
        }
    // the record: row runs, merged down the rows where a run repeats exactly
    std::vector<size_t> open;
    for (int z = r.z; z < r.z1(); ++z) {
        std::vector<size_t> next;
        for (int x = r.x; x < r.x1();) {
            if (!mask[idx(x, z)]) { ++x; continue; }
            int x1 = x;
            while (x1 < r.x1() && mask[idx(x1, z)]) ++x1;
            bool merged = false;
            for (const size_t o : open)
                if (l.parcel[o].x == x && l.parcel[o].w == x1 - x && l.parcel[o].z1() == z) { ++l.parcel[o].d; next.push_back(o); merged = true; break; }
            if (!merged) { l.parcel.push_back({x, z, x1 - x, 1}); next.push_back(l.parcel.size() - 1); }
            x = x1;
        }
        open = std::move(next);
    }
}

// M5c-2 (D56-D58 PROPOSED): the CROFTS - behind the parcel of every lot whose typology the tier's program names, ONE
// rect of the ground nothing else takes. No croft dimension exists in any source read (the grounding pass of
// 2026-11-14), so none is written: the croft is sized by the ground. From the FINISHED map only:
//   taken   = every toft rect, every way's band (the centreline's cells grown by half its width + 1), every water
//             cell, every market area; a lot's OWN rows that its parcel gave up are free to its own croft (O22 c);
//   reach   = per column of the parcel's back edge, the free cells straight back until a taken cell or the site edge;
//   the rect = the largest rectangle standing on that edge (ties: the lower start, then the wider);
//   contended cells are settled by growing every croft ONE ROW PER ROUND in lot order up to its own depth;
//   a croft thinner than 2 cells either way is dropped (the fence's and the terrace rule's minimum).
static void planCrofts(SettlementMap& map, const SettlementMapInput& in) {
    if (!in.tier || in.tier->croftTypologies.empty() || map.W <= 0 || map.D <= 0) return;
    const int W = map.W, D = map.D;
    const auto idx = [W](int x, int z) { return static_cast<size_t>(z) * W + static_cast<size_t>(x); };
    std::vector<int> taken(static_cast<size_t>(W) * D, -1);   // -1 free | -2 way, water, market | >= 0 the lot id whose toft holds the cell
    const auto mark = [&](int x, int z, int v) { if (x >= 0 && z >= 0 && x < W && z < D && taken[idx(x, z)] == -1) taken[idx(x, z)] = v; };
    for (const auto& l : map.lots)
        for (int z = l.rect.z; z < l.rect.z1(); ++z) for (int x = l.rect.x; x < l.rect.x1(); ++x) mark(x, z, l.id);
    for (const auto& w : map.ways) {
        const int r = static_cast<int>(std::ceil(w.width / 2.0f)) + 1;
        for (size_t i = 0; i + 1 < w.nodes.size(); ++i) {
            const MapNode* a = map.node(w.nodes[i]); const MapNode* b = map.node(w.nodes[i + 1]);
            if (!a || !b) continue;
            const int n = std::max({std::abs(b->x - a->x), std::abs(b->z - a->z), 1});
            for (int t = 0; t <= n; ++t) {
                const int cx = a->x + static_cast<int>(std::lround(static_cast<float>(b->x - a->x) * t / n));
                const int cz = a->z + static_cast<int>(std::lround(static_cast<float>(b->z - a->z) * t / n));
                for (int dz = -r; dz <= r; ++dz) for (int dx = -r; dx <= r; ++dx) mark(cx + dx, cz + dz, -2);
            }
        }
    }
    for (const auto& a : map.areas)
        for (int z = a.rect.z; z < a.rect.z1(); ++z) for (int x = a.rect.x; x < a.rect.x1(); ++x) mark(x, z, -2);
    if (in.waterAt)
        for (int z = 0; z < D; ++z) for (int x = 0; x < W; ++x) if (in.waterAt(x, z)) mark(x, z, -2);

    struct Cand { int lot = -1; int ax = 0, az = 0; int ux = 0, uz = 0; int bx = 0, bz = 0; int start = 0, width = 0, depth = 0, grown = 0; };
    std::vector<Cand> cands;
    for (const auto& l : map.lots) {
        if (!in.tier->hasCroft(l.typology)) continue;
        const Rect p = l.parcelRect();
        Cand c; c.lot = l.id;
        int cols = 0;
        switch (((l.orientation % 4) + 4) % 4) {   // a = the first cell behind the back edge, u = along it, b = back
            case 0: c.ax = p.x; c.az = p.z - 1; c.ux = 1; c.bz = -1; cols = p.w; break;
            case 2: c.ax = p.x; c.az = p.z1(); c.ux = 1; c.bz = 1; cols = p.w; break;
            case 1: c.ax = p.x - 1; c.az = p.z; c.uz = 1; c.bx = -1; cols = p.d; break;
            default: c.ax = p.x1(); c.az = p.z; c.uz = 1; c.bx = 1; cols = p.d; break;
        }
        std::vector<int> reach(static_cast<size_t>(cols), 0);
        for (int i = 0; i < cols; ++i) {
            int k = 0;
            for (;; ++k) {
                const int x = c.ax + c.ux * i + c.bx * k, z = c.az + c.uz * i + c.bz * k;
                if (x < 0 || z < 0 || x >= W || z >= D) break;
                const int t = taken[idx(x, z)];
                if (t == -1) continue;
                if (t == l.id && !l.inParcel(x, z)) continue;   // its own toft's rows the parcel gave up
                break;
            }
            reach[static_cast<size_t>(i)] = k;
        }
        long best = 0;
        for (int a = 0; a < cols; ++a) {
            int least = INT_MAX;
            for (int b = a; b < cols; ++b) {
                least = std::min(least, reach[static_cast<size_t>(b)]);
                if (least < 2) break;
                const int wdt = b - a + 1;
                const long area = static_cast<long>(wdt) * least;
                if (wdt >= 2 && area > best) { best = area; c.start = a; c.width = wdt; c.depth = least; }
            }
        }
        if (best > 0) cands.push_back(c);
    }
    std::vector<uint8_t> claimed(static_cast<size_t>(W) * D, 0);
    for (bool any = true; any;) {
        any = false;
        for (auto& c : cands) {
            if (c.grown >= c.depth) continue;
            bool freeRow = true;
            for (int i = 0; i < c.width && freeRow; ++i) {
                const int x = c.ax + c.ux * (c.start + i) + c.bx * c.grown, z = c.az + c.uz * (c.start + i) + c.bz * c.grown;
                if (claimed[idx(x, z)]) freeRow = false;
            }
            if (!freeRow) { c.depth = c.grown; continue; }   // met another croft: it stops here
            for (int i = 0; i < c.width; ++i)
                claimed[idx(c.ax + c.ux * (c.start + i) + c.bx * c.grown, c.az + c.uz * (c.start + i) + c.bz * c.grown)] = 1;
            ++c.grown; any = true;
        }
    }
    for (const auto& c : cands) {
        if (c.grown < 2) continue;   // (its claimed row, if any, stays unowned - counted by nobody; stated)
        const int x0 = c.ax + c.ux * c.start, z0 = c.az + c.uz * c.start;
        const int x1 = x0 + c.ux * (c.width - 1) + c.bx * (c.grown - 1), z1 = z0 + c.uz * (c.width - 1) + c.bz * (c.grown - 1);
        MapArea a; a.id = static_cast<int>(map.areas.size()); a.landuse = "croft"; a.lot = c.lot;
        a.rect = Rect{std::min(x0, x1), std::min(z0, z1), std::abs(x1 - x0) + 1, std::abs(z1 - z0) + 1};
        map.areas.push_back(a);
    }
}

// M5d-1 (D59-D61 PROPOSED): the WALL as map data, from the FINISHED map. The pipeline's SHAPE is van der Staaij et
// al. (arXiv 2309.10871, section V-B: the convex hull of the plots, pushed outwards, joined by weighted A*); the paper
// gives no parameter, so every one below is DESIGN and says so:
//   hull    = the convex hull of every lot rect's and the market's corner cells;
//   push    = each hull vertex moved away from the hull's centroid by ceil(margin * sqrt 2) + 2 cells (the least that
//             clears a rect grown by the tier's margin in any direction, plus the band's own two cells);
//   route   = consecutive pushed vertices joined on the cube grid (4-connected), integer costs in sixteenths of a
//             step: 16 a step, + 16 a turn, + round(16 * parcelSlopeWeight) * dh^2 (the parcel conquest's slope
//             weight, reused), + 64 on a way's band (so a way is crossed square, not followed), + 16 * (grid w + d) on
//             a water cell (water is crossed only where no dry route exists, by the fewest cells);
//   forbidden = a lot rect grown by the margin, the market, the hull itself (so the ring goes ROUND every lot); and
//             to each leg, a cut from the centroid out through the stop half the ring away (so it goes the SHORT way);
//   gates   = where a way's centreline - or its straight continuation out of the site from an end node on the site
//             edge - meets the ring: one gate per run of ring cells, at the run's middle, as wide as the wider of the
//             way and the tier's gate_width;
//   road gates = where the ring stands on the WORLD road (in.roadAt, M5d-1b): the arriving road runs on out of the
//             site past the settlement's own ways, and a wall that gates only those seals it in (chain 253, live);
//   water gaps = the runs of ring cells in water: the band is left open there (D61) and the record says so.
// The ring may leave the site rect (the lots touch its edge); it reads `in`'s own samplers, not the site cache.
static void planWall(SettlementMap& map, const SettlementMapInput& in) {
    if (!in.tier || !in.tier->walls.enabled || map.lots.empty()) return;
    MapWall& wall = map.wall;
    const auto wallT0 = std::chrono::steady_clock::now();
    struct Timer { MapWall& w; std::chrono::steady_clock::time_point t0; ~Timer() { w.bakeMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count(); } } timer{wall, wallT0};
    const int mg = std::max(0, in.tier->walls.marginCubes);
    wall.margin = mg;
    using P = std::pair<long, long>;
    std::vector<P> pts;
    const auto addRect = [&pts](const Rect& r) {
        if (r.w <= 0 || r.d <= 0) return;
        pts.push_back({r.x, r.z}); pts.push_back({r.x1() - 1, r.z}); pts.push_back({r.x, r.z1() - 1}); pts.push_back({r.x1() - 1, r.z1() - 1});
    };
    for (const auto& l : map.lots) addRect(l.rect);
    for (const auto& a : map.areas) if (a.landuse == "market") addRect(a.rect);
    std::sort(pts.begin(), pts.end()); pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
    const auto cross = [](const P& o, const P& a, const P& b) { return (a.first - o.first) * (b.second - o.second) - (a.second - o.second) * (b.first - o.first); };
    std::vector<P> hull;   // monotone chain, counter-clockwise, collinear points dropped
    for (int pass = 0; pass < 2; ++pass) {
        const size_t base = hull.size();
        for (size_t k = 0; k < pts.size(); ++k) {
            const P& q = pass == 0 ? pts[k] : pts[pts.size() - 1 - k];
            while (hull.size() >= base + 2 && cross(hull[hull.size() - 2], hull.back(), q) <= 0) hull.pop_back();
            hull.push_back(q);
        }
        hull.pop_back();
    }
    if (hull.size() < 3) { wall.refusal = "the lots span no area to wall"; return; }
    double cx = 0.0, cz = 0.0;
    for (const auto& h : hull) { cx += static_cast<double>(h.first); cz += static_cast<double>(h.second); }
    cx /= static_cast<double>(hull.size()); cz /= static_cast<double>(hull.size());
    const int push = static_cast<int>(std::ceil(static_cast<double>(mg) * 1.41421356)) + 2;

    int gx0 = 0, gz0 = 0, gx1 = map.W, gz1 = map.D;
    for (const auto& h : hull) {
        gx0 = std::min(gx0, static_cast<int>(h.first)); gz0 = std::min(gz0, static_cast<int>(h.second));
        gx1 = std::max(gx1, static_cast<int>(h.first) + 1); gz1 = std::max(gz1, static_cast<int>(h.second) + 1);
    }
    const int pad = push + 12;
    gx0 -= pad; gz0 -= pad; gx1 += pad; gz1 += pad;
    const int GW = gx1 - gx0, GD = gz1 - gz0;
    const auto gi = [gx0, gz0, GW](int x, int z) { return static_cast<size_t>(z - gz0) * GW + static_cast<size_t>(x - gx0); };
    const auto inGrid = [gx0, gz0, gx1, gz1](int x, int z) { return x >= gx0 && z >= gz0 && x < gx1 && z < gz1; };
    std::vector<uint8_t> forbidden(static_cast<size_t>(GW) * GD, 0), wet(static_cast<size_t>(GW) * GD, 0), onWay(static_cast<size_t>(GW) * GD, 0);
    std::vector<int> hgt(static_cast<size_t>(GW) * GD, 0);
    // M5d-1b (chain 253): the WORLD road, where the request knows it - the ring is gated wherever it stands on one
    std::vector<uint8_t> road(static_cast<size_t>(GW) * GD, 0);
    for (int z = gz0; z < gz1; ++z) for (int x = gx0; x < gx1; ++x) {
        hgt[gi(x, z)] = in.heightAt(x, z);
        if (in.waterAt && in.waterAt(x, z)) wet[gi(x, z)] = 1;
        if (in.roadAt && in.roadAt(x, z) > 0) road[gi(x, z)] = 1;
        bool inHull = true;
        for (size_t k = 0; k < hull.size() && inHull; ++k)
            if (cross(hull[k], hull[(k + 1) % hull.size()], P{x, z}) < 0) inHull = false;
        if (inHull) forbidden[gi(x, z)] = 1;
    }
    const auto forbidRect = [&](const Rect& r, int grow) {
        for (int z = r.z - grow; z < r.z1() + grow; ++z) for (int x = r.x - grow; x < r.x1() + grow; ++x) if (inGrid(x, z)) forbidden[gi(x, z)] = 1;
    };
    for (const auto& l : map.lots) forbidRect(l.rect, mg);
    for (const auto& a : map.areas) if (a.landuse == "market") forbidRect(a.rect, 0);
    // every way's line: between its nodes, and straight out of the site from an end node on the site edge
    const auto wayLine = [&map, &inGrid](const MapWay& w) {
        std::vector<std::pair<int, int>> line;
        for (size_t i = 0; i + 1 < w.nodes.size(); ++i) {
            const MapNode* a = map.node(w.nodes[i]); const MapNode* b = map.node(w.nodes[i + 1]);
            if (!a || !b) continue;
            const int n = std::max({std::abs(b->x - a->x), std::abs(b->z - a->z), 1});
            for (int t = 0; t <= n; ++t)
                line.push_back({a->x + static_cast<int>(std::lround(static_cast<float>(b->x - a->x) * t / n)), a->z + static_cast<int>(std::lround(static_cast<float>(b->z - a->z) * t / n))});
        }
        if (w.nodes.empty()) return line;
        for (const int end : {w.nodes.front(), w.nodes.back()}) {
            const MapNode* n = map.node(end);
            if (!n) continue;
            int dx = 0, dz = 0;
            if (n->x <= 0) dx = -1; else if (n->x >= map.W - 1) dx = 1; else if (n->z <= 0) dz = -1; else if (n->z >= map.D - 1) dz = 1; else continue;
            for (int k = 1; inGrid(n->x + dx * k, n->z + dz * k); ++k) line.push_back({n->x + dx * k, n->z + dz * k});
        }
        return line;
    };
    for (const auto& w : map.ways) {
        const int r = static_cast<int>(std::ceil(w.width / 2.0f));
        for (const auto& c : wayLine(w))
            for (int dz = -r; dz <= r; ++dz) for (int dx = -r; dx <= r; ++dx) if (inGrid(c.first + dx, c.second + dz)) onWay[gi(c.first + dx, c.second + dz)] = 1;
    }

    std::vector<std::pair<int, int>> stops;
    for (const auto& h : hull) {
        const double vx = static_cast<double>(h.first) - cx, vz = static_cast<double>(h.second) - cz;
        const double len = std::max(1e-9, std::sqrt(vx * vx + vz * vz));
        bool placed = false;
        for (int extra = 0; extra <= 8 && !placed; ++extra) {
            const int x = static_cast<int>(h.first) + static_cast<int>(std::lround(vx / len * (push + extra)));
            const int z = static_cast<int>(h.second) + static_cast<int>(std::lround(vz / len * (push + extra)));
            if (!inGrid(x, z) || forbidden[gi(x, z)]) continue;
            if (stops.empty() || stops.back() != std::make_pair(x, z)) stops.push_back({x, z});
            placed = true;
        }
        if (!placed) { wall.refusal = "a hull corner has no free cell within reach of its push"; return; }
    }
    if (stops.size() >= 2 && stops.front() == stops.back()) stops.pop_back();
    if (stops.size() < 3) { wall.refusal = "the pushed hull has fewer than three corners"; return; }

    const int kStep = 16, kTurn = 16, kWay = 64, kWater = 16 * (GW + GD);
    const int kSlope = static_cast<int>(std::lround(16.0f * in.parcelSlopeWeight));
    static const int DX[4] = {1, 0, -1, 0}, DZ[4] = {0, 1, 0, -1};
    // chain 236: a leg whose own side is dear (it must cross water) went the LONG way round the town and the ring
    // erased itself. Each leg is therefore cut off from the far side: the cells of the line from the centroid out
    // through the stop OPPOSITE the leg (half the ring away) to the grid's edge are closed to it.
    std::vector<uint8_t> cut(static_cast<size_t>(GW) * GD, 0);
    const auto setCut = [&](std::pair<int, int> through) {
        std::fill(cut.begin(), cut.end(), static_cast<uint8_t>(0));
        const double vx = static_cast<double>(through.first) - cx, vz = static_cast<double>(through.second) - cz;
        const double len = std::max(1e-9, std::max(std::abs(vx), std::abs(vz)));
        for (int k = 0;; ++k) {   // one cell a step along the longer axis: an 8-connected line, which a 4-connected route cannot pass
            const int x = static_cast<int>(std::lround(cx + vx / len * k)), z = static_cast<int>(std::lround(cz + vz / len * k));
            if (!inGrid(x, z)) break;
            cut[gi(x, z)] = 1;
        }
    };
    const auto route = [&](std::pair<int, int> from, std::pair<int, int> to, std::vector<std::pair<int, int>>& outCells) {
        const size_t N = static_cast<size_t>(GW) * GD;
        std::vector<long long> best(N * 5, LLONG_MAX);   // long is 32 bits on MSVC (the audit of M5d-1)
        std::vector<int> prev(N * 5, -1);
        using Q = std::pair<long long, int>;   // (cost + heuristic, state); state = cell * 5 + direction (4 = none)
        std::priority_queue<Q, std::vector<Q>, std::greater<Q>> open;
        const auto heur = [&](int x, int z) { return static_cast<long long>(kStep) * (std::abs(x - to.first) + std::abs(z - to.second)); };
        const int s0 = static_cast<int>(gi(from.first, from.second)) * 5 + 4;
        best[static_cast<size_t>(s0)] = 0; open.push({heur(from.first, from.second), s0});
        int goal = -1;
        while (!open.empty()) {
            const auto [f, st] = open.top(); open.pop();
            const int cell = st / 5, dir = st % 5;
            const int x = gx0 + cell % GW, z = gz0 + cell / GW;
            const long long g = best[static_cast<size_t>(st)];
            if (f - heur(x, z) > g) continue;
            if (x == to.first && z == to.second) { goal = st; break; }
            for (int d = 0; d < 4; ++d) {
                if (dir != 4 && d == (dir + 2) % 4) continue;   // no reversal
                const int nx = x + DX[d], nz = z + DZ[d];
                if (!inGrid(nx, nz) || forbidden[gi(nx, nz)] || cut[gi(nx, nz)]) continue;
                const int dh = hgt[gi(nx, nz)] - hgt[gi(x, z)];
                long long c = kStep + static_cast<long long>(kSlope) * dh * dh;
                if (dir != 4 && d != dir) c += kTurn;
                if (onWay[gi(nx, nz)] || road[gi(nx, nz)]) c += kWay;   // a way's band or the world road: crossed square, not followed
                if (wet[gi(nx, nz)]) c += kWater;
                const int ns = static_cast<int>(gi(nx, nz)) * 5 + d;
                if (g + c < best[static_cast<size_t>(ns)]) { best[static_cast<size_t>(ns)] = g + c; prev[static_cast<size_t>(ns)] = st; open.push({g + c + heur(nx, nz), ns}); }
            }
        }
        if (goal < 0) return false;
        std::vector<std::pair<int, int>> rev;
        for (int st = goal; st >= 0; st = prev[static_cast<size_t>(st)]) rev.push_back({gx0 + (st / 5) % GW, gz0 + (st / 5) / GW});
        outCells.assign(rev.rbegin(), rev.rend());
        return true;
    };
    // the closed walk through every stop, loops erased as they close (a cell met again cuts the walk back to it)
    std::vector<std::pair<int, int>> walk;
    std::map<std::pair<int, int>, size_t> at;
    bool closed = false;
    for (size_t i = 0; i < stops.size() && !closed; ++i) {
        std::vector<std::pair<int, int>> leg;
        setCut(stops[(i + 1 + stops.size() / 2) % stops.size()]);
        if (!route(stops[i], stops[(i + 1) % stops.size()], leg)) { wall.refusal = "no route between two corners of the pushed hull"; return; }
        const bool last = i + 1 == stops.size();
        for (size_t k = (i == 0 ? 0 : 1); k < leg.size(); ++k) {
            const auto it = at.find(leg[k]);
            if (it == at.end()) { at[leg[k]] = walk.size(); walk.push_back(leg[k]); continue; }
            const size_t back = it->second;
            if (last) {   // the ring closes on a cell of its own start: what lies before that cell is a spur, dropped
                walk.erase(walk.begin(), walk.begin() + static_cast<std::ptrdiff_t>(back));
                closed = true; break;
            }
            for (size_t e = back + 1; e < walk.size(); ++e) at.erase(walk[e]);
            walk.resize(back + 1);
        }
    }
    if (!closed || walk.size() < 8) {
        wall.refusal = "the route did not close into a ring (" + std::to_string(stops.size()) + " corners, " + std::to_string(walk.size()) + " cells, " + (closed ? "closed" : "open") + "; corners:";
        for (const auto& st : stops) wall.refusal += " " + std::to_string(st.first) + "," + std::to_string(st.second);
        wall.refusal += ")";
        return;
    }
    for (size_t i = 0; i < walk.size(); ++i) {
        const auto& a = walk[i]; const auto& b = walk[(i + 1) % walk.size()];
        if (std::abs(a.first - b.first) + std::abs(a.second - b.second) != 1) { wall.refusal = "the ring is not continuous"; return; }
    }
    // start the ring at its first corner in (z, x) order - the record's form (MapWall: nodes are corners)
    for (const auto& c : walk) wall.ring.push_back({c.first, c.second});
    {
        const auto cs = wall.corners();
        if (cs.size() < 4) { wall.ring.clear(); wall.refusal = "the ring has fewer than four corners"; return; }
        MapWall::Pt first = cs[0];
        for (const auto& c : cs) if (c.z < first.z || (c.z == first.z && c.x < first.x)) first = c;
        size_t k = 0;
        while (wall.ring[k].x != first.x || wall.ring[k].z != first.z) ++k;
        std::rotate(wall.ring.begin(), wall.ring.begin() + static_cast<std::ptrdiff_t>(k), wall.ring.end());
    }
    std::map<std::pair<int, int>, size_t> ringAt;
    for (size_t i = 0; i < wall.ring.size(); ++i) ringAt[{wall.ring[i].x, wall.ring[i].z}] = i;
    for (const auto& w : map.ways) {
        const auto line = wayLine(w);
        std::vector<uint8_t> hit(wall.ring.size(), 0);
        for (const auto& c : line) { const auto it = ringAt.find(c); if (it != ringAt.end()) hit[it->second] = 1; }
        const size_t n = wall.ring.size();
        size_t start = n;
        for (size_t i = 0; i < n; ++i) if (!hit[i]) { start = i; break; }
        if (start == n) continue;   // the whole ring lies on one way: not a crossing this rule can gate
        for (size_t k = 0; k < n;) {
            const size_t i = (start + k) % n;
            if (!hit[i]) { ++k; continue; }
            size_t len = 0;
            while (len < n && hit[(i + len) % n]) ++len;
            const MapWall::Pt mid = wall.ring[(i + len / 2) % n];
            wall.gates.push_back({mid.x, mid.z, w.id, std::max(in.tier->walls.gateWidthCubes, static_cast<int>(std::ceil(w.width)))});
            k += len;
        }
    }
    {   // M5d-1b: every ring cell on the world road lies inside a gate's opening. A cell is COVERED by a gate when it
        // is within half that gate's width of it along the ring (the audit of M5d-1b: a gate of the settlement's own
        // ways somewhere in the run did not cover a road running along the ring). Each stretch left uncovered gets a
        // road gate at its middle, as wide as the wider of the stretch and the tier's gate width.
        const size_t n = wall.ring.size();
        std::vector<uint8_t> need(n, 0);
        size_t needed = 0;
        for (size_t i2 = 0; i2 < n; ++i2) if (road[gi(wall.ring[i2].x, wall.ring[i2].z)]) { need[i2] = 1; ++needed; }
        for (const auto& g : wall.gates) {
            const auto it = ringAt.find({g.x, g.z});
            if (it == ringAt.end()) continue;
            const size_t half = static_cast<size_t>(std::max(1, g.width / 2));
            for (size_t d = 0; d <= half && d < n; ++d) { need[(it->second + d) % n] = 0; need[(it->second + n - d) % n] = 0; }
        }
        size_t start = n;
        for (size_t i2 = 0; i2 < n; ++i2) if (!need[i2]) { start = i2; break; }
        if (start == n && needed > 0) { wall.ring.clear(); wall.gates.clear(); wall.refusal = "the whole ring stands on the world road"; return; }
        for (size_t k = 0; start < n && k < n;) {
            const size_t i2 = (start + k) % n;
            if (!need[i2]) { ++k; continue; }
            size_t len = 0;
            while (len < n && need[(i2 + len) % n]) ++len;
            const MapWall::Pt mid = wall.ring[(i2 + len / 2) % n];
            wall.gates.push_back({mid.x, mid.z, -1, std::max(in.tier->walls.gateWidthCubes, static_cast<int>(len))});
            k += len;
        }
    }
    {
        const size_t n = wall.ring.size();
        const auto isWet = [&](size_t i) { return wet[gi(wall.ring[i].x, wall.ring[i].z)] != 0; };
        size_t start = n;
        for (size_t i = 0; i < n; ++i) if (!isWet(i)) { start = i; break; }
        if (start == n) { wall.ring.clear(); wall.gates.clear(); wall.refusal = "the whole ring stands in water"; return; }
        for (size_t k = 0; k < n;) {
            const size_t i = (start + k) % n;
            if (!isWet(i)) { ++k; continue; }
            int len = 0;
            while (static_cast<size_t>(len) < n && isWet((i + static_cast<size_t>(len)) % n)) ++len;
            wall.waterGaps.push_back({wall.ring[i].x, wall.ring[i].z, len});
            k += static_cast<size_t>(len);
        }
    }
    // O29 b (chain 275): a way that ends ON THE SITE EDGE and is gated where its straight continuation meets the ring
    // is carried through its own gate - one more node 3 cells beyond the ring (clear of the band's two cells) - so
    // the realizer's ways unit paves and grades it to the ground outside. Without it the way stops at the edge and
    // the gate opens onto whatever lies there (live: a road's cutting two cubes below the street's end).
    {
        int nextId = 0;
        for (const auto& nd : map.nodes) nextId = std::max(nextId, nd.id + 1);
        for (auto& w : map.ways) {
            if (w.nodes.size() < 2) continue;
            for (int endIdx = 0; endIdx < 2; ++endIdx) {
                const MapNode* nd = map.node(endIdx == 0 ? w.nodes.front() : w.nodes.back());
                if (!nd) continue;
                int dx = 0, dz = 0;
                if (nd->x <= 0) dx = -1; else if (nd->x >= map.W - 1) dx = 1; else if (nd->z <= 0) dz = -1; else if (nd->z >= map.D - 1) dz = 1; else continue;
                const int ex = nd->x, ez = nd->z;
                for (int k = 1; inGrid(ex + dx * (k + 3), ez + dz * (k + 3)); ++k) {
                    if (!ringAt.count({ex + dx * k, ez + dz * k})) continue;
                    const int tx = ex + dx * (k + 3), tz = ez + dz * (k + 3);
                    if (wet[gi(tx, tz)] || forbidden[gi(tx, tz)]) break;
                    MapNode out; out.id = nextId++; out.x = tx; out.z = tz; out.y = hgt[gi(tx, tz)];
                    map.nodes.push_back(out);
                    if (endIdx == 0) w.nodes.insert(w.nodes.begin(), out.id); else w.nodes.push_back(out.id);
                    break;
                }
            }
        }
    }
    // M5d-3 (D64 as refined, PROPOSED): a tower at each ROUTED corner - the stops the ring was routed through, which
    // are the pushed hull's corners. The form gives the places and the count; no spacing number exists (O26 a) and
    // none is written. The drum is the tier's tower_size square, centred on the corner; where that stands on
    // something it may not, it is tried further OUT along the corner's own push, one cell at a time, for as long as
    // the drum still holds the corner's ring cell (so the curtain meets it). A drum may not stand on a lot rect, a
    // croft or the market, a way's band, a gate's opening, the world road or water, nor touch another drum; its
    // doorway (TowerForge's, on the side facing the hull's centroid first) must open on a cell strictly inside the
    // ring. A corner with no such place gets an entry that is NOT BUILT and says why.
    if (in.tier->walls.towers) {
        const TownWallSpec& ws = in.tier->walls;
        const int size = std::max(1, ws.towerSize);
        const size_t rn = wall.ring.size();
        std::vector<uint8_t> outside(static_cast<size_t>(GW) * GD, 0);
        if (!ringAt.count({gx0, gz0})) {   // the grid's corner lies outside the ring (the grid is padded past it)
            std::vector<std::pair<int, int>> stack{{gx0, gz0}};
            outside[gi(gx0, gz0)] = 1;
            while (!stack.empty()) {
                const auto c = stack.back(); stack.pop_back();
                for (int d = 0; d < 4; ++d) {
                    const int nx = c.first + DX[d], nz = c.second + DZ[d];
                    if (!inGrid(nx, nz) || outside[gi(nx, nz)] || ringAt.count({nx, nz})) continue;
                    outside[gi(nx, nz)] = 1; stack.push_back({nx, nz});
                }
            }
        }
        const auto insideRing = [&](int x, int z) { return inGrid(x, z) && !outside[gi(x, z)] && !ringAt.count({x, z}); };
        // 1 a lot | 2 a croft or the market | 3 a way's band | 4 a gate's opening | 5 the world road | 6 water | 7 another drum
        std::vector<uint8_t> taken(static_cast<size_t>(GW) * GD, 0);
        const auto take = [&](int x, int z, uint8_t v) { if (inGrid(x, z) && !taken[gi(x, z)]) taken[gi(x, z)] = v; };
        for (const auto& l : map.lots) for (int z = l.rect.z; z < l.rect.z1(); ++z) for (int x = l.rect.x; x < l.rect.x1(); ++x) take(x, z, 1);
        for (const auto& a : map.areas) for (int z = a.rect.z; z < a.rect.z1(); ++z) for (int x = a.rect.x; x < a.rect.x1(); ++x) take(x, z, 2);
        for (const auto& g : wall.gates) {   // the opening: the ring within half the gate's width, and the passage 3 cells each side of it
            const auto it = ringAt.find({g.x, g.z});
            if (it == ringAt.end()) continue;
            const size_t half = static_cast<size_t>(std::max(1, g.width / 2));
            for (size_t d = 0; d <= half && d < rn; ++d)
                for (const size_t i : {(it->second + d) % rn, (it->second + rn - d) % rn})
                    for (int dz = -3; dz <= 3; ++dz) for (int dx = -3; dx <= 3; ++dx) take(wall.ring[i].x + dx, wall.ring[i].z + dz, 4);
        }
        for (int z = gz0; z < gz1; ++z) for (int x = gx0; x < gx1; ++x) {
            if (onWay[gi(x, z)]) take(x, z, 3);
            if (road[gi(x, z)]) take(x, z, 5);
            if (wet[gi(x, z)]) take(x, z, 6);
        }
        static const char* const kWhat[8] = {"", "a lot", "a croft or the market", "a way", "a gate's opening", "the world road", "water", "another tower"};
        struct Side { char s; int nx, nz; };
        for (const auto& st : stops) {
            MapWall::Tower t;
            t.x = st.first; t.z = st.second; t.cornerX = st.first; t.cornerZ = st.second; t.size = size;
            if (!ringAt.count(st)) { t.skipped = "the corner did not survive on the ring"; wall.towers.push_back(t); continue; }
            const double vx = static_cast<double>(st.first) - cx, vz = static_cast<double>(st.second) - cz;
            const double len = std::max(1e-9, std::sqrt(vx * vx + vz * vz));
            std::vector<Side> sides{{'N', 0, 1}, {'E', 1, 0}, {'S', 0, -1}, {'W', -1, 0}};
            std::stable_sort(sides.begin(), sides.end(), [&](const Side& a, const Side& b) { return -(a.nx * vx + a.nz * vz) > -(b.nx * vx + b.nz * vz); });
            std::string why;
            bool refused = false;
            for (int k = 0; k <= size / 2 && !t.built && !refused; ++k) {
                MapWall::Tower c = t;
                c.x = st.first + static_cast<int>(std::lround(vx / len * k)); c.z = st.second + static_cast<int>(std::lround(vz / len * k));
                const Rect bb = c.bbox();
                std::set<std::pair<int, int>> body;
                for (const auto& f : towerFootprintCells(bb, ws.towerShape)) body.insert({bb.x + f.x, bb.z + f.y});
                if (!body.count(st)) break;   // further out the drum no longer holds the corner
                uint8_t hit = 0;
                bool offGrid = false;
                for (const auto& b : body) {
                    if (!inGrid(b.first, b.second)) { offGrid = true; break; }
                    if (taken[gi(b.first, b.second)]) { hit = taken[gi(b.first, b.second)]; break; }
                    for (int dz = -1; dz <= 1 && !hit; ++dz) for (int dx = -1; dx <= 1 && !hit; ++dx)   // the audit of M5d-3: corner to corner is touching too
                        if (inGrid(b.first + dx, b.second + dz) && taken[gi(b.first + dx, b.second + dz)] == 7) hit = 7;
                    if (hit) break;
                }
                if (offGrid || hit) {
                    if (why.empty()) why = offGrid ? std::string("its drum would leave the planning grid") : std::string("its drum would meet ") + kWhat[hit];
                    continue;
                }
                for (const Side& sd : sides) {
                    const TowerPlan tp = planTower(bb, muralTowerSpec(ws, sd.s));
                    if (!tp.ok) { why = tp.refusal; refused = true; break; }   // TowerForge refuses the footprint: no place changes that
                    const int dxc = bb.x + tp.doorCell.x, dzc = bb.z + tp.doorCell.y;
                    const int oxc = dxc + sd.nx, ozc = dzc + sd.nz;
                    if (body.count({oxc, ozc}) || !insideRing(oxc, ozc)) continue;
                    const uint8_t on = taken[gi(oxc, ozc)];
                    if (on != 0 && on != 3) continue;   // a doorway may open onto a way; onto nothing else that is taken
                    c.doorSide = sd.s; c.doorX = dxc; c.doorZ = dzc; c.outX = oxc; c.outZ = ozc; c.built = true;
                    break;
                }
                if (refused) break;
                if (!c.built) { if (why.empty()) why = "no side of its drum has a doorway that opens inside the ring"; continue; }
                t = c;
                for (const auto& b : body) taken[gi(b.first, b.second)] = 7;
            }
            if (!t.built) t.skipped = why.empty() ? std::string("no place for its drum holds the corner") : why;
            wall.towers.push_back(t);
        }
    }
    wall.present = true;
}

SettlementMap planSettlementMapAsBuilt(const SettlementMapInput& in) {
    SettlementMap map;
    map.era = in.era; map.seed = in.seed; map.originX = in.originX; map.originY = in.originY; map.originZ = in.originZ;
    map.W = in.W; map.D = in.D;
    if (!in.tier || in.W <= 0 || in.D <= 0 || !in.heightAt) { map.refusal = "no tier preset or site"; return map; }
    if (const std::string miss = missingTypology(in); !miss.empty()) { map.tier = in.tier->name; map.refusal = miss; return map; }
    const BuildabilityMap site = analyzeSite(in.W, in.D, in.maxRelief, in.heightAt, in.waterAt ? in.waterAt : std::function<bool(int, int)>(), 1, 6);
    map.tier = in.tier->name;
    if (in.tier->morphology == "cluster") { map.morphology = "cluster (as built)"; planClusterAsBuilt(in, site, map); }
    else {
        // M5b-1: the street tiers AS BUILT (the main-street scenario's axis chooser + row layout) stay as
        // the CONTROL of CityAxesFollowTerrain once the planner routes the street tiers' ways
        map.morphology = in.tier->morphology + " (as built)";
        std::map<std::pair<int, int>, int> nodeIndex;
        planStreetTiers(in, site, map, nodeIndex);
    }
    return map;
}

SettlementMap planSettlementMap(const SettlementMapInput& in) {
    const auto t0 = std::chrono::steady_clock::now();
    SettlementMap map;
    map.era = in.era; map.seed = in.seed; map.originX = in.originX; map.originY = in.originY; map.originZ = in.originZ;
    map.W = in.W; map.D = in.D;
    if (!in.tier) { map.refusal = "no tier preset"; return map; }
    map.tier = in.tier->name; map.morphology = in.tier->morphology;
    if (in.W <= 0 || in.D <= 0 || !in.heightAt) { map.refusal = "no site (size or height sampler missing)"; return map; }
    if (in.lotCap < 1) { map.refusal = "lot cap below 1"; return map; }
    if (const std::string miss = missingTypology(in); !miss.empty()) { map.refusal = miss; return map; }
    // the samplers are read ONCE per cell into a cache (L4 run 1, 2026-11-05: analyzeSite's 13 x 13 relief window
    // called the generator's column sampler 169 times per cell - 9.4 s for a 40 x 32 hamlet on Release; the
    // map is identical, the calls are 1 per cell)
    const int cw = in.W, cd = in.D;
    std::vector<int> hcache(static_cast<size_t>(cw) * cd);
    std::vector<uint8_t> wcache(static_cast<size_t>(cw) * cd, 0);
    for (int z = 0; z < cd; ++z) for (int x = 0; x < cw; ++x) {
        hcache[static_cast<size_t>(z) * cw + x] = in.heightAt(x, z);
        if (in.waterAt) wcache[static_cast<size_t>(z) * cw + x] = in.waterAt(x, z) ? 1 : 0;
    }
    SettlementMapInput cached = in;
    cached.heightAt = [&hcache, cw, cd](int x, int z) { return hcache[static_cast<size_t>(std::clamp(z, 0, cd - 1)) * cw + std::clamp(x, 0, cw - 1)]; };
    cached.waterAt = in.waterAt ? std::function<bool(int, int)>([&wcache, cw, cd](int x, int z) { return wcache[static_cast<size_t>(std::clamp(z, 0, cd - 1)) * cw + std::clamp(x, 0, cw - 1)] != 0; })
                                : std::function<bool(int, int)>();
    const SettlementMapInput& in2 = cached;
    const int window = std::max(1, 6);
    const BuildabilityMap site = analyzeSite(in2.W, in2.D, in2.maxRelief, in2.heightAt, in2.waterAt, 1, window);
    map.buildableFraction = static_cast<float>(site.buildableFraction());
    std::map<std::pair<int, int>, int> nodeIndex;
    if (in.tier->morphology == "cluster") {
        if (kClusterGrowth) planGrowth(in2, site, map, nodeIndex, false);
        else planClusterAsBuilt(in2, site, map);
    } else {
        planGrowth(in2, site, map, nodeIndex, true);   // M5b-1: the street tiers grown from the map (planStreetTiers = the as-built control)
    }
    if (static_cast<int>(map.lots.size()) > in.lotCap) {
        // the M5 stress cap: the per-lot connection's search budget is sized for 400 lots per site
        map.lots.resize(static_cast<size_t>(in.lotCap));
        map.refusal = "more lots than the cap of " + std::to_string(in.lotCap) + " (the M5 stress cap) - truncated";
    }
    // M5b-1a (sequencing amendment 1): each lot's DOOR (the forge's own autofill, seeded by the building's world
    // corner) and its STYLE (D49: the realizer's draw by lot index, pinned) are map data
    for (size_t i = 0; i < map.lots.size(); ++i) {
        MapLot& l = map.lots[i];
        const LotDoor d = predictLotDoor(in.rooms, l.typology, l.footprint, l.orientation, in.originX + l.footprint.x, in.originZ + l.footprint.z, in.era);
        if (d.found) { l.hasDoor = true; l.doorX = d.wall.x; l.doorZ = d.wall.y; l.doorNX = d.normal.x; l.doorNZ = d.normal.y; }
        if (!in.styles.empty()) l.style = pickBuildingVariant(static_cast<int>(i), std::vector<std::string>{l.typology}, in.styles, in.varietySeed).style;
    }
    // M5b-2: each lot's PARCEL, before the terraces and the ways' grading read it
    if (in.parcels)
        for (auto& l : map.lots) conquerParcel(l, in2, in.parcelSlopeWeight);
    // increment C: each lot's SEAT. The ways' paved surface is the paver's own pure plan over this planner's ground
    // (the ribbons exactly as the realizer builds them: world cubes, the rounded width; the ground the realizer's
    // surfMicro: (height + 1) * 9 per cube column), less the columns inside lot footprints (the ways unit drops
    // them); the way is read at each door with the rule the realizer applied to the paving STAMP until now (the
    // centre column of the first paved cell 1..3 out along the normal, else that cell's lowest column; else the
    // highest column of the approach cell, then of its 8 neighbours). A door that meets no paving there has no seat.
    if (!map.ways.empty() && !map.lots.empty()) {
        auto fl9 = [](int v) { return v >= 0 ? v / 9 : -((-v + 8) / 9); };
        auto key = [](int x, int z) { return (static_cast<long long>(x) << 32) ^ static_cast<long long>(static_cast<unsigned int>(z)); };
        std::vector<WayRibbon> ribbons;
        for (const auto& w : map.ways) {
            std::vector<glm::ivec2> cells;
            for (int nid : w.nodes) if (const MapNode* n = map.node(nid)) cells.push_back({in.originX + n->x, in.originZ + n->z});
            if (cells.size() >= 2) ribbons.push_back({cells, std::max(1, static_cast<int>(std::lround(w.width)))});
        }
        const int oX = in.originX, oZ = in.originZ;
        // the ground the ways are graded over (LOCAL cubes): the natural ground - the cache inside the site rect, the
        // caller's sampler beyond it - with the parcels' TERRACES applied when the realizer applies them (chain 156:
        // 9 of 33 seats on the village and the town read a way level one cube off the paving stamp - the ways unit
        // runs after the terrace units). The rule is the unit's (SettlementBuildService "terracing parcel"): the
        // parcel takes the median of its rect as it stands when its turn comes, a 4-cube skirt ramps one cube a cell
        // to the ground beyond; every target of a parcel is computed before any is applied.
        std::unordered_map<long long, int> terraced, emitted;
        const auto groundAt = [&](int x, int z) {
            const auto it = terraced.find(key(x, z));
            if (it != terraced.end()) return it->second;
            if (in.gradeHeightAt) {   // the ground AS EMITTED (chain 161: an arriving road's earthwork under a lot's approach)
                const auto et = emitted.find(key(x, z));
                if (et != emitted.end()) return et->second;
                const int h = in.gradeHeightAt(x, z);
                emitted.emplace(key(x, z), h);
                return h;
            }
            return (x >= 0 && z >= 0 && x < in.W && z < in.D) ? in2.heightAt(x, z) : in.heightAt(x, z);
        };
        if (in.terraceLots) {
            constexpr int SK = 4;
            for (auto& l : map.lots) {
                // M5b-2: THE terrace rule (planParcelTerrace - the realizer's unit calls the same function), over the
                // PARCEL; every target of a parcel is computed before any is applied
                const ParcelTerrace t = planParcelTerrace(l.parcelRect(), SK, groundAt);
                if (!t.ok) continue;
                l.hasTerrace = true; l.terraceY = t.grade;
                std::vector<std::pair<long long, int>> targets;
                for (const auto& c : t.cells) if (c.target != groundAt(c.x, c.z)) targets.push_back({key(c.x, c.z), c.target});
                for (const auto& tg : targets) terraced[tg.first] = tg.second;
            }
        }
        const std::function<int(int, int)> groundMicro = [&groundAt, fl9, oX, oZ](int mx, int mz) { return (groundAt(fl9(mx) - oX, fl9(mz) - oZ) + 1) * 9; };
        const PavingPlan pave = planWayPaving(ribbons, groundMicro, in.agent, std::string());
        std::unordered_map<long long, int> surf;
        surf.reserve(pave.columns.size());
        for (const auto& c : pave.columns) {
            const int cx = fl9(c.x) - oX, cz = fl9(c.z) - oZ;
            bool inFoot = false;
            for (const auto& l : map.lots)
                if (cx >= l.footprint.x && cx < l.footprint.x1() && cz >= l.footprint.z && cz < l.footprint.z1()) { inFoot = true; break; }
            if (!inFoot) surf.emplace(key(c.x, c.z), c.surface);
        }
        for (auto& l : map.lots) {
            if (!l.hasDoor) continue;
            const glm::ivec2 wall(oX + l.doorX, oZ + l.doorZ), n(l.doorNX, l.doorNZ);
            int wayTop = INT_MIN;
            for (int k = 1; k <= 3 && wayTop == INT_MIN; ++k) {
                const glm::ivec2 c = wall + n * k;
                int lowest = INT_MAX;
                for (int dz = 0; dz < 9; ++dz) for (int dx = 0; dx < 9; ++dx) {
                    const auto it = surf.find(key(c.x * 9 + dx, c.y * 9 + dz));
                    if (it != surf.end()) lowest = std::min(lowest, it->second);
                }
                const auto ct = surf.find(key(c.x * 9 + 4, c.y * 9 + 4));
                if (ct != surf.end()) wayTop = ct->second;
                else if (lowest != INT_MAX) wayTop = lowest;
            }
            const glm::ivec2 approach = wall + n * 2;
            for (int r = 0; r <= 1 && wayTop == INT_MIN; ++r)
                for (int dz = -r; dz <= r; ++dz) for (int dx = -r; dx <= r; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                    const int ax = approach.x + dx, az = approach.y + dz;
                    for (int mz = 0; mz < 9; ++mz) for (int mx = 0; mx < 9; ++mx) {
                        const auto it = surf.find(key(ax * 9 + mx, az * 9 + mz));
                        if (it != surf.end() && it->second > wayTop) wayTop = it->second;
                    }
                }
            if (wayTop == INT_MIN) continue;
            const auto oi = in.thresholdAbovePad.find(l.style);
            const int off = oi != in.thresholdAbovePad.end() ? oi->second : 12;
            l.hasSeat = true;
            l.wayLevel = wayTop + 1;
            l.padY = padCubeForStreet(l.wayLevel, off, 4);   // 4 = the agent's step-up, as the realizer's seat
            l.threshold = l.padY * 9 + off;
            // increment D: the approach. The realizer's doorsteps unit reads, per cell outward (<= 6), whether the cell
            // is PAVED by a way (any paving column in it) and that cell's standing level (the centre column's top + 1);
            // the unpaved cells before it take planDoorstepLevels' line from the threshold - which depends on the
            // threshold, the way's level and the step alone, not on the ground found in them (hence the INT_MAX: every
            // unpaved cell is given its level). The same function, the same inputs, from the planner's paving plan.
            {
                constexpr int kMaxSteps = 6, kStepUp = 4;
                std::vector<int> ground; std::vector<bool> paved; int kw = -1;
                for (int k = 1; k <= kMaxSteps && kw < 0; ++k) {
                    const glm::ivec2 c = wall + n * k;
                    // chain 180 (3 of 35 doors differed, each at a cell the way paves only at its EDGE): the way's level
                    // in such a cell is its PAVING's - the seat's own stamp rule above (the centre column, the lowest
                    // paved column otherwise) - not the ground at the cell's centre
                    int lowest = INT_MAX;
                    for (int dz = 0; dz < 9; ++dz) for (int dx = 0; dx < 9; ++dx) {
                        const auto it = surf.find(key(c.x * 9 + dx, c.y * 9 + dz));
                        if (it != surf.end()) lowest = std::min(lowest, it->second);
                    }
                    if (lowest == INT_MAX) { ground.push_back(INT_MAX); paved.push_back(false); continue; }
                    const auto ct = surf.find(key(c.x * 9 + 4, c.y * 9 + 4));
                    ground.push_back((ct != surf.end() ? ct->second : lowest) + 1);
                    paved.push_back(true); kw = k - 1;
                }
                if (kw >= 0) {
                    const std::vector<int> levels = planDoorstepLevels(l.threshold, ground, paved, kStepUp);
                    for (int i = 0; i <= kw; ++i) {
                        const size_t si = static_cast<size_t>(i);
                        l.approach.push_back({l.doorX + n.x * (i + 1), l.doorZ + n.y * (i + 1), levels[si] != INT_MIN ? levels[si] : ground[si]});
                    }
                    l.hasApproach = true;
                }
            }
        }
    }
    // M5c-2: the crofts, from the finished map (they take only what the lots, the ways and the water left)
    if (in.crofts) planCrofts(map, in2);
    // M5d-1: the wall, from the finished map (a tier whose program enables `walls`); the UNCACHED samplers - it may leave the site rect
    planWall(map, in);
    map.bakeMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return map;
}

WalkabilityReport mapWalkabilityStandIn(const SettlementMap& map, const AgentBox& box, int wallHeightCubes) {
    std::vector<Rect> solid;
    for (const auto& l : map.lots) solid.push_back(l.footprint);
    const int hMicro = std::max(1, wallHeightCubes) * 9;
    auto occupied = [solid, hMicro](int x, int y, int z) -> bool {
        if (y < 0) return true;   // the flat ground of the stand-in
        if (y >= hMicro) return false;
        for (const auto& r : solid)
            if (x >= r.x * 9 && x < r.x1() * 9 && z >= r.z * 9 && z < r.z1() * 9) return true;
        return false;
    };
    std::vector<WalkRoute> routes;
    glm::ivec3 goal(0);
    bool haveGoal = false;
    if (const MapNode* e = map.node(map.entryNode)) { goal = {e->x * 9 + 4, 0, e->z * 9 + 4}; haveGoal = true; }
    else if (!map.ways.empty() && !map.ways.front().nodes.empty()) {
        if (const MapNode* n = map.node(map.ways.front().nodes.back())) { goal = {n->x * 9 + 4, 0, n->z * 9 + 4}; haveGoal = true; }
    }
    WalkabilityReport rep;
    if (!haveGoal) return rep;   // nothing to walk to: no routes, no verdict (reported as 0 / 0)
    for (const auto& l : map.lots) {
        const glm::ivec2 d = lotDoorApproach(l);
        WalkRoute r;
        r.label = "lot " + std::to_string(l.id) + " " + l.typology + ": door -> entry";
        r.from = {d.x * 9 + 4, 0, d.y * 9 + 4};
        r.to = goal;
        routes.push_back(r);
    }
    const glm::ivec3 lo(-9, -9, -9), hi((map.W + 1) * 9, hMicro + 27, (map.D + 1) * 9);
    return checkRoutes(occupied, box, routes, lo, hi, false);
}

SettlementPaths pathsForMapWays(const std::vector<std::vector<glm::ivec2>>& wayCells, const std::vector<DoorAnchor>& doors,
                                const std::function<int(int, int)>& groundMicroAt, const AgentBox& box) {
    SettlementPaths out;
    auto anchor = [&](const glm::ivec2& c) { const int mx = c.x * 9 + 4, mz = c.y * 9 + 4; return glm::ivec3(mx, groundMicroAt(mx, mz), mz); };
    // the map's ways, a graded terrain path per node pair (the map's geometry as built)
    for (const auto& w : wayCells)
        for (size_t k = 0; k + 1 < w.size(); ++k) {
            ++out.edges;
            PathPlan ramp = planTerrainPath(groundMicroAt, anchor(w[k]), anchor(w[k + 1]), box);
            if (ramp.ok) { out.paths.push_back(std::move(ramp)); ++out.connected; }
            else out.failedEdges.emplace_back(static_cast<int>(k), static_cast<int>(k + 1));
        }
    // every REALIZED door to the nearest way node (the lot's door-approach is a way's first node by
    // construction; the forge may set the door a few cubes along the wall - a short spur joins them)
    for (size_t di = 0; di < doors.size(); ++di) {
        const DoorAnchor& d = doors[di];
        glm::ivec2 best(0); long bestD = LONG_MAX;
        for (const auto& w : wayCells)
            for (const auto& c : w) {
                const long dx = static_cast<long>(c.x * 9 + 4 - d.x), dz = static_cast<long>(c.y * 9 + 4 - d.z);
                const long dd = dx * dx + dz * dz;
                if (dd < bestD) { bestD = dd; best = c; }
            }
        if (bestD == LONG_MAX) continue;
        ++out.edges;
        PathPlan spur = planTerrainPath(groundMicroAt, {d.x, d.surfaceY, d.z}, anchor(best), box);
        if (spur.ok) { out.paths.push_back(std::move(spur)); ++out.connected; }
        else out.failedEdges.emplace_back(-1 - static_cast<int>(di), 0);
    }
    return out;
}

MapProjection projectMap(const SettlementMap& map) {
    MapProjection p;
    // M5b-2: the realizer's parcel consumers (clearing, terrace, fences, yard props) take the PARCEL - one rect (D51
    // as revised); a record without one, or with more than one rect, falls back to the nominal toft
    for (const auto& l : map.lots) { Plot pl; pl.rect = l.parcelRect(); p.layout.plots.push_back(pl); }
    // M5b-1: the ways are POLYLINES paved by the ribbon paver; the axis-aligned rect of a 2-node way stays for
    // the legacy consumers (the as-built control's streets), every other way that reaches the site edge adds an
    // EDGE rect of its width there (the wall's gates read layout.streets: planTownWall cuts a gate where a street
    // leaves the site), and the primary's bounding box is msl.mainStreet (the response's main_street)
    for (const auto& w : map.ways) {
        if (w.nodes.size() < 2 || w.width <= 0.0f) continue;
        const int wd = std::max(1, static_cast<int>(std::lround(w.width)));
        bool rectEmitted = false;
        if (w.nodes.size() == 2) {
            const MapNode* a = map.node(w.nodes[0]); const MapNode* b = map.node(w.nodes[1]);
            if (a && b && a->z == b->z) {   // along X
                const int x0 = std::min(a->x, b->x), x1 = std::max(a->x, b->x) + 1;
                p.layout.streets.push_back(Rect{x0, a->z - wd / 2, x1 - x0, wd}); rectEmitted = true;
            } else if (a && b && a->x == b->x) {
                const int z0 = std::min(a->z, b->z), z1 = std::max(a->z, b->z) + 1;
                p.layout.streets.push_back(Rect{a->x - wd / 2, z0, wd, z1 - z0}); rectEmitted = true;
            }
        }
        int minx = INT_MAX, minz = INT_MAX, maxx = INT_MIN, maxz = INT_MIN;
        for (int nid : w.nodes) if (const MapNode* n = map.node(nid)) { minx = std::min(minx, n->x); maxx = std::max(maxx, n->x); minz = std::min(minz, n->z); maxz = std::max(maxz, n->z); }
        if (minx > maxx) continue;
        if (w.highway == "primary" && !p.mainStreet) {
            p.msl.mainStreet = Rect{minx - wd / 2, minz - wd / 2, maxx - minx + wd, maxz - minz + wd};
            p.mainStreet = true; p.primaryWay = w.id;
            const MapNode* mid = map.node(w.nodes[w.nodes.size() / 2]);
            if (mid) {   // the well's verge: the first cell beside the band at the middle node that is inside no lot
                const int r = wd / 2;
                const glm::ivec2 tries[4] = {{mid->x, mid->z - r - 1}, {mid->x, mid->z + r + 1}, {mid->x - r - 1, mid->z}, {mid->x + r + 1, mid->z}};
                for (const auto& t : tries) {
                    if (t.x < 0 || t.y < 0 || t.x >= map.W || t.y >= map.D) continue;
                    bool inLot = false;
                    for (const auto& l : map.lots) if (t.x >= l.rect.x && t.x < l.rect.x1() && t.y >= l.rect.z && t.y < l.rect.z1()) { inLot = true; break; }
                    if (!inLot) { p.wellCell = t; p.hasWell = true; break; }
                }
            }
        }
        if (!rectEmitted) {
            const MapNode* ends[2] = {map.node(w.nodes.front()), map.node(w.nodes.back())};
            for (const MapNode* n : ends) {
                if (!n) continue;
                if (n->x <= 0)             p.layout.streets.push_back(Rect{0, n->z - wd / 2, wd + 1, wd});
                else if (n->x >= map.W - 1) p.layout.streets.push_back(Rect{map.W - wd - 1, n->z - wd / 2, wd + 1, wd});
                else if (n->z <= 0)        p.layout.streets.push_back(Rect{n->x - wd / 2, 0, wd, wd + 1});
                else if (n->z >= map.D - 1) p.layout.streets.push_back(Rect{n->x - wd / 2, map.D - wd - 1, wd, wd + 1});
            }
        }
    }
    for (const auto& a : map.areas) if (a.landuse == "market") { p.msl.marketSquare = a.rect; p.msl.hasSquare = true; }
    if (p.msl.hasSquare) {
        // M5e-1: the ways' bands INSIDE the square, as rects, for the square's dressing (a routed way emits no
        // street rect where it bends, and the dressing then saw no carriageway at all). One rect per leg - the
        // leg's two node cells grown by half the way's width, clipped to the square; collinear legs are merged
        // so a straight way stays ONE band (the pads are cut on band edges).
        const Rect sq = p.msl.marketSquare;
        std::vector<Rect> bands;
        for (const auto& w : map.ways) {
            // D67 PROPOSED: across the market a way keeps its THROUGH LANE clear (the map's market_lane), not its
            // full width - the market is the widened street, and the stalls stand in that width beside the lane
            const float bw = map.marketLane > 0 ? std::min(w.width, static_cast<float>(map.marketLane)) : w.width;
            const int r = static_cast<int>(std::floor(bw / 2.0f));
            for (size_t i = 0; i + 1 < w.nodes.size(); ++i) {
                const MapNode* a = map.node(w.nodes[i]); const MapNode* b = map.node(w.nodes[i + 1]);
                if (!a || !b) continue;
                const int x0 = std::max(std::min(a->x, b->x) - r, sq.x), x1 = std::min(std::max(a->x, b->x) + r + 1, sq.x1());
                const int z0 = std::max(std::min(a->z, b->z) - r, sq.z), z1 = std::min(std::max(a->z, b->z) + r + 1, sq.z1());
                if (x0 < x1 && z0 < z1) bands.push_back(Rect{x0, z0, x1 - x0, z1 - z0});
            }
        }
        for (bool merged = true; merged;) {
            merged = false;
            for (size_t i = 0; i < bands.size() && !merged; ++i)
                for (size_t j = i + 1; j < bands.size() && !merged; ++j) {
                    Rect& a = bands[i]; const Rect& b = bands[j];
                    const bool sameZ = a.z == b.z && a.d == b.d && a.x <= b.x1() && b.x <= a.x1();
                    const bool sameX = a.x == b.x && a.w == b.w && a.z <= b.z1() && b.z <= a.z1();
                    const bool inside = b.x >= a.x && b.x1() <= a.x1() && b.z >= a.z && b.z1() <= a.z1();
                    if (!sameZ && !sameX && !inside) continue;
                    const int x0 = std::min(a.x, b.x), z0 = std::min(a.z, b.z), x1 = std::max(a.x1(), b.x1()), z1 = std::max(a.z1(), b.z1());
                    a = Rect{x0, z0, x1 - x0, z1 - z0};
                    bands.erase(bands.begin() + static_cast<std::ptrdiff_t>(j));
                    merged = true;
                }
        }
        p.msl.squareBands = std::move(bands);
        p.msl.squareBandsGiven = true;
    }
    for (const auto& l : map.lots) {
        AssignedPlot ap; ap.plot.rect = l.parcelRect(); ap.typology = l.typology; ap.streetSide = SettlementMap::sideChar(l.orientation);
        ap.setback = l.setback; ap.footprint = l.footprint;
        p.msl.assigned.push_back(ap);
    }
    p.msl.base = p.layout;
    p.msl.ok = !map.lots.empty();
    return p;
}

}  // namespace Core
}  // namespace Phyxel
