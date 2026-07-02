#include "core/RealizedWorldValidator.h"

#include <algorithm>
#include <climits>
#include <map>
#include <utility>

namespace Phyxel {
namespace Core {

bool RealizedWorldValidator::isClutter(const std::string& t) {
    return t == "mug" || t == "bottle" || t == "plate" || t == "goblet" ||
           t == "candle" || t == "candlestick";
}

bool RealizedWorldValidator::isHearth(const std::string& t) {
    return t.find("fireplace") != std::string::npos || t.find("forge") != std::string::npos ||
           t.find("oven") != std::string::npos || t == "hearth";
}

bool RealizedWorldValidator::isGrass(const std::string& m) {
    return m == "Grass" || m == "GrassForest" || m == "GrassSavanna";
}

// V10 — grass under a house. For each structure, scan the footprint columns in the few cube rows just
// below the floor (min.y) for a grass surface cube. Any remaining grass beneath the building is a
// defect (hidden blades under the floor); reports the count + a sample cell per structure.
ValidationReport RealizedWorldValidator::checkGrassUnderFootprint(
    const std::vector<FootprintScan>& structures, const MaterialAt& matAt, int probeDepth) {
    ValidationReport rep;
    if (!matAt) return rep;
    for (const auto& s : structures) {
        const int floorY = s.min.y;
        int count = 0;
        glm::ivec3 sample{0};
        for (int x = s.min.x; x <= s.max.x; ++x) {
            for (int z = s.min.z; z <= s.max.z; ++z) {
                for (int y = floorY - 1; y >= floorY - probeDepth; --y) {
                    if (isGrass(matAt(x, y, z))) {
                        if (count == 0) sample = {x, y, z};
                        ++count;
                    }
                }
            }
        }
        if (count > 0) {
            rep.addError("grass_under_house",
                "structure '" + s.id + "' has " + std::to_string(count) + " grass cube(s) under its "
                "floor (e.g. " + std::to_string(sample.x) + "," + std::to_string(sample.y) + "," +
                std::to_string(sample.z) + ") — grass beneath a building must be cleared", s.id);
        }
    }
    return rep;
}

// V8 — chimney seated on hearth. A correct stack has Stone directly above the hearth footprint; the
// observed defect has the stack one cube to the side (hearth x=1, chimney x=2). Fires offset (Stone
// rises adjacent, not over the hearth) or missing (no Stone rises above at all).
ValidationReport RealizedWorldValidator::checkChimneyOverHearth(
    const std::vector<PlacedBox>& fireplaces, const CubeHasMaterial& hasMat, int rise) {
    ValidationReport rep;
    if (!hasMat) return rep;
    for (const auto& fp : fireplaces) {
        if (!isHearth(fp.type)) continue;
        const int top = fp.max.y;   // hearth top
        // (1) Stone directly above the hearth footprint == a seated stack.
        bool seated = false;
        for (int x = fp.min.x; x <= fp.max.x && !seated; ++x)
            for (int z = fp.min.z; z <= fp.max.z && !seated; ++z)
                for (int y = top + 1; y <= top + rise; ++y)
                    if ((hasMat(x, y, z, "Bricks") || hasMat(x, y, z, "Stone"))) { seated = true; break; }
        if (seated) continue;
        // (2) Not seated — look for an offset stack in the immediate neighborhood (outside footprint).
        bool offset = false;
        glm::ivec3 sample{0};
        for (int x = fp.min.x - 1; x <= fp.max.x + 1 && !offset; ++x)
            for (int z = fp.min.z - 1; z <= fp.max.z + 1 && !offset; ++z) {
                const bool inFoot = x >= fp.min.x && x <= fp.max.x && z >= fp.min.z && z <= fp.max.z;
                if (inFoot) continue;
                for (int y = top + 1; y <= top + rise; ++y)
                    if ((hasMat(x, y, z, "Bricks") || hasMat(x, y, z, "Stone"))) { offset = true; sample = {x, y, z}; break; }
            }
        if (offset) {
            rep.addError("chimney_offset_from_hearth",
                "fireplace '" + fp.id + "' hearth is at x[" + std::to_string(fp.min.x) + ".." +
                std::to_string(fp.max.x) + "] z[" + std::to_string(fp.min.z) + ".." +
                std::to_string(fp.max.z) + "] but its chimney rises at (" + std::to_string(sample.x) +
                "," + std::to_string(sample.y) + "," + std::to_string(sample.z) +
                ") — the stack does not sit above the hearth", fp.id);
        } else {
            rep.addError("chimney_missing",
                "fireplace '" + fp.id + "' has no Stone chimney rising above its hearth", fp.id);
        }
    }
    return rep;
}

// V7 — path under a house. A Cobblestone path in the footprint INTERIOR (inset 1 from the walls) is a
// path routed through the building; perimeter cobblestone (a path meeting the door) is allowed.
ValidationReport RealizedWorldValidator::checkPathUnderFootprint(
    const std::vector<FootprintScan>& structures, const CubeHasMaterial& hasMat, int band) {
    ValidationReport rep;
    if (!hasMat) return rep;
    for (const auto& s : structures) {
        // Need an interior (inset 1) to distinguish a through-path from a doorstep at the wall.
        if (s.max.x - s.min.x < 2 || s.max.z - s.min.z < 2) continue;
        const int floorY = s.min.y;
        int count = 0;
        glm::ivec3 sample{0};
        for (int x = s.min.x + 1; x <= s.max.x - 1; ++x)
            for (int z = s.min.z + 1; z <= s.max.z - 1; ++z)
                for (int y = floorY - band; y <= floorY + band; ++y)
                    if (hasMat(x, y, z, "Cobblestone")) {
                        if (count == 0) sample = {x, y, z};
                        ++count;
                    }
        if (count > 0) {
            rep.addError("path_under_house",
                "structure '" + s.id + "' has " + std::to_string(count) + " path (Cobblestone) cell(s) "
                "inside its footprint (e.g. " + std::to_string(sample.x) + "," + std::to_string(sample.y) +
                "," + std::to_string(sample.z) + ") — paths must route around buildings, not through them",
                s.id);
        }
    }
    return rep;
}

// V3 — yard flatness. Sample the terrain surface height in the ring around the footprint; a large span
// means the yard is a slope, not a graded flat.
ValidationReport RealizedWorldValidator::checkYardFlatness(
    const std::vector<FootprintScan>& structures, const SurfaceHeight& surfaceH, int yardWidth,
    int flatTol) {
    ValidationReport rep;
    if (!surfaceH) return rep;
    for (const auto& s : structures) {
        int lo = INT_MAX, hi = INT_MIN;
        glm::ivec3 loAt{0}, hiAt{0};
        for (int x = s.min.x - yardWidth; x <= s.max.x + yardWidth; ++x)
            for (int z = s.min.z - yardWidth; z <= s.max.z + yardWidth; ++z) {
                const bool inFoot = x >= s.min.x && x <= s.max.x && z >= s.min.z && z <= s.max.z;
                if (inFoot) continue;
                const int h = surfaceH(x, z);
                if (h == INT_MIN) continue;
                if (h < lo) { lo = h; loAt = {x, h, z}; }
                if (h > hi) { hi = h; hiAt = {x, h, z}; }
            }
        if (hi == INT_MIN) continue;   // no terrain sampled around this structure
        const int span = hi - lo;
        if (span > flatTol) {
            rep.addError("yard_not_flat",
                "structure '" + s.id + "' yard height spans " + std::to_string(span) + " cubes (" +
                std::to_string(lo) + " at (" + std::to_string(loAt.x) + "," + std::to_string(loAt.z) +
                ") .. " + std::to_string(hi) + " at (" + std::to_string(hiAt.x) + "," +
                std::to_string(hiAt.z) + ")) — a house yard should be graded flat (max span " +
                std::to_string(flatTol) + ")", s.id);
        }
    }
    return rep;
}

// V6 — fence corner overlap. At a corner (fence neighbours on both axes) the two runs should share one
// post; if each stamps a full picket section the corner has ~2x the Log micros of a straight cell.
// Self-calibrating: compares the corner to the average of its own straight neighbours.
ValidationReport RealizedWorldValidator::checkFenceCornerOverlaps(
    const std::vector<FencePost>& posts, double overlapFactor) {
    ValidationReport rep;
    std::map<std::pair<int, int>, FencePost> grid;
    for (const auto& p : posts) grid[{p.pos.x, p.pos.z}] = p;
    const int dirs[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (const auto& p : posts) {
        const int x = p.pos.x, z = p.pos.z;
        int nsum = 0, ncnt = 0;
        bool hasX = false, hasZ = false;
        for (const auto& d : dirs) {
            auto it = grid.find({x + d[0], z + d[1]});
            if (it == grid.end()) continue;
            nsum += it->second.logMicros;
            ++ncnt;
            if (d[0] != 0) hasX = true; else hasZ = true;
        }
        if (!(hasX && hasZ) || ncnt == 0) continue;   // straight run / isolated — not a corner
        const double avg = static_cast<double>(nsum) / ncnt;
        if (avg > 0 && p.logMicros > overlapFactor * avg) {
            rep.addError("fence_corner_overlap",
                "fence corner at (" + std::to_string(x) + "," + std::to_string(p.pos.y) + "," +
                std::to_string(z) + ") has " + std::to_string(p.logMicros) + " Log micros vs ~" +
                std::to_string(static_cast<int>(avg)) + " on the straight runs — the two runs overlap "
                "at the corner instead of sharing one post",
                "(" + std::to_string(x) + "," + std::to_string(z) + ")");
        }
    }
    return rep;
}

// V11 — chest facing. Clasp direction from rotation (engine rotateOffset: rot0=+Z, 90=-X, 180=-Z,
// 270=+X). A correctly placed chest backs onto its nearest wall (back faces the closest wall). Fires
// when the back does NOT face the nearest wall — the chest is turned the wrong way / opens at a wall.
ValidationReport RealizedWorldValidator::checkChestFacing(
    const std::vector<ChestPlacement>& chests, const WallAt& wallAt, int reach) {
    ValidationReport rep;
    if (!wallAt) return rep;
    // clasp (dx,dz) for rotation 0/90/180/270
    const int cd[4][2] = {{0, 1}, {-1, 0}, {0, -1}, {1, 0}};
    for (const auto& c : chests) {
        const int r = (((c.rotation % 360) + 360) % 360) / 90;
        const int bx = -cd[r][0], bz = -cd[r][1];   // back direction
        auto nearest = [&](int dx, int dz) -> int {
            for (int s = 1; s <= reach; ++s)
                if (wallAt(c.center.x + dx * s, c.center.y, c.center.z + dz * s)) return s;
            return 999;
        };
        const int dpx = nearest(1, 0), dmx = nearest(-1, 0), dpz = nearest(0, 1), dmz = nearest(0, -1);
        const int minD = std::min(std::min(dpx, dmx), std::min(dpz, dmz));
        if (minD > reach) continue;                  // no wall nearby — open placement, can't judge
        const int backD = nearest(bx, bz);
        if (backD > minD) {
            rep.addError("chest_facing_wrong",
                "chest '" + c.id + "' at (" + std::to_string(c.center.x) + "," +
                std::to_string(c.center.y) + "," + std::to_string(c.center.z) + ") rot " +
                std::to_string(c.rotation) + " does not back onto its nearest wall (back faces an open "
                "side; nearest wall is " + std::to_string(minD) + " away on another side) — the clasp "
                "opens the wrong way", c.id);
        }
    }
    return rep;
}

// Floor-flush — the building floor should sit level with the surrounding yard, so entry has no step.
ValidationReport RealizedWorldValidator::checkFloorFlush(
    const std::vector<FootprintScan>& structures, const SurfaceHeight& surfaceH, int flushTol) {
    ValidationReport rep;
    if (!surfaceH) return rep;
    for (const auto& s : structures) {
        const int floorY = s.min.y;                 // floor cube level (slab substructure)
        std::vector<int> ys;                        // yard terrain heights in the 1-ring outside the footprint
        for (int x = s.min.x - 1; x <= s.max.x + 1; ++x)
            for (int z = s.min.z - 1; z <= s.max.z + 1; ++z) {
                const bool inFoot = x >= s.min.x && x <= s.max.x && z >= s.min.z && z <= s.max.z;
                if (inFoot) continue;
                const int h = surfaceH(x, z);
                if (h != INT_MIN) ys.push_back(h);
            }
        if (ys.empty()) continue;                   // no terrain around it — can't judge
        // The WORST-deviating side is where you'd step (median washes out on a balanced slope). Flag the
        // biggest gap between the floor and any adjacent yard column.
        int worst = 0, worstYard = floorY;
        for (int h : ys) {
            const int d = std::abs(floorY - h);
            if (d > worst) { worst = d; worstYard = h; }
        }
        if (worst > flushTol) {
            rep.addError("floor_not_flush",
                "structure '" + s.id + "' floor (y=" + std::to_string(floorY) + ") is " +
                std::to_string(worst) + " cubes " + (floorY > worstYard ? "above" : "below") +
                " the yard (y=" + std::to_string(worstYard) + " on the worst side) — you step " +
                (floorY > worstYard ? "up" : "down") + " to enter; grade the structure+yard unit so the "
                "floor sits flush with the yard", s.id);
        }
    }
    return rep;
}

// V2 — fence along a terrain cliff. A fence post whose adjacent terrain steps by >= cliffTol cubes is
// hugging a cliff/rise; the terrain is already the barrier, so the fence is pointless there.
ValidationReport RealizedWorldValidator::checkFenceAgainstRise(
    const std::vector<FencePost>& posts, const SurfaceHeight& surfaceH, int cliffTol) {
    ValidationReport rep;
    if (!surfaceH) return rep;
    const int dirs[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (const auto& p : posts) {
        const int base = surfaceH(p.pos.x, p.pos.z);
        if (base == INT_MIN) continue;
        for (const auto& d : dirs) {
            const int ns = surfaceH(p.pos.x + d[0], p.pos.z + d[1]);
            if (ns == INT_MIN) continue;
            int step = ns - base;
            if (step < 0) step = -step;
            if (step >= cliffTol) {
                const bool rise = ns > base;
                rep.addError("fence_along_cliff",
                    "fence post at (" + std::to_string(p.pos.x) + "," + std::to_string(p.pos.y) + "," +
                    std::to_string(p.pos.z) + ") runs along a terrain cliff — adjacent ground " +
                    (rise ? "rises " : "drops ") + std::to_string(step) + " cubes (to " +
                    std::to_string(ns) + " from " + std::to_string(base) + "); the cliff is already the "
                    "barrier, so a fence here is pointless",
                    "(" + std::to_string(p.pos.x) + "," + std::to_string(p.pos.z) + ")");
                break;
            }
        }
    }
    return rep;
}

// V1 — fence floating over a path. A fence post with a Cobblestone (path) cell directly below it is
// straddling a path with no gate; the fence reads as floating over the path surface.
ValidationReport RealizedWorldValidator::checkFenceOverPath(
    const std::vector<FencePost>& posts, const CubeHasMaterial& hasMat, int depth) {
    ValidationReport rep;
    if (!hasMat) return rep;
    for (const auto& p : posts) {
        for (int dy = 1; dy <= depth; ++dy) {
            if (hasMat(p.pos.x, p.pos.y - dy, p.pos.z, "Cobblestone")) {
                rep.addError("fence_over_path",
                    "fence post at (" + std::to_string(p.pos.x) + "," + std::to_string(p.pos.y) + "," +
                    std::to_string(p.pos.z) + ") sits over a path (Cobblestone " + std::to_string(dy) +
                    " below) — a path crossing the fence line needs a gate/gap, not a fence over it",
                    "(" + std::to_string(p.pos.x) + "," + std::to_string(p.pos.z) + ")");
                break;
            }
        }
    }
    return rep;
}

// Inclusive integer-cube AABB intersection test.
static bool aabbOverlap(const PlacedBox& a, const PlacedBox& b) {
    return a.min.x <= b.max.x && a.max.x >= b.min.x &&
           a.min.y <= b.max.y && a.max.y >= b.min.y &&
           a.min.z <= b.max.z && a.max.z >= b.min.z;
}

// V5 — no fixture may intersect another fixture (or a hearth). O(n^2) over the fixture set (n is small
// per building); clutter is skipped since it sits ON furniture a cube up.
ValidationReport RealizedWorldValidator::checkFurnitureOverlaps(const std::vector<PlacedBox>& items) {
    ValidationReport rep;
    for (size_t i = 0; i < items.size(); ++i) {
        if (isClutter(items[i].type)) continue;
        for (size_t j = i + 1; j < items.size(); ++j) {
            if (isClutter(items[j].type)) continue;
            // Furniture overlap is a per-building concern: two adjacent buildings can legitimately
            // share a wall cell, so only compare fixtures owned by the SAME structure (when known).
            if (!items[i].parent.empty() && !items[j].parent.empty() &&
                items[i].parent != items[j].parent)
                continue;
            if (!aabbOverlap(items[i], items[j])) continue;
            const auto& A = items[i];
            const auto& B = items[j];
            const std::string where = A.id + " & " + B.id;
            if (isHearth(A.type) || isHearth(B.type)) {
                rep.addError("furniture_on_fireplace",
                    "fixture '" + A.id + "' (" + A.type + ") overlaps '" + B.id + "' (" + B.type +
                    ") — furniture must not intersect a hearth", where);
            } else {
                rep.addError("furniture_overlap",
                    "fixtures '" + A.id + "' (" + A.type + ") and '" + B.id + "' (" + B.type +
                    ") occupy overlapping space", where);
            }
        }
    }
    return rep;
}

} // namespace Core
} // namespace Phyxel
