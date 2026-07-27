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

namespace {
LocationType locationTypeForTypology(const std::string& t) {
    if (t == "tavern") return LocationType::Tavern;
    if (t == "blacksmith" || t == "bakery" || t == "general_store" ||
        t == "apothecary" || t == "butcher")
        return LocationType::Work;
    if (t == "croft" || t == "longhouse" || t == "hall_house" || t == "manor_hall")
        return LocationType::Home;
    return LocationType::Custom;
}
} // namespace

std::vector<LocationMarker> StructureRealizer::deriveLocations(const BuildingProgram& program,
                                                               const std::string& typology,
                                                               const AssemblyPlan& plan,
                                                               const glm::ivec3& worldOrigin,
                                                               int floorTopMicro) {
    const int W = std::max(program.footprintW, 1);
    const int D = std::max(program.footprintD, 1);

    // Ground-story exterior door: openings record the portal's WALL coordinate, so a
    // perimeter door sits on the footprint boundary line (px in {0,W} or pz in {0,D});
    // interior doors have interior coords. Lowest y = ground story. Doors on an L-plan's
    // inner walls miss the boundary test — those buildings fall back to the centre anchor.
    // Dwellings carry OPPOSED cross-passage door pairs, so "any perimeter door" can pick
    // the back door and anchor the marker facing AWAY from the street (measured live:
    // 6/14 village anchors unreachable) — prefer the door on program.front, the wall the
    // settlement planner faced at the street.
    auto onFrontWall = [&](const OpeningCut& o) {
        if (program.front == "x0") return o.x == 0;
        if (program.front == "x1") return o.x == W;
        if (program.front == "z0") return o.z == 0;
        if (program.front == "z1") return o.z == D;
        return false;
    };
    const OpeningCut* door = nullptr;
    bool doorOnFront = false;
    for (const auto& o : plan.openings) {
        if (o.kind != "door") continue;
        if (o.x != 0 && o.x != W && o.z != 0 && o.z != D) continue;
        const bool f = onFrontWall(o);
        if (!door || (f && !doorOnFront) || (f == doorOnFront && o.y < door->y)) {
            door = &o;
            doorOnFront = f;
        }
    }

    LocationMarker m;
    m.type = locationTypeForTypology(typology);
    glm::ivec3 local;
    if (door) {
        // A door on an X-facing wall (px in {0,W}) runs along Z, and vice versa. Anchor
        // two cells beyond the wall cube so the marker is open ground clear of trim —
        // reachable street-side, never inside the wall band.
        const bool onXWall = (door->x == 0 || door->x == W);
        const int cz = onXWall ? door->z + door->w / 2 : door->z;
        const int cx = onXWall ? door->x : door->x + door->w / 2;
        if (door->x == 0)      local = {-2, 0, cz};
        else if (door->x == W) local = {W + 1, 0, cz};
        else if (door->z == 0) local = {cx, 0, -2};
        else                   local = {cx, 0, D + 1};
        local.y = 0;   // outdoor grade: worldOrigin.y is the standing level beside the pad
    } else {
        local = {W / 2, floorTopMicro / 9, D / 2};
    }
    const glm::ivec3 wpos = worldOrigin + local;
    const std::string base = typology.empty()
        ? (program.function.empty() ? std::string("building") : program.function)
        : typology;
    m.id = base + "_" + std::to_string(wpos.x) + "_" + std::to_string(wpos.z);
    m.name = base;
    m.position = glm::vec3(wpos);
    return {m};
}

