// C0 brick-size sweep (docs/ContinuousLodPlan.md §2.3) — decides the LOD/cull unit
// with EVIDENCE instead of the grid-convenience guess a grounding audit rejected.
//
// The original draft asserted an 8³ brick citing Tree64 and binary-greedy-meshing.
// Both citations were wrong (Tree64 is 4³-branching per our own RayTracingPlan.md:28;
// cgerikj packs 64-tall bit COLUMNS on whole 62³ chunks), so the number became an open
// experimental variable. This test is the experiment.
//
// It runs on a REAL realized tavern (StructureRealizer::realizeShell on the shipped
// tavern typology + room_program canon) — not a synthetic volume — and feeds it through
// the SHIPPED squash(). Numbers are printed so they can be pasted into the plan.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>

#include "core/Chunk.h"
#include "core/WorldGenerator.h"
#include "core/AssemblyPlan.h"
#include "core/BuildingProgram.h"
#include "core/LodBrick.h"
#include "core/RoomLayout.h"
#include "core/RoomProgram.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"

using namespace Phyxel::Core;

namespace {

bool loadShippedCanon(RoomProgramRegistry& reg) {
    for (const char* p : {"resources/room_program.json", "../resources/room_program.json",
                          "../../resources/room_program.json", "../../../resources/room_program.json"}) {
        std::ifstream f(p);
        if (f.good()) return reg.loadFromFile(p);
    }
    return false;
}

StyleProfile tavernStyle() {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"({
        "timber_cottage": { "roof_style":"gable", "foundation":"crawlspace",
            "thickness": { "exterior_wall":0.333, "interior_wall":0.222, "foundation_wall":0.667,
                           "floor":0.333, "ceiling":0.222 },
            "materials": { "structure":"Wood", "floor":"Wood", "roof":"Wood", "foundation":"Stone" },
            "roof": { "pitch":0.8 } } })"));
    return *reg.get("timber_cottage");
}

BuildingProgram tavernProgram(const RoomProgram* rp) {
    BuildingProgram p;
    p.name = "tavern"; p.style = "timber_cottage"; p.footprintW = 16; p.footprintD = 7;
    p.substructure = "slab"; p.typology = "tavern";
    ProgStory s; s.height = 3; p.stories.push_back(s);
    autofillRoomLayout(p, 7u, rp);
    return p;
}

