#pragma once

#include <glm/glm.hpp>
#include <vector>

namespace Phyxel {
namespace Core {

class NavGrid;

/// Result of a pathfinding query.
struct PathResult {
    bool found = false;                    ///< True if a valid path was found
    std::vector<glm::vec3> waypoints;      ///< World-space waypoints (Y = surfaceY + 1)
    int nodesExpanded = 0;                 ///< Number of A* nodes expanded (for diagnostics)
};

/// A* pathfinder operating on a NavGrid.
/// Supports 8-directional movement with octile heuristic and height-change penalties.
class AStarPathfinder {
public:
    /// Cost for orthogonal movement (N/S/E/W).
    static constexpr float COST_ORTHOGONAL = 1.0f;
    /// Cost for diagonal movement (NE/SE/SW/NW).
    static constexpr float COST_DIAGONAL = 1.414f;
    /// Extra cost per block of height change.
    static constexpr float COST_HEIGHT_PENALTY = 0.5f;
    /// Extra cost for moving through a cell adjacent to a wall (body clearance).
    static constexpr float COST_NEAR_WALL = 3.0f;
    /// Default maximum A* iterations before giving up.
    static constexpr int DEFAULT_MAX_ITERATIONS = 10000;

    explicit AStarPathfinder(NavGrid* grid);

    /// Find a path from start to goal in world space.
    /// Returns waypoints with Y = surfaceY + 1 (standing height).
    PathResult findPath(const glm::vec3& start, const glm::vec3& goal,
                        int maxIterations = DEFAULT_MAX_ITERATIONS) const;

    /// Access the underlying navigation grid.
    NavGrid* getGrid() const { return m_grid; }

private:
    /// Smooth path by removing redundant collinear waypoints.
    static void smoothPath(std::vector<glm::vec3>& waypoints);

    NavGrid* m_grid = nullptr;
};

} // namespace Core
} // namespace Phyxel
