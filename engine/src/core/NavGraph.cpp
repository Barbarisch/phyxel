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

NavGraph::NavGraph(ChunkManager* chunkManager) : m_chunkManager(chunkManager) {}
NavGraph::NavGraph(VoxelQueryFunc queryFunc) : m_queryFunc(std::move(queryFunc)) {}

int64_t NavGraph::packKey(int x, int z) {
    return (static_cast<int64_t>(static_cast<uint32_t>(x)) << 32) |
            static_cast<int64_t>(static_cast<uint32_t>(z));
}

bool NavGraph::hasVoxel(const glm::ivec3& p) const {
    if (m_queryFunc) return m_queryFunc(p);
    if (m_chunkManager) return m_chunkManager->hasVoxelAt(p);
    return false;
}

// Scan a column bottom-to-top, recording every solid voxel whose top has enough
// empty headroom for the agent to stand. Multi-level by construction.
std::vector<NavSurface> NavGraph::buildColumn(int x, int z, const NavAgentProfile& agent) const {
    std::vector<NavSurface> surfaces;
    const int need = std::max(1, agent.height);
    for (int y = MIN_SCAN_Y; y <= MAX_SCAN_Y; ++y) {
        if (!hasVoxel({x, y, z})) continue;            // need a solid voxel to stand on
        if (hasVoxel({x, y + 1, z})) continue;         // its top must be exposed
        int hr = 0;
        for (int h = 1; h <= MAX_HEADROOM && !hasVoxel({x, y + h, z}); ++h) ++hr;
        if (hr >= need) {
            surfaces.push_back(NavSurface{x, z, y, static_cast<uint16_t>(std::min(hr, MAX_HEADROOM)), false});
        }
    }
    return surfaces;
}

void NavGraph::buildRegion(const glm::ivec2& minXZ, const glm::ivec2& maxXZ, const NavAgentProfile& agent) {
    std::unique_lock lock(m_mutex);
    m_columns.clear();
    m_minBounds = glm::min(minXZ, maxXZ);
    m_maxBounds = glm::max(minXZ, maxXZ);
    for (int x = m_minBounds.x; x <= m_maxBounds.x; ++x) {
        for (int z = m_minBounds.y; z <= m_maxBounds.y; ++z) {
            auto col = buildColumn(x, z, agent);
            if (!col.empty()) m_columns.emplace(packKey(x, z), std::move(col));
        }
    }
}

void NavGraph::rebuildColumn(int x, int z, const NavAgentProfile& agent) {
    std::unique_lock lock(m_mutex);
    auto col = buildColumn(x, z, agent);
    int64_t key = packKey(x, z);
    if (col.empty()) m_columns.erase(key);
    else             m_columns[key] = std::move(col);
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

    // 4-connected for now (no diagonal corner-cutting). Diagonals + jump/climb/fall
    // edges are a later slice.
    static const int dx[4] = { 1, -1, 0,  0 };
    static const int dz[4] = { 0,  0, 1, -1 };
    for (int d = 0; d < 4; ++d) {
        const int nx = s->x + dx[d];
        const int nz = s->z + dz[d];
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
    const int feetY = static_cast<int>(std::floor(worldPos.y));
    int bestLevel = -1, bestFloor = INT_MIN;
    for (int lvl = 0; lvl < static_cast<int>(col.size()); ++lvl) {
        const int floorY = col[lvl].floorY;
        if (floorY <= feetY + 1 && floorY > bestFloor) {   // highest floor at/just below the feet
            bestFloor = floorY;
            bestLevel = lvl;
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
        return glm::vec3(s->x + 0.5f, s->floorY + 1.0f, s->z + 0.5f);
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
             + std::abs(s->floorY - sGoal->floorY) * 0.4f;
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
            const int dy = sNb->floorY - sCur->floorY;
            const float stepCost = 1.0f + std::abs(dy) * 0.4f;   // climbing/falling costs extra
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
    // Both endpoints must stand on the same level for a straight shortcut. floorY is one
    // below the standing point (waypoints sit at floorY+1).
    const int floorA = static_cast<int>(std::floor(a.y)) - 1;
    const int floorB = static_cast<int>(std::floor(b.y)) - 1;
    if (std::abs(floorA - floorB) > agent.stepHeight) return false;

    const float dx = b.x - a.x, dz = b.z - a.z;
    const float dist = std::sqrt(dx * dx + dz * dz);
    const int need = std::max(1, agent.height);
    // Sample finely enough that we can't tunnel through a 1-wide wall (<= ~0.25 cell).
    const int steps = std::max(1, static_cast<int>(std::ceil(dist / 0.25f)));
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

size_t NavGraph::surfaceCount() const {
    std::shared_lock lock(m_mutex);
    size_t n = 0;
    for (const auto& [key, col] : m_columns) n += col.size();
    return n;
}

} // namespace Core
} // namespace Phyxel