/// Convert a realized micro canvas into a level-0 (cube-resolution) LodVolume.
/// coverage = occupied microcubes in the cube's 9x9x9; bulkMaterial = volume majority;
/// skinMaterial = majority among EXPOSED micros (those with an empty micro neighbour),
/// which is the finest-level form of the surface-area vote (plan §2.2b).
/// `plan` supplies the AUTHORED opening hints (AssemblyPlan::openings, each with micro-coord
/// `reveal` boxes whose role "clear" is carved air). Marking those cubes preserveOpening is the
/// feed plan §2.2a calls for: infer nothing from voxels, take it from the generator's own anatomy.
LodVolume volumeFromCanvas(const MicroCanvas& canvas, const AssemblyPlan& plan,
                           std::map<std::string, uint16_t>& palette, int* outMarkedCubes) {
    glm::ivec3 lo, hi;
    if (!canvas.microBounds(lo, hi)) return LodVolume();

    const glm::ivec3 microDim = hi - lo + glm::ivec3(1);
    const glm::ivec3 cubeDim((microDim.x + 8) / 9, (microDim.y + 8) / 9, (microDim.z + 8) / 9);
    LodVolume v(cubeDim, 0);

    auto idFor = [&palette](const std::string& m) -> uint16_t {
        if (m.empty()) return 0;
        auto it = palette.find(m);
        if (it != palette.end()) return it->second;
        const uint16_t id = static_cast<uint16_t>(palette.size() + 1);   // 0 reserved for air
        palette.emplace(m, id);
        return id;
    };

    for (int cx = 0; cx < cubeDim.x; ++cx)
    for (int cy = 0; cy < cubeDim.y; ++cy)
    for (int cz = 0; cz < cubeDim.z; ++cz) {
        std::map<uint16_t, uint32_t> volWeight, skinWeight;
        uint32_t coverage = 0;
        for (int mx = 0; mx < 9; ++mx)
        for (int my = 0; my < 9; ++my)
        for (int mz = 0; mz < 9; ++mz) {
            const glm::ivec3 g = lo + glm::ivec3(cx * 9 + mx, cy * 9 + my, cz * 9 + mz);
            if (!canvas.occupiedMicro(g.x, g.y, g.z)) continue;
            ++coverage;
            const uint16_t id = idFor(canvas.materialAt(g.x, g.y, g.z));
            volWeight[id] += 1;
            // exposed == at least one of the 6 micro neighbours is empty
            static const int off[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
            for (const auto& o : off) {
                if (!canvas.occupiedMicro(g.x + o[0], g.y + o[1], g.z + o[2])) { skinWeight[id] += 1; break; }
            }
        }
        if (coverage == 0) continue;
        LodCell& c = v.at(cx, cy, cz);
        c.coverage = coverage;
        auto argmax = [](const std::map<uint16_t, uint32_t>& m) -> uint16_t {
            uint16_t best = 0; uint32_t bw = 0;
            for (const auto& kv : m) if (kv.second > bw) { bw = kv.second; best = kv.first; }
            return best;
        };
        c.bulkMaterial = argmax(volWeight);
        c.skinMaterial = skinWeight.empty() ? c.bulkMaterial : argmax(skinWeight);
    }

    // --- authored opening hints -> preserveOpening -------------------------------------
    int marked = 0;
    auto markMicroBox = [&](int mx, int my, int mz, int mw, int mh, int md) {
        // micro box -> the cube cells it touches (inclusive), relative to `lo`
        const int x0 = (mx - lo.x) / 9,          y0 = (my - lo.y) / 9,          z0 = (mz - lo.z) / 9;
        const int x1 = (mx + mw - 1 - lo.x) / 9, y1 = (my + mh - 1 - lo.y) / 9, z1 = (mz + md - 1 - lo.z) / 9;
        for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z) {
            if (!v.inBounds(x, y, z)) continue;
            LodCell& c = v.at(x, y, z);
            if (!c.preserveOpening) { c.preserveOpening = true; ++marked; }
            // Also record the VOID VOLUME (micro cells of this box inside this cube), which is
            // what OrWithOpeningMask carries upward instead of blanking the cell.
            const int mx0 = std::max(mx, lo.x + x * 9),  mx1 = std::min(mx + mw - 1, lo.x + x * 9 + 8);
            const int my0 = std::max(my, lo.y + y * 9),  my1 = std::min(my + mh - 1, lo.y + y * 9 + 8);
            const int mz0 = std::max(mz, lo.z + z * 9),  mz1 = std::min(mz + md - 1, lo.z + z * 9 + 8);
            if (mx1 >= mx0 && my1 >= my0 && mz1 >= mz0)
                c.openingCoverage += uint64_t(mx1 - mx0 + 1) * (my1 - my0 + 1) * (mz1 - mz0 + 1);
        }
    };
    for (const OpeningCut& o : plan.openings) {
        bool usedReveal = false;
        for (const TrimBox& t : o.reveal) {
            if (t.role != "clear") continue;      // jamb/lintel/sill are SOLID trim, not the void
            markMicroBox(t.x, t.y, t.z, t.w, t.h, t.d);
            usedReveal = true;
        }
        if (!usedReveal) markMicroBox(o.x, o.y, o.z, o.w, o.h, o.d);   // fallback: the cut box
    }
    if (outMarkedCubes) *outMarkedCubes = marked;
    return v;
}

/// Fraction of fine solid cells whose coarse representative disagrees on MATERIAL.
/// A measurable proxy for visual error at a given brick size. (True pop VISIBILITY
/// needs the C4 renderer; this is the offline stand-in and is labelled as such.)
double materialMismatch(const LodVolume& fine, const LodVolume& coarse) {
    const int ratio = coarse.cellSizeInCubes() / std::max(1, fine.cellSizeInCubes());
    if (ratio <= 0) return 0.0;
    size_t total = 0, mismatched = 0;
    const glm::ivec3 d = fine.dim();
    for (int x = 0; x < d.x; ++x)
    for (int y = 0; y < d.y; ++y)
    for (int z = 0; z < d.z; ++z) {
        const LodCell& f = fine.at(x, y, z);
        if (!f.solid()) continue;
        ++total;
        const LodCell& c = coarse.atClamped(x / ratio, y / ratio, z / ratio);
        // A carved opening is not a material error -- without this the metric reports the
        // CARVE (58-92%) instead of material drift (0.6%), which is what it is named for.
        if (c.preserveOpening || !c.solid()) { --total; continue; }
        if (c.skinMaterial != f.skinMaterial) ++mismatched;
    }
    return total ? double(mismatched) / double(total) : 0.0;
}

