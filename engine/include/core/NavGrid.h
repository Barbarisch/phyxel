#pragma once

#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <functional>

namespace Phyxel {

class ChunkManager;

namespace Core {

/// A single cell in the navigation grid, representing a column of world XZ space.
struct NavCell {
    int x = 0;                 ///< World X coordinate
    int z = 0;                 ///< World Z coordinate
    int surfaceY = -1;         ///< Y of the topmost solid voxel (-1 = no surface)
    bool walkable = false;     ///< True if NPC can stand here (surface + headroom)
    bool nearWall = false;     ///< True if adjacent to a non-walkable cell (wall/obstacle)
};

/// Function type for querying whether a voxel exists at a given world position.
using VoxelQueryFunc = std::function<bool(const glm::ivec3&)>;

/// 2.5D navigation grid built from voxel data for A* pathfinding.
/// Each cell is 1×1 voxel resolution in XZ, storing topmost surface Y.
/// Supports multi-chunk worlds via sparse storage.
class NavGrid {
public:
    /// Maximum Y range to scan when finding surface voxels.
    static constexpr int MAX_SCAN_Y = 128;
    /// Minimum headroom (empty voxels above surface) for a cell to be walkable.
    static constexpr int MIN_HEADROOM = 2;
    /// Maximum step-up height (blocks) between adjacent walkable cells.
    /// Set to 0 because there is no climbing mechanic — NPCs route around raised
    /// surfaces rather than stepping onto them. Increase if stair traversal is added.
    static constexpr int MAX_STEP_UP = 0;
    /// Maximum step-down height (blocks) between adjacent walkable cells.
    static constexpr int MAX_STEP_DOWN = 2;

    /// Construct from a ChunkManager (uses hasVoxelAt for queries).
    explicit NavGrid(ChunkManager* chunkManager);

    /// Construct from a custom voxel query function (for testing).
    explicit NavGrid(VoxelQueryFunc queryFunc);

    /// Build the grid for a rectangular XZ region (inclusive bounds).
    void buildFromRegion(const glm::ivec2& minXZ, const glm::ivec2& maxXZ);

    /// Rebuild a single cell after a voxel change.
    void rebuildCell(int x, int z);

    /// Rebuild all cells in an XZ region after bulk voxel changes (e.g. template spawn).
    void rebuildRegion(int minX, int minZ, int maxX, int maxZ);

    /// Get cell at world XZ. Returns nullptr if not in grid.
    const NavCell* getCell(int x, int z) const;

    /// Get walkable neighbors (up to 8). Filters by height difference and diagonal rules.
    std::vector<const NavCell*> getNeighbors(const NavCell* cell) const;

    /// Find the nearest walkable cell to a world position.
    const NavCell* findNearestWalkable(const glm::vec3& worldPos, int searchRadius = 5) const;

    /// Find the nearest walkable cell that is NOT nearWall.
    const NavCell* findNearestNonWall(const glm::vec3& worldPos, int searchRadius = 5) const;

    /// Total number of cells in the grid.
    size_t cellCount() const { return m_cells.size(); }

    /// Total number of walkable cells.
    size_t walkableCellCount() const;

private:
    /// Pack XZ into a single key for hashmap lookup.
    static int64_t packKey(int x, int z);

    /// Scan downward from maxY to find the topmost solid voxel at (x, z).
    int findSurfaceY(int x, int z, int maxY) const;

    /// Check if there are at least MIN_HEADROOM empty voxels above surfaceY at (x, z).
    bool hasHeadroom(int x, int z, int surfaceY) const;

    /// Build a single cell at world (x, z) and insert it into m_cells.
    NavCell buildCell(int x, int z) const;

    /// Compute nearWall flags for all cells (must be called after all cells are built).
    void computeNearWallFlags();

    /// Query whether a voxel exists at a given world position.
    bool hasVoxel(const glm::ivec3& pos) const;

    ChunkManager* m_chunkManager = nullptr;
    VoxelQueryFunc m_queryFunc;
    std::unordered_map<int64_t, NavCell> m_cells;
    glm::ivec2 m_minBounds{0, 0};
    glm::ivec2 m_maxBounds{0, 0};
};

} // namespace Core
} // namespace Phyxel
