#include "core/TowerForge.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "core/TownWall.h"   // towerFootprintCells — one definition of "round"

namespace Phyxel {
namespace Core {

namespace {
constexpr int kMicroPerCube = 9;
constexpr int kSubcube = 3;          ///< a tread's rise: 3 micro, inside the agent's 4-micro step
}  // namespace

TowerPlan planTower(const Rect& bbox, const TowerSpec& spec) {
    TowerPlan p;
    if (bbox.w < 5 || bbox.d < 5) {
        p.refusal = "tower footprint " + std::to_string(bbox.w) + "x" + std::to_string(bbox.d) +
                    " is too small to hold a wall, a stair and a room — a smaller drum would be "
                    "solid decoration, not a tower";
        return p;
    }
    if (spec.heightCubes < spec.storeyCubes * 2) {
        p.refusal = "tower is shorter than two storeys — nothing to climb to";
        return p;
    }

    // ---- BODY / RIM / INTERIOR -------------------------------------------------
    const auto bodyCells = towerFootprintCells(bbox, spec.shape);
    std::set<std::pair<int, int>> body;
    for (const auto& c : bodyCells) body.insert({c.x, c.y});
    auto isBody = [&](int x, int z) { return body.count({x, z}) > 0; };

    std::set<std::pair<int, int>> rim, interior;
    for (const auto& c : body) {
        const bool edge = !isBody(c.first + 1, c.second) || !isBody(c.first - 1, c.second) ||
                          !isBody(c.first, c.second + 1) || !isBody(c.first, c.second - 1);
        (edge ? rim : interior).insert(c);
    }
    if (interior.size() < 4) {
        p.refusal = "no usable interior once the wall is taken out (" +
                    std::to_string(interior.size()) + " cells) — widen the tower";
        return p;
    }

    const int topMicro = spec.heightCubes * kMicroPerCube;

    // ---- THE SPIRAL STAIR ------------------------------------------------------
    // Treads hug the inside of the wall — the ring of interior cells that touch the rim,
    // which is where a newel stair in a drum actually runs. Ordered by ANGLE so the flight
    // winds continuously instead of hopping across the shaft.
    const double cx = (bbox.w - 1) / 2.0, cz = (bbox.d - 1) / 2.0;
    std::vector<std::pair<int, int>> ring;
    for (const auto& c : interior) {
        const bool touchesRim = rim.count({c.first + 1, c.second}) || rim.count({c.first - 1, c.second}) ||
                                rim.count({c.first, c.second + 1}) || rim.count({c.first, c.second - 1});
        if (touchesRim) ring.push_back(c);
    }
    if (ring.size() < 6) {
        p.refusal = "the interior ring is too short (" + std::to_string(ring.size()) +
                    " cells) to carry a climbable spiral";
        return p;
    }
    std::sort(ring.begin(), ring.end(), [&](const auto& a, const auto& b) {
        const double aa = std::atan2(a.second - cz, a.first - cx);
        const double ab = std::atan2(b.second - cz, b.first - cx);
        if (aa != ab) return aa < ab;
        return a < b;
    });

    // ---- DOORWAY (chosen BEFORE the spiral, because the spiral must start AT it) -------
    // A newel stair begins where you come in. Winding it from an arbitrary angle instead
    // put the doorway against a tread that had already climbed 9 micro — you opened the
    // door onto a wall of stair and could not enter (measured: the probe never got inside).
    auto pickDoor = [&]() -> std::pair<int, int> {
        std::pair<int, int> best{-1, -1};
        double bestScore = 1e18;
        for (const auto& c : rim) {
            double s = 0;
            switch (spec.doorSide) {
                case 'N': s = -(c.second); break;
                case 'S': s = c.second; break;
                case 'W': s = c.first; break;
                default:  s = -(c.first); break;
            }
            const bool opensIn = interior.count({c.first + 1, c.second}) ||
                                 interior.count({c.first - 1, c.second}) ||
                                 interior.count({c.first, c.second + 1}) ||
                                 interior.count({c.first, c.second - 1});
            if (!opensIn) s += 1000;
            if (s < bestScore) { bestScore = s; best = c; }
        }
        return best;
    };
    const auto door = pickDoor();
    if (door.first < 0) { p.refusal = "no rim cell can host a doorway"; return p; }

    // Rotate the ring so the flight starts at the tread beside the doorway.
    {
        size_t startIdx = 0;
        int bestD = 1 << 30;
        for (size_t i = 0; i < ring.size(); ++i) {
            const int d = std::abs(ring[i].first - door.first) +
                          std::abs(ring[i].second - door.second);
            if (d < bestD) { bestD = d; startIdx = i; }
        }
        std::rotate(ring.begin(), ring.begin() + static_cast<long>(startIdx), ring.end());
    }

    // A discrete circle's ring, ordered by angle, takes DIAGONAL steps at its corners —
    // and an agent walks orthogonally, so it arrives at the corner and stops. (Measured:
    // the climb died at (5,1)->(6,2), feet stuck at y=15.) Bridge every diagonal with the
    // interior cell orthogonally adjacent to both, giving a genuinely 4-connected flight.
    std::vector<std::pair<int, int>> path;
    auto orthogonal = [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
        return std::abs(a.first - b.first) + std::abs(a.second - b.second) == 1;
    };
    for (size_t i = 0; i < ring.size(); ++i) {
        const auto& cur = ring[i];
        const auto& nxt = ring[(i + 1) % ring.size()];
        if (std::find(path.begin(), path.end(), cur) == path.end()) path.push_back(cur);
        if (orthogonal(cur, nxt)) continue;
        const std::pair<int, int> bridges[2] = {{cur.first, nxt.second}, {nxt.first, cur.second}};
        for (const auto& b : bridges) {
            if (!interior.count(b)) continue;
            if (!orthogonal(cur, b) || !orthogonal(b, nxt)) continue;
            if (std::find(path.begin(), path.end(), b) != path.end()) continue;
            path.push_back(b);
            break;
        }
    }
    if (path.size() < 6) {
        p.refusal = "could not wind a 4-connected flight around the interior";
        return p;
    }

    // Core cells — the interior the stair does NOT use — carry the floor slabs, so the
    // climb arrives in a room rather than onto more stair.
    std::vector<std::pair<int, int>> core;
    for (const auto& c : interior)
        if (std::find(path.begin(), path.end(), c) == path.end()) core.push_back(c);
    std::sort(core.begin(), core.end());
    if (core.empty()) {
        p.refusal = "the stair consumed the whole interior — no room to arrive in";
        return p;
    }

    // Wind the spiral from the base to the top, one tread per path cell, +3 micro each.
    // The rise per tread is the whole point: 3 micro clears the agent's 4-micro step, a
    // full cube (9) would not, and the stair would be scenery.
    // The ground storey is FLOORED (walking surface at 3), so the first tread sits at 3 and
    // its surface at 6 — one 3-micro step off that floor. Getting this wrong either way is
    // fatal and silent: a tread flush with the ground put a 6-micro lip in front of the
    // agent, and starting higher walled the doorway off entirely.
    int y = kSubcube;
    std::map<std::pair<int, int>, int> lastTreadTop;   // ring cell -> highest tread top
    size_t idx = 0;
    while (y + kSubcube <= topMicro - kMicroPerCube) {     // stop below the parapet deck
        const auto& cell = path[idx % path.size()];
        TowerPlate t;
        t.cx = cell.first;
        t.cz = cell.second;
        t.yMicro = y;
        t.thicknessMicro = kSubcube;
        t.tread = true;
        p.plates.push_back(t);
        lastTreadTop[cell] = y + kSubcube;
        y += kSubcube;
        ++idx;
    }

    // ---- FLOORS ----------------------------------------------------------------
    // A slab over the core at each storey, so the climb ARRIVES somewhere. The ring is
    // left open — that is the stairwell.
    const int storeyMicro = spec.storeyCubes * kMicroPerCube;
    for (int fy = storeyMicro; fy + kMicroPerCube <= topMicro; fy += storeyMicro) {
        for (const auto& c : core) {
            TowerPlate f;
            f.cx = c.first;
            f.cz = c.second;
            f.yMicro = fy;
            f.thicknessMicro = kSubcube;
            f.tread = false;
            p.plates.push_back(f);
        }
        ++p.floorCount;
    }
    // The GROUND STOREY is floored across the whole interior — ring included — plus a
    // threshold in the doorway itself, so you step in onto a level floor instead of onto
    // the side of the stair. Flooring only the core left the cell inside the door unfloored
    // and the entry unusable.
    for (const auto& c : interior) {
        TowerPlate f;
        f.cx = c.first; f.cz = c.second; f.yMicro = 0; f.thicknessMicro = kSubcube; f.tread = false;
        p.plates.push_back(f);
    }
    {
        TowerPlate th;
        th.cx = door.first; th.cz = door.second; th.yMicro = 0;
        th.thicknessMicro = kSubcube; th.tread = false;
        p.plates.push_back(th);
    }

    const int doorTopMicro = 3 * kMicroPerCube;    // 3 cubes clear: 27 > the agent's 16
    // ---- WALLS (rim runs, with the door and the loops cut out) ------------------
    std::set<std::pair<int, int>> loopCells;
    if (spec.arrowLoops) {
        // One loop per storey, spread around the rim so each face is covered.
        std::vector<std::pair<int, int>> rimOrdered(rim.begin(), rim.end());
        std::sort(rimOrdered.begin(), rimOrdered.end(), [&](const auto& a, const auto& b) {
            return std::atan2(a.second - cz, a.first - cx) < std::atan2(b.second - cz, b.first - cx);
        });
        int loopIdx = 0;
        for (int fy = storeyMicro; fy + kMicroPerCube <= topMicro; fy += storeyMicro) {
            for (int k = 0; k < 4 && !rimOrdered.empty(); ++k) {
                const auto& rc = rimOrdered[(loopIdx * 7 + k * rimOrdered.size() / 4) % rimOrdered.size()];
                loopCells.insert(rc);
                p.loopCells.push_back({rc.first, (fy + kSubcube) / kMicroPerCube, rc.second});
            }
            ++loopIdx;
        }
    }

    for (const auto& c : rim) {
        std::vector<std::pair<int, int>> gaps;          // [from,to) micro spans to leave open
        if (c == door) gaps.push_back({0, doorTopMicro});
        for (const auto& lc : p.loopCells)
            if (lc.x == c.first && lc.z == c.second) {
                const int base = lc.y * kMicroPerCube + kSubcube;
                gaps.push_back({base, base + kSubcube * 2});   // a 6-micro slit
            }
        std::sort(gaps.begin(), gaps.end());
        int cursor = 0;
        for (const auto& g : gaps) {
            if (g.first > cursor) p.walls.push_back({c.first, c.second, cursor, g.first});
            cursor = std::max(cursor, g.second);
        }
        if (cursor < topMicro) p.walls.push_back({c.first, c.second, cursor, topMicro});
    }

    // ---- THE TWO POSES THE L3 PROOF USES ---------------------------------------
    // Feet just inside the doorway, and the top chamber floor.
    p.doorFeetMicro = {door.first * kMicroPerCube + 4, kSubcube,
                       door.second * kMicroPerCube + 4};
    int topFloorY = 0;
    for (const auto& pl : p.plates)
        if (!pl.tread) topFloorY = std::max(topFloorY, pl.yMicro);
    const auto& goalCell = core.front();
    p.topFeetMicro = {goalCell.first * kMicroPerCube + 4, topFloorY + kSubcube,
                      goalCell.second * kMicroPerCube + 4};

    p.ok = true;
    return p;
}

} // namespace Core
} // namespace Phyxel