/// FATTENING: how much solid volume the coarse representation ADDS. Under the OR rule a
/// brick is solid if ANY child is, so a hollow building inflates toward a solid block —
/// this is the dominant visual error at coarse LOD (a tavern reading as a lump), and unlike
/// the material metric it varies strongly with brick size.
/// Ratio of (solid bricks x brick volume) to fine solid cube count. 1.0 = no inflation.
double fatteningRatio(const LodVolume& fine, const LodVolume& coarse) {
    const long long cellVol = 1LL * coarse.cellSizeInCubes() * coarse.cellSizeInCubes() *
                              coarse.cellSizeInCubes();
    const double coarseVol = double(coarse.solidCellCount()) * double(cellVol);
    const double fineVol = double(fine.solidCellCount());
    return fineVol > 0.0 ? coarseVol / fineVol : 0.0;
}

/// OVER-CARVE: the price of opening preservation at coarse sizes. Because an opening WINS over
/// the OR merge, a single door inside a brick blanks the WHOLE brick — at 16^3 one doorway erases
/// 4096 cubes of wall. This measures the fraction of genuinely-solid fine cubes that the coarse
/// level reports as empty for that reason.
///
/// (Two earlier metrics were discarded here as vacuous, and the reasoning is kept so nobody
/// reinstates them: a bbox-relative "hollowness" inflated at coarse sizes purely from bounding-box
/// rounding — it read 50% at 16^3 where the whole building is ONE brick; and "openings retained"
/// is tautologically 100% at every size, because OrPreserveOpenings is *defined* to let an opening
/// win. Neither could discriminate. Over-carve does.)
double overCarve(const LodVolume& fine, const LodVolume& coarse) {
    const int ratio = coarse.cellSizeInCubes() / std::max(1, fine.cellSizeInCubes());
    if (ratio <= 0) return 0.0;
    size_t solid = 0, lost = 0;
    const glm::ivec3 d = fine.dim();
    for (int x = 0; x < d.x; ++x)
    for (int y = 0; y < d.y; ++y)
    for (int z = 0; z < d.z; ++z) {
        if (!fine.at(x, y, z).solid()) continue;
        ++solid;
        if (!coarse.atClamped(x / ratio, y / ratio, z / ratio).solid()) ++lost;
    }
    return solid ? double(lost) / double(solid) : 0.0;
}

} // namespace

/// Tile a level-0 volume into a GxG grid with `gap` cubes of street between copies, so the
/// subject spans many bricks in X and Z. Each copy is the SAME real realized tavern — which is
/// what a settlement row actually is. Y is unchanged: a single-story building is ~9 cubes tall,
/// and no amount of tiling makes it taller. That asymmetry is REAL, not a test artifact.
LodVolume tileVolume(const LodVolume& src, int g, int gap) {
    const glm::ivec3 d = src.dim();
    const glm::ivec3 stride(d.x + gap, d.y, d.z + gap);
    LodVolume out(glm::ivec3(stride.x * g - gap, d.y, stride.z * g - gap), 0);
    for (int gx = 0; gx < g; ++gx)
    for (int gz = 0; gz < g; ++gz)
        for (int x = 0; x < d.x; ++x)
        for (int y = 0; y < d.y; ++y)
        for (int z = 0; z < d.z; ++z) {
            const int ox = gx * stride.x + x, oz = gz * stride.z + z;
            if (out.inBounds(ox, y, oz)) out.at(ox, y, oz) = src.at(x, y, z);
        }
    return out;
}

