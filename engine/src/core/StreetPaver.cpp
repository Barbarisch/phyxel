#include "core/StreetPaver.h"

#include <algorithm>
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
        if (!spur.ok) { ++plan.spursFailed; continue; }
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

}  // namespace Core
}  // namespace Phyxel
