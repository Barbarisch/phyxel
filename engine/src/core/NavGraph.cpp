#include "core/NavGraph.h"
#include "core/ChunkManager.h"
#include <algorithm>
#include <cmath>
#include <climits>

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

size_t NavGraph::surfaceCount() const {
    size_t n = 0;
    for (const auto& [key, col] : m_columns) n += col.size();
    return n;
}

} // namespace Core
} // namespace Phyxel