// ---------------------------------------------------------------------------
// THE SWEEP. Brick candidates are exactly the power-of-two divisors of a 32-cube
// chunk: 4, 8, 16 (per chunk: 512, 64, 8 draw units).
// ---------------------------------------------------------------------------
TEST(LodBrickSweepTest, BrickSizeSweepOnRealizedTavern) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";
    const RoomProgram* rp = reg.get("tavern");
    ASSERT_NE(rp, nullptr);

    BuildingProgram p = tavernProgram(rp);
    auto sh = StructureRealizer::realizeShell(p, tavernStyle());
    ASSERT_TRUE(sh.ok) << sh.error;

    std::map<std::string, uint16_t> palette;
    int markedCubes = 0;
    const LodVolume base = volumeFromCanvas(sh.canvas, sh.plan, palette, &markedCubes);
    ASSERT_FALSE(base.empty()) << "canvas produced no cube-resolution volume";
    ASSERT_GT(base.solidCellCount(), 0u);

    SquashConfig cfg;   // OrPreserveOpenings + SurfaceAreaMajority (the recommended rules)

    std::cout << "\n=== C0 BRICK-SIZE SWEEP — realized tavern (16x7, 1 story, timber_cottage) ===\n";
    std::cout << "source: StructureRealizer::realizeShell on the shipped `tavern` typology\n";
    std::cout << "level-0 volume: " << base.dim().x << "x" << base.dim().y << "x" << base.dim().z
              << " cubes, " << base.solidCellCount() << " solid cells, "
              << palette.size() << " materials, coverage " << base.totalCoverage()
              << " micros\n\n";
    std::cout << "brick lvl coarse    solid  units/  fattening  over-    mat_      scale us\n"
              << "          dims      bricks chunk              carve    mismatch  ok?\n";

    // Build the pyramid once, timing each squash.
    std::vector<LodVolume> levels;
    std::vector<long long> squashUs;
    levels.push_back(base);
    for (int i = 0; i < 4; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        levels.push_back(squash(levels.back(), cfg));
        const auto t1 = std::chrono::steady_clock::now();
        squashUs.push_back(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
    }

    struct Row { int brick, level; size_t solid; long long units; double fatten, carve, mismatch; size_t leaks; long long us; };
    std::vector<Row> rows;
    for (int brick : {4, 8, 16}) {
        int level = 0; for (int b = brick; b > 1; b >>= 1) ++level;   // log2
        ASSERT_LT(size_t(level), levels.size());
        const LodVolume& lv = levels[level];
        const long long perChunk = (32 / brick) * (32 / brick) * (32 / brick);
        const Row r{brick, level, lv.solidCellCount(), perChunk,
                    fatteningRatio(base, lv), overCarve(base, lv),
                    materialMismatch(base, lv), countWatertightViolations(base, lv),
                    squashUs[level - 1]};
        rows.push_back(r);
        const glm::ivec3 cd = lv.dim();
        // SCALE VALIDITY: an over-carve figure only means something if the structure spans
        // several bricks in EVERY axis. Where a brick is comparable to the structure's own
        // extent, any opening necessarily blanks nearly everything — that is geometry of the
        // test subject, not a property of the rule. (Audit finding 2026-07-29.)
        const bool scaleOk = cd.x >= 3 && cd.y >= 3 && cd.z >= 3;
        printf("  %2d^3  %d  %2dx%2dx%-2d  %6zu  %6lld  %7.2fx  %6.1f%%  %7.4f%%  %-4s %lld\n",
               r.brick, r.level, cd.x, cd.y, cd.z, r.solid, r.units, r.fatten,
               r.carve * 100.0, r.mismatch * 100.0, scaleOk ? "yes" : "NO", r.us);
    }
    std::cout << "\nfattening    = added solid volume vs the fine form (OR over-fill): a hollow building\n"
              << "               inflating toward a lump.\n"
              << "over-carve   = solid fine cubes the coarse level reports EMPTY, because an opening\n"
              << "               anywhere in a brick blanks the WHOLE brick. This is the PRICE of\n"
              << "               preserving openings, and it dominates: at 16^3 a single doorway\n"
              << "               erases 4096 cubes of wall.\n"
              << "mat_mismatch = skin-material drift, ignoring carved openings. INSENSITIVE on this\n"
              << "               structure (only 2 materials). Recorded, not used to decide.\n"
              << "units/chunk  = (32/brick)^3, the cull-unit multiplier vs one-draw-per-chunk today.\n"
              << "\nAuthored opening hints ARE fed here: AssemblyPlan::openings -> preserveOpening,\n"
              << "using only role \"clear\" reveal boxes (jamb/lintel/sill are solid trim, not void).\n"
              << "Two earlier metrics were discarded as vacuous and must not be reinstated: a\n"
              << "bbox-relative \"hollowness\" (inflated purely from bbox rounding -- read 50% at\n"
              << "16^3 where the whole building is ONE brick), and \"openings retained\"\n"
              << "(tautologically 100% at every size, since OrPreserveOpenings is DEFINED to let an\n"
              << "opening win). Neither discriminated. Over-carve does.\n"
              << "True pop VISIBILITY needs the C4 renderer and is NOT measured here.\n\n";

    // --- assertions that make this a test, not just a print ---
    for (const auto& r : rows) {
        EXPECT_EQ(r.leaks, 0u) << "brick " << r.brick << " leaks: coarse level has holes where the "
                                  "fine level is solid";
        EXPECT_GT(r.solid, 0u) << "structure vanished entirely at brick " << r.brick;
        EXPECT_LT(r.carve, 1.0) << "brick " << r.brick << " over-carved the ENTIRE structure away";
    }
    // Fidelity must degrade monotonically with brick size. Asserted on FATTENING, which
    // actually varies — the material metric is insensitive on a 2-material structure and
    // would make this assertion vacuous (it reported an identical 0.644% at every size).
    for (size_t i = 1; i < rows.size(); ++i) {
        EXPECT_GT(rows[i].fatten, rows[i - 1].fatten)
            << "coarser brick " << rows[i].brick << " inflated LESS than " << rows[i - 1].brick
            << " — squash or metric is inconsistent";
        EXPECT_GE(rows[i].carve, rows[i - 1].carve - 1e-9)
            << "coarser brick " << rows[i].brick << " over-carved LESS than " << rows[i - 1].brick
            << " — a bigger brick cannot lose less wall to the same opening";
    }
    // And the cost side must fall as bricks grow.
    for (size_t i = 1; i < rows.size(); ++i)
        EXPECT_LT(rows[i].units, rows[i - 1].units);
}

