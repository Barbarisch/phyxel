#pragma once
// WorldHealth - the load-time self-check of a shipped world (WalkabilityGateAndPlaytestLoop
// layer B, increment 5). When a world scene is ready, path from the spawn to every authored
// anchor (NPCs, trigger regions, locations) on the runtime NavGraph and check there is
// terrain under the spawn at all. A failure is a generator defect that escaped layer A -
// or an empty world (Ravenmere G-84: a 0-chunk database spawned the player into the void).
// The report is logged and served as /api/rpg/world_health so a harness asserts it before
// playing.
#include <functional>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

class NavGraph;

struct WorldHealthAnchor {
    std::string id;
    std::string kind;        ///< "npc" | "trigger" | "location" | custom
    glm::vec3   pos{0.0f};   ///< feet position (world); a region's centre for triggers
    bool        reachable = false;
    int         waypoints = 0;
};

struct WorldHealthReport {
    glm::vec3 spawn{0.0f};
    bool terrainUnderSpawn = false;   ///< any voxel within 8 cubes below the spawn column
    bool graphAvailable = false;
    int  reachable = 0;
    int  total = 0;
    std::vector<WorldHealthAnchor> anchors;

    bool ok() const { return terrainUnderSpawn && graphAvailable && reachable == total; }
    nlohmann::json toJson() const;
    /// One line: "WorldHealth: reachable N/M, terrain under spawn yes/no" + the failures.
    std::string summary() const;
};

class WorldHealth {
public:
    using HasVoxelFunc = std::function<bool(const glm::ivec3& cube)>;

    /// `graph` may be null (reported as graphAvailable=false, every anchor unreachable).
    static WorldHealthReport check(const NavGraph* graph, const HasVoxelFunc& hasVoxel,
                                   const glm::vec3& spawn, std::vector<WorldHealthAnchor> anchors);
    /// Local time as "YYYY-MM-DDTHH:MM:SS" (defect records).
    static std::string timestamp();
};

}  // namespace Core
}  // namespace Phyxel
