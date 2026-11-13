#include "core/StreetPaver.h"

#include <algorithm>
#include <queue>
#include <cmath>
#include <unordered_map>

namespace Phyxel {
namespace Core {

namespace {
// floor-division micro -> cube (negative-safe, matches the build_settlement stamper)
inline int fl9(int v) { return v >= 0 ? v / 9 : -((-v + 8) / 9); }
inline long long colKey(int x, int z) {
    return (static_cast<long long>(x) << 32) ^ (z & 0xffffffffLL);
}
}  // namespace

// M0 row 2b: a failed spur says WHY, with its numbers (door, street junction, run length,
// drop, grade cap) - never a bare "too steep" count.
static std::string spurFailureLine(const glm::ivec3& door, const glm::ivec3& street, long manhattan,
                                   const AgentBox& box, const std::string& reason) {
    const int cap = std::max(1, box.maxStepUpMicro / std::max(1, box.halfWidthMicro));
    return "door (" + std::to_string(door.x) + "," + std::to_string(door.z) + " y" +
           std::to_string(door.y) + ") -> street (" + std::to_string(street.x) + "," +
           std::to_string(street.z) + " y" + std::to_string(street.y) + "): " +
           std::to_string(manhattan + 1) + " cells, drop " +
           std::to_string(std::abs(door.y - street.y)) + " micro, cap " + std::to_string(cap) +
           " micro/cell: " + reason;
}

PavingPlan planStreetPaving(const std::vector<Rect>& streets, glm::ivec2 originCubes,
                            const std::vector<DoorAnchor>& doors,
                            const std::vector<glm::ivec4>& buildingFootprints,
                            const std::function<int(int, int)>& groundMicroAt,
                            const AgentBox& box, const std::string& material) {
    PavingPlan plan;
    plan.material = material;
    if (streets.empty()) return plan;

    // V7: never pave a building footprint INTERIOR (inset 1 cube — a spur meeting the door at the
    // perimeter is fine). Footprint packing matches build_settlement: (x=bx, y=bz, z=bw, w=bd).
    auto insideFootprintInterior = [&](int mx, int mz) {
        const int cbx = fl9(mx), cbz = fl9(mz);
        for (const auto& f : buildingFootprints)
            if (cbx >= f.x + 1 && cbx <= f.x + f.z - 2 && cbz >= f.y + 1 && cbz <= f.y + f.w - 2)
                return true;
        return false;
    };

    // Column map: FIRST writer wins (streets are inserted before spurs, so at a junction the
    // street's graded surface dominates and the spur MEETS it).
    std::unordered_map<long long, int> cols;

    // --- Streets: grade the centerline along the long axis, broadcast LEVEL across the width ---
    for (const auto& s : streets) {
        const int wx0 = (originCubes.x + s.x) * 9, wz0 = (originCubes.y + s.z) * 9;
        const int wx1 = wx0 + s.w * 9, wz1 = wz0 + s.d * 9;  // exclusive
        if (s.w <= 0 || s.d <= 0) continue;
        const bool alongX = s.w >= s.d;
        const int cv = alongX ? (wz0 + wz1) / 2 : (wx0 + wx1) / 2;  // centerline micro row
        glm::ivec3 a, b;
        if (alongX) {
            a = {wx0, groundMicroAt(wx0, cv), cv};
            b = {wx1 - 1, groundMicroAt(wx1 - 1, cv), cv};
        } else {
            a = {cv, groundMicroAt(cv, wz0), wz0};
            b = {cv, groundMicroAt(cv, wz1 - 1), wz1 - 1};
        }
        const PathPlan center = planTerrainPath(groundMicroAt, a, b, box);
        if (!center.ok) continue;  // street too steep end-to-end (surfaced via missing coverage)
        for (const auto& c : center.cells) {
            if (alongX)
                for (int z = wz0; z < wz1; ++z) cols.emplace(colKey(c.x, z), c.surfaceY);
            else
                for (int x = wx0; x < wx1; ++x) cols.emplace(colKey(x, c.z), c.surfaceY);
        }
    }

    // --- Door spurs: door -> nearest street column, meeting the STREET's planned surface ---
    for (const auto& d : doors) {
        // nearest point on any street rect (world micro), Manhattan
        long bestDist = -1;
        int gx = 0, gz = 0;
        for (const auto& s : streets) {
            const int wx0 = (originCubes.x + s.x) * 9, wz0 = (originCubes.y + s.z) * 9;
            const int wx1 = wx0 + s.w * 9 - 1, wz1 = wz0 + s.d * 9 - 1;  // inclusive
            const int px = std::clamp(d.x, wx0, wx1), pz = std::clamp(d.z, wz0, wz1);
            const long dist = std::abs(px - d.x) + std::abs(pz - d.z);
            if (bestDist < 0 || dist < bestDist) { bestDist = dist; gx = px; gz = pz; }
        }
        if (bestDist < 0) continue;
        // A door AT (or hard against) the street is trivially connected — an urban setback-0 row
        // house opens straight onto the paving; a zero-length "spur" is not a failure (live find:
        // 11/21 flush town doors read as "too steep" on flat ground).
        if (bestDist <= box.halfWidthMicro + 1 || cols.count(colKey(d.x, d.z))) {
            cols.emplace(colKey(d.x, d.z), groundMicroAt(d.x, d.z));   // pave the threshold cell
            ++plan.spursPlanned;
            continue;
        }
        auto it = cols.find(colKey(gx, gz));
        const int gS = (it != cols.end()) ? it->second : groundMicroAt(gx, gz);
        const PathPlan spur =
            planTerrainPath(groundMicroAt, {d.x, d.surfaceY, d.z}, {gx, gS, gz}, box);
        if (!spur.ok) {
            ++plan.spursFailed;
            plan.spurFailures.push_back(spurFailureLine({d.x, d.surfaceY, d.z}, {gx, gS, gz},
                                                        bestDist, box, spur.reason));
            continue;
        }
        ++plan.spursPlanned;
        // carve the spur box.halfWidthMicro wide, perpendicular to travel (the proven ribbon shape)
        const auto& cs = spur.cells;
        for (size_t i = 0; i < cs.size(); ++i) {
            bool tX = false, tZ = false;
            if (i + 1 < cs.size()) { tX |= cs[i + 1].x != cs[i].x; tZ |= cs[i + 1].z != cs[i].z; }
            if (i > 0)             { tX |= cs[i].x != cs[i - 1].x; tZ |= cs[i].z != cs[i - 1].z; }
            if (!tX && !tZ) tX = true;
            for (int o = -box.halfWidthMicro; o <= box.halfWidthMicro; ++o) {
                const int cx = cs[i].x + (tZ ? o : 0), cz = cs[i].z + (tX ? o : 0);
                cols.emplace(colKey(cx, cz), cs[i].surfaceY);
            }
        }
    }

    // --- Classify + order (deterministic: sorted by z, then x) ---
    // CUT columns are INCLUDED (pc.cut=true): the applier removes the terrain cubes whose top face
    // exceeds `surface` (cubes >= surface/9), then paves [base .. surface] — closing the old
    // stamper's cut_cells_unpaved gap (unwalkable terrain bulges at hill transitions).
    std::vector<PavedColumn> out;
    out.reserve(cols.size());
    for (const auto& [k, S] : cols) {
        const int x = static_cast<int>(k >> 32);
        const int z = static_cast<int>(static_cast<int32_t>(k & 0xffffffffLL));
        if (insideFootprintInterior(x, z)) continue;
        const int terr = groundMicroAt(x, z);
        PavedColumn pc;
        pc.x = x; pc.z = z; pc.surface = S;
        if (S > terr) { ++plan.fillCols; }
        else if (S == terr) { ++plan.levelCols; }
        else { pc.cut = true; ++plan.cutCols; }
        out.push_back(pc);
    }
    std::sort(out.begin(), out.end(), [](const PavedColumn& a, const PavedColumn& b) {
        if (a.z != b.z) return a.z < b.z;
        return a.x < b.x;
    });
    plan.columns = std::move(out);
    plan.ok = !plan.columns.empty();
    return plan;
}

PavingPlan planWayPaving(const std::vector<WayRibbon>& ways,
                         const std::function<int(int, int)>& groundMicroAt,
                         const AgentBox& box, const std::string& material) {
    PavingPlan plan;
    plan.material = material;
    if (!groundMicroAt) return plan;
    std::unordered_map<long long, int> cols;   // first writer wins (a junction keeps the first way's surface)
    for (const auto& w : ways) {
        if (w.cells.size() < 2 || w.widthCubes <= 0) continue;
        const float r = 0.5f * static_cast<float>(w.widthCubes) * 9.0f;   // the ribbon's half-width (micro)
        for (size_t k = 0; k + 1 < w.cells.size(); ++k) {
            // the centre line of this leg, graded over the terrain between the node centres (both nodes
            // pinned at their own ground, so consecutive legs of one way meet at the shared node's grade)
            const glm::ivec2 ca = w.cells[k], cb = w.cells[k + 1];
            const glm::ivec3 a{ca.x * 9 + 4, groundMicroAt(ca.x * 9 + 4, ca.y * 9 + 4), ca.y * 9 + 4};
            const glm::ivec3 b{cb.x * 9 + 4, groundMicroAt(cb.x * 9 + 4, cb.y * 9 + 4), cb.y * 9 + 4};
            if (a.x == b.x && a.z == b.z) continue;
            const PathPlan centre = planTerrainPath(groundMicroAt, a, b, box);
            if (!centre.ok || centre.cells.empty()) continue;   // too steep end to end: surfaced as missing coverage
            // every micro column within r of the leg (a capsule: the segment plus a disc at each node) takes the
            // surface of the centre-line cell it projects onto - LEVEL across the width, graded along it
            const glm::vec2 pa(static_cast<float>(a.x), static_cast<float>(a.z)), pb(static_cast<float>(b.x), static_cast<float>(b.z));
            const glm::vec2 ab = pb - pa;
            const float ab2 = glm::dot(ab, ab);
            const int x0 = static_cast<int>(std::floor(std::min(pa.x, pb.x) - r)), x1 = static_cast<int>(std::ceil(std::max(pa.x, pb.x) + r));
            const int z0 = static_cast<int>(std::floor(std::min(pa.y, pb.y) - r)), z1 = static_cast<int>(std::ceil(std::max(pa.y, pb.y) + r));
            const int last = static_cast<int>(centre.cells.size()) - 1;
            for (int z = z0; z <= z1; ++z)
                for (int x = x0; x <= x1; ++x) {
                    const glm::vec2 pt(static_cast<float>(x), static_cast<float>(z));
                    const float t = ab2 > 0.0f ? std::clamp(glm::dot(pt - pa, ab) / ab2, 0.0f, 1.0f) : 0.0f;
                    const glm::vec2 q = pa + ab * t;
                    if (glm::length(pt - q) > r) continue;
                    const int i = std::clamp(static_cast<int>(std::lround(t * static_cast<float>(last))), 0, last);
                    cols.emplace(colKey(x, z), centre.cells[static_cast<size_t>(i)].surfaceY);
                }
        }
    }
    // chain 89 (M5b-1): at a BEND two legs' capsules overlap and the first writer's surface meets the other leg's
    // at a seam - 6 adjacent columns differed by more than the step-up where leg 1 was still descending the
    // ridge's cut and leg 2 started at the node's grade. The surface is therefore relaxed into the 2-D
    // slope-limited LOWER envelope over the column set (S'(c) = min over c' of S(c') + cap x dist4(c, c'), the
    // 1-D rule of planTerrainPath in two dimensions; a Dijkstra with the cap per micro step): every 4-neighbour
    // step is at most the cap (<= the step-up), nothing rises, a seam becomes a shallow cut.
    {
        const int cap = std::max(1, box.maxStepUpMicro / std::max(1, box.halfWidthMicro));
        using QE = std::pair<int, long long>;
        std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
        for (const auto& [k, S] : cols) open.push({S, k});
        static const int NB[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        while (!open.empty()) {
            const auto [S, k] = open.top(); open.pop();
            auto it = cols.find(k);
            if (it == cols.end() || S > it->second) continue;
            const int x = static_cast<int>(k >> 32), z = static_cast<int>(static_cast<int32_t>(k & 0xffffffffLL));
            for (const auto& nb : NB) {
                auto jt = cols.find(colKey(x + nb[0], z + nb[1]));
                if (jt == cols.end()) continue;
                if (S + cap < jt->second) { jt->second = S + cap; open.push({jt->second, jt->first}); }
            }
        }
    }
    std::vector<PavedColumn> out;
    out.reserve(cols.size());
    for (const auto& [k, S] : cols) {
        const int x = static_cast<int>(k >> 32);
        const int z = static_cast<int>(static_cast<int32_t>(k & 0xffffffffLL));
        const int terr = groundMicroAt(x, z);
        PavedColumn pc;
        pc.x = x; pc.z = z; pc.surface = S;
        if (S > terr) { ++plan.fillCols; }
        else if (S == terr) { ++plan.levelCols; }
        else { pc.cut = true; ++plan.cutCols; }
        out.push_back(pc);
    }
    std::sort(out.begin(), out.end(), [](const PavedColumn& a, const PavedColumn& b) {
        if (a.z != b.z) return a.z < b.z;
        return a.x < b.x;
    });
    plan.columns = std::move(out);
    plan.ok = !plan.columns.empty();
    return plan;
}

PavingPlan planDoorSpurs(const std::vector<DoorAnchor>& doors,
                         const std::vector<glm::ivec4>& buildingFootprints,
                         const std::vector<glm::ivec4>& streetColumns,
                         const std::function<int(int, int)>& groundMicroAt,
                         const AgentBox& box, const std::string& material) {
    PavingPlan plan;
    plan.material = material;
    if (doors.empty() || streetColumns.empty()) return plan;

    // M5b-1 (chain 103): a spur never paves a cube INSIDE a building's footprint, walls included - the applier clears
    // the whole cube column of every paved micro column, and the spur starts at the cell OUTSIDE the door anyway.
    auto insideFootprintInterior = [&](int mx, int mz) {
        const int cbx = fl9(mx), cbz = fl9(mz);
        for (const auto& f : buildingFootprints)
            if (cbx >= f.x && cbx <= f.x + f.z - 1 && cbz >= f.y && cbz <= f.y + f.w - 1)
                return true;
        return false;
    };

    // The street's planned surfaces (already stamped): the spur MEETS these, never re-emits them.
    std::unordered_map<long long, int> street;
    street.reserve(streetColumns.size());
    for (const auto& c : streetColumns) street.emplace(colKey(c.x, c.z), c.w);   // (x, startRow, z, surface)

    std::unordered_map<long long, int> cols;   // spur columns only
    for (const auto& d : doors) {
        // nearest street column, Manhattan (the street is a dense set of columns, so the nearest
        // column IS the nearest point of the paving)
        long bestDist = -1;
        int gx = 0, gz = 0, gS = 0;
        for (const auto& c : streetColumns) {
            const long dist = std::abs(c.x - d.x) + std::abs(c.z - d.z);
            if (bestDist < 0 || dist < bestDist) { bestDist = dist; gx = c.x; gz = c.z; gS = c.w; }
        }
        if (bestDist < 0) continue;
        if (bestDist <= box.halfWidthMicro + 1 || street.count(colKey(d.x, d.z))) {
            if (!street.count(colKey(d.x, d.z)))
                cols.emplace(colKey(d.x, d.z), groundMicroAt(d.x, d.z));   // pave the threshold cell
            ++plan.spursPlanned;
            continue;
        }
        const PathPlan spur =
            planTerrainPath(groundMicroAt, {d.x, d.surfaceY, d.z}, {gx, gS, gz}, box);
        if (!spur.ok) {
            ++plan.spursFailed;
            plan.spurFailures.push_back(spurFailureLine({d.x, d.surfaceY, d.z}, {gx, gS, gz},
                                                        bestDist, box, spur.reason));
            continue;
        }
        ++plan.spursPlanned;
        const auto& cs = spur.cells;
        for (size_t i = 0; i < cs.size(); ++i) {
            bool tX = false, tZ = false;
            if (i + 1 < cs.size()) { tX |= cs[i + 1].x != cs[i].x; tZ |= cs[i + 1].z != cs[i].z; }
            if (i > 0)             { tX |= cs[i].x != cs[i - 1].x; tZ |= cs[i].z != cs[i - 1].z; }
            if (!tX && !tZ) tX = true;
            for (int o = -box.halfWidthMicro; o <= box.halfWidthMicro; ++o) {
                const int cx = cs[i].x + (tZ ? o : 0), cz = cs[i].z + (tX ? o : 0);
                if (street.count(colKey(cx, cz))) continue;   // first writer wins: the street
                cols.emplace(colKey(cx, cz), cs[i].surfaceY);
            }
        }
    }

    std::vector<PavedColumn> out;
    out.reserve(cols.size());
    for (const auto& [k, S] : cols) {
        const int x = static_cast<int>(k >> 32);
        const int z = static_cast<int>(static_cast<int32_t>(k & 0xffffffffLL));
        if (insideFootprintInterior(x, z)) continue;
        const int terr = groundMicroAt(x, z);
        PavedColumn pc;
        pc.x = x; pc.z = z; pc.surface = S;
        if (S > terr) { ++plan.fillCols; }
        else if (S == terr) { ++plan.levelCols; }
        else { pc.cut = true; ++plan.cutCols; }
        out.push_back(pc);
    }
    std::sort(out.begin(), out.end(), [](const PavedColumn& a, const PavedColumn& b) {
        if (a.z != b.z) return a.z < b.z;
        return a.x < b.x;
    });
    plan.columns = std::move(out);
    plan.ok = plan.spursPlanned > 0;   // a plan with only trivially-connected doors is still OK
    return plan;
}

std::vector<int> planDoorstepFills(int thresholdFeet, const std::vector<int>& groundFeet, int stepUpMicro) {
    // M5b-1 chain 98: the standing level descends at most `stepUp` per cell FROM THE PREVIOUS CELL'S LEVEL, the
    // threshold first. A cell whose ground offers that level (or more) takes its ground as the level - no fill - and the
    // walk goes on outward: the face can lie beyond a level apron (the hall house of chain 98: apron 901 one cell out,
    // the street at 892 behind it; the rule as it stood ended at the apron and left a 9-micro riser). A cell whose
    // ground is lower is raised to (previous level - stepUp).
    std::vector<int> fills(groundFeet.size(), INT_MIN);
    int level = thresholdFeet;
    for (size_t k = 0; k < groundFeet.size(); ++k) {
        const int feetOutside = groundFeet[k];
        if (feetOutside == INT_MIN) break;
        if (feetOutside >= level - stepUpMicro) { level = feetOutside; continue; }
        fills[k] = level - stepUpMicro;
        level = fills[k];
    }
    return fills;
}

int padCubeForStreet(int wayFeetMicro, int thresholdAbovePadMicro, int stepUpMicro) {
    // threshold = padCube * 9 + thresholdAbovePad >= wayFeet - 1: the smallest such pad (ceil division, negative-safe).
    // stepUp is the doorsteps' business from here (T - W in [-1, 7] <= one cell of stoop plus a sill).
    (void)stepUpMicro;
    const int num = wayFeetMicro - 1 - thresholdAbovePadMicro;
    return (num >= 0) ? (num + 8) / 9 : -((-num) / 9);
}

std::vector<int> planDoorstepLevels(int thresholdFeet, const std::vector<int>& groundFeet, const std::vector<bool>& paved,
                                    int stepUpMicro) {
    std::vector<int> target(groundFeet.size(), INT_MIN);
    int kw = -1;
    for (size_t i = 0; i < groundFeet.size(); ++i) {
        if (groundFeet[i] == INT_MIN) break;
        if (i < paved.size() && paved[i]) { kw = static_cast<int>(i); break; }
    }
    // chain 107: anchor or none, every unpaved cell takes clamp(aim, level - step, level) - never above the previous
    // level (the lintel: a cell above the threshold is CUT), never more than a step below it (FILLED); the aim is
    // the way's level when a way is in reach, the cell's own ground otherwise (the descent rule, cuts included)
    const int last = (kw >= 0) ? kw : static_cast<int>(groundFeet.size());
    int level = thresholdFeet;
    for (int i = 0; i < last; ++i) {
        const int g = groundFeet[static_cast<size_t>(i)];
        if (g == INT_MIN) break;
        const int aim = (kw >= 0) ? groundFeet[static_cast<size_t>(kw)] : g;
        const int desired = std::clamp(aim, level - stepUpMicro, level);
        if (g != desired) target[static_cast<size_t>(i)] = desired;
        level = desired;
    }
    // the riser onto the way itself: within a step, or ONE sill on the way's edge cell when that brings the way within a
    // step (chain 109: an 18-micro "sill" rose on a way edge 22 below the door - a wall, not a step; left as found and
    // reported instead)
    if (kw >= 0) {
        const int drop = level - groundFeet[static_cast<size_t>(kw)];
        if (drop > stepUpMicro && drop <= 2 * stepUpMicro) target[static_cast<size_t>(kw)] = level - stepUpMicro;
    }
    return target;
}

SweepPlan planStreetSweep(const std::vector<glm::ivec3>& band, const std::vector<glm::ivec4>& footprints,
                          const std::vector<std::pair<glm::ivec3, glm::ivec3>>& objectBoxes) {
    SweepPlan plan;
    plan.cells.reserve(band.size());
    for (const auto& c : band) {
        bool inFoot = false;
        for (const auto& f : footprints)
            if (c.x >= f.x && c.x <= f.x + f.z - 1 && c.z >= f.y && c.z <= f.y + f.w - 1) { inFoot = true; break; }
        if (inFoot) { ++plan.keptForBuildings; continue; }
        bool inObject = false;
        for (const auto& b : objectBoxes)
            if (c.x >= b.first.x && c.x <= b.second.x && c.y >= b.first.y && c.y <= b.second.y &&
                c.z >= b.first.z && c.z <= b.second.z) { inObject = true; break; }
        if (inObject) { ++plan.keptForObjects; continue; }
        plan.cells.push_back(c);
    }
    return plan;
}

}  // namespace Core
}  // namespace Phyxel
