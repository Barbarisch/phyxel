// docs/VoxelFoundationPlan.md F7.21a -- the furnisher on the 1/8 m grid (docs/evidence/voxfound_f7_prediction.txt A2).
// FurnishPlanEquivalenceTest's two houses (timber cottage), the shell realized on the 1/8 m foundation grid, extents
// from the 1/8 m furniture library (resources/templates_g8/furniture, by FurnitureCatalog's template name).
#include <gtest/gtest.h>

#include <cstdio>
#include <set>

#include "core/BuildingProgram.h"
#include "core/FurnitureCatalog.h"
#include "core/FurniturePlacer.h"
#include "core/StructureGrid.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"
#include "core/voxfound/StorageLab.h"

using namespace Phyxel::Core;

namespace {

StyleProfile cottage() {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"({
        "timber_cottage": {
            "roof_style": "gable", "foundation": "slab",
            "thickness": { "exterior_wall": 0.333, "interior_wall": 0.222,
                           "foundation_wall": 0.667, "floor": 0.333, "ceiling": 0.222 },
            "materials": { "structure": "WoodPlanks", "floor": "Wood", "roof": "Wood",
                           "foundation": "Stone", "trim": "Log" },
            "roof": { "pitch": 0.8 }
        }
    })"));
    return *reg.get("timber_cottage");
}

ProgPortal portal(const std::string& a, const std::string& b, int px, int pz, int w, int h, const std::string& kind) {
    ProgPortal p; p.a = a; p.b = b; p.px = px; p.pz = pz; p.width = w; p.height = h; p.kind = kind;
    return p;
}

BuildingProgram furnishedHouse() {
    BuildingProgram p;
    p.name = "f8_house"; p.style = "timber_cottage";
    p.footprintW = 10; p.footprintD = 9; p.substructure = "slab";
    ProgStory st; st.height = 3;
    ProgRoom a; a.id = "hall"; a.rect = {0, 0, 5, 9}; a.purpose = "living";
    ProgRoom b; b.id = "kitchen"; b.rect = {5, 0, 5, 5}; b.purpose = "kitchen";
    ProgRoom c; c.id = "bed"; c.rect = {5, 5, 5, 4}; c.purpose = "bedchamber";
    st.rooms.push_back(a); st.rooms.push_back(b); st.rooms.push_back(c);
    st.portals.push_back(portal("exterior", "hall", 0, 4, 1, 2, "door"));
    st.portals.push_back(portal("hall", "kitchen", 5, 2, 1, 2, "door"));
    st.portals.push_back(portal("kitchen", "bed", 7, 5, 1, 2, "door"));
    p.stories.push_back(st);
    return p;
}

BuildingProgram stairHouse() {
    BuildingProgram p;
    p.name = "f8_stair"; p.style = "timber_cottage";
    p.footprintW = 7; p.footprintD = 9; p.substructure = "slab";
    for (int s = 0; s < 2; ++s) {
        ProgStory st; st.height = 3;
        ProgRoom r; r.id = s ? "loft" : "hall"; r.rect = {0, 0, 7, 9};
        r.purpose = s ? "bedchamber" : "living";
        st.rooms.push_back(r);
        p.stories.push_back(st);
    }
    p.stories[0].portals.push_back(portal("exterior", "hall", 0, 3, 1, 2, "door"));
    ProgStair sr; sr.fromStory = 0; sr.toStory = 1; sr.rect = {2, 3, 2, 4}; sr.form = "switchback";
    p.stories[0].stairs.push_back(sr);
    return p;
}

