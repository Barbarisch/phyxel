// Structure LOD chain (TemplateLodChain::structureConfig + buildFromSoup) — the operator
// generalized from trees to buildings. What defines a structure is its exposed SHELL:
// 1-voxel walls lose every coverage contest at coarse cells (11-33% of a 2-3 voxel cell)
// and erode into facade holes under the tree rules, while interior room fill fattens the
// box. The structure preset protects exposed-shell cells unconditionally, hollows fully
// enclosed cells, and keeps small islands (fences, wells) alive.

#include <gtest/gtest.h>

#include "core/TemplateLodChain.h"

#include <algorithm>
#include <set>

using namespace Phyxel::Core;

namespace {

/// Add a solid voxel box [min, max] (inclusive, voxel coords) to the soup as micros.
void addBox(TemplateLodChain::MicroSoup& soup, const glm::ivec3& mn, const glm::ivec3& mx,
            uint16_t mat) {
    for (int x = mn.x * 9; x < (mx.x + 1) * 9; ++x)
        for (int y = mn.y * 9; y < (mx.y + 1) * 9; ++y)
            for (int z = mn.z * 9; z < (mx.z + 1) * 9; ++z)
                soup.micros.push_back({glm::ivec3(x, y, z), mat});
}

/// A 9-wide x 6-high x 1-thick wall at z=0, with a 2x2 window hole at (3..4, 2..3).
TemplateLodChain::MicroSoup wallWithWindow() {
    TemplateLodChain::MicroSoup soup;
    soup.materials = {"StoneBricks"};
    for (int x = 0; x <= 8; ++x)
        for (int y = 0; y <= 5; ++y) {
            if (x >= 3 && x <= 4 && y >= 2 && y <= 3) continue;   // window opening
            addBox(soup, glm::ivec3(x, y, 0), glm::ivec3(x, y, 0), 0);
        }
    return soup;
}

/// The level whose cell edge is exactly one voxel (9 micros) — robust against ladder
/// densification (the 2026-08-05 ladder {3,6,9,13,18,27} moved it from index 1 to 2).
const TemplateLodChain::Level& voxelLevelOf(
    const std::vector<TemplateLodChain::Level>& levels) {
    for (const auto& l : levels)
        if (l.cellSizeMicros == 9) return l;
    static TemplateLodChain::Level empty;
    return empty;
}

} // namespace

// THE POINT: a 1-voxel-thick wall must survive EVERY level as a connected, hole-free
// surface — under the tree rules it is 11% coverage at 3-voxel cells and simply vanishes.
TEST(StructureLodChainTest, ThinWallSurvivesEveryLevel) {
    auto soup = wallWithWindow();
    const auto levels =
        TemplateLodChain::buildFromSoup(soup, TemplateLodChain::structureConfig());
    ASSERT_EQ(levels.size(), 6u);   // densified 2026-08-05: {3,6,9,13,18,27} micros

    for (const auto& level : levels) {
        ASSERT_FALSE(level.cells.empty()) << "wall vanished at cell size "
                                          << level.cellSizeMicros;
        // The wall's full extent must be represented: cells span x 0..8 and y 0..5
        // (voxel units), whatever the cell size.
        const int c = level.cellSizeMicros;
        int maxCellX = 0, maxCellY = 0;
        for (const auto& cell : level.cells) {
            maxCellX = std::max(maxCellX, cell.pos.x);
            maxCellY = std::max(maxCellY, cell.pos.y);
        }
        EXPECT_GE((maxCellX + 1) * c, 9 * 9 - c) << "wall lost width at cell " << c;
        EXPECT_GE((maxCellY + 1) * c, 6 * 9 - c) << "wall lost height at cell " << c;
    }
}

// The window opening must stay OPEN at sub-voxel and voxel resolution — sealing it over
// is exactly the "lost detail" the structure preset exists to avoid.
TEST(StructureLodChainTest, WindowStaysOpenAtFineLevels) {
    auto soup = wallWithWindow();
    const auto levels =
        TemplateLodChain::buildFromSoup(soup, TemplateLodChain::structureConfig());

    for (const auto& level : levels) {
        if (level.cellSizeMicros > 9) continue;   // coarse cells may legally swallow it
        const int c = level.cellSizeMicros;
        for (const auto& cell : level.cells) {
            // Window voxels x 3..4, y 2..3 -> micro span x 27..44, y 18..35. A cell fully
            // inside that span would seal the opening.
            const int x0 = cell.pos.x * c, y0 = cell.pos.y * c;
            const bool insideWindow = x0 >= 27 && x0 + c <= 45 && y0 >= 18 && y0 + c <= 36;
            EXPECT_FALSE(insideWindow)
                << "window sealed at cell size " << c << " by cell (" << cell.pos.x << ","
                << cell.pos.y << "," << cell.pos.z << ")";
        }
    }
}

