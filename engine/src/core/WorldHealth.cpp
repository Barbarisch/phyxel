#include "core/WorldHealth.h"

#include "core/NavGraph.h"

#include <cmath>
#include <ctime>
#include <sstream>

namespace Phyxel {
namespace Core {

nlohmann::json WorldHealthReport::toJson() const {
    nlohmann::json a = nlohmann::json::array();
    for (const auto& an : anchors)
        a.push_back({{"id", an.id}, {"kind", an.kind}, {"reachable", an.reachable}, {"waypoints", an.waypoints},
                     {"position", {{"x", an.pos.x}, {"y", an.pos.y}, {"z", an.pos.z}}},
                     {"void_beyond", an.voidBeyond}});
    return {{"ok", ok()}, {"reachable", reachable}, {"total", total},
            {"terrain_under_spawn", terrainUnderSpawn}, {"graph_available", graphAvailable},
            {"exits_facing_void", exitsFacingVoid},
            {"spawn", {{"x", spawn.x}, {"y", spawn.y}, {"z", spawn.z}}}, {"anchors", a}};
}

std::string WorldHealthReport::summary() const {
    std::ostringstream os;
    os << "WorldHealth: reachable " << reachable << "/" << total
       << ", terrain under spawn " << (terrainUnderSpawn ? "yes" : "NO")
       << ", graph " << (graphAvailable ? "ok" : "MISSING");
    if (exitsFacingVoid) os << ", " << exitsFacingVoid << " exit(s) facing a void";
    for (const auto& an : anchors) {
        if (!an.reachable)
            os << "\n  UNREACHABLE " << an.kind << " '" << an.id << "' at (" << an.pos.x << ", " << an.pos.y
               << ", " << an.pos.z << ")";
        if (!an.voidBeyond.empty()) {
            os << "\n  VOID beyond exit '" << an.id << "' within " << kExitMarginCubes << " m to";
            for (const auto& d : an.voidBeyond) os << " " << d;
        }
    }
    return os.str();
}

std::string WorldHealth::timestamp() {
    const std::time_t t = std::time(nullptr);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tmv);
    return buf;
}

WorldHealthReport WorldHealth::check(const NavGraph* graph, const HasVoxelFunc& hasVoxel,
                                     const glm::vec3& spawn, std::vector<WorldHealthAnchor> anchors) {
    WorldHealthReport rep;
    rep.spawn = spawn;
    rep.graphAvailable = (graph != nullptr);
    // Terrain under the spawn: the spawn column must hold a voxel within 8 cubes below the
    // feet (a 0-chunk world has none - that is the void, not a walkability question).
    const int cx = static_cast<int>(std::floor(spawn.x));
    const int cz = static_cast<int>(std::floor(spawn.z));
    const int cy = static_cast<int>(std::floor(spawn.y));
    if (hasVoxel)
        for (int y = cy; y >= cy - 8 && !rep.terrainUnderSpawn; --y)
            if (hasVoxel(glm::ivec3(cx, y, cz))) rep.terrainUnderSpawn = true;
    rep.total = static_cast<int>(anchors.size());
    for (auto& an : anchors) {
        an.reachable = false;
        an.waypoints = 0;
        if (graph) {
            // "Reachable" means a player can get within talking distance: the anchor's own
            // cell, or any cell one metre away. An NPC parked on a stair tread or against a
            // fixture resolves to a surface the graph cannot enter while the floor beside
            // him is open (run 43: Bram at the tavern's stair rail reported unreachable
            // while every playthrough talked to him from 1.5 m).
            NavAgentProfile agent;
            static const glm::vec3 kOffsets[5] = {
                {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}};
            for (const auto& off : kOffsets) {
                const auto r = graph->findPath(spawn, an.pos + off, agent);
                if (r.found) { an.reachable = true; an.waypoints = static_cast<int>(r.waypoints.size()); break; }
            }
        }
        if (an.reachable) ++rep.reachable;
        // A scene exit with no world past it (G-88): sample the ground column
        // kExitMarginCubes away in each direction; no voxel within 8 cubes below the
        // anchor's feet there means the road leads into nothing.
        if (an.kind == "trigger" && hasVoxel) {
            static const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            static const char* names[4] = {"+x", "-x", "+z", "-z"};
            const int ax = static_cast<int>(std::floor(an.pos.x)), az = static_cast<int>(std::floor(an.pos.z));
            const int ay = static_cast<int>(std::floor(an.pos.y));
            for (int d = 0; d < 4; ++d) {
                const int sx = ax + dirs[d][0] * kExitMarginCubes, sz = az + dirs[d][1] * kExitMarginCubes;
                bool ground = false;
                for (int y = ay + 2; y >= ay - 8 && !ground; --y)
                    if (hasVoxel(glm::ivec3(sx, y, sz))) ground = true;
                if (!ground) an.voidBeyond.push_back(names[d]);
            }
            if (!an.voidBeyond.empty()) ++rep.exitsFacingVoid;
        }
    }
    rep.anchors = std::move(anchors);
    return rep;
}

}  // namespace Core
}  // namespace Phyxel
