// Writing a FINE voxel over a COARSE one must not vaporise the coarse one.
//
// A subcube fills 27 micro cells. When a microcube was written into one of them,
// ChunkVoxelManager deleted the whole subcube and created just the incoming
// micro — silently destroying the other 26 and leaving a 1/3-metre void.
//
// That is not a theoretical concern. It is how the generated tavern got a hole
// punched through its upper floor and out through the wall beside it: the
// chimney stack is placed as loose MICROcubes up through a floor slab that is
// exactly one SUBcube thick, so every stack cell ate the slab around it. The
// realized-shell validators cannot catch it — they run on the shell before
// anything is placed into the world, and the shell is intact.
//
// Contract: refining a voxel changes its RESOLUTION, never its filled volume.

#include <gtest/gtest.h>

#include "core/Chunk.h"

using namespace Phyxel;

namespace {

// How many of a subcube's 27 micro cells are filled.
int filledMicros(Chunk& c, const glm::ivec3& cube, const glm::ivec3& sub) {
    int n = 0;
    for (int x = 0; x < 3; ++x)
        for (int y = 0; y < 3; ++y)
            for (int z = 0; z < 3; ++z)
                if (c.getMicrocubeAt(cube, sub, glm::ivec3(x, y, z))) ++n;
    return n;
}

}  // namespace

TEST(FineOverCoarse, AMicrocubeRefinesASubcubeInsteadOfErasingIt) {
    Chunk chunk(glm::ivec3(0, 0, 0));
    chunk.initializeForLoading();
    const glm::ivec3 cube(4, 4, 4), sub(1, 1, 1), micro(0, 0, 0);

    ASSERT_TRUE(chunk.addSubcube(cube, sub, "Wood")) << "fixture: subcube did not place";
    ASSERT_NE(chunk.getSubcubeAt(cube, sub), nullptr);

    // Write ONE micro cell of that subcube with a different material — the chimney
    // stack writing itself through a floor slab.
    ASSERT_TRUE(chunk.addMicrocube(cube, sub, micro, "Bricks"));

    // The cell written is the caller's material...
    const Microcube* written = chunk.getMicrocubeAt(cube, sub, micro);
    ASSERT_NE(written, nullptr);
    EXPECT_EQ(written->getMaterialName(), "Bricks");

    // ...and the REST of the subcube is still there. 26 survivors + 1 written = 27.
    EXPECT_EQ(filledMicros(chunk, cube, sub), 27)
        << "refining one micro cell destroyed the rest of the subcube — a 1/3 m hole";

    // The survivors keep the coarse voxel's material, not the newcomer's.
    int woodCells = 0;
    for (int x = 0; x < 3; ++x)
        for (int y = 0; y < 3; ++y)
            for (int z = 0; z < 3; ++z) {
                const glm::ivec3 mp(x, y, z);
                if (mp == micro) continue;
                const Microcube* m = chunk.getMicrocubeAt(cube, sub, mp);
                if (m && m->getMaterialName() == "Wood") ++woodCells;
            }
    EXPECT_EQ(woodCells, 26) << "the subdivided cells lost their original material";
}

// Refining a SECOND cell of the same subcube must not re-fill or duplicate.
TEST(FineOverCoarse, RefiningTwiceStaysConsistent) {
    Chunk chunk(glm::ivec3(0, 0, 0));
    chunk.initializeForLoading();
    const glm::ivec3 cube(2, 3, 2), sub(0, 2, 1);
    ASSERT_TRUE(chunk.addSubcube(cube, sub, "Stone"));
    ASSERT_TRUE(chunk.addMicrocube(cube, sub, glm::ivec3(0, 0, 0), "Bricks"));
    ASSERT_TRUE(chunk.addMicrocube(cube, sub, glm::ivec3(2, 2, 2), "Bricks"));

    EXPECT_EQ(filledMicros(chunk, cube, sub), 27) << "second refinement lost cells";
    const Microcube* a = chunk.getMicrocubeAt(cube, sub, glm::ivec3(0, 0, 0));
    const Microcube* b = chunk.getMicrocubeAt(cube, sub, glm::ivec3(2, 2, 2));
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(a->getMaterialName(), "Bricks");
    EXPECT_EQ(b->getMaterialName(), "Bricks");
}
