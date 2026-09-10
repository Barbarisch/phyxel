#include "core/NavGraph.h"
#include "core/ChunkManager.h"
#include <algorithm>
#include <cmath>
#include <climits>
#include <queue>
#include <unordered_set>
#include <mutex>

namespace Phyxel {
namespace Core {

namespace {
inline int divFloor(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

// Footprint sample columns: centre + 4 corners (the TraversalProbe rectangle).
inline void footprint(int fx, int fz, int hw, int xs[5], int zs[5]) {
    xs[0] = fx;      zs[0] = fz;
    xs[1] = fx - hw; zs[1] = fz - hw;
    xs[2] = fx + hw; zs[2] = fz - hw;
    xs[3] = fx - hw; zs[3] = fz + hw;
    xs[4] = fx + hw; zs[4] = fz + hw;
}

// 4-direction table shared by neighbors(), linkColumn() and the clear-walk test.
constexpr int kDX[4] = { 1, -1, 0,  0 };
constexpr int kDZ[4] = { 0,  0, 1, -1 };
inline int dirIndex(int dx, int dz) {
    for (int d = 0; d < 4; ++d) if (kDX[d] == dx && kDZ[d] == dz) return d;
    return -1;
}
} // namespace

NavGraph::NavGraph(ChunkManager* chunkManager) : m_chunkManager(chunkManager) {
    // The live world is sub-cube: classify by cube type, sample Partial cubes at 1/9 m.
    if (m_chunkManager) {
        m_fillFunc = [cm = m_chunkManager](const glm::ivec3& cube) -> CellFill {
            switch (cm->getVoxelTypeAt(cube)) {
                case VoxelLocation::EMPTY: return CellFill::Empty;
                case VoxelLocation::CUBE:  return CellFill::Solid;
                default:                   return CellFill::Partial;
            }
        };
        m_microFunc = [cm = m_chunkManager](const glm::ivec3& micro) { return cm->occupiedMicro(micro); };
    }
}
NavGraph::NavGraph(VoxelQueryFunc queryFunc) : m_queryFunc(std::move(queryFunc)) {}
NavGraph::NavGraph(CellFillFunc fill, MicroQueryFunc micro)
    : m_fillFunc(std::move(fill)), m_microFunc(std::move(micro)) {}

int64_t NavGraph::packKey(int x, int z) {
    return (static_cast<int64_t>(static_cast<uint32_t>(x)) << 32) |
            static_cast<int64_t>(static_cast<uint32_t>(z));
}

int64_t NavGraph::packCell(const glm::ivec3& c) {
    return (static_cast<int64_t>(static_cast<uint16_t>(c.y)) << 48) |
           (static_cast<int64_t>(static_cast<uint32_t>(c.x) & 0xFFFFFF) << 24) |
            static_cast<int64_t>(static_cast<uint32_t>(c.z) & 0xFFFFFF);
}

bool NavGraph::hasVoxel(const glm::ivec3& p) const {
    if (m_queryFunc) return m_queryFunc(p);
    if (m_chunkManager) return m_chunkManager->hasVoxelAt(p);
    return false;
}

// ---------------------------------------------------------------------------
// CUBE mode: scan a column bottom-to-top, recording every solid voxel whose top has
// enough empty headroom for the agent to stand. Multi-level by construction.
// ---------------------------------------------------------------------------
std::vector<NavSurface> NavGraph::buildColumnCube(int x, int z, const NavAgentProfile& agent) const {
    std::vector<NavSurface> surfaces;
    const int need = std::max(1, agent.height);
    for (int y = MIN_SCAN_Y; y <= MAX_SCAN_Y; ++y) {
        if (!hasVoxel({x, y, z})) continue;            // need a solid voxel to stand on
        if (hasVoxel({x, y + 1, z})) continue;         // its top must be exposed
        int hr = 0;
        for (int h = 1; h <= MAX_HEADROOM && !hasVoxel({x, y + h, z}); ++h) ++hr;
        if (hr >= need) {
            NavSurface s;
            s.x = x; s.z = z; s.floorY = y;
            s.headroom = static_cast<uint16_t>(std::min(hr, MAX_HEADROOM));
            s.floorTopMicro = (y + 1) * MICRO;
            s.headroomMicro = static_cast<uint16_t>(s.headroom * MICRO);
            surfaces.push_back(s);
        }
    }
    return surfaces;
}

// ---------------------------------------------------------------------------
// MICRO mode
// ---------------------------------------------------------------------------
CellFill NavGraph::fillAt(const glm::ivec3& cube, FillCache& cache) const {
    const int64_t k = packCell(cube);
    auto it = cache.cells.find(k);
    if (it != cache.cells.end()) return it->second;
    const CellFill f = m_fillFunc ? m_fillFunc(cube) : CellFill::Empty;
    cache.cells.emplace(k, f);
    return f;
}

bool NavGraph::microSolid(const glm::ivec3& micro, FillCache& cache) const {
    const glm::ivec3 cube(divFloor(micro.x, MICRO), divFloor(micro.y, MICRO), divFloor(micro.z, MICRO));
    switch (fillAt(cube, cache)) {
        case CellFill::Empty: return false;
        case CellFill::Solid: return true;
        default:              return m_microFunc(micro);
    }
}

bool NavGraph::boxFits(int fx, int fy, int fz, int hw, int heightMicro, FillCache& cache) const {
    int xs[5], zs[5];
    footprint(fx, fz, hw, xs, zs);
    const int yTop = fy + heightMicro;                 // exclusive
    for (int i = 0; i < 5; ++i) {
        const int cx = divFloor(xs[i], MICRO), cz = divFloor(zs[i], MICRO);
        // Walk the span cube by cube so Empty/Solid cubes cost one lookup each.
        for (int cy = divFloor(fy, MICRO); cy * MICRO < yTop; ++cy) {
            const CellFill f = fillAt({cx, cy, cz}, cache);
            if (f == CellFill::Empty) continue;
            if (f == CellFill::Solid) return false;
            const int y0 = std::max(fy, cy * MICRO), y1 = std::min(yTop, (cy + 1) * MICRO);
            for (int y = y0; y < y1; ++y)
                if (m_microFunc({xs[i], y, zs[i]})) return false;
        }
    }
    return true;
}

bool NavGraph::boxSupported(int fx, int fy, int fz, int hw, FillCache& cache) const {
    int xs[5], zs[5];
    footprint(fx, fz, hw, xs, zs);
    for (int i = 0; i < 5; ++i)
        if (microSolid({xs[i], fy - 1, zs[i]}, cache)) return true;
    return false;
}

std::vector<NavSurface> NavGraph::buildColumnMicro(int x, int z, const NavAgentProfile& agent,
                                                   FillCache& cache) const {
    std::vector<NavSurface> surfaces;
    const int hw = std::clamp(agent.halfWidthMicro, 0, 4);   // footprint stays inside its own cell
    const int need = std::max(1, agent.heightMicro);
    const int fx = x * MICRO + 4, fz = z * MICRO + 4;         // cell centre, micro
    const int maxHeadroomMicro = MAX_HEADROOM * MICRO;

    // A layer is "solid" for standing/blocking if any footprint sample is solid there.
    auto layerSolid = [&](int my) { return boxSupported(fx, my + 1, fz, hw, cache); };

    for (int cy = MIN_SCAN_Y; cy <= MAX_SCAN_Y; ++cy) {
        const CellFill f = fillAt({x, cy, z}, cache);
        if (f == CellFill::Empty) continue;                     // nothing to stand on in this cube
        const int base = cy * MICRO;
        // Candidate floor tops: a solid layer with a free layer above it.
        const int firstLayer = (f == CellFill::Solid) ? base + MICRO - 1 : base;
        for (int my = firstLayer; my < base + MICRO; ++my) {
            if (!layerSolid(my) || layerSolid(my + 1)) continue;
            const int feet = my + 1;
            // Headroom above the feet, cube-accelerated.
            int hr = 0;
            bool open = true;
            for (int hy = divFloor(feet, MICRO); open && hr < maxHeadroomMicro; ++hy) {
                const CellFill fa = fillAt({x, hy, z}, cache);
                const int y0 = std::max(feet, hy * MICRO), y1 = (hy + 1) * MICRO;
                if (fa == CellFill::Empty) { hr += y1 - y0; continue; }
                for (int y = y0; y < y1; ++y) {
                    if (layerSolid(y)) { open = false; break; }
                    ++hr;
                }
            }
            hr = std::min(hr, maxHeadroomMicro);
            if (hr < need) continue;
            NavSurface s;
            s.x = x; s.z = z;
            s.floorY = divFloor(my, MICRO);
            s.floorTopMicro = feet;
            s.headroomMicro = static_cast<uint16_t>(hr);
            s.headroom = static_cast<uint16_t>(std::min(hr / MICRO, MAX_HEADROOM));
            surfaces.push_back(s);
        }
    }
    return surfaces;
}

bool NavGraph::sweepEdge(const NavSurface& from, const NavSurface& to, const NavAgentProfile& agent,
                         FillCache& cache, int lateral) const {
    const int fa = from.floorTopMicro, fb = to.floorTopMicro;
    const int dy = fb - fa;
    if (dy > agent.stepUpMicro)   return false;       // too tall a step up
    if (-dy > agent.maxFallMicro()) return false;     // drop too far down
    const int hw = std::clamp(agent.halfWidthMicro, 0, 4);
    const int H  = std::max(1, agent.heightMicro);
    const int dx = to.x - from.x, dz = to.z - from.z;
    // Lateral shift is perpendicular to the travel axis.
    const int sx = from.x * MICRO + 4 + (dx == 0 ? lateral : 0);
    const int sz = from.z * MICRO + 4 + (dz == 0 ? lateral : 0);
    const bool isStep = std::abs(dy) <= agent.stepUpMicro;
    for (int k = 1; k <= MICRO; ++k) {
        const int px = sx + dx * k, pz = sz + dz * k;
        const bool inFrom = divFloor(px, MICRO) == from.x && divFloor(pz, MICRO) == from.z;
        bool ok;
        if (isStep) {
            // A STEP (<= the auto step-up): the controller lifts the body the moment it
            // meets the riser, so around the boundary the footprint may straddle the riser
            // at EITHER level — the box must fit at one of them (TraversalProbe's step
            // rule). Testing only the "current cell's" level rejected every generated
            // door: the interior slab is flush with the outer wall face, and the trailing
            // corners of a box at the low level clip the slab edge (Ravenmere L4).
            ok = boxFits(px, fa, pz, hw, H, cache) || boxFits(px, fb, pz, hw, H, cache);
        } else {
            // A FALL: walk at the high level until unsupported, then the body sweeps the
            // whole span down to the landing — the `to` side must be clear from `fb` up
            // to `fa + H`.
            ok = inFrom ? boxFits(px, fa, pz, hw, H, cache)
                        : boxFits(px, fb, pz, hw, H + (fa - fb), cache);
        }
        if (!ok) return false;
    }
    return true;
}

void NavGraph::linkColumn(int x, int z, const NavAgentProfile& agent, FillCache& cache) {
    auto it = m_columns.find(packKey(x, z));
    if (it == m_columns.end()) return;
    for (NavSurface& s : it->second) {
        for (int d = 0; d < 4; ++d) {
            s.edge[d] = -1;
            s.slack[d] = -1;
            const auto& col = columnSurfaces(x + kDX[d], z + kDZ[d]);
            int bestAbs = INT_MAX;
            for (int lvl = 0; lvl < static_cast<int>(col.size()) && lvl < 127; ++lvl) {
                const int dy = col[lvl].floorTopMicro - s.floorTopMicro;
                if (dy > agent.stepUpMicro || -dy > agent.maxFallMicro()) continue;
                if (std::abs(dy) >= bestAbs) continue;
                if (!sweepEdge(s, col[lvl], agent, cache)) continue;
                bestAbs = std::abs(dy);
                s.edge[d] = static_cast<int8_t>(lvl);
                // Lateral slack: widen symmetrically until a shifted sweep hits something.
                int slack = 0;
                while (slack < 4 && sweepEdge(s, col[lvl], agent, cache, slack + 1)
                                 && sweepEdge(s, col[lvl], agent, cache, -(slack + 1))) ++slack;
                s.slack[d] = static_cast<int8_t>(slack);
            }
        }
    }
}

void NavGraph::buildRegion(const glm::ivec2& minXZ, const glm::ivec2& maxXZ, const NavAgentProfile& agent) {
    std::unique_lock lock(m_mutex);
    m_columns.clear();
    m_minBounds = glm::min(minXZ, maxXZ);
    m_maxBounds = glm::max(minXZ, maxXZ);
    FillCache cache;
    for (int x = m_minBounds.x; x <= m_maxBounds.x; ++x) {
        for (int z = m_minBounds.y; z <= m_maxBounds.y; ++z) {
            auto col = microMode() ? buildColumnMicro(x, z, agent, cache) : buildColumnCube(x, z, agent);
            if (!col.empty()) m_columns.emplace(packKey(x, z), std::move(col));
        }
    }
    if (microMode()) {
        for (auto& [key, col] : m_columns) {
            if (col.empty()) continue;
            linkColumn(col.front().x, col.front().z, agent, cache);
        }
    }
}

void NavGraph::rebuildColumn(int x, int z, const NavAgentProfile& agent) {
    std::unique_lock lock(m_mutex);
    FillCache cache;
    auto col = microMode() ? buildColumnMicro(x, z, agent, cache) : buildColumnCube(x, z, agent);
    int64_t key = packKey(x, z);
    if (col.empty()) m_columns.erase(key);
    else             m_columns[key] = std::move(col);
    if (microMode()) {
        // The column's own edges, and the neighbours' edges that point at it.
        linkColumn(x, z, agent, cache);
        for (int d = 0; d < 4; ++d) linkColumn(x + kDX[d], z + kDZ[d], agent, cache);
    }
}

const std::vector<NavSurface>& NavGraph::columnSurfaces(int x, int z) const {
    static const std::vector<NavSurface> kEmpty;
    auto it = m_columns.find(packKey(x, z));
    return it != m_columns.end() ? it->second : kEmpty;
}

const NavSurface* NavGraph::surface(const NavNodeId& id) const {
    if (!id.valid()) return nullptr;
    const auto& col = columnSurfaces(id.x, id.z);
    if (id.level < 0 || id.level >= static_cast<int>(col.size())) return nullptr;
    return &col[id.level];
}

std::vector<NavNodeId> NavGraph::neighbors(const NavNodeId& id, const NavAgentProfile& agent) const {
    std::vector<NavNodeId> out;
    const NavSurface* s = surface(id);
    if (!s) return out;

    if (microMode()) {
        // Edges were swept at build time against the agent's footprint (see linkColumn).
        for (int d = 0; d < 4; ++d)
            if (s->edge[d] >= 0) out.push_back(NavNodeId{s->x + kDX[d], s->z + kDZ[d], s->edge[d]});
        return out;
    }

    // CUBE mode: 4-connected (no diagonal corner-cutting). Diagonals + jump/climb/fall
    // edges are a later slice.
    for (int d = 0; d < 4; ++d) {
        const int nx = s->x + kDX[d];
        const int nz = s->z + kDZ[d];
        const auto& col = columnSurfaces(nx, nz);
        for (int lvl = 0; lvl < static_cast<int>(col.size()); ++lvl) {
            const int dyUp = col[lvl].floorY - s->floorY;   // + up, - down
            if (dyUp >  agent.stepHeight) continue;          // too tall a step up
            if (-dyUp > agent.maxFallY)   continue;          // drop too far down
            out.push_back(NavNodeId{nx, nz, lvl});
        }
    }
    return out;
}

NavNodeId NavGraph::surfaceAt(const glm::vec3& worldPos) const {
    const int x = static_cast<int>(std::floor(worldPos.x));
    const int z = static_cast<int>(std::floor(worldPos.z));
    const auto& col = columnSurfaces(x, z);
    int bestLevel = -1;
    if (microMode()) {
        // Highest feet level at or within one cube above the agent's feet.
        const int feetMicro = static_cast<int>(std::floor(worldPos.y * MICRO));
        int bestTop = INT_MIN;
        for (int lvl = 0; lvl < static_cast<int>(col.size()); ++lvl) {
            const int top = col[lvl].floorTopMicro;
            if (top <= feetMicro + MICRO && top > bestTop) { bestTop = top; bestLevel = lvl; }
        }
    } else {
        const int feetY = static_cast<int>(std::floor(worldPos.y));
        int bestFloor = INT_MIN;
        for (int lvl = 0; lvl < static_cast<int>(col.size()); ++lvl) {
            const int floorY = col[lvl].floorY;
            if (floorY <= feetY + 1 && floorY > bestFloor) {   // highest floor at/just below the feet
                bestFloor = floorY;
                bestLevel = lvl;
            }
        }
    }
    return bestLevel >= 0 ? NavNodeId{x, z, bestLevel} : NavNodeId{};
}

namespace {
// Hash for using NavNodeId as an unordered_map/set key in A*.
struct NodeHash {
    size_t operator()(const NavNodeId& n) const noexcept {
        uint64_t k = (static_cast<uint64_t>(static_cast<uint32_t>(n.x)) << 32) ^
                      static_cast<uint64_t>(static_cast<uint32_t>(n.z));
        return std::hash<uint64_t>{}(k * 1000003ull + static_cast<uint32_t>(n.level));
    }
};
} // namespace

NavGraph::PathResult NavGraph::findPathCore(const NavNodeId& start, const NavNodeId& goal,
                                            const NavAgentProfile& agent) const {
    PathResult result;
    const NavSurface* sStart = surface(start);
    const NavSurface* sGoal  = surface(goal);
    if (!sStart || !sGoal) return result;

    auto worldOf = [](const NavSurface* s) {
        return glm::vec3(s->x + 0.5f, static_cast<float>(s->floorTopMicro) / static_cast<float>(MICRO),
                         s->z + 0.5f);
    };
    auto cubesUp = [](const NavSurface* a, const NavSurface* b) {
        return std::abs(a->floorTopMicro - b->floorTopMicro) / static_cast<float>(MICRO);
    };
    if (start == goal) {
        result.found = true;
        result.nodes = {start};
        result.waypoints = {worldOf(sStart)};
        return result;
    }

    // Admissible heuristic: XZ Manhattan (min 1.0/step) + a small vertical term.
    auto heuristic = [&](const NavSurface* s) {
        return static_cast<float>(std::abs(s->x - sGoal->x) + std::abs(s->z - sGoal->z))
             + cubesUp(s, sGoal) * 0.4f;
    };

    struct OpenNode { float f; NavNodeId id; };
    struct Cmp { bool operator()(const OpenNode& a, const OpenNode& b) const { return a.f > b.f; } };
    std::priority_queue<OpenNode, std::vector<OpenNode>, Cmp> open;
    std::unordered_map<NavNodeId, float, NodeHash>    gScore;
    std::unordered_map<NavNodeId, NavNodeId, NodeHash> cameFrom;
    std::unordered_set<NavNodeId, NodeHash>            closed;

    gScore[start] = 0.0f;
    open.push({heuristic(sStart), start});

    while (!open.empty()) {
        OpenNode cur = open.top();
        open.pop();
        if (closed.count(cur.id)) continue;   // lazy deletion of stale entries
        closed.insert(cur.id);

        if (cur.id == goal) {
            result.found = true;
            std::vector<NavNodeId> rev;
            for (NavNodeId n = goal; !(n == start); n = cameFrom[n]) rev.push_back(n);
            rev.push_back(start);
            std::reverse(rev.begin(), rev.end());
            result.nodes = std::move(rev);
            for (const auto& id : result.nodes) result.waypoints.push_back(worldOf(surface(id)));
            return result;
        }

        ++result.nodesExpanded;
        const NavSurface* sCur = surface(cur.id);
        const float curG = gScore[cur.id];
        for (const NavNodeId& nb : neighbors(cur.id, agent)) {
            if (closed.count(nb)) continue;
            const NavSurface* sNb = surface(nb);
            const float stepCost = 1.0f + cubesUp(sNb, sCur) * 0.4f;   // climbing/falling costs extra
            const float tentative = curG + stepCost;
            auto it = gScore.find(nb);
            if (it == gScore.end() || tentative < it->second) {
                gScore[nb]   = tentative;
                cameFrom[nb] = cur.id;
                open.push({tentative + heuristic(sNb), nb});
            }
        }
    }
    return result;  // no path
}

NavGraph::PathResult NavGraph::findPath(const NavNodeId& start, const NavNodeId& goal,
                                        const NavAgentProfile& agent) const {
    std::shared_lock lock(m_mutex);
    return findPathCore(start, goal, agent);
}

NavGraph::PathResult NavGraph::findPath(const glm::vec3& from, const glm::vec3& to,
                                        const NavAgentProfile& agent) const {
    std::shared_lock lock(m_mutex);
    // surfaceAt()/findPathCore() are non-locking; safe under the shared lock held here.
    return findPathCore(surfaceAt(from), surfaceAt(to), agent);
}

bool NavGraph::hasClearWalkCore(const glm::vec3& a, const glm::vec3& b,
                                const NavAgentProfile& agent) const {
    const float dx = b.x - a.x, dz = b.z - a.z;
    const float dist = std::sqrt(dx * dx + dz * dz);
    // Sample finely enough that we can't tunnel through a 1-wide wall (<= ~0.25 cell).
    const int steps = std::max(1, static_cast<int>(std::ceil(dist / 0.25f)));

    if (microMode()) {
        // Follow the sampled line cell by cell. A straight line crosses cells OFF-CENTRE,
        // while edges were swept centre-to-centre, so a shortcut is only allowed where the
        // build recorded full lateral slack: every crossing must be a slack-4 edge and every
        // intermediate cell fully open. Tight crossings (door reveals) therefore keep their
        // cell-centre waypoints and the mover threads them exactly as the sweep proved.
        const NavNodeId start = surfaceAt(a);
        const NavNodeId end   = surfaceAt(b);
        if (!start.valid() || !end.valid()) return false;
        NavNodeId cur = start;
        int entryDir = -1;   // direction of travel when `cur` was entered (-1: the start cell)

        // The line's lateral offset (micro) from the face centre where it leaves `from`
        // in direction d. Crossings are proven by sweeps at every offset in [-slack, slack].
        auto lateralAt = [&](const NavNodeId& from, int d) -> int {
            if (kDX[d] != 0) {
                const float bx = static_cast<float>(from.x + (kDX[d] > 0 ? 1 : 0));
                const float t  = (bx - a.x) / dx;
                const float zc = a.z + dz * t;
                return static_cast<int>(std::lround((zc - (from.z + 0.5f)) * MICRO));
            }
            const float bz = static_cast<float>(from.z + (kDZ[d] > 0 ? 1 : 0));
            const float t  = (bz - a.z) / dz;
            const float xc = a.x + dx * t;
            return static_cast<int>(std::lround((xc - (from.x + 0.5f)) * MICRO));
        };
        // Cross from `from` into (nx,nz): the edge must exist and the line must pass
        // within the edge's lateral slack (`requireFull`: the whole face, for corner routes).
        auto step = [&](NavNodeId from, int nx, int nz, bool requireFull, NavNodeId& out) -> bool {
            const NavSurface* s = surface(from);
            if (!s) return false;
            const int d = dirIndex(nx - from.x, nz - from.z);
            if (d < 0 || s->edge[d] < 0) return false;
            if (requireFull ? s->slack[d] < 4 : std::abs(lateralAt(from, d)) > s->slack[d]) return false;
            out = NavNodeId{nx, nz, s->edge[d]};
            return true;
        };
        for (int i = 1; i <= steps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            const int cx = static_cast<int>(std::floor(a.x + dx * t));
            const int cz = static_cast<int>(std::floor(a.z + dz * t));
            if (cx == cur.x && cz == cur.z) continue;
            const int ddx = cx - cur.x, ddz = cz - cur.z;
            if (std::abs(ddx) > 1 || std::abs(ddz) > 1) return false;   // sampling skipped a cell
            NavNodeId next;
            if (ddx != 0 && ddz != 0) {
                // Corner crossing: both orthogonal routes must be fully open (no corner-cutting),
                // and the cell being left must be open unless it is the start.
                const NavSurface* sc = surface(cur);
                if (!sc || (!(cur == start) && !sc->openCell())) return false;
                NavNodeId midA, midB, viaA, viaB;
                const bool okA = step(cur, cx, cur.z, true, midA) && step(midA, cx, cz, true, viaA);
                const bool okB = step(cur, cur.x, cz, true, midB) && step(midB, cx, cz, true, viaB);
                if (!okA || !okB) return false;
                next = viaA;
                entryDir = -2;   // entered via a corner: treat as a bend
            } else {
                const int d = dirIndex(ddx, ddz);
                // Leaving an intermediate cell through a face other than the one opposite
                // its entry (the line bends inside the cell): the cell must be fully open.
                // Straight through (entryDir == d) is covered by the two crossings' sweeps.
                if (!(cur == start) && entryDir != d) {
                    const NavSurface* sc = surface(cur);
                    if (!sc || !sc->openCell()) return false;
                }
                if (!step(cur, cx, cz, false, next)) return false;
                entryDir = d;
            }
            cur = next;
        }
        return cur == end;
    }

    // CUBE mode. Both endpoints must stand on the same level for a straight shortcut.
    // floorY is one below the standing point (waypoints sit at floorY+1).
    const int floorA = static_cast<int>(std::floor(a.y)) - 1;
    const int floorB = static_cast<int>(std::floor(b.y)) - 1;
    if (std::abs(floorA - floorB) > agent.stepHeight) return false;
    const int need = std::max(1, agent.height);
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const glm::vec3 p(a.x + dx * t, 0.0f, a.z + dz * t);
        const int cx = static_cast<int>(std::floor(p.x));
        const int cz = static_cast<int>(std::floor(p.z));
        const auto& col = columnSurfaces(cx, cz);
        const int wantFloor = static_cast<int>(std::round(floorA + (floorB - floorA) * t));
        bool ok = false;
        for (const NavSurface& s : col) {
            if (std::abs(s.floorY - wantFloor) <= agent.stepHeight && s.headroom >= need) {
                ok = true;
                break;
            }
        }
        if (!ok) return false;   // wall / gap / wrong level in the way
    }
    return true;
}

bool NavGraph::hasClearWalk(const glm::vec3& a, const glm::vec3& b,
                            const NavAgentProfile& agent) const {
    std::shared_lock lock(m_mutex);
    return hasClearWalkCore(a, b, agent);
}

std::vector<glm::vec3> NavGraph::smoothWaypoints(const std::vector<glm::vec3>& raw,
                                                 const NavAgentProfile& agent) const {
    if (raw.size() <= 2) return raw;
    std::shared_lock lock(m_mutex);

    std::vector<glm::vec3> out;
    out.push_back(raw.front());
    size_t anchor = 0;
    // Greedily extend the segment from `anchor` as far as the line stays walkable; when it
    // would break, commit the previous point and restart the segment there.
    for (size_t i = 2; i < raw.size(); ++i) {
        if (!hasClearWalkCore(raw[anchor], raw[i], agent)) {
            out.push_back(raw[i - 1]);
            anchor = i - 1;
        }
    }
    out.push_back(raw.back());
    return out;
}

float NavGraph::arrivalRadiusAt(const glm::vec3& worldPos) const {
    std::shared_lock lock(m_mutex);
    if (!microMode()) return kArriveLoose;
    const NavSurface* s = surface(surfaceAt(worldPos));
    if (!s) return kArriveLoose;
    return s->tightCell() ? kArriveTight : kArriveLoose;
}

std::vector<float> NavGraph::arrivalRadii(const std::vector<glm::vec3>& waypoints) const {
    std::vector<float> out;
    out.reserve(waypoints.size());
    for (const auto& w : waypoints) out.push_back(arrivalRadiusAt(w));
    return out;
}

size_t NavGraph::surfaceCount() const {
    std::shared_lock lock(m_mutex);
    size_t n = 0;
    for (const auto& [key, col] : m_columns) n += col.size();
    return n;
}

} // namespace Core
} // namespace Phyxel