StructureRealizer::ShellResult StructureRealizer::realizeShell(const BuildingProgram& program,
                                                               const StyleProfile& style) {
    ShellResult res;
    if (program.stories.empty()) { res.error = "no stories"; return res; }

    const ProgStory& story = program.stories[0];     // P1: single story
    MicroCanvas& c = res.canvas;
    AssemblyPlan& plan = res.plan;

    // ---- materials + thicknesses from the style ----
    // Wall wood is WoodPlanks (lapped siding); Wood (plank flooring) is the FLOOR wood.
    const std::string matFloor = style.materialOf("floor", "Wood");
    const std::string matExt   = style.materialOf("structure", "WoodPlanks");
    const std::string matInt   = style.materialOf("structure", "WoodPlanks");
    const std::string matFound = style.materialOf("foundation", "Stone");
    const std::string matRoof  = style.materialOf("roof", "WoodShingle");
    const std::string matCeil  = style.materialOf("structure", "WoodPlanks");

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

        // finish floor over the footprint (recorded per story — an intermediate floor
        // is also the ceiling of the story below; the plan is the feature-query source)
        for (const auto& [cx, cz] : footprint)
            c.fillMicroBox(cx * 9, fBase, cz * 9, 9, floorT, 9, matFloor);
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
                // Record the REAL partition line (featureAt queries this): the wall plane
                // sits on the cube boundary at `coord`, spanning [lo, hi) along its run axis.
                if (s.axis == 'x')
                    plan.walls.push_back({s.coord, s.lo, s.coord, s.hi, yCubes, st.height,
                                          style.thicknessOf("interior_wall", 0.222), matInt, "interior"});
                else
                    plan.walls.push_back({s.lo, s.coord, s.hi, s.coord, yCubes, st.height,
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
            // Claims Ledger increment 2: RECORD the reveal as it is painted. Every box
            // routed through rec() lands in the canvas exactly as before AND in the
            // OpeningCut record (role + material) — recording only, zero paint change
            // (CanvasDigestTest before/after artifacts prove it).
            OpeningCut cut;
            cut.x = p.px; cut.y = yCubes + sill; cut.z = p.pz;
            cut.w = p.width; cut.h = p.height; cut.d = 1;
            cut.kind = p.kind;
            cut.infill = (p.kind == "window") ? p.infill : std::string("open");
            auto rec = [&](int bx, int by, int bz, int bw, int bh, int bd,
                           const std::string& mat, const char* role) {
                c.fillMicroBox(bx, by, bz, bw, bh, bd, mat);
                TrimBox t; t.x = bx; t.y = by; t.z = bz; t.w = bw; t.h = bh; t.d = bd;
                t.role = role; t.material = mat;
                cut.reveal.push_back(t);
            };
            if (ext) {
                for (int k = 0; k < std::max(1, p.width); ++k) {
                    int cx = alongZ ? p.px - (p.px == W ? 1 : 0) : p.px + k;
                    int cz = alongZ ? p.pz + k : p.pz - (p.pz == D ? 1 : 0);
                    rec(cx * 9, oyBase, cz * 9, 9, oyTop - oyBase, 9, "", "clear");   // carve to air
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
                    const std::string matTrim = style.materialOf("trim",
                        (matExt == "Wood" || matExt == "WoodPlanks") ? "Log" : "WoodPlanks");
                    const int kJamb = 1, kLintel = 2;
                    const int w = std::max(1, p.width);
                    const int jambTop = oyTop - kLintel;
                    // ---- finish_forge P3: window INFILL — no more open-air holes (the "ruin"
                    // read). GROUNDED default = shuttered (glazing unaffordable pre-1558, croft
                    // windows.size source); "glass" only where a typology cites it. Shutters are
                    // plank leaves (trim material): CLOSED = a 1-micro leaf filling the clear
                    // reveal; OPEN = the leaf folded back — two proud panels flanking the opening
                    // on the facade, reveal stays air. Open/closed is a deterministic per-opening
                    // hash (a lived-in street mixes both). Glass = a 1-micro Glass pane, closed.
                    const bool isWin = (p.kind == "window");
                    const bool shuttered = isWin && p.infill == "shuttered";
                    const bool glazed    = isWin && p.infill == "glass";
                    // Shutters are JOINERY — plank leaves regardless of wall material (a stone
                    // house hangs wooden shutters; stone-trim "shutters" read as bricked-up).
                    const std::string matLeaf =
                        (matTrim.rfind("Wood", 0) == 0 || matTrim == "Log") ? matTrim : "WoodPlanks";
                    unsigned h = static_cast<unsigned>(p.px) * 2654435761u
                               + static_cast<unsigned>(p.pz) * 2246822519u
                               + static_cast<unsigned>(yCubes) * 40503u;
                    h ^= h >> 16; h *= 2246822519u; h ^= h >> 13;
                    const bool leafClosed = glazed || (shuttered && (h & 1u) == 0u);
                    if (alongZ) {
                        int cx  = p.px - (p.px == W ? 1 : 0);
                        int fx0 = (p.px == 0) ? cx * 9 : cx * 9 + 9 - extT;   // wall band depth
                        int z0 = p.pz * 9, z1 = (p.pz + w) * 9;
                        rec(fx0, oyBase, z0,          extT, jambTop - oyBase, kJamb, matTrim, "jamb");
                        rec(fx0, oyBase, z1 - kJamb,  extT, jambTop - oyBase, kJamb, matTrim, "jamb");
                        rec(fx0, jambTop, z0,         extT, kLintel, z1 - z0, matTrim, "lintel");
                        if (isWin) {
                            int proudX = (p.px == 0) ? cx * 9 - 1 : cx * 9 + 9;
                            rec(proudX, oyBase - 1, z0, 1, 2, z1 - z0, matTrim, "ledge");    // proud ledge
                            rec(fx0,    oyBase - 1, z0, extT, 1, z1 - z0, matTrim, "sill");  // sill board
                            const int leafX = fx0 + extT / 2;                 // mid-reveal plane
                            const int leafH = jambTop - oyBase;
                            if (leafClosed) {
                                rec(leafX, oyBase, z0 + kJamb, 1, leafH,
                                    (z1 - z0) - 2 * kJamb, glazed ? "Glass" : matLeaf, "leaf");
                            } else if (shuttered) {                           // folded back on the facade
                                const int panelW = std::max(2, (z1 - z0) / 2);
                                rec(proudX, oyBase, z0 - panelW, 1, leafH, panelW, matLeaf, "leaf");
                                rec(proudX, oyBase, z1,          1, leafH, panelW, matLeaf, "leaf");
                            }
                        }
                    } else {
                        int cz  = p.pz - (p.pz == D ? 1 : 0);
                        int fz0 = (p.pz == 0) ? cz * 9 : cz * 9 + 9 - extT;
                        int x0 = p.px * 9, x1 = (p.px + w) * 9;
                        rec(x0,         oyBase, fz0, kJamb, jambTop - oyBase, extT, matTrim, "jamb");
                        rec(x1 - kJamb, oyBase, fz0, kJamb, jambTop - oyBase, extT, matTrim, "jamb");
                        rec(x0,         jambTop, fz0, x1 - x0, kLintel, extT, matTrim, "lintel");
                        if (isWin) {
                            int proudZ = (p.pz == 0) ? cz * 9 - 1 : cz * 9 + 9;
                            rec(x0, oyBase - 1, proudZ, x1 - x0, 2, 1, matTrim, "ledge");
                            rec(x0, oyBase - 1, fz0,    x1 - x0, 1, extT, matTrim, "sill");
                            const int leafZ = fz0 + extT / 2;
                            const int leafH = jambTop - oyBase;
                            if (leafClosed) {
                                rec(x0 + kJamb, oyBase, leafZ, (x1 - x0) - 2 * kJamb,
                                    leafH, 1, glazed ? "Glass" : matLeaf, "leaf");
                            } else if (shuttered) {
                                const int panelW = std::max(2, (x1 - x0) / 2);
                                rec(x0 - panelW, oyBase, proudZ, panelW, leafH, 1, matLeaf, "leaf");
                                rec(x1,          oyBase, proudZ, panelW, leafH, 1, matLeaf, "leaf");
                            }
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
                            rec((sw.coord - 1) * 9, oyBase, (p.pz + k) * 9,
                                18, oyTop - oyBase, 9, "", "clear");
                        else                  // wall runs along X at z=coord; doorway runs along X
                            rec((p.px + k) * 9, oyBase, (sw.coord - 1) * 9,
                                9, oyTop - oyBase, 18, "", "clear");
                    }
                }
            }
            plan.openings.push_back(cut);
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
                StairPlan sp = planStair(rc.w, rc.d, riseMicro,
                                         stairFormFromString(sr.form), maxStepMicro);

                // (1) cut the stairwell hole through the upper story's floor slab
                c.fillMicroBox(rc.x * 9 + sp.holeX, holeBase, rc.z * 9 + sp.holeZ,
                               sp.holeW, topMicro - holeBase, sp.holeD, "");

                // (2) build the planned treads + landings (local micro → offset into the well)
                for (const auto& s : sp.solids)
                    c.fillMicroBox(rc.x * 9 + s.x, botMicro + s.y, rc.z * 9 + s.z,
                                   s.w, s.h, s.d, matFloor);

                // (3) RECORD the flight in the plan at the moment it is built (Claims
                // Ledger increment 1): furniture reservation, featureAt, and validators
                // query this record instead of re-planning from ProgStair.
                StairRecord rec;
                rec.x = rc.x; rec.z = rc.z; rec.w = rc.w; rec.d = rc.d;
                rec.fromStory = a; rec.toStory = b;
                rec.baseY = botMicro / 9;
                rec.topY = (topMicro + 8) / 9;
                rec.botWalkMicro = botMicro; rec.topWalkMicro = topMicro;
                rec.form = sr.form;
                rec.holeX = rc.x * 9 + sp.holeX; rec.holeZ = rc.z * 9 + sp.holeZ;
                rec.holeW = sp.holeW; rec.holeD = sp.holeD;
                plan.stairs.push_back(rec);
            }
    }

    // ---- pass 4: ceiling slab over the footprint ----
    for (const auto& [cx, cz] : footprint)
        c.fillMicroBox(cx * 9, wallTopMicro, cz * 9, 9, ceilT, 9, matCeil);
    plan.floors.push_back({bx0, bz0, bx1 - bx0, bz1 - bz0, wallTopMicro / 9,
                           style.thicknessOf("ceiling", 0.222), matCeil, "ceiling"});

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
        struct QCorner { int cx, cz, dx, dz; int ccx, ccz; };   // outermost wall micro + inward dir + corner CUBE
        const QCorner corners[4] = {
            {bx0 * 9,     bz0 * 9,     +1, +1, bx0,     bz0},
            {bx1 * 9 - 1, bz0 * 9,     -1, +1, bx1 - 1, bz0},
            {bx0 * 9,     bz1 * 9 - 1, +1, -1, bx0,     bz1 - 1},
            {bx1 * 9 - 1, bz1 * 9 - 1, -1, -1, bx1 - 1, bz1 - 1},
        };
        for (const auto& q : corners) {
            // Claims Ledger increment 2: record the corner zone the courses dress.
            plan.corners.push_back({q.ccx, q.ccz, q.dx, q.dz,
                                    qBase / 9, (wallTopMicro + 8) / 9,
                                    qLong, qShort, matTrim});
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
    // Decompose the footprint into 1-2 rectangular roof RANGES (P2.6; RoofForgeTest.LPlan*):
    // group contiguous rows (then columns) whose occupied cells form one identical interval.
    // 1 band = the plain rectangle; 2 bands with nested intervals = an L/T plan — the wider
    // band is the MAIN range and the narrower interval extends across the full depth as the
    // CROSS-WING (the grounded composition mechanism: hall + cross-wing ranges, not blobs —
    // FinishDetailPlan P2.6). The wing range deliberately OVERLAPS the main range so the two
    // gable surfaces intersect; the pointwise max of the two height fields forms the VALLEY.
    // 3+ bands or split intervals keep the flat-cap fallback (H-plans etc. — later).
    auto decomposeRoofRanges = [&]() -> std::vector<Rect> {
        auto rowSpan = [&](int fixed, bool byZ, int& lo, int& hi) -> bool {
            lo = INT_MAX; hi = INT_MIN;
            for (const auto& [cx, cz] : footprint) {
                const int f = byZ ? cz : cx, v = byZ ? cx : cz;
                if (f != fixed) continue;
                lo = std::min(lo, v); hi = std::max(hi, v + 1);
            }
            if (lo >= hi) return false;
            for (int v = lo; v < hi; ++v)
                if (!inFoot(byZ ? v : fixed, byZ ? fixed : v)) return false;   // split interval
            return true;
        };
        for (const bool byZ : {true, false}) {
            const int f0 = byZ ? bz0 : bx0, f1 = byZ ? bz1 : bx1;
            struct Band { int f0, f1, lo, hi; };
            std::vector<Band> bands;
            bool ok = true;
            for (int f = f0; f < f1 && ok; ++f) {
                int lo, hi;
                if (!rowSpan(f, byZ, lo, hi)) { ok = false; break; }
                if (!bands.empty() && bands.back().lo == lo && bands.back().hi == hi)
                    bands.back().f1 = f + 1;
                else
                    bands.push_back({f, f + 1, lo, hi});
            }
            if (!ok) continue;
            if (bands.size() == 1) {
                const Band& b = bands[0];
                if (byZ) return {{b.lo, b.f0, b.hi - b.lo, b.f1 - b.f0}};
                return {{b.f0, b.lo, b.f1 - b.f0, b.hi - b.lo}};
            }
            if (bands.size() == 2) {
                const Band& wide   = (bands[0].hi - bands[0].lo >= bands[1].hi - bands[1].lo)
                                     ? bands[0] : bands[1];
                const Band& narrow = (&wide == &bands[0]) ? bands[1] : bands[0];
                if (narrow.lo < wide.lo || narrow.hi > wide.hi) continue;      // not nested
                Rect mainR, wingR;
                if (byZ) {
                    mainR = {wide.lo, wide.f0, wide.hi - wide.lo, wide.f1 - wide.f0};
                    wingR = {narrow.lo, f0, narrow.hi - narrow.lo, f1 - f0};
                } else {
                    mainR = {wide.f0, wide.lo, wide.f1 - wide.f0, wide.hi - wide.lo};
                    wingR = {f0, narrow.lo, f1 - f0, narrow.hi - narrow.lo};
                }
                if (mainR.w < 2 || mainR.d < 2 || wingR.w < 2 || wingR.d < 2) continue;
                return {mainR, wingR};
            }
        }
        return {};
    };
    const std::vector<Rect> roofRanges =
        (roofStyle == "gable" || roofStyle == "hip") ? decomposeRoofRanges() : std::vector<Rect>{};

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
        // Eave overhang on ALL four sides (a hip has no gable verge) — same thin-sheet
        // continuation as the gable pass; ⚠ modern-analog value, flagged in the style.
        const int oM = std::max(0, (int)std::lround(style.roofOf("overhang", 0.0) * 9.0));
        for (int mx = -oM; mx < WM + oM; ++mx) {
            const int dx = std::min(mx, WM - 1 - mx);
            for (int mz = -oM; mz < DM + oM; ++mz) {
                const int d     = std::min(dx, std::min(mz, DM - 1 - mz));   // negative outside
                const int top   = eaveM + (d * pitch) / 3;
                const bool inside = d >= 0;
                const int under = inside ? std::max(eaveM, (top - shellM + 1) / 3 * 3)
                                         : std::max(0, top - 2);
                if (top < 0 || top - under + 1 <= 0) continue;
                c.fillMicroBox(bx0 * 9 + mx, under, bz0 * 9 + mz, 1, top - under + 1, 1, matRoof);
            }
        }
    } else if (!roofRanges.empty() && (roofStyle == "gable" || roofStyle == "hip") &&
               W_c >= 2 && D_c >= 2) {
        // COMPOSED GABLE roof over 1-2 ranges (P2.5 micro-stepping + P2.6 L-plan valleys).
        // Honor the GROUNDED roof pitch from the style (degrees). The gable rises `pitch`
        // subcubes per cube of run toward the ridge, so roof angle = atan(pitch/3): the
        // grounded subcube pitch = round(3*tan(deg)). thatch 50deg -> 4 (~53deg); tile 40deg
        // -> 3 (~45deg). Absent pitch_deg falls back to the legacy ~34deg (pitch 2).
        // Each range gets its own micro-stepped gable height field (rise pitch/3 micro per
        // micro of run; ridge along its longer axis); the roof surface is the POINTWISE MAX
        // across ranges, so the valley forms where the two fields cross — watertight by
        // construction. Eave overhang (⚠ modern-analog, flagged in styles) extends each
        // field past eave walls that face the EXTERIOR only; no verge overhang (bargeboard =
        // NEEDS-RESEARCH). NOTE: an L-plan with a hip style renders as intersecting gables
        // for now — hip-valley geometry is future work, disclosed here.
        constexpr double PI = 3.14159265358979323846;
        const double pitchDeg = style.roofOf("pitch_deg", 0.0);
        const int pitch = pitchDeg > 0.0
            ? std::max(1, (int)std::lround(3.0 * std::tan(pitchDeg * PI / 180.0)))
            : 2;
        const int eaveM  = eaveSub * 3;      // micro y of the lowest roof course (eave-flush)
        const int shellM = (pitch + 1) * 3;  // same vertical shell depth as the cube-stepped roof
        const int oM = std::max(0, (int)std::lround(style.roofOf("overhang", 0.0) * 9.0));
        auto fdivq = [](int a) { return a >= 0 ? a / 9 : (a - 8) / 9; };
        // Candidate surface height of one range at an absolute micro column; INT_MIN = none.
        auto rangeTop = [&](const Rect& R, int mx, int mz, bool& inside) -> int {
            const bool sInZ = R.w >= R.d;                 // ridge along the longer axis
            const int spanM = (sInZ ? R.d : R.w) * 9;
            const int perpM = (sInZ ? R.w : R.d) * 9;
            const int mb = sInZ ? mz - R.z * 9 : mx - R.x * 9;
            const int mp = sInZ ? mx - R.x * 9 : mz - R.z * 9;
            if (mp < 0 || mp >= perpM) return INT_MIN;    // no verge overhang
            if (mb < -oM || mb >= spanM + oM) return INT_MIN;
            inside = mb >= 0 && mb < spanM;
            // Overhang only over EXTERIOR ground — never into the other range's footprint.
            if (!inside && inFoot(fdivq(mx), fdivq(mz))) return INT_MIN;
            return eaveM + (std::min(mb, spanM - 1 - mb) * pitch) / 3;
        };
        for (int mx = bx0 * 9 - oM; mx < bx1 * 9 + oM; ++mx) {
            for (int mz = bz0 * 9 - oM; mz < bz1 * 9 + oM; ++mz) {
                int best = INT_MIN; bool bestInside = false;
                for (const Rect& R : roofRanges) {
                    bool ins = false;
                    const int t = rangeTop(R, mx, mz, ins);
                    if (t > best) { best = t; bestInside = ins; }
                    else if (t == best && t != INT_MIN && ins) bestInside = true;  // valley tie: interior shell
                }
                if (best == INT_MIN || best < 0) continue;
                // Underside snaps DOWN to the subcube grid inside (subcube-coarsenable shell,
                // VoxelCountIsReasonable budget); overhang = thin 3-micro watertight sheet.
                const int under = bestInside ? std::max(eaveM, (best - shellM + 1) / 3 * 3)
                                             : std::max(0, best - 2);
                if (best - under + 1 <= 0) continue;
                c.fillMicroBox(mx, under, mz, 1, best - under + 1, 1, matRoof);
            }
        }
        // Gable-end triangle walls per range — THIN (exterior-wall thickness), and only on
        // ridge-axis end faces that meet the EXTERIOR (an end absorbed inside the other
        // range needs no wall; the roof mass covers it).
        for (const Rect& R : roofRanges) {
            const bool sInZ = R.w >= R.d;
            const int spanM = (sInZ ? R.d : R.w) * 9;
            for (int mb = 0; mb < spanM; ++mb) {
                const int top   = eaveM + (std::min(mb, spanM - 1 - mb) * pitch) / 3;
                const int under = std::max(eaveM, (top - shellM + 1) / 3 * 3);
                if (under <= eaveM) continue;
                const int gh = under - eaveM;
                if (sInZ) {
                    const int cz = fdivq(R.z * 9 + mb);
                    if (!inFoot(R.x - 1, cz))
                        c.fillMicroBox(R.x * 9, eaveM, R.z * 9 + mb, extT, gh, 1, matExt);
                    if (!inFoot(R.x + R.w, cz))
                        c.fillMicroBox((R.x + R.w) * 9 - extT, eaveM, R.z * 9 + mb, extT, gh, 1, matExt);
                } else {
                    const int cx = fdivq(R.x * 9 + mb);
                    if (!inFoot(cx, R.z - 1))
                        c.fillMicroBox(R.x * 9 + mb, eaveM, R.z * 9, 1, gh, extT, matExt);
                    if (!inFoot(cx, R.z + R.d))
                        c.fillMicroBox(R.x * 9 + mb, eaveM, (R.z + R.d) * 9 - extT, 1, gh, extT, matExt);
                }
            }
        }
    } else {
        // non-rectangular / flat: a simple flat roof cap one cube above the ceiling
        for (const auto& [cx, cz] : footprint)
            c.fillMicroBox(cx * 9, ceilTopMicro, cz * 9, 9, 3, 9, matRoof);
    }
    plan.roof.push_back({bx0, bz0, bx1, bz1, eaveSub / 3, style.roofOf("pitch_deg", 0.0),
                         !roofRanges.empty() ? roofStyle : "flat", matRoof});

    res.floorTopByStory = floorTopByStory;   // per-story walkable micro-Y (for the harness / KI-2)
    res.ok = true;
    return res;
}

} // namespace Core
} // namespace Phyxel