// ---------------------------------------------------------------------------
// SCALE CONTROL (audit finding 2026-07-29). The single tavern is 17x9x7 cubes, so at 8^3 its
// DEPTH already collapses to one brick and at 16^3 both height and depth do. In that regime an
// opening has nowhere else to land, so over-carve is near-total for reasons of test-subject
// geometry -- NOT because the rule is inherently bad. This runs the same sweep on a 4x4 tiled
// BLOCK of the same real tavern (streets between), so X and Z span many bricks.
//
// Y deliberately does NOT grow: a single-story building is ~9 cubes tall and no amount of tiling
// changes that. So if over-carve stays high at 16^3 here while X/Z are ample, the cause is the
// VERTICAL collapse -- which is a real property of buildings, not an artifact.
// ---------------------------------------------------------------------------
TEST(LodBrickSweepTest, BrickSizeSweepOnTiledSettlementBlock) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";
    const RoomProgram* rp = reg.get("tavern");
    ASSERT_NE(rp, nullptr);
    BuildingProgram p = tavernProgram(rp);
    auto sh = StructureRealizer::realizeShell(p, tavernStyle());
    ASSERT_TRUE(sh.ok) << sh.error;

    std::map<std::string, uint16_t> palette;
    int marked = 0;
    const LodVolume one = volumeFromCanvas(sh.canvas, sh.plan, palette, &marked);
    ASSERT_FALSE(one.empty());
    const LodVolume base = tileVolume(one, 4, 4);   // 4x4 taverns, 4-cube streets

    SquashConfig cfg;
    std::vector<LodVolume> levels{base};
    for (int i = 0; i < 4; ++i) levels.push_back(squash(levels.back(), cfg));

    printf("\n=== C0 BRICK SWEEP - 4x4 TILED BLOCK of the same realized tavern (scale control) ===\n");
    printf("level-0 volume: %dx%dx%d cubes, %zu solid cells\n\n",
           base.dim().x, base.dim().y, base.dim().z, base.solidCellCount());
    printf("brick lvl coarse    solid  units/  fattening  over-    scale\n");
    printf("          dims      bricks chunk              carve    ok?\n");
    for (int brick : {4, 8, 16}) {
        int level = 0; for (int b = brick; b > 1; b >>= 1) ++level;
        const LodVolume& lv = levels[level];
        const glm::ivec3 cd = lv.dim();
        const bool scaleOk = cd.x >= 3 && cd.y >= 3 && cd.z >= 3;
        printf("  %2d^3  %d  %2dx%2dx%-2d  %6zu  %6lld  %7.2fx  %6.1f%%  %-4s\n",
               brick, level, cd.x, cd.y, cd.z, lv.solidCellCount(),
               (long long)((32 / brick) * (32 / brick) * (32 / brick)),
               fatteningRatio(base, lv), overCarve(base, lv) * 100.0, scaleOk ? "yes" : "NO");
        EXPECT_EQ(countWatertightViolations(base, lv), 0u);
    }
    printf("\nCompare with the single-tavern table. If over-carve falls sharply now that X/Z span\n"
           "many bricks, the single-tavern figure was scale-limited. Whatever remains at 16^3 is\n"
           "the VERTICAL collapse (a 9-cube-tall building cannot span three 16-cube bricks in Y),\n"
           "and that IS real for buildings.\n\n");
}