// A solid cube hollows: the level keeps its shell and drops enclosed interior cells.
TEST(StructureLodChainTest, InteriorIsHollowedOut) {
    TemplateLodChain::MicroSoup soup;
    soup.materials = {"Wood"};
    addBox(soup, glm::ivec3(0, 0, 0), glm::ivec3(8, 8, 8), 0);   // 9^3 voxel solid

    const auto levels =
        TemplateLodChain::buildFromSoup(soup, TemplateLodChain::structureConfig());
    // At voxel resolution (cell = 9 micros): solid would be 9^3 = 729 cells; the shell is
    // 9^3 - 7^3 = 386. Hollowing must reach the shell count exactly.
    const auto& voxelLevel = voxelLevelOf(levels);
    ASSERT_EQ(voxelLevel.cellSizeMicros, 9);
    EXPECT_EQ(voxelLevel.cells.size(), 386u)
        << "interior cells survived hollowing (or shell cells were lost)";
}

// Majority material per cell: a glass pane inset in a stone wall renders as glass where
// the cell is mostly glass — never OR-promoted to the surrounding wall material.
TEST(StructureLodChainTest, GlassPaneKeepsItsMaterial) {
    TemplateLodChain::MicroSoup soup;
    soup.materials = {"StoneBricks", "Glass"};
    for (int x = 0; x <= 8; ++x)
        for (int y = 0; y <= 5; ++y)
            addBox(soup, glm::ivec3(x, y, 0), glm::ivec3(x, y, 0),
                   (x >= 3 && x <= 5 && y >= 2 && y <= 3) ? uint16_t(1) : uint16_t(0));

    const auto levels =
        TemplateLodChain::buildFromSoup(soup, TemplateLodChain::structureConfig());
    const auto& voxelLevel = voxelLevelOf(levels);   // cell = 9 micros = 1 voxel
    ASSERT_EQ(voxelLevel.cellSizeMicros, 9);
    size_t glass = 0;
    for (const auto& cell : voxelLevel.cells)
        if (cell.material == "Glass") ++glass;
    EXPECT_EQ(glass, 6u) << "glass pane voxels lost their material in decimation";
}

// Fences are legitimate small islands: a detached 2-cell fence post pair must survive
// (tree island culling would delete it as debris).
TEST(StructureLodChainTest, DetachedFencePostsSurvive) {
    TemplateLodChain::MicroSoup soup;
    soup.materials = {"Wood"};
    addBox(soup, glm::ivec3(0, 0, 0), glm::ivec3(8, 5, 0), 0);      // main wall
    addBox(soup, glm::ivec3(20, 0, 0), glm::ivec3(20, 1, 0), 0);    // detached 2-voxel post

    const auto levels =
        TemplateLodChain::buildFromSoup(soup, TemplateLodChain::structureConfig());
    const auto& voxelLevel = voxelLevelOf(levels);
    bool postSurvives = false;
    for (const auto& cell : voxelLevel.cells)
        if (cell.pos.x == 20) { postSurvives = true; break; }
    EXPECT_TRUE(postSurvives) << "detached fence post culled as debris";
}

// Determinism across builds (two independent runs agree cell-for-cell).
TEST(StructureLodChainTest, Deterministic) {
    auto soup = wallWithWindow();
    const auto a = TemplateLodChain::buildFromSoup(soup, TemplateLodChain::structureConfig());
    const auto b = TemplateLodChain::buildFromSoup(soup, TemplateLodChain::structureConfig());
    ASSERT_EQ(a.size(), b.size());
    for (size_t li = 0; li < a.size(); ++li) {
        ASSERT_EQ(a[li].cells.size(), b[li].cells.size());
        for (size_t i = 0; i < a[li].cells.size(); ++i) {
            EXPECT_EQ(a[li].cells[i].pos, b[li].cells[i].pos);
            EXPECT_EQ(a[li].cells[i].material, b[li].cells[i].material);
        }
    }
}
