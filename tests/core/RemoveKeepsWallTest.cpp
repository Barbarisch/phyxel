// Removing furniture must not take the wall behind it.
//
// Furniture is PLACED at micro resolution (placeTemplateMicro) but was ERASED at
// cube resolution: remove() called clearRegion() over the object's whole bounding
// box, deleting every cube it touched. A 0.33 m stool standing against a wall
// shares a cube with the wall skin, so removing it punched a cube-sized hole clean
// through the building — the recessed bays in the tavern's east elevation, which
// you could see the interior and the chimney through.
//
// Nothing caught it: the canvas is flush, the export is lossless, placement drops
// nothing and the mesher is innocent, because the damage happens afterwards through
// PlacedObjectManager. This is the missing assertion.

#include <gtest/gtest.h>

#include "core/Chunk.h"
#include "core/ChunkManager.h"
#include "core/ObjectTemplateManager.h"
#include "core/PlacedObjectManager.h"

using namespace Phyxel;
using namespace Phyxel::Core;

TEST(RemoveKeepsWall, RemovingFurnitureLeavesTheWallStanding) {
    ChunkManager chunks;
    chunks.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
    ObjectTemplateManager templates(&chunks, nullptr);
    templates.loadTemplates("resources/templates");
    if (!templates.getTemplate("stool")) GTEST_SKIP() << "repo-root CWD required for templates";

    PlacedObjectManager placed(&chunks, &templates, nullptr);

    // A wall skin: one micro-thin column of WoodPlanks standing in cube (5,5,5),
    // occupying only the outermost 1/3 slice — exactly how a real exterior wall
    // band sits inside its cube.
    const glm::ivec3 wallCube(5, 5, 5);
    chunks.ensureChunkAt(wallCube);
    Chunk* c = chunks.getChunkAtFast(wallCube);
    ASSERT_NE(c, nullptr);
    const glm::ivec3 lp = Utils::CoordinateUtils::worldToLocalCoord(wallCube);
    int wallCells = 0;
    for (int sy = 0; sy < 3; ++sy)
        for (int sz = 0; sz < 3; ++sz)
            for (int my = 0; my < 3; ++my)
                for (int mz = 0; mz < 3; ++mz)
                    if (c->addMicrocube(lp, glm::ivec3(2, sy, sz), glm::ivec3(2, my, mz),
                                        "WoodPlanks"))
                        ++wallCells;
    ASSERT_GT(wallCells, 0) << "fixture: wall skin did not build";

    auto wallIntact = [&]() {
        int n = 0;
        for (int sy = 0; sy < 3; ++sy)
            for (int sz = 0; sz < 3; ++sz)
                for (int my = 0; my < 3; ++my)
                    for (int mz = 0; mz < 3; ++mz)
                        if (c->getMicrocubeAt(lp, glm::ivec3(2, sy, sz), glm::ivec3(2, my, mz)))
                            ++n;
        return n;
    };
    ASSERT_EQ(wallIntact(), wallCells);

    // Stand a stool in the SAME cube, against the inner face of that wall.
    const std::string id = placed.placeTemplateMicro("stool", wallCube * 9, 0);
    ASSERT_FALSE(id.empty()) << "fixture: stool did not place";

    // Now remove it. The stool goes; the wall stays.
    ASSERT_TRUE(placed.remove(id));
    EXPECT_EQ(wallIntact(), wallCells)
        << "removing the stool destroyed " << (wallCells - wallIntact())
        << " of the wall's " << wallCells << " cells — a hole through the building";
}