// ---------------------------------------------------------------------------
// TERRAIN-ONLY SWEEP (the last C0 sweep item). Terrain has NO openings, so
// over-carve must be 0 by construction and only `fattening` is in play. The
// prediction from the structure sweeps: larger bricks should be far cheaper here,
// so a single global brick size for terrain AND structures is unlikely.
//
// Real generator output: WorldGenerator(Perlin) into real Chunks, same params the
// LodBench project uses (heightScale 14 / freq 0.03 / oct 4 / persist 0.5).
// ---------------------------------------------------------------------------
TEST(LodBrickSweepTest, BrickSizeSweepOnGeneratedTerrain) {
    // 2x2 chunks of terrain at the surface band -> 64x32x64 cubes.
    Phyxel::WorldGenerator gen(Phyxel::WorldGenerator::GenerationType::Perlin, 777001u);
    auto& tp = gen.getTerrainParams();
    tp.heightScale = 14.0f; tp.frequency = 0.03f; tp.octaves = 4; tp.persistence = 0.5f;

    const int CH = 2;   // chunks per axis in X/Z
    LodVolume base(glm::ivec3(CH * 32, 32, CH * 32), 0);
    std::map<std::string, uint16_t> palette;
    auto idFor = [&palette](const std::string& m) -> uint16_t {
        if (m.empty()) return 0;
        auto it = palette.find(m);
        if (it != palette.end()) return it->second;
        const uint16_t id = static_cast<uint16_t>(palette.size() + 1);
        palette.emplace(m, id);
        return id;
    };

    for (int cx = 0; cx < CH; ++cx)
    for (int cz = 0; cz < CH; ++cz) {
        const glm::ivec3 coord(cx, 1, cz);            // chunk y=1 straddles the y~53 surface
        auto chunk = std::make_unique<Phyxel::Chunk>(coord * 32);
        chunk->initializeForLoading();
        gen.generateChunk(*chunk, coord);
        for (int x = 0; x < 32; ++x)
        for (int y = 0; y < 32; ++y)
        for (int z = 0; z < 32; ++z) {
            const auto* c = chunk->getCubeAt(glm::ivec3(x, y, z));
            if (!c) continue;
            LodCell& cell = base.at(cx * 32 + x, y, cz * 32 + z);
            cell.coverage = LodVolume::kFullCoverage;     // a full cube of terrain
            cell.bulkMaterial = cell.skinMaterial = idFor(c->getMaterialName());
        }
    }
    ASSERT_GT(base.solidCellCount(), 0u) << "generator produced no terrain in chunk y=1";

    SquashConfig cfg;
    std::vector<LodVolume> levels{base};
    std::vector<long long> us;
    for (int i = 0; i < 4; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        levels.push_back(squash(levels.back(), cfg));
        us.push_back(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - t0).count());
    }

    printf("\n=== C0 BRICK SWEEP - GENERATED TERRAIN (Perlin, 2x2 chunks at the surface) ===\n");
    printf("level-0 volume: %dx%dx%d cubes, %zu solid cells, %zu materials\n\n",
           base.dim().x, base.dim().y, base.dim().z, base.solidCellCount(), palette.size());
    printf("brick lvl coarse    solid  units/  fattening  over-    mat_      scale us\n");
    printf("          dims      bricks chunk              carve    mismatch  ok?\n");
    for (int brick : {4, 8, 16}) {
        int level = 0; for (int b = brick; b > 1; b >>= 1) ++level;
        const LodVolume& lv = levels[level];
        const glm::ivec3 cd = lv.dim();
        const bool scaleOk = cd.x >= 3 && cd.y >= 3 && cd.z >= 3;
        const double carve = overCarve(base, lv);
        printf("  %2d^3  %d  %2dx%2dx%-2d  %6zu  %6lld  %7.2fx  %6.1f%%  %7.4f%%  %-4s %lld\n",
               brick, level, cd.x, cd.y, cd.z, lv.solidCellCount(),
               (long long)((32 / brick) * (32 / brick) * (32 / brick)),
               fatteningRatio(base, lv), carve * 100.0,
               materialMismatch(base, lv) * 100.0, scaleOk ? "yes" : "NO", us[level - 1]);
        // Terrain has no authored openings, so nothing may ever be carved away.
        EXPECT_DOUBLE_EQ(carve, 0.0) << "terrain over-carved at brick " << brick
                                     << " -- impossible without openings";
        EXPECT_EQ(countWatertightViolations(base, lv), 0u);
    }
    printf("\nover-carve is 0%% by construction here (no openings). Compare `fattening` against the\n"
           "structure tables: if terrain fattens far less at 8^3/16^3, then terrain and structures\n"
           "want DIFFERENT brick sizes and a single global constant is the wrong model.\n\n");
}