// The 1/8 m library's footprint of a fixture type (cells: max index along x / z, height); width / depth in metres.
bool g8Footprint(const std::string& type, Footprint& fp) {
    std::string name = FurnitureCatalog::templateFor(type);
    const auto slash = name.find_last_of('/');
    if (slash != std::string::npos) name = name.substr(slash + 1);
    const VoxFound::GridTemplate t = VoxFound::loadGridTemplate("resources/templates_g8/furniture/" + name + ".voxel");
    if (!t.error.empty() || t.cellsPerM != 8) return false;
    fp.microW = t.bounds.x - 1;
    fp.microD = t.bounds.z - 1;
    fp.microH = t.bounds.y;
    fp.width = (t.bounds.x + 7) / 8;
    fp.depth = (t.bounds.z + 7) / 8;
    return true;
}

struct Score {
    int pieces = 0, noTemplate = 0, overlapPieces = 0, overlapCells = 0, backed = 0, gaps = 0;
};

Score furnishAndScore(const BuildingProgram& p, int perM, const char* tag) {
    const auto shell = StructureRealizer::realizeShell(p, cottage(), StructureGrid::foundation());
    EXPECT_TRUE(shell.ok) << shell.error;
    Score sc;
    for (size_t si = 0; si < p.stories.size(); ++si) {
        const int floorTop = shell.floorTopByStory.empty() ? shell.floorTopMicro : shell.floorTopByStory[si];
        const int floorY = floorTop / 8;
        // the types first (no footprints), then the real run with every type's g8 footprint
        std::map<std::string, Footprint> fps;
        for (const auto& pl : FurniturePlacer::furnishFromPlan(p.stories[si], int(si), {0, 0, 0}, floorY, shell.plan,
                                                               {}, nullptr, "", {}, perM)) {
            Footprint fp;
            if (g8Footprint(pl.type, fp)) fps[pl.type] = fp;
        }
        const int extT = FurniturePlacer::planExteriorThicknessMicro(shell.plan, perM);
        const auto placed = FurniturePlacer::furnishFromPlan(p.stories[si], int(si), {0, 0, 0}, floorY, shell.plan, fps,
                                                             nullptr, "", {}, perM);
        for (const auto& pl : placed) {
            auto it = fps.find(pl.type);
            if (it == fps.end()) { ++sc.noTemplate; continue; }
            const Footprint& fp = it->second;
            ++sc.pieces;
            const glm::ivec3 a0 = FurniturePlacer::microWorldPos(pl, extT, floorTop, fp, perM);
            // the consumer's ceiling: the underside of what is above the piece's centre column (a slab or the roof),
            // NOT floorTop + height x M -- that approximation (StructureForge's, today) hangs a chandelier into the
            // slab of the story above
            int ceil = floorTop + p.stories[si].height * 8;
            {
                const glm::ivec3 c0 = FurniturePlacer::microWorldPos(pl, extT, floorTop, fp, perM);
                const int r0 = ((pl.rotation % 360) + 360) % 360;
                const bool t0 = r0 == 90 || r0 == 270;
                const int cx = c0.x + (t0 ? fp.microD : fp.microW) / 2, cz = c0.z + (t0 ? fp.microW : fp.microD) / 2;
                for (int y = floorTop + 1; y < floorTop + p.stories[si].height * 8 + 16; ++y)
                    if (shell.canvas.occupiedMicro(cx, y, cz)) { ceil = y; break; }
            }
            const int ay = FurniturePlacer::mountedMicroY(pl.type, floorTop, ceil, fp.microH, perM);
            const int r = ((pl.rotation % 360) + 360) % 360;
            const bool turned = r == 90 || r == 270;
            const int ex = turned ? fp.microD : fp.microW, ez = turned ? fp.microW : fp.microD;
            int ov = 0;
            for (int x = a0.x; x <= a0.x + ex; ++x)
                for (int y = ay; y < ay + fp.microH; ++y)
                    for (int z = a0.z; z <= a0.z + ez; ++z) ov += shell.canvas.occupiedMicro(x, y, z);
            sc.overlapCells += ov;
            sc.overlapPieces += ov > 0;
            if (pl.backDir != glm::ivec3(0)) {   // flush: the layer behind the back face holds shell cells
                ++sc.backed;
                bool touch = false;
                for (int y = ay; y < ay + fp.microH && !touch; ++y) {
                    if (pl.backDir.x != 0) {
                        const int x = pl.backDir.x < 0 ? a0.x - 1 : a0.x + ex + 1;
                        for (int z = a0.z; z <= a0.z + ez && !touch; ++z) touch = shell.canvas.occupiedMicro(x, y, z);
                    }
                    if (pl.backDir.z != 0) {
                        const int z = pl.backDir.z < 0 ? a0.z - 1 : a0.z + ez + 1;
                        for (int x = a0.x; x <= a0.x + ex && !touch; ++x) touch = shell.canvas.occupiedMicro(x, y, z);
                    }
                }
                if (!touch) {
                    ++sc.gaps;
                    std::printf("  [%s perM %d] gap: %s at (%d,%d,%d) rot %d back (%d,%d)\n", tag, perM, pl.type.c_str(),
                                a0.x, ay, a0.z, pl.rotation, pl.backDir.x, pl.backDir.z);
                }
            }
            if (ov > 0)
                std::printf("  [%s perM %d] overlap: %s %d cells at (%d,%d,%d) rot %d\n", tag, perM, pl.type.c_str(), ov,
                            a0.x, ay, a0.z, pl.rotation);
        }
    }
    std::printf("[F7.21a %s perM %d] pieces %d (no g8 template %d) | overlap pieces %d cells %d | backed %d gaps %d\n", tag,
                perM, sc.pieces, sc.noTemplate, sc.overlapPieces, sc.overlapCells, sc.backed, sc.gaps);
    return sc;
}

}  // namespace

