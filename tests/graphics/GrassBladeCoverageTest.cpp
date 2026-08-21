// GrassBladeCoverageTest.cpp — every exposed grass top sprouts a blade instance.
//
// Live bug (user-reported 2026-08-21, BridgeVis meadow): blade instances are ABSENT in
// strips hugging terrain step contours — bald bare terraces that read as "broken LOD"
// against the bladed meadow, view-angle contrast making them pop in and out. The world
// data was proven correct (voxels == generator, 109/110 columns) and the mesh faithful
// (remesh no-op); the defect is in the blade PLANTING scan in rebuildCubeFaces.
//
// The contract pinned here: a grass-topped cube with air above emits exactly ONE
// GrassInstanceData, terraces included ("a terrace inside a meadow must not taper" — the
// C4 comment; absence is the extreme taper). Pure CPU via rebuildAllFaces.

#include <gtest/gtest.h>

#include "graphics/ChunkRenderManager.h"
#include "core/Cube.h"
#include "core/Microcube.h"
#include "core/Subcube.h"

#include <memory>
#include <set>
#include <vector>

using namespace Phyxel;
using namespace Phyxel::Graphics;

namespace {

std::vector<std::unique_ptr<Subcube>> noSubs() { return {}; }
std::vector<std::unique_ptr<Microcube>> noMicros() { return {}; }

// Decode the blade's cell from GrassInstanceData.packed (x | y<<5 | z<<10, 5 bits each).
glm::ivec3 bladeCell(const GrassInstanceData& g) {
    return {static_cast<int>(g.packed & 0x1F), static_cast<int>((g.packed >> 5) & 0x1F),
            static_cast<int>((g.packed >> 10) & 0x1F)};
}

}  // namespace

// A full 32x32 grass floor with a STEP: west half tops at y=11, east half at y=10 (the
// 1-cube terrace every gentle slope produces). Every column's top must carry a blade.
TEST(GrassBladeCoverageTest, EveryGrassTopOnATerracedFloorSproutsABlade) {
    std::vector<std::unique_ptr<Cube>> cubes;
    auto topY = [](int x) { return x < 16 ? 11 : 10; };
    for (int x = 0; x < 32; ++x)
        for (int z = 0; z < 32; ++z)
            for (int y = 8; y <= topY(x); ++y)
                cubes.push_back(std::make_unique<Cube>(glm::ivec3(x, y, z), "Grass"));

    ChunkRenderManager crm;
    crm.rebuildAllFaces(cubes, noSubs(), noMicros(), glm::ivec3(0, 0, 0));

    std::set<std::pair<int, int>> bladed;
    for (const auto& g : crm.getGrassInstances()) {
        const glm::ivec3 c = bladeCell(g);
        EXPECT_EQ(c.y, topY(c.x)) << "blade planted off the surface at (" << c.x << ","
                                  << c.y << "," << c.z << ")";
        bladed.insert({c.x, c.z});
    }
    std::string missing;
    int missCount = 0;
    for (int x = 0; x < 32; ++x)
        for (int z = 0; z < 32; ++z)
            if (!bladed.count({x, z})) {
                ++missCount;
                if (missCount <= 12)
                    missing += "(" + std::to_string(x) + "," + std::to_string(z) + ") ";
            }
    EXPECT_EQ(missCount, 0) << missCount
                            << " grass tops have NO blade instance — first: " << missing;
    EXPECT_EQ(crm.getGrassInstances().size(), 1024u)
        << "expected exactly one blade instance per column";
}

// Same floor, flat (control): trivially full coverage — pins that the fixture itself is
// planted correctly so a terraced failure above is attributable to the step.
TEST(GrassBladeCoverageTest, FlatFloorControlIsFullyBladed) {
    std::vector<std::unique_ptr<Cube>> cubes;
    for (int x = 0; x < 32; ++x)
        for (int z = 0; z < 32; ++z)
            cubes.push_back(std::make_unique<Cube>(glm::ivec3(x, 10, z), "Grass"));

    ChunkRenderManager crm;
    crm.rebuildAllFaces(cubes, noSubs(), noMicros(), glm::ivec3(0, 0, 0));
    EXPECT_EQ(crm.getGrassInstances().size(), 1024u);
}
