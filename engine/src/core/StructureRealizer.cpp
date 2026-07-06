#include "core/StructureRealizer.h"
#include "core/StairPlanner.h"

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
            if (ext) {
                for (int k = 0; k < std::max(1, p.width); ++k) {
                    int cx = alongZ ? p.px - (p.px == W ? 1 : 0) : p.px + k;
                    int cz = alongZ ? p.pz + k : p.pz - (p.pz == D ? 1 : 0);
                    c.fillMicroBox(cx * 9, oyBase, cz * 9, 9, oyTop - oyBase, 9, "");   // carve to air
                }

                // ---- finish_forge P1: FRAME the opening (cut_openings leaves no raw holes).
                // Jambs flank the clear span, a lintel carries the head (checklist D5/H1),
                // windows get a sill ledge proud of the facade (E7/H6). Dims: jamb 1 micro
                // (1-cube door clear stays 7 micro = 0.78 m — real clear openings 0.76-0.81 m);
                // lintel 2 micro (~0.22 m — real timber lintels 0.15-0.23 m); sill projection
                // 1 micro (~0.11 m, stylized vs real 25-75 mm — GroundingGaps). Trim material
                // from the style ("trim" layer); fallback contrasts with the wall so frames
                // read against the facade instead of vanishing into it.
                if (p.kind == "door" || p.kind == "window") {
                    const std::string matTrim =
                        style.materialOf("trim", matExt == "Wood" ? "Log" : "Wood");
                    const int kJamb = 1, kLintel = 2;
                    const int w = std::max(1, p.width);
                    const int jambTop = oyTop - kLintel;
                    if (alongZ) {
                        int cx  = p.px - (p.px == W ? 1 : 0);
                        int fx0 = (p.px == 0) ? cx * 9 : cx * 9 + 9 - extT;   // wall band depth
                        int z0 = p.pz * 9, z1 = (p.pz + w) * 9;
                        c.fillMicroBox(fx0, oyBase, z0,          extT, jambTop - oyBase, kJamb, matTrim);
                        c.fillMicroBox(fx0, oyBase, z1 - kJamb,  extT, jambTop - oyBase, kJamb, matTrim);
                        c.fillMicroBox(fx0, jambTop, z0,         extT, kLintel, z1 - z0, matTrim);
                        if (p.kind == "window") {
                            int proudX = (p.px == 0) ? cx * 9 - 1 : cx * 9 + 9;
                            c.fillMicroBox(proudX, oyBase - 1, z0, 1, 2, z1 - z0, matTrim);      // proud ledge
                            c.fillMicroBox(fx0,    oyBase - 1, z0, extT, 1, z1 - z0, matTrim);   // sill board
                        }
                    } else {
                        int cz  = p.pz - (p.pz == D ? 1 : 0);
                        int fz0 = (p.pz == 0) ? cz * 9 : cz * 9 + 9 - extT;
                        int x0 = p.px * 9, x1 = (p.px + w) * 9;
                        c.fillMicroBox(x0,         oyBase, fz0, kJamb, jambTop - oyBase, extT, matTrim);
                        c.fillMicroBox(x1 - kJamb, oyBase, fz0, kJamb, jambTop - oyBase, extT, matTrim);
                        c.fillMicroBox(x0,         jambTop, fz0, x1 - x0, kLintel, extT, matTrim);
                        if (p.kind == "window") {
                            int proudZ = (p.pz == 0) ? cz * 9 - 1 : cz * 9 + 9;
                            c.fillMicroBox(x0, oyBase - 1, proudZ, x1 - x0, 2, 1, matTrim);
                            c.fillMicroBox(x0, oyBase - 1, fz0,    x1 - x0, 1, extT, matTrim);
                        }
                    }
                }
            } else {
                // Interior: the wall band is CENTERED on the shared coord (coord*9), straddling the
                // cube boundary, so a single-cube carve leaves a wall sliver and the doorway isn't
                // passable. Clear the full band — both cubes straddling the coord — over the opening
                // run at door height. (BuildingHarness rooms-reachable caught the sliver.)
                Seg sw = sharedWall(srooms[p.a], srooms[p.b]);
                if (sw.ok) {
                    for (int k = 0; k < std::max(1, p.width); ++k) {
                        if (sw.axis == 'x')   // wall runs along Z at x=coord; doorway runs along Z
                            c.fillMicroBox((sw.coord - 1) * 9, oyBase, (p.pz + k) * 9,
                                           18, oyTop - oyBase, 9, "");
                        else                  // wall runs along X at z=coord; doorway runs along X
                            c.fillMicroBox((p.px + k) * 9, oyBase, (sw.coord - 1) * 9,
                                           9, oyTop - oyBase, 18, "");
                    }
                }
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
                const int riseMicro = topMicro - botMicro;

                // Plan a CLIMBABLE stair. StairPlanner is the shared source of truth with
                // BuildingProgramValidator, so what we build is exactly what the gate checks.
                // maxStepMicro = the character's step-up (m_maxStepHeight 4/9 m) on the grid.
                const int maxStepMicro = 4;
                StairPlan plan = planStair(rc.w, rc.d, riseMicro,
                                           stairFormFromString(sr.form), maxStepMicro);

                // (1) cut the stairwell hole through the upper story's floor slab
                c.fillMicroBox(rc.x * 9 + plan.holeX, holeBase, rc.z * 9 + plan.holeZ,
                               plan.holeW, topMicro - holeBase, plan.holeD, "");

                // (2) build the planned treads + landings (local micro → offset into the well)
                for (const auto& s : plan.solids)
                    c.fillMicroBox(rc.x * 9 + s.x, botMicro + s.y, rc.z * 9 + s.z,
                                   s.w, s.h, s.d, matFloor);
            }
    }

    // ---- pass 4: ceiling slab over the footprint ----
    for (const auto& [cx, cz] : footprint)
        c.fillMicroBox(cx * 9, wallTopMicro, cz * 9, 9, ceilT, 9, matCeil);

    // ---- pass 4.5: corner QUOINS (P2 place_trim increment A; TrimForgeTest) ----
    // Styles with flags.quoins get alternating dressed-stone corner blocks proud of both
    // facade planes at every corner of the RECTANGULAR bounding footprint, ground to wall
    // top. GROUNDED (TrimGrounding.md): reclaimed stone quoins 450x300x145 mm (Britannia
    // Stone) -> long leg 4 micro, short leg 3 micro, proud face + course height 1 micro
    // (the 145 mm dimension at the grid floor). Long-and-short work alternates the leg
    // orientation per course; the 4:3 rhythm comes from the grounded block itself (a
    // sourced numeric alternation RATIO remains NEEDS-RESEARCH). Material = the style's
    // trim layer; the outermost wall skin under each leg is recolored to trim so the
    // quoin reads as one dressed stone, not a floating shell. L-plan notch corners are
    // deferred with the P2.6 composition work.
    const int W_c = bx1 - bx0, D_c = bz1 - bz0;
    const bool rectangular = (int)footprint.size() == W_c * D_c;
    if (style.flag("quoins", false) && rectangular) {
        const std::string matTrim = style.materialOf("trim", matExt);
        const int qLong = 4, qShort = 3;
        const int qBase = floorTopByStory.empty() ? floorTopMicro : floorTopByStory[0];
        struct QCorner { int cx, cz, dx, dz; };   // outermost wall cell + inward direction
        const QCorner corners[4] = {
            {bx0 * 9,     bz0 * 9,     +1, +1},
            {bx1 * 9 - 1, bz0 * 9,     -1, +1},
            {bx0 * 9,     bz1 * 9 - 1, +1, -1},
            {bx1 * 9 - 1, bz1 * 9 - 1, -1, -1},
        };
        for (const auto& q : corners) {
            for (int y = qBase; y < wallTopMicro; ++y) {
                const int course = y - qBase;
                const int runX = (course % 2 == 0) ? qLong : qShort;  // leg along the z-facade
                const int runZ = (course % 2 == 0) ? qShort : qLong;  // leg along the x-facade
                for (int k = 0; k < runX; ++k) {
                    c.setMicroCell(q.cx + q.dx * k, y, q.cz - q.dz, matTrim);  // proud shell
                    c.setMicroCell(q.cx + q.dx * k, y, q.cz, matTrim);         // wall skin
                }
                for (int k = 0; k < runZ; ++k) {
                    c.setMicroCell(q.cx - q.dx, y, q.cz + q.dz * k, matTrim);
                    c.setMicroCell(q.cx, y, q.cz + q.dz * k, matTrim);
                }
                c.setMicroCell(q.cx - q.dx, y, q.cz - q.dz, matTrim);          // corner cell
            }
        }
    }

    // ---- pass 5: gable roof over the bounding rectangle ----
    const std::string roofStyle = !program.roofStyle.empty() ? program.roofStyle : style.roofStyle;
    // Subcube row the roof's lowest course sits on. FLOOR-divide (not ceil): the eave rests ON the
    // wall/ceiling top with NO air row. wallTopMicro is always a multiple of 3 (crawl*9 + floorT(3) +
    // height*9), and ceilT < 3, so floor(ceilTop/3)*3 == wallTopMicro — the eave lands exactly on the
    // wall top. The old ceil() pushed it one subcube higher, leaving the visible 1-micro hover
    // (V1 RealizedStructureValidator::checkRoofEaveFlush).
    int eaveSub = ceilTopMicro / 3;
    if (rectangular && roofStyle == "hip" && W_c >= 2 && D_c >= 2) {
        // HIP roof (P2.5, RoofForgeTest): the surface slopes up from ALL FOUR eaves —
        // height above the eave = (distance to the nearest footprint edge) * pitch,
        // which forms a ridge segment along the longer axis. Same grounded pitch_deg
        // conversion and micro-stepped rasterization as the gable; same subcube-snapped
        // underside so the shell interior stays coarsenable. No gable-end walls: every
        // side is roof down to its eave.
        constexpr double PI = 3.14159265358979323846;
        const double pitchDeg = style.roofOf("pitch_deg", 0.0);
        const int pitch = pitchDeg > 0.0
            ? std::max(1, (int)std::lround(3.0 * std::tan(pitchDeg * PI / 180.0)))
            : 2;
        const int shellM = (pitch + 1) * 3;
        const int eaveM  = eaveSub * 3;
        const int WM = W_c * 9, DM = D_c * 9;
        for (int mx = 0; mx < WM; ++mx) {
            const int dx = std::min(mx, WM - 1 - mx);
            for (int mz = 0; mz < DM; ++mz) {
                const int d     = std::min(dx, std::min(mz, DM - 1 - mz));
                const int top   = eaveM + (d * pitch) / 3;
                const int under = std::max(eaveM, (top - shellM + 1) / 3 * 3);
                c.fillMicroBox(bx0 * 9 + mx, under, bz0 * 9 + mz, 1, top - under + 1, 1, matRoof);
            }
        }
    } else if (rectangular && roofStyle == "gable" && W_c >= 2 && D_c >= 2) {
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
        // P2.5 MICRO-STEPPED slope (finish_forge; RoofForgeTest): advance the tread one
        // MICRO of run at a time instead of one cube. Rise = pitch subcubes per cube of
        // run = pitch/3 micro per micro, so a 50deg (pitch 4) surface steps 1-2 micro per
        // column instead of jumping pitch*3 = 12 micro at every cube boundary — the roof
        // reads as a plane, not metre stairs. The wedge interior still greedily coarsens
        // in export(); only the stepped surface shell stays micro (tree-forge economics).
        const int spanM  = span * 9;
        const int eaveM  = eaveSub * 3;    // micro y of the lowest roof course (eave-flush)
        const int shellM = shell * 3;      // same vertical shell depth as the cube-stepped roof
        const int perpM  = perp * 9;
        for (int mb = 0; mb < spanM; ++mb) {
            const int dd    = std::min(mb, spanM - 1 - mb);
            const int top   = eaveM + (dd * pitch) / 3;   // floor-div: never above the grounded plane
            // Underside snaps DOWN to the subcube grid (3-micro treads, thickness grows <=2
            // micro): only the visible TOP surface pays micro cost; the shell interior stays
            // subcube-coarsenable (VoxelCountIsReasonable budget).
            const int under = std::max(eaveM, (top - shellM + 1) / 3 * 3);
            // One micro-thin slice of roof mass across the full perpendicular extent.
            if (slopeInZ) c.fillMicroBox(bx0 * 9, under, bz0 * 9 + mb, perpM, top - under + 1, 1, matRoof);
            else          c.fillMicroBox(bx0 * 9 + mb, under, bz0 * 9, 1, top - under + 1, perpM, matRoof);
            // The gable-end triangle stays a THIN wall (exterior-wall thickness) on the end
            // face, NOT a full-cube cross-section — else the ends read as 1 m Minecraft walls.
            if (under > eaveM) {
                const int gh = under - eaveM;
                if (slopeInZ) {
                    c.fillMicroBox(bx0 * 9,        eaveM, bz0 * 9 + mb, extT, gh, 1, matExt);
                    c.fillMicroBox(bx1 * 9 - extT, eaveM, bz0 * 9 + mb, extT, gh, 1, matExt);
                } else {
                    c.fillMicroBox(bx0 * 9 + mb, eaveM, bz0 * 9,        1, gh, extT, matExt);
                    c.fillMicroBox(bx0 * 9 + mb, eaveM, bz1 * 9 - extT, 1, gh, extT, matExt);
                }
            }
        }
    } else {
        // non-rectangular / flat: a simple flat roof cap one cube above the ceiling
        for (const auto& [cx, cz] : footprint)
            c.fillMicroBox(cx * 9, ceilTopMicro, cz * 9, 9, 3, 9, matRoof);
    }
    plan.roof.push_back({bx0, bz0, bx1, bz1, eaveSub / 3, style.roofOf("pitch_deg", 0.0),
                         rectangular ? roofStyle : "flat", matRoof});

    res.floorTopByStory = floorTopByStory;   // per-story walkable micro-Y (for the harness / KI-2)
    res.ok = true;
    return res;
}

} // namespace Core
} // namespace Phyxel
