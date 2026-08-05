// World-look C4: "grass should taper at the edge of a grassy area."
//
// The grass shader has no neighbour knowledge — every voxel's clump renders at full meadow
// height, so a lawn ends in a hard green cliff against dirt, sand, stone or a drop. The fix is
// per-voxel EDGE-NESS baked at mesh time: when ChunkRenderManager emits a grass instance it
// counts how many of the 8 horizontal neighbour columns are also grass-topped (within a ±1
// voxel step, the same tolerance walking terrain has) and stores the count in the previously
// reserved high bits of GrassInstanceData::tex (bits 16-19). grass.vert consumes it as a height
// multiplier: interior voxels (8/8) keep full height, boundary voxels shorten.
//
// Cross-chunk neighbours count as GRASSY by construction: material is not queryable across the
// border at mesh time, and a wrong "edge" there would draw a taper seam along every chunk
// boundary through open meadow — the exact class of per-chunk artifact the grass density LOD
// just had to remove. Missing taper on a real cross-chunk edge is the cheap half of that trade.
//
// RED before the feature: the high bits are always 0, so the interior-voxel assertion fails.

#include <gtest/gtest.h>

#include <memory>

#include "core/Chunk.h"
#include "core/Types.h"

using namespace Phyxel;

namespace {

// Flat ground: solid y=0..6 everywhere, top layer (y=6) grass for x < 16, dirt for x >= 16.
// One raised grass column at (5,5): top at y=7 — a 1-step terrace inside the meadow.
std::unique_ptr<Chunk> meadowWithDirtBorder() {
    auto c = std::make_unique<Chunk>(glm::ivec3(0));
    c->initializeForLoading();
    for (int x = 0; x < 32; ++x)
        for (int z = 0; z < 32; ++z) {
            for (int y = 0; y < 6; ++y) c->addCube(glm::ivec3(x, y, z), "Stone");
            c->addCube(glm::ivec3(x, 6, z), (x < 16) ? "Grass" : "Dirt");
        }
    c->addCube(glm::ivec3(5, 7, 5), "Grass");
    return c;
}

int edgeBitsAt(const Chunk& c, int x, int y, int z) {
    for (const auto& gi : c.getGrassInstances()) {
        const int gx = static_cast<int>(gi.packed & 0x1Fu);
        const int gy = static_cast<int>((gi.packed >> 5) & 0x1Fu);
        const int gz = static_cast<int>((gi.packed >> 10) & 0x1Fu);
        if (gx == x && gy == y && gz == z) return static_cast<int>((gi.tex >> 16) & 0xFu);
    }
    return -1;   // no grass instance at that voxel
}

} // namespace

TEST(GrassEdgeTaperTest, InteriorVoxelsCarryFullEdgeness) {
    auto c = meadowWithDirtBorder();
    c->rebuildFaces();
    // Deep inside the meadow, away from the dirt line, the terrace and the chunk border.
    EXPECT_EQ(edgeBitsAt(*c, 8, 6, 20), 8)
        << "interior grass voxel does not carry 8/8 grassy neighbours — edge-ness not baked";
}

TEST(GrassEdgeTaperTest, DirtBorderVoxelsReadAsEdge) {
    auto c = meadowWithDirtBorder();
    c->rebuildFaces();
    // x=15 borders dirt at x=16: the three +x neighbours are not grassy.
    const int e = edgeBitsAt(*c, 15, 6, 20);
    ASSERT_GE(e, 0) << "no grass instance emitted at the border voxel";
    EXPECT_LE(e, 5) << "voxel bordering dirt still reads as interior";
    EXPECT_GE(e, 4) << "a straight border should lose exactly its 3 cross-line neighbours";
}

TEST(GrassEdgeTaperTest, OneStepTerraceStaysInterior) {
    auto c = meadowWithDirtBorder();
    c->rebuildFaces();
    // The raised grass column at (5,7,5): all 8 neighbours are grass one step DOWN — walking
    // terrain steps like this must not taper, or every rolling hill gets mowed stripes.
    EXPECT_EQ(edgeBitsAt(*c, 5, 7, 5), 8)
        << "a 1-voxel terrace inside the meadow tapers — step tolerance missing";
    // And the voxels AROUND it see it as grassy too (step up).
    EXPECT_EQ(edgeBitsAt(*c, 4, 6, 5), 8);
}

TEST(GrassEdgeTaperTest, ChunkBorderCountsAsGrassyNotAsEdge) {
    auto c = meadowWithDirtBorder();
    c->rebuildFaces();
    // z=0 is the chunk border: material unknown across it, so it must count as grassy —
    // a taper seam along every chunk boundary is worse than a missing taper.
    EXPECT_EQ(edgeBitsAt(*c, 8, 6, 0), 8)
        << "chunk-border neighbours counted as edge — this draws a seam through open meadow";
}
