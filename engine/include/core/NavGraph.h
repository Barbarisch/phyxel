#pragma once

#include "core/NavGrid.h"   // for Core::VoxelQueryFunc (shared during NavGrid -> NavGraph migration)
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace Phyxel {

class ChunkManager;

namespace Core {

// ============================================================================
// NavGraph — Layer 1 of the navigation architecture (docs/NavigationArchitecture.md).
//
// A voxel-native, TRUE-3D walkable-surface graph: unlike the flat 2.5D NavGrid
// (one surface per XZ column), it keeps EVERY standable level in a column — so a
// bridge and the ground beneath it, or floor 1 and floor 2 of a building, are
// distinct nodes at the same XZ. Built alongside NavGrid during migration.
// ============================================================================

/// One walkable surface within an XZ column. Multiple may stack per column.
struct NavSurface {
    int      x = 0;
    int      z = 0;
    int      floorY = -1;     ///< Y of the solid voxel the agent stands ON (feet at floorY+1).
    uint16_t headroom = 0;    ///< empty voxels above the floor (clamped to MAX_HEADROOM).
    bool     nearEdge = false;///< adjacent to a drop / non-walkable column (steering hint).
};

/// Identity of a surface node: its column plus the level index within that column.
struct NavNodeId {
    int x = 0;
    int z = 0;
    int level = -1;           ///< index into the column's surface list; < 0 = invalid.
    bool valid() const { return level >= 0; }
    bool operator==(const NavNodeId& o) const { return x == o.x && z == o.z && level == o.level; }
    bool operator!=(const NavNodeId& o) const { return !(*this == o); }
};

/// Movement capabilities — paths depend on the agent, so queries take a profile.
struct NavAgentProfile {
    int  height     = 2;      ///< empty voxels needed above the floor to stand/walk.
    int  stepHeight = 1;      ///< max upward floorY difference traversable as a step.
    int  maxFallY   = 4;      ///< max downward drop traversable (down only).
    int  jumpHeight = 1;      ///< reserved for jump edges (later slice).
    bool canClimb   = true;   ///< reserved for climb edges (later slice).
};

class NavGraph {
public:
    static constexpr int MIN_SCAN_Y   = -64;  ///< lowest Y scanned per column.
    static constexpr int MAX_SCAN_Y   = 256;  ///< highest Y scanned per column.
    static constexpr int MAX_HEADROOM = 32;   ///< headroom is clamped to this.

    explicit NavGraph(ChunkManager* chunkManager);
    explicit NavGraph(VoxelQueryFunc queryFunc);   ///< for tests / custom worlds.

    /// Build all columns in an inclusive XZ region for the given agent.
    void buildRegion(const glm::ivec2& minXZ, const glm::ivec2& maxXZ, const NavAgentProfile& agent);

    /// Rebuild a single column after a voxel change (incremental update entry point).
    void rebuildColumn(int x, int z, const NavAgentProfile& agent);

    /// Surfaces in a column (bottom-to-top). Empty ref if the column has none / unbuilt.
    const std::vector<NavSurface>& columnSurfaces(int x, int z) const;

    /// Resolve a node id to its surface (nullptr if invalid / not present).
    const NavSurface* surface(const NavNodeId& id) const;

    /// Walkable neighbors reachable from a node for this agent (step up/down within limits).
    std::vector<NavNodeId> neighbors(const NavNodeId& id, const NavAgentProfile& agent) const;

    /// The surface an agent at worldPos is standing on (highest floor at/just below the
    /// agent's feet). Returns an invalid id if the column has no surface there.
    NavNodeId surfaceAt(const glm::vec3& worldPos) const;

    size_t columnCount() const { return m_columns.size(); }
    size_t surfaceCount() const;

private:
    static int64_t packKey(int x, int z);
    bool hasVoxel(const glm::ivec3& p) const;
    std::vector<NavSurface> buildColumn(int x, int z, const NavAgentProfile& agent) const;

    ChunkManager*  m_chunkManager = nullptr;
    VoxelQueryFunc m_queryFunc;
    std::unordered_map<int64_t, std::vector<NavSurface>> m_columns;
    glm::ivec2 m_minBounds{0, 0};
    glm::ivec2 m_maxBounds{0, 0};
};

} // namespace Core
} // namespace Phyxel