TEST(VoxFoundFurnish8, PiecesOnTheEighthShellAreClearAndFlush) {
    // the engine's recipes (the build loads them; without them the placer falls back to fewer pieces -- and the
    // result would depend on which test ran first)
    ASSERT_TRUE(FurniturePlacer::loadRecipesFromFile("resources/furnishing_recipes.json"));
    for (const auto& [tag, prog] : {std::pair<const char*, BuildingProgram>{"house", furnishedHouse()},
                                    std::pair<const char*, BuildingProgram>{"stair", stairHouse()}}) {
        const Score red = furnishAndScore(prog, 9, tag);   // today's units on the 1/8 m shell
        const Score green = furnishAndScore(prog, 8, tag);
        ASSERT_GT(green.pieces, 0) << tag;
        EXPECT_GT(red.overlapPieces + red.gaps, 0) << tag << ": the red must show the unit mismatch";
        EXPECT_EQ(green.overlapPieces, 0) << tag;
        EXPECT_EQ(green.gaps, 0) << tag;
    }
}

// Diagnostic (not a gate): today's 1/9 m shell furnished with perM 9 -- does a hung piece land over the stair there too?
TEST(VoxFoundFurnish8, DiagnosticLegacyStairChandelier) {
    ASSERT_TRUE(FurniturePlacer::loadRecipesFromFile("resources/furnishing_recipes.json"));
    const BuildingProgram p = stairHouse();
    const auto shell = StructureRealizer::realizeShell(p, cottage());   // the legacy 1/9 m grid
    ASSERT_TRUE(shell.ok);
    const int floorTop = shell.floorTopByStory.empty() ? shell.floorTopMicro : shell.floorTopByStory[0];
    const auto placed = FurniturePlacer::furnishFromPlan(p.stories[0], 0, {0, 0, 0}, floorTop / 9, shell.plan);
    for (const auto& pl : placed)
        if (FurniturePlacer::mountFor(pl.type) == FurniturePlacer::Mount::Ceiling)
            std::printf("[F7.21a legacy] %s at cube (%d,%d) -- stair rect x 2..3, z 3..6\n", pl.type.c_str(),
                        pl.worldPos.x, pl.worldPos.z);
}
