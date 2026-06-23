#include <gtest/gtest.h>

#include "core/MicroCanvas.h"
#include "core/VoxelTemplate.h"
#include "core/PlacedObjectManager.h"   // full InteractionPointDef (VoxelTemplate member) for construction

using namespace Phyxel::Core;

// ============================================================================
// Greedy coarsening — the core contract.
// ============================================================================

TEST(MicroCanvasTest, SingleCubeIsOneCube) {
    MicroCanvas c;
    c.addCube(0, 0, 0, "Stone");
    auto r = c.report();
    EXPECT_EQ(r.cubes, 1);
    EXPECT_EQ(r.subcubes, 0);
    EXPECT_EQ(r.microcubes, 0);
}

TEST(MicroCanvasTest, UniformMicroFillCoarsensToOneCube) {
    MicroCanvas c;
    c.fillMicroBox(0, 0, 0, 9, 9, 9, "Stone");   // 729 micro cells, one material
    auto r = c.report();
    EXPECT_EQ(r.cubes, 1);
    EXPECT_EQ(r.total(), 1);
}

TEST(MicroCanvasTest, SingleSubcubeStaysOneSubcube) {
    MicroCanvas c;
    c.addSubcube(0, 0, 0, 1, 1, 1, "Wood");
    auto r = c.report();
    EXPECT_EQ(r.cubes, 0);
    EXPECT_EQ(r.subcubes, 1);
    EXPECT_EQ(r.microcubes, 0);
}

TEST(MicroCanvasTest, SingleMicroStaysOneMicro) {
    MicroCanvas c;
    c.addMicro(0, 0, 0, 0, 0, 0, 1, 1, 1, "Wood");
    auto r = c.report();
    EXPECT_EQ(r.total(), 1);
    EXPECT_EQ(r.microcubes, 1);
}

TEST(MicroCanvasTest, MixedMaterialCubeDoesNotCoarsenToCube) {
    MicroCanvas c;
    c.addCube(0, 0, 0, "Stone");
    c.addMicro(0, 0, 0, 0, 0, 0, 0, 0, 0, "Wood");   // one odd micro in one subcube
    auto r = c.report();
    EXPECT_EQ(r.cubes, 0);            // no longer uniform
    EXPECT_EQ(r.subcubes, 26);        // 26 clean subcubes
    EXPECT_EQ(r.microcubes, 27);      // the dirtied subcube splits to 27 micros
}

TEST(MicroCanvasTest, PaintingAirCarvesCell) {
    MicroCanvas c;
    c.addCube(0, 0, 0, "Stone");
    EXPECT_EQ(c.cellCount(), 729u);
    c.setMicroCell(0, 0, 0, "");      // AIR
    EXPECT_EQ(c.cellCount(), 728u);
}

// ============================================================================
// THE SPIKE: the v2 wall-cost rule.
//   A 1/3-cube-thick (subcube-thick) wall sits on subcube boundaries and
//   coarsens to ~9 subcubes per linear cube — cheap. A 2-micro wall does NOT
//   and stays raw microcubes. This locks the performance model before the
//   realizer is built.
// ============================================================================

namespace {
constexpr int N = 10;   // wall length in cubes (along X)
constexpr int H = 3;    // wall height in cubes (along Y)
constexpr int M = MicroCanvas::MICRO_PER_CUBE;  // 9
}

TEST(MicroCanvasTest, SubcubeThickWallCoarsensToSubcubesOnly) {
    MicroCanvas c;
    // Full height/length, 1/3 cube thick (3 micro deep along Z, on the subcube grid).
    c.fillMicroBox(0, 0, 0, N * M, H * M, 3, "Wood");
    auto r = c.report();
    EXPECT_EQ(r.cubes, 0);
    EXPECT_EQ(r.microcubes, 0)
        << "a subcube-aligned wall must NOT spill into microcubes";
    EXPECT_EQ(r.subcubes, 9 * N * H)       // 9 subcubes per wall-cube
        << "expected ~9 subcubes per linear cube of wall";
    // 270 voxels vs a naive 7290 micro cells — the cost win the model promises.
    EXPECT_LT(r.total(), r.microCells() / 10);
}

TEST(MicroCanvasTest, TwoMicroWallStaysMicrocubes) {
    MicroCanvas c;
    // 2 micro thick (~0.22 m) — off the subcube boundary, so it cannot coarsen.
    c.fillMicroBox(0, 0, 0, N * M, H * M, 2, "Wood");
    auto r = c.report();
    EXPECT_EQ(r.cubes, 0);
    EXPECT_EQ(r.subcubes, 0);
    EXPECT_EQ(r.microcubes, 9 * 9 * 2 * N * H)   // 162 micro per wall-cube
        << "a 2-micro wall is the expensive case the model warns about";
}

// ============================================================================
// Export adapter to the asset/template path.
// ============================================================================

TEST(MicroCanvasTest, ToVoxelTemplateMatchesReport) {
    MicroCanvas c;
    c.fillMicroBox(0, 0, 0, N * M, H * M, 3, "Wood");   // the subcube wall
    Phyxel::VoxelTemplate tmpl;
    c.toVoxelTemplate(tmpl);
    EXPECT_TRUE(tmpl.cubes.empty());
    EXPECT_TRUE(tmpl.microcubes.empty());
    EXPECT_EQ(tmpl.subcubes.size(), static_cast<size_t>(9 * N * H));
}

TEST(MicroCanvasTest, ChamferCarvesAndBreaksUpTheCube) {
    MicroCanvas c;
    c.addCube(0, 0, 0, "Stone");
    c.chamferEdge(0, 0, 0, 9, 9, 9, "x", "+y+z", 3);   // bevel the top-front edge
    EXPECT_LT(c.cellCount(), 729u);     // material was removed
    EXPECT_GT(c.report().total(), 1);   // no longer a single clean cube
}
