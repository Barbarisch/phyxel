#include "core/NavGrid.h"
#include "core/ChunkManager.h"
#include "utils/Logger.h"
#include <cmath>
#include <algorithm>

namespace Phyxel {
namespace Core {

NavGrid::NavGrid(ChunkManager* chunkManager)
    : m_chunkManager(chunkManager)
{
}

NavGrid::NavGrid(VoxelQueryFunc queryFunc)
    : m_queryFunc(std::move(queryFunc))
{
}

void NavGrid::buildFromRegion(const glm::ivec2& minXZ, const glm::ivec2& maxXZ) {
    m_cells.clear();
    m_minBounds = minXZ;
    m_maxBounds = maxXZ;

    for (int x = minXZ.x; x <= maxXZ.x; ++x) {
        for (int z = minXZ.y; z <= maxXZ.y; ++z) {
            NavCell cell = buildCell(x, z);
            m_cells[packKey(x, z)] = cell;
        }
    }

    computeNearWallFlags();
    buildJumpLinks();
}

void NavGrid::rebuildCell(int x, int z) {
    int64_t key = packKey(x, z);
    auto it = m_cells.find(key);
    if (it != m_cells.end()) {
        it->second = buildCell(x, z);
    }

    // Recompute nearWall for this cell and its 8 neighbors
    static const int dx[] = { 0,  1, 1, 1, 0, -1, -1, -1, 0 };
    static const int dz[] = { 1,  1, 0, -1, -1, -1,  0,  1, 0 };
    for (int i = 0; i < 9; ++i) {
        int nx = x + dx[i];
        int nz = z + dz[i];
        auto cit = m_cells.find(packKey(nx, nz));
        if (cit == m_cells.end() || !cit->second.walkable) continue;

        // Check if any of the 8 neighbors is an obstacle or height barrier
        bool near = false;
        static const int odx[] = { 0, 1, 1, 1, 0, -1, -1, -1 };
        static const int odz[] = { 1, 1, 0, -1, -1, -1, 0, 1 };
        for (int j = 0; j < 8 && !near; ++j) {
            auto nit = m_cells.find(packKey(nx + odx[j], nz + odz[j]));
            if (nit == m_cells.end()) continue;
            if (!nit->second.walkable) {
                near = true;
            } else {
                // Higher neighbor creating physical wall (3+ blocks above)
                int heightDiff = nit->second.surfaceY - cit->second.surfaceY;
                if (heightDiff > MAX_STEP_DOWN) {
                    near = true;
                }
            }
        }
        cit->second.nearWall = near;
    }

    // Phase 3 — Dynamic gap support: remove any links touching the changed cell
    // (their gap may be closed or the endpoint may no longer be walkable), then
    // rebuild links for the local neighbourhood so new gaps are discovered.
    removeLinksAt(x, z);
    buildJumpLinksNear(x, z, /*radius=*/3);
}

void NavGrid::rebuildRegion(int minX, int minZ, int maxX, int maxZ) {
    // Rebuild all cells in the region (only those already in the grid)
    for (int x = minX; x <= maxX; ++x) {
        for (int z = minZ; z <= maxZ; ++z) {
            int64_t key = packKey(x, z);
            auto it = m_cells.find(key);
            if (it != m_cells.end()) {
                it->second = buildCell(x, z);
            }
        }
    }

    // Recompute nearWall for the region + 1-cell border
    static const int dx[] = { 0, 1, 1, 1, 0, -1, -1, -1 };
    static const int dz[] = { 1, 1, 0, -1, -1, -1, 0, 1 };
    for (int x = minX - 1; x <= maxX + 1; ++x) {
        for (int z = minZ - 1; z <= maxZ + 1; ++z) {
            auto cit = m_cells.find(packKey(x, z));
            if (cit == m_cells.end() || !cit->second.walkable) continue;

            bool near = false;
            for (int i = 0; i < 8 && !near; ++i) {
                auto nit = m_cells.find(packKey(x + dx[i], z + dz[i]));
                if (nit == m_cells.end()) continue;
                if (!nit->second.walkable) {
                    near = true;
                } else {
                    int heightDiff = nit->second.surfaceY - cit->second.surfaceY;
                    if (heightDiff > MAX_STEP_DOWN) {
                        near = true;
                    }
                }
            }
            cit->second.nearWall = near;
        }
    }
}

const NavCell* NavGrid::getCell(int x, int z) const {
    auto it = m_cells.find(packKey(x, z));
    return (it != m_cells.end()) ? &it->second : nullptr;
}

std::vector<const NavCell*> NavGrid::getNeighbors(const NavCell* cell) const {
    if (!cell) return {};

    // 8 directions: N, NE, E, SE, S, SW, W, NW
    static const int dx[] = { 0,  1, 1, 1, 0, -1, -1, -1 };
    static const int dz[] = { 1,  1, 0, -1, -1, -1,  0,  1 };

    std::vector<const NavCell*> neighbors;
    neighbors.reserve(8);

    for (int i = 0; i < 8; ++i) {
        int nx = cell->x + dx[i];
        int nz = cell->z + dz[i];

        const NavCell* neighbor = getCell(nx, nz);
        if (!neighbor || !neighbor->walkable) continue;

        // Skip nearWall cells — NPC physics body (0.85 wide) clips obstacles
        // when walking through cells adjacent to walls.
        if (neighbor->nearWall) continue;

        // Check height difference
        int heightDiff = neighbor->surfaceY - cell->surfaceY;
        if (heightDiff > MAX_STEP_UP || heightDiff < -MAX_STEP_DOWN) continue;

        // Diagonal movement: both orthogonal neighbors must be walkable
        if (dx[i] != 0 && dz[i] != 0) {
            const NavCell* adj1 = getCell(cell->x + dx[i], cell->z);
            const NavCell* adj2 = getCell(cell->x, cell->z + dz[i]);
            if (!adj1 || !adj1->walkable || !adj2 || !adj2->walkable) continue;
        }

        neighbors.push_back(neighbor);
    }

    return neighbors;
}

const NavCell* NavGrid::findNearestWalkable(const glm::vec3& worldPos, int searchRadius) const {
    int cx = static_cast<int>(std::floor(worldPos.x));
    int cz = static_cast<int>(std::floor(worldPos.z));

    // Check center first
    const NavCell* center = getCell(cx, cz);
    if (center && center->walkable) return center;

    // Expand outward in rings
    for (int r = 1; r <= searchRadius; ++r) {
        const NavCell* best = nullptr;
        float bestDist = std::numeric_limits<float>::max();

        for (int dx = -r; dx <= r; ++dx) {
            for (int dz = -r; dz <= r; ++dz) {
                if (std::abs(dx) != r && std::abs(dz) != r) continue;
                const NavCell* cell = getCell(cx + dx, cz + dz);
                if (cell && cell->walkable) {
                    float dist = static_cast<float>(dx * dx + dz * dz);
                    if (dist < bestDist) {
                        bestDist = dist;
                        best = cell;
                    }
                }
            }
        }
        if (best) return best;
    }

    return nullptr;
}

const NavCell* NavGrid::findNearestNonWall(const glm::vec3& worldPos, int searchRadius) const {
    int cx = static_cast<int>(std::floor(worldPos.x));
    int cz = static_cast<int>(std::floor(worldPos.z));

    // Check center first
    const NavCell* center = getCell(cx, cz);
    if (center && center->walkable && !center->nearWall) return center;

    // Expand outward in rings
    for (int r = 1; r <= searchRadius; ++r) {
        const NavCell* best = nullptr;
        float bestDist = std::numeric_limits<float>::max();

        for (int dx = -r; dx <= r; ++dx) {
            for (int dz = -r; dz <= r; ++dz) {
                if (std::abs(dx) != r && std::abs(dz) != r) continue;
                const NavCell* cell = getCell(cx + dx, cz + dz);
                if (cell && cell->walkable && !cell->nearWall) {
                    float dist = static_cast<float>(dx * dx + dz * dz);
                    if (dist < bestDist) {
                        bestDist = dist;
                        best = cell;
                    }
                }
            }
        }
        if (best) return best;
    }

    return nullptr;
}

size_t NavGrid::walkableCellCount() const {
    size_t count = 0;
    for (const auto& [key, cell] : m_cells) {
        if (cell.walkable) ++count;
    }
    return count;
}

int64_t NavGrid::packKey(int x, int z) {
    return (static_cast<int64_t>(x) << 32) | (static_cast<int64_t>(z) & 0xFFFFFFFF);
}

int NavGrid::findSurfaceY(int x, int z, int maxY) const {
    for (int y = maxY; y >= 0; --y) {
        if (hasVoxel(glm::ivec3(x, y, z))) {
            return y;
        }
    }
    return -1;
}

bool NavGrid::hasHeadroom(int x, int z, int surfaceY) const {
    for (int dy = 1; dy <= MIN_HEADROOM; ++dy) {
        if (hasVoxel(glm::ivec3(x, surfaceY + dy, z))) {
            return false;
        }
    }
    return true;
}

NavCell NavGrid::buildCell(int x, int z) const {
    NavCell cell;
    cell.x = x;
    cell.z = z;
    cell.surfaceY = findSurfaceY(x, z, MAX_SCAN_Y);
    cell.walkable = (cell.surfaceY >= 0) && hasHeadroom(x, z, cell.surfaceY);
    cell.nearWall = false; // Computed after all cells are built
    return cell;
}

void NavGrid::computeNearWallFlags() {
    // A walkable cell is "nearWall" if any of its 8 neighbors (orthogonal + diagonal):
    //   1) exists in the grid but is non-walkable (solid obstacle), OR
    //   2) exists but is significantly HIGHER (surfaceY diff > MAX_STEP_DOWN),
    //      creating a physical wall the NPC body could clip into.
    // Threshold uses MAX_STEP_DOWN (2) not MAX_STEP_UP (1) so that traversable
    // ledges (1-2 block height changes) don't trigger nearWall, but true walls
    // (3+ blocks higher, like a 3-high stone wall) do.
    // Missing neighbors (outside the grid) do NOT trigger nearWall.
    // Using 8-neighbor check prevents NPC physics bodies (0.85 wide, half=0.425)
    // from clipping diagonal wall corners during straight-line movement.
    static const int dx[] = { 0, 1, 1, 1, 0, -1, -1, -1 };
    static const int dz[] = { 1, 1, 0, -1, -1, -1, 0, 1 };

    for (auto& [key, cell] : m_cells) {
        if (!cell.walkable) continue;

        cell.nearWall = false;
        for (int i = 0; i < 8; ++i) {
            auto nit = m_cells.find(packKey(cell.x + dx[i], cell.z + dz[i]));
            if (nit == m_cells.end()) continue; // Edge of grid, not a wall
            const NavCell& neighbor = nit->second;
            if (!neighbor.walkable) {
                cell.nearWall = true;
                break;
            }
            // Higher neighbor creating physical wall (3+ blocks above surface)
            int heightDiff = neighbor.surfaceY - cell.surfaceY;
            if (heightDiff > MAX_STEP_DOWN) {
                cell.nearWall = true;
                break;
            }
        }
    }
}

bool NavGrid::hasVoxel(const glm::ivec3& pos) const {
    if (m_queryFunc) return m_queryFunc(pos);
    if (m_chunkManager) return m_chunkManager->hasVoxelAt(pos);
    return false;
}

// ============================================================
// Phase 3 — Navigation Links (Jump Links)
// ============================================================

const std::vector<NavLink>* NavGrid::getLinksAt(int x, int z) const {
    auto it = m_links.find(packKey(x, z));
    return (it != m_links.end()) ? &it->second : nullptr;
}

void NavGrid::addLink(const NavLink& link) {
    const NavCell* src = getCell(link.start.x, link.start.y);
    const NavCell* dst = getCell(link.end.x,   link.end.y);
    if (!src || !src->walkable || !dst || !dst->walkable) return;
    m_links[packKey(link.start.x, link.start.y)].push_back(link);
}

void NavGrid::removeLinksAt(int x, int z) {
    // Remove all links starting at (x, z)
    m_links.erase(packKey(x, z));

    // Remove any links ending at (x, z): these come from cells up to 2 steps away.
    // Jump links are at most 2 cells long so we only need to check cardinal directions.
    static const int dx[] = {1, -1, 0, 0};
    static const int dz[] = {0,  0, 1, -1};
    for (int d = 0; d < 4; ++d) {
        for (int steps = 1; steps <= 2; ++steps) {
            int sx = x + dx[d] * steps;
            int sz = z + dz[d] * steps;
            auto it = m_links.find(packKey(sx, sz));
            if (it == m_links.end()) continue;
            auto& vec = it->second;
            vec.erase(std::remove_if(vec.begin(), vec.end(),
                [x, z](const NavLink& lnk) {
                    return lnk.end.x == x && lnk.end.y == z;
                }), vec.end());
            if (vec.empty()) m_links.erase(it);
        }
    }
}

void NavGrid::buildJumpLinks() {
    m_links.clear();
    buildJumpLinksNear(0, 0, std::numeric_limits<int>::max()); // rebuild all
}

void NavGrid::buildJumpLinksNear(int cx, int cz, int radius) {
    static constexpr int MAX_HEIGHT_DIFF  = 2;  // max landing height delta for a jump
    static constexpr float LINK_BASE_COST = 2.0f;
    static const int dx[] = {1, -1, 0, 0};
    static const int dz[] = {0,  0, 1, -1};

    // Determine the range of cells to scan.  When radius is max (full rebuild)
    // we iterate the entire m_cells map; otherwise we scan the bounding box.
    auto tryBuildFrom = [&](const NavCell& cell) {
        if (!cell.walkable) return;
        int64_t key = packKey(cell.x, cell.z);
        for (int d = 0; d < 4; ++d) {
            // The immediate neighbour must be non-walkable (the gap).
            int midX = cell.x + dx[d];
            int midZ = cell.z + dz[d];
            const NavCell* mid = getCell(midX, midZ);
            if (mid && mid->walkable) continue;  // Not a gap — regular path covers this

            // The landing cell is 2 steps over.
            int landX = cell.x + dx[d] * 2;
            int landZ = cell.z + dz[d] * 2;
            const NavCell* landing = getCell(landX, landZ);
            if (!landing || !landing->walkable) continue;

            // Height check: don't jump into pits or over walls that are too tall.
            int heightDiff = std::abs(landing->surfaceY - cell.surfaceY);
            if (heightDiff > MAX_HEIGHT_DIFF) continue;

            NavLink link;
            link.start = {cell.x, cell.z};
            link.end   = {landX, landZ};
            link.type  = NavLinkType::Jump;
            link.cost  = LINK_BASE_COST + static_cast<float>(heightDiff) * 0.5f;
            m_links[key].push_back(link);
        }
    };

    if (radius == std::numeric_limits<int>::max()) {
        for (const auto& [key, cell] : m_cells) tryBuildFrom(cell);
    } else {
        for (int x = cx - radius; x <= cx + radius; ++x) {
            for (int z = cz - radius; z <= cz + radius; ++z) {
                const NavCell* cell = getCell(x, z);
                if (cell) tryBuildFrom(*cell);
            }
        }
    }
}

size_t NavGrid::linkCount() const {
    size_t n = 0;
    for (const auto& [key, vec] : m_links) n += vec.size();
    return n;
}

void NavGrid::checkPathIntersectsCell(int x, int z, std::vector<glm::vec3>& pathNodes,
                                       PathInvalidationCallback onIntersect) const {
    for (const glm::vec3& wp : pathNodes) {
        int wx = static_cast<int>(std::floor(wp.x));
        int wz = static_cast<int>(std::floor(wp.z));
        if (wx == x && wz == z) {
            onIntersect();
            return;
        }
    }
}

} // namespace Core
} // namespace Phyxel
