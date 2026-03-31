#include "core/AStarPathfinder.h"
#include "core/NavGrid.h"
#include "utils/Logger.h"
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <algorithm>

namespace Phyxel {
namespace Core {

AStarPathfinder::AStarPathfinder(NavGrid* grid)
    : m_grid(grid)
{
}

PathResult AStarPathfinder::findPath(const glm::vec3& start, const glm::vec3& goal,
                                      int maxIterations) const {
    PathResult result;
    if (!m_grid) return result;

    // Find nearest walkable cells to start and goal
    const NavCell* startCell = m_grid->findNearestWalkable(start);
    const NavCell* goalCell = m_grid->findNearestWalkable(goal);

    if (!startCell || !goalCell) {
        LOG_WARN("AStarPathfinder", "findPath failed: startCell={} goalCell={}, start=({},{},{}) goal=({},{},{})",
                 startCell ? "found" : "NULL", goalCell ? "found" : "NULL",
                 start.x, start.y, start.z, goal.x, goal.y, goal.z);
        return result;
    }

    // If start is nearWall, find the nearest non-nearWall cell so A* can expand
    // (getNeighbors skips nearWall cells, so start's only reachable neighbors are non-nearWall)
    if (startCell->nearWall) {
        const NavCell* safeStart = m_grid->findNearestNonWall(start);
        if (safeStart) startCell = safeStart;
    }

    // If goal is nearWall, find the nearest non-nearWall cell so A* can reach it
    // (getNeighbors skips nearWall cells for physics body clearance)
    if (goalCell->nearWall) {
        const NavCell* safeGoal = m_grid->findNearestNonWall(goal);
        if (safeGoal) goalCell = safeGoal;
    }
    LOG_DEBUG("AStarPathfinder", "A* from cell({},{} sY={}) to cell({},{} sY={})",
              startCell->x, startCell->z, startCell->surfaceY,
              goalCell->x, goalCell->z, goalCell->surfaceY);
    if (startCell == goalCell) {
        result.found = true;
        result.waypoints.push_back(glm::vec3(
            static_cast<float>(goalCell->x) + 0.5f,
            static_cast<float>(goalCell->surfaceY) + 1.0f,
            static_cast<float>(goalCell->z) + 0.5f));
        return result;
    }

    // Pack cell coords into key for lookup
    auto cellKey = [](const NavCell* c) -> int64_t {
        return (static_cast<int64_t>(c->x) << 32) | (static_cast<int64_t>(c->z) & 0xFFFFFFFF);
    };

    // Octile heuristic
    auto heuristic = [&](const NavCell* a, const NavCell* b) -> float {
        float dx = static_cast<float>(std::abs(a->x - b->x));
        float dz = static_cast<float>(std::abs(a->z - b->z));
        return COST_ORTHOGONAL * (dx + dz) + (COST_DIAGONAL - 2.0f * COST_ORTHOGONAL) * std::min(dx, dz);
    };

    struct Node {
        const NavCell* cell;
        float fScore;
        bool operator>(const Node& other) const { return fScore > other.fScore; }
    };

    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> openSet;
    std::unordered_map<int64_t, float> gScore;
    std::unordered_map<int64_t, int64_t> cameFrom;
    std::unordered_set<int64_t> closedSet;

    int64_t startKey = cellKey(startCell);
    int64_t goalKey = cellKey(goalCell);

    gScore[startKey] = 0.0f;
    openSet.push({startCell, heuristic(startCell, goalCell)});

    int iterations = 0;

    while (!openSet.empty() && iterations < maxIterations) {
        ++iterations;

        Node current = openSet.top();
        openSet.pop();

        int64_t currentKey = cellKey(current.cell);

        if (currentKey == goalKey) {
            // Reconstruct path
            result.found = true;
            result.nodesExpanded = iterations;

            std::vector<glm::vec3> path;
            int64_t key = goalKey;
            while (key != startKey) {
                auto it = m_grid->getCell(
                    static_cast<int>(key >> 32),
                    static_cast<int>(key & 0xFFFFFFFF));
                if (!it) break;
                path.push_back(glm::vec3(
                    static_cast<float>(it->x) + 0.5f,
                    static_cast<float>(it->surfaceY) + 1.0f,
                    static_cast<float>(it->z) + 0.5f));
                auto cf = cameFrom.find(key);
                if (cf == cameFrom.end()) break;
                key = cf->second;
            }

            std::reverse(path.begin(), path.end());
            smoothPath(path);
            result.waypoints = std::move(path);
            return result;
        }

        if (closedSet.count(currentKey)) continue;
        closedSet.insert(currentKey);

        for (const NavCell* neighbor : m_grid->getNeighbors(current.cell)) {
            int64_t neighborKey = cellKey(neighbor);
            if (closedSet.count(neighborKey)) continue;

            // Movement cost
            bool isDiagonal = (neighbor->x != current.cell->x) && (neighbor->z != current.cell->z);
            float moveCost = isDiagonal ? COST_DIAGONAL : COST_ORTHOGONAL;

            // Height change penalty
            int heightDiff = std::abs(neighbor->surfaceY - current.cell->surfaceY);
            moveCost += static_cast<float>(heightDiff) * COST_HEIGHT_PENALTY;

            float tentativeG = gScore[currentKey] + moveCost;

            auto gIt = gScore.find(neighborKey);
            if (gIt == gScore.end() || tentativeG < gIt->second) {
                gScore[neighborKey] = tentativeG;
                cameFrom[neighborKey] = currentKey;
                float f = tentativeG + heuristic(neighbor, goalCell);
                openSet.push({neighbor, f});
            }
        }
    }

    result.nodesExpanded = iterations;
    if (!result.found) {
        LOG_WARN("AStarPathfinder", "A* exhausted: {} iterations, openSet empty={}, closedSet size={}",
                 iterations, openSet.empty(), closedSet.size());
    }
    return result;
}

void AStarPathfinder::smoothPath(std::vector<glm::vec3>& waypoints) {
    if (waypoints.size() <= 2) return;

    std::vector<glm::vec3> smoothed;
    smoothed.push_back(waypoints.front());

    for (size_t i = 1; i + 1 < waypoints.size(); ++i) {
        const glm::vec3& prev = smoothed.back();
        const glm::vec3& curr = waypoints[i];
        const glm::vec3& next = waypoints[i + 1];

        // Check if prev→curr→next are collinear in XZ
        glm::vec2 d1(curr.x - prev.x, curr.z - prev.z);
        glm::vec2 d2(next.x - curr.x, next.z - curr.z);

        float cross = d1.x * d2.y - d1.y * d2.x;

        // Keep the waypoint if direction changes or height changes
        if (std::abs(cross) > 0.001f || std::abs(curr.y - prev.y) > 0.01f) {
            smoothed.push_back(curr);
        }
    }

    smoothed.push_back(waypoints.back());
    waypoints = std::move(smoothed);
}

} // namespace Core
} // namespace Phyxel