// ---------------------------------------------------------------------------
// RULE A/B: binary carve vs sub-brick MASK, on the tiled settlement block.
// This is the fix for the sweep's headline finding — the carve erased 49.7% of the
// block's wall at 4^3 and 100% at 16^3. The mask must erase NOTHING while still
// carrying the opening volume upward.
// ---------------------------------------------------------------------------
TEST(LodBrickSweepTest, OpeningMaskBeatsBinaryCarveOnSettlementBlock) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";
    const RoomProgram* rp = reg.get("tavern");
    ASSERT_NE(rp, nullptr);
    BuildingProgram p = tavernProgram(rp);
    auto sh = StructureRealizer::realizeShell(p, tavernStyle());
    ASSERT_TRUE(sh.ok) << sh.error;

    std::map<std::string, uint16_t> palette;
    int marked = 0;
    const LodVolume one = volumeFromCanvas(sh.canvas, sh.plan, palette, &marked);
    const LodVolume base = tileVolume(one, 4, 4);
    uint64_t baseOpenVol = 0;
    for (const auto& c : base.cells()) baseOpenVol += c.openingCoverage;
    ASSERT_GT(baseOpenVol, 0u) << "no authored opening volume recorded at level 0";

    struct Cfg { const char* name; OccupancyRule rule; };
    const Cfg cfgs[] = {{"binary carve ", OccupancyRule::OrPreserveOpenings},
                        {"opening MASK ", OccupancyRule::OrWithOpeningMask}};

    printf("\n=== C0 RULE A/B - tiled settlement block (%dx%dx%d, %zu solid) ===\n",
           base.dim().x, base.dim().y, base.dim().z, base.solidCellCount());
    printf("level-0 authored opening volume: %llu microcubes\n\n",
           (unsigned long long)baseOpenVol);
    printf("rule           brick  solid bricks  over-carve  opening vol kept\n");

    for (const Cfg& c : cfgs) {
        SquashConfig cfg; cfg.occupancy = c.rule;
        std::vector<LodVolume> levels{base};
        for (int i = 0; i < 4; ++i) levels.push_back(squash(levels.back(), cfg));
        for (int brick : {4, 8, 16}) {
            int level = 0; for (int b = brick; b > 1; b >>= 1) ++level;
            const LodVolume& lv = levels[level];
            uint64_t open = 0;
            for (const auto& cell : lv.cells()) open += cell.openingCoverage;
            const double carve = overCarve(base, lv);
            printf("  %s %2d^3   %8zu      %6.1f%%     %6.1f%%\n",
                   c.name, brick, lv.solidCellCount(), carve * 100.0,
                   100.0 * double(open) / double(baseOpenVol));
            if (c.rule == OccupancyRule::OrWithOpeningMask) {
                EXPECT_DOUBLE_EQ(carve, 0.0)
                    << "the MASK rule deleted geometry at brick " << brick
                    << " — it must never carve";
                EXPECT_EQ(open, baseOpenVol)
                    << "opening volume not conserved at brick " << brick;
                EXPECT_GT(lv.solidCellCount(), 0u);
            } else {
                EXPECT_GT(carve, 0.0) << "precondition: the carve rule DOES delete geometry";
            }
        }
    }
    printf("\nThe mask rule must show 0%% over-carve and 100%% opening volume kept at every brick\n"
           "size, against the carve rule's 49.7-100%% loss. That is the C4 opening mechanism.\n\n");
}
