#include "core/StructureRealizer.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <map>
#include <set>
#include <utility>

namespace Phyxel {
namespace Core {

namespace {

using Cell = std::pair<int, int>;   // (cx, cz)

// Shared-wall segment between two rooms: (axis 'x'|'z', coord, lo, hi). Empty hi==lo if none.
struct Seg { char axis = 0; int coord = 0, lo = 0, hi = 0; bool ok = false; };

Seg sharedWall(const Rect& a, const Rect& b) {
    Seg s;
    if (a.x1() == b.x || b.x1() == a.x) {
        int lo = std::max(a.z, b.z), hi = std::min(a.z1(), b.z1());
        if (hi - lo > 0) { s = {'x', (a.x1() == b.x) ? a.x1() : b.x1(), lo, hi, true}; return s; }
    }
    if (a.z1() == b.z || b.z1() == a.z) {
        int lo = std::max(a.x, b.x), hi = std::min(a.x1(), b.x1());
        if (hi - lo > 0) { s = {'z', (a.z1() == b.z) ? a.z1() : b.z1(), lo, hi, true}; return s; }
    }
    return s;
}

} // namespace

StructureResult StructureRealizer::toStructureResult(const ShellResult& shell,
                                                     const glm::ivec3& worldOrigin) {
    StructureResult out;
    for (const auto& v : shell.canvas.exportVoxels()) {
        VoxelPlacement vp;
        vp.position = worldOrigin + v.cube;
        vp.material = v.material;
        switch (v.res) {
            case CanvasRes::Cube:
                vp.level = VoxelLevel::Cube;
                break;
            case CanvasRes::Subcube:
                vp.level = VoxelLevel::Subcube;
                vp.subcubePos = v.sub;
                break;
            case CanvasRes::Microcube:
                vp.level = VoxelLevel::Microcube;
                vp.subcubePos = v.sub;
                vp.microcubePos = v.micro;
                break;
        }
        out.voxels.push_back(vp);
    }
    return out;
}

int StructureRealizer::thicknessMicro(double cubes) {
    int t = static_cast<int>(std::lround(cubes * 9.0));
    return std::max(1, std::min(t, 9));
}

StructureRealizer::ShellResult StructureRealizer::realizeShell(const BuildingProgram& program,
                                                               const StyleProfile& style) {
    ShellResult res;
    if (program.stories.empty()) { res.error = "no stories"; return res; }

    const ProgStory& story = program.stories[0];     // P1: single story
    MicroCanvas& c = res.canvas;
    AssemblyPlan& plan = res.plan;

    // ---- materials + thicknesses from the style ----
    const std::string matFloor = style.materialOf("floor", "Wood");
    const std::string matExt   = style.materialOf("structure", "Wood");
    const std::string matInt   = style.materialOf("structure", "Wood");
    const std::string matFound = style.materialOf("foundation", "Stone");
    const std::string matRoof  = style.materialOf("roof", "Wood");
    const std::string matCeil  = style.materialOf("structure", "Wood");

    const int extT   = thicknessMicro(style.thicknessOf("exterior_wall", 0.333));
    const int intT   = thicknessMicro(style.thicknessOf("interior_wall", 0.222));
    const int foundT = thicknessMicro(style.thicknessOf("foundation_wall", 0.667));
    const int floorT = thicknessMicro(style.thicknessOf("floor", 0.333));
    const int ceilT  = thicknessMicro(style.thicknessOf("ceiling", 0.222));

    // ---- pass 0/1: substructure level ----
    const int crawlH = (program.substructure == "crawlspace") ? 1
                     : (program.substructure == "basement")   ? 3 : 0;   // basement body deferred to P5
    res.crawlHeightCubes = crawlH;

    // ---- footprint = union of room rects; + per-room map ----
    std::set<Cell> footprint;
    std::map<std::string, Rect> rooms;
    int bx0 = INT_MAX, bz0 = INT_MAX, bx1 = INT_MIN, bz1 = INT_MIN;
    for (const auto& rm : story.rooms) {
        rooms[rm.id] = rm.rect;
        for (int x = rm.rect.x; x < rm.rect.x1(); ++x)
            for (int z = rm.rect.z; z < rm.rect.z1(); ++z) footprint.insert({x, z});
        bx0 = std::min(bx0, rm.rect.x); bz0 = std::min(bz0, rm.rect.z);
        bx1 = std::max(bx1, rm.rect.x1()); bz1 = std::max(bz1, rm.rect.z1());
    }
    if (footprint.empty()) { res.error = "empty footprint"; return res; }
    auto inFoot = [&](int x, int z) { return footprint.count({x, z}) > 0; };

    // ---- vertical layout (micro): the GROUND story's floor ----
    const int floorBaseMicro = crawlH * 9;
    const int floorTopMicro  = floorBaseMicro + floorT;     // ground walkable surface
    res.floorTopMicro = floorTopMicro;

    // ---- pass 1: foundation walls (perimeter, crawlspace) ----
    const Cell dirs[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    auto paintEdgeBand = [&](int cx, int cz, const Cell& d, int t, int y0, int y1,
                             const std::string& mat) {
        int gx0 = cx * 9, gz0 = cz * 9;
        int x0 = gx0, x1 = gx0 + 9, z0 = gz0, z1 = gz0 + 9;
        if (d.first == 1)  x0 = gx0 + 9 - t;
        if (d.first == -1) x1 = gx0 + t;
        if (d.second == 1) z0 = gz0 + 9 - t;
        if (d.second == -1) z1 = gz0 + t;
        c.fillMicroBox(x0, y0, z0, x1 - x0, y1 - y0, z1 - z0, mat);
    };

    if (crawlH > 0) {
        for (const auto& [cx, cz] : footprint)
            for (const auto& d : dirs)
                if (!inFoot(cx + d.first, cz + d.second))
                    paintEdgeBand(cx, cz, d, foundT, 0, floorBaseMicro, matFound);
    }
    // record foundation columns (perimeter) for the plan
    for (const auto& [cx, cz] : footprint) {
        bool perim = false;
        for (const auto& d : dirs) if (!inFoot(cx + d.first, cz + d.second)) perim = true;
        if (perim) plan.foundation.push_back({cx, cz, 0, crawlH, matFound});
    }

    // ---- stack_stories (#36): build FLOOR + WALLS + OPENINGS for EVERY story,
    // stacked up a running base micro-Y. Each story's floor sits directly on the
    // wall-top of the one below, so an intermediate floor IS the ceiling of the
    // story beneath it; only the TOP story gets a separate ceiling slab (pass 4).
    // Exterior walls use the shared building footprint; partitions/openings use
    // each story's own rooms/portals (so upper floors can be laid out differently).
    const int W = program.footprintW > 0 ? program.footprintW : bx1;
    const int D = program.footprintD > 0 ? program.footprintD : bz1;
    int base = floorBaseMicro;                 // this story's floor-bottom micro-Y
    std::vector<int> floorBaseByStory, floorTopByStory;   // micro-Ys, for place_stairs (#12)
    for (size_t si = 0; si < program.stories.size(); ++si) {
        const ProgStory& st = program.stories[si];
        const int fBase = base;
        const int wBase = fBase + floorT;      // this story's walkable surface
        const int wTop  = wBase + st.height * 9;
        const int yCubes = fBase / 9;
        floorBaseByStory.push_back(fBase);
        floorTopByStory.push_back(wBase);

        // finish floor over the footprint
        for (const auto& [cx, cz] : footprint)
            c.fillMicroBox(cx * 9, fBase, cz * 9, 9, floorT, 9, matFloor);
        if (si == 0)
            plan.floors.push_back({bx0, bz0, bx1 - bx0, bz1 - bz0, yCubes,
                                   style.thicknessOf("floor", 0.333), matFloor, "floor"});

        // exterior walls (perimeter edges of the shared footprint)
        for (const auto& [cx, cz] : footprint)
            for (const auto& d : dirs)
                if (!inFoot(cx + d.first, cz + d.second)) {
                    paintEdgeBand(cx, cz, d, extT, wBase, wTop, matExt);
                    plan.walls.push_back({cx, cz, cx + d.first, cz + d.second, yCubes, st.height,
                                          style.thicknessOf("exterior_wall", 0.333), matExt, "exterior"});
                }

        // interior partitions on this story's shared room walls (thin straddling slab)
        std::map<std::string, Rect> srooms;
        std::vector<std::string> ids;
        for (const auto& rm : st.rooms) { srooms[rm.id] = rm.rect; ids.push_back(rm.id); }
        for (size_t i = 0; i < ids.size(); ++i)
            for (size_t j = i + 1; j < ids.size(); ++j) {
                Seg s = sharedWall(srooms[ids[i]], srooms[ids[j]]);
                if (!s.ok) continue;
                int half = intT / 2;
                if (s.axis == 'x') {                   // wall runs along Z at x = coord
                    int gx = s.coord * 9 - half;
                    c.fillMicroBox(gx, wBase, s.lo * 9, intT, wTop - wBase, (s.hi - s.lo) * 9, matInt);
                } else {                               // wall runs along X at z = coord
                    int gz = s.coord * 9 - half;
                    c.fillMicroBox(s.lo * 9, wBase, gz, (s.hi - s.lo) * 9, wTop - wBase, intT, matInt);
                }
                plan.walls.push_back({0, 0, 0, 0, yCubes, st.height,
                                      style.thicknessOf("interior_wall", 0.222), matInt, "interior"});
            }

        // carve this story's door/window/arch openings through its wall band
        for (const auto& p : st.portals) {
            if (p.kind == "window" && p.height <= 0) continue;
            bool ext = (p.a == "exterior" || p.b == "exterior");
            int sill = (p.kind == "window") ? 1 : 0;       // windows sit on a ~1-cube sill
            int oyBase = wBase + sill * 9;
            int oyTop  = std::min(wTop, oyBase + p.height * 9);
            bool alongZ = (p.px == 0 || p.px == W);        // wall faces +/-x -> opening runs in Z
            for (int k = 0; k < std::max(1, p.width); ++k) {
                int cx = alongZ ? p.px - (p.px == W ? 1 : 0) : p.px + k;
                int cz = alongZ ? p.pz + k : p.pz - (p.pz == D ? 1 : 0);
                if (!ext) { cx = p.px + (alongZ ? 0 : k); cz = p.pz + (alongZ ? k : 0); }
                c.fillMicroBox(cx * 9, oyBase, cz * 9, 9, oyTop - oyBase, 9, "");   // carve to air
            }
            plan.openings.push_back({p.px, yCubes + sill, p.pz, p.width, p.height, 1, p.kind, "open"});
        }

        base = wTop;                               // next story's floor sits on this wall top
    }

    // Top-of-stack values for the (single) ceiling slab + the roof. The old single-
    // story names are reused so the ceiling/roof passes below are unchanged.
    const int wallTopMicro = base;
    const int ceilTopMicro = wallTopMicro + ceilT;

    // ---- place_stairs (#12): realize each ProgStair (previously parsed but NEVER built).
    // Cut the stairwell through the upper story's floor slab, then build a solid stepped
    // flight climbing from the lower walkable to the upper one (emerges through the hole).
    // Adjacent stories only for this slice; steeper-than-comfort risers are a grounding
    // follow-up, the geometry + reachability is the point.
    {
        const int nStory = static_cast<int>(floorTopByStory.size());
        std::set<long long> seenStairs;
        for (const auto& st : program.stories)
            for (const auto& sr : st.stairs) {
                int a = sr.fromStory, b = sr.toStory;
                if (a > b) std::swap(a, b);
                if (a < 0 || b >= nStory || b != a + 1) continue;      // adjacent only
                const Rect& rc = sr.rect;
                if (rc.w < 1 || rc.d < 1) continue;
                long long key = ((long long)a << 48) | ((long long)b << 32) |
                                ((long long)(rc.x + 4096) << 16) | (long long)(rc.z + 4096);
                if (!seenStairs.insert(key).second) continue;

                const int botMicro  = floorTopByStory[a];   // lower walkable surface
                const int topMicro  = floorTopByStory[b];   // upper walkable surface
                const int holeBase  = floorBaseByStory[b];  // bottom of the upper floor slab

                // (1) cut the stairwell hole through the upper story's floor slab
                for (int x = rc.x; x < rc.x1(); ++x)
                    for (int z = rc.z; z < rc.z1(); ++z)
                        c.fillMicroBox(x * 9, holeBase, z * 9, 9, topMicro - holeBase, 9, "");

                // (2) build a solid stepped flight along the rect's longer axis
                const bool runZ   = rc.d >= rc.w;
                const int  runLen = runZ ? rc.d : rc.w;
                const int  rise   = topMicro - botMicro;
                const int  step   = std::max(1, (rise + runLen - 1) / runLen);   // ceil
                for (int i = 0; i < runLen; ++i) {
                    int h = std::min(topMicro, botMicro + (i + 1) * step);
                    if (runZ)
                        for (int x = rc.x; x < rc.x1(); ++x)
                            c.fillMicroBox(x * 9, botMicro, (rc.z + i) * 9, 9, h - botMicro, 9, matFloor);
                    else
                        for (int z = rc.z; z < rc.z1(); ++z)
                            c.fillMicroBox((rc.x + i) * 9, botMicro, z * 9, 9, h - botMicro, 9, matFloor);
                }
            }
    }

    // ---- pass 4: ceiling slab over the footprint ----
    for (const auto& [cx, cz] : footprint)
        c.fillMicroBox(cx * 9, wallTopMicro, cz * 9, 9, ceilT, 9, matCeil);

    // ---- pass 5: gable roof over the bounding rectangle ----
    const int W_c = bx1 - bx0, D_c = bz1 - bz0;
    const bool rectangular = (int)footprint.size() == W_c * D_c;
    const std::string roofStyle = !program.roofStyle.empty() ? program.roofStyle : style.roofStyle;
    int eaveSub = (ceilTopMicro + 2) / 3;                 // subcube row at/above the ceiling top
    if (rectangular && roofStyle == "gable" && W_c >= 2 && D_c >= 2) {
        // Honor the GROUNDED roof pitch from the style (degrees). The gable rises `pitch`
        // subcubes per cube of run toward the ridge, so roof angle = atan(pitch/3): the
        // grounded subcube pitch = round(3*tan(deg)). thatch 50deg -> 4 (~53deg); tile 40deg
        // -> 3 (~45deg). Absent pitch_deg falls back to the legacy ~34deg (pitch 2).
        constexpr double PI = 3.14159265358979323846;
        const double pitchDeg = style.roofOf("pitch_deg", 0.0);
        const int pitch = pitchDeg > 0.0
            ? std::max(1, (int)std::lround(3.0 * std::tan(pitchDeg * PI / 180.0)))
            : 2;
        const int shell = pitch + 1;
        const bool slopeInZ = W_c >= D_c;                 // ridge along the longer axis
        const int span = slopeInZ ? D_c : W_c;
        const int perp = slopeInZ ? W_c : D_c;
        // The sloped roof slab is a full-cross-section subcube layer (solid roof mass).
        auto roofLayer = [&](int a, int b, int syAbs, const std::string& mat) {
            int cx = slopeInZ ? bx0 + a : bx0 + b;
            int cz = slopeInZ ? bz0 + b : bz0 + a;
            int cy = syAbs / 3, subY = syAbs % 3;
            for (int sx = 0; sx < 3; ++sx)
                for (int sz = 0; sz < 3; ++sz) c.addSubcube(cx, cy, cz, sx, subY, sz, mat);
        };
        // The gable-end triangle is a THIN wall (exterior-wall thickness) on the end face,
        // NOT a full-cube cross-section — otherwise the end walls read as 1 m Minecraft walls.
        auto gableBand = [&](int a, int b, int syAbs, const std::string& mat) {
            int cx = slopeInZ ? bx0 + a : bx0 + b;
            int cz = slopeInZ ? bz0 + b : bz0 + a;
            int y0 = syAbs * 3;
            if (slopeInZ) {
                int x0 = (a == 0) ? cx * 9 : cx * 9 + 9 - extT;
                c.fillMicroBox(x0, y0, cz * 9, extT, 3, 9, mat);
            } else {
                int z0 = (a == 0) ? cz * 9 : cz * 9 + 9 - extT;
                c.fillMicroBox(cx * 9, y0, z0, 9, 3, extT, mat);
            }
        };
        for (int b = 0; b < span; ++b) {
            int dd = std::min(b, span - 1 - b);
            int top = eaveSub + pitch * dd;
            int under = eaveSub + std::max(0, pitch * dd - shell + 1);
            for (int a = 0; a < perp; ++a)
                for (int sy = under; sy <= top; ++sy) roofLayer(a, b, sy, matRoof);
            for (int a : {0, perp - 1})
                for (int sy = eaveSub; sy < under; ++sy) gableBand(a, b, sy, matExt);  // thin gable wall
        }
    } else {
        // non-rectangular / flat: a simple flat roof cap one cube above the ceiling
        for (const auto& [cx, cz] : footprint)
            c.fillMicroBox(cx * 9, ceilTopMicro, cz * 9, 9, 3, 9, matRoof);
    }
    plan.roof.push_back({bx0, bz0, bx1, bz1, eaveSub / 3, style.roofOf("pitch_deg", 0.0),
                         rectangular ? roofStyle : "flat", matRoof});

    res.ok = true;
    return res;
}

} // namespace Core
} // namespace Phyxel
