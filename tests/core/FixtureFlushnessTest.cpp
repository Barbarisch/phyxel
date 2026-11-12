#include <gtest/gtest.h>

#include "core/FurniturePlacer.h"
#include "core/BuildingProgram.h"

using namespace Phyxel::Core;

// ============================================================================
// KI-5b — wall fixtures not flush against walls (USER observation): every
// wall-backed placement was inset by the EXTERIOR wall thickness, but interior
// partitions are thin and their band straddles the room boundary — on a stone
// style (exterior clamps to 9 micro) a piece backed on a 2-micro partition
// floated 9-1=8 micro (~0.9 m) off the wall. RED (pre-change arithmetic):
// microWorldPos(interior-wall piece) = cube*9 - 9; GREEN: cube*9 - 1 (the
// partition's half-band). Placements now carry their own per-wall insetMicro.
// ============================================================================

namespace {
ProgStory twoRooms() {
    // 10x6 footprint, two rooms sharing an interior partition at x=5.
    ProgStory s;
    s.height = 3;
    ProgRoom a; a.id = "west"; a.rect = {0, 0, 5, 6}; a.purpose = "chamber";
    ProgRoom b; b.id = "east"; b.rect = {5, 0, 5, 6}; b.purpose = "chamber";
    s.rooms.push_back(a);
    s.rooms.push_back(b);
    return s;
}
} // namespace

TEST(FixtureFlushnessTest, InteriorPartitionInsetIsThin) {
    const ProgStory st = twoRooms();
    std::map<std::string, Footprint> fps;
    fps["bed"] = {2, 3};
    fps["wardrobe"] = {1, 2};

    // extT=9 (stone, cube-thick), intT=2 (0.222 partitions).
    auto placements = FurniturePlacer::furnish(st, glm::ivec3(0), 17, fps, nullptr, 9, "",
                                               {}, 2);
    ASSERT_FALSE(placements.empty());

    int extSeen = 0, intSeen = 0;
    for (const auto& p : placements) {
        if (p.backDir == glm::ivec3(0)) continue;             // centred pieces carry no inset
        // PER-AXIS: the x-axis wall is the interior partition (x=5) when the piece backs
        // toward it from either room; footprint-edge walls (x=0, x=10, z=0, z=6) are
        // exterior. A corner piece must carry BOTH the thin and thick inset, per axis.
        if (p.backDir.x != 0) {
            ASSERT_GE(p.insetMicroX, 0) << p.type << " x-wall piece missing inset";
            const bool interiorX = (p.backDir.x > 0 && p.worldPos.x < 5) ||
                                   (p.backDir.x < 0 && p.worldPos.x >= 5);
            if (interiorX) {
                EXPECT_EQ(p.insetMicroX, (2 + 1) / 2)
                    << p.type << " on the interior partition got x-inset " << p.insetMicroX
                    << " (would float " << (p.insetMicroX - 1) << " micro off the wall)";
                ++intSeen;
            } else {
                EXPECT_EQ(p.insetMicroX, 9) << p.type << " exterior x-inset wrong";
                ++extSeen;
            }
        }
        if (p.backDir.z != 0) {
            ASSERT_GE(p.insetMicroZ, 0) << p.type << " z-wall piece missing inset";
            EXPECT_EQ(p.insetMicroZ, 9)                      // all z walls here are exterior
                << p.type << " exterior z-inset wrong";
            ++extSeen;
        }
        // The consumer arithmetic honors the carried per-axis insets.
        const glm::ivec3 mp = FurniturePlacer::microWorldPos(p, /*extTMicro=*/9, 153);
        const int tx = p.insetMicroX >= 0 ? p.insetMicroX : 9;
        const int tz = p.insetMicroZ >= 0 ? p.insetMicroZ : 9;
        EXPECT_EQ(mp.x, p.worldPos.x * 9 - p.backDir.x * tx);
        EXPECT_EQ(mp.z, p.worldPos.z * 9 - p.backDir.z * tz);
    }
    EXPECT_GT(extSeen, 0) << "no exterior-wall piece placed - fixture can't discriminate";
    // Interior-partition placements depend on packing order; if none landed there the
    // discrimination assert above never ran — flag it so the fixture gets adjusted
    // rather than silently passing.
    EXPECT_GT(intSeen, 0) << "no interior-partition piece placed - adjust the fixture";
}

// ============================================================================
// KI-5i (found 2026-11-11 while moving hearths to the 1/8 m grid) - a piece that
// is NOT a whole number of cubes deep floats off a MAX-side (+x / +z) wall. The
// anchor was footprintMin*9 - backDir*inset for every wall: flush on a -side
// wall (the back face IS the anchor), but on a +side wall the back face is the
// anchor PLUS the piece's extent, which falls short of the wall by
// cubes*9 - extent micro. 60 of 81 committed furniture/architecture templates
// are not whole cubes; a wall_lantern floated 7 micro (0.78 m) off a +z wall.
// GREEN: the back face sits on the wall's inner face on all four walls, and a
// whole-cube piece lands exactly where it always did.
// ============================================================================
TEST(FixtureFlushnessTest, ShallowPieceIsFlushOnEveryWall) {
    // A 1x1 shelf 4 micro deep (bookshelf: microW 8, microD 3), back = template z 0.
    Footprint fp; fp.width = 1; fp.depth = 1; fp.microW = 8; fp.microD = 3;
    const int t = 3;                              // exterior band 3 micro
    struct C { int rot; glm::ivec3 back; glm::ivec3 cube; };
    for (const C& c : {C{0,   {0, 0, -1}, {4, 0, 0}},     // -z wall: band z 0..2
                       C{180, {0, 0,  1}, {4, 0, 8}},     // +z wall: band z 78..80 (room z1 = 9)
                       C{270, {-1, 0, 0}, {0, 0, 4}},     // -x wall: band x 0..2
                       C{90,  {1, 0,  0}, {6, 0, 4}}}) {  // +x wall: band x 60..62 (room x1 = 7)
        FurniturePlacement p;
        p.worldPos = c.cube; p.backDir = c.back; p.rotation = c.rot;
        p.insetMicroX = c.back.x ? t : -1; p.insetMicroZ = c.back.z ? t : -1;
        const glm::ivec3 m = FurniturePlacer::microWorldPos(p, t, 0, fp);
        const bool turned = c.rot == 90 || c.rot == 270;
        const int ex = (turned ? fp.microD : fp.microW) + 1;   // occupied micro along x
        const int ez = (turned ? fp.microW : fp.microD) + 1;   // occupied micro along z
        if (c.back.z < 0) EXPECT_EQ(m.z, t) << c.rot;                       // back face on z = 3
        if (c.back.z > 0) EXPECT_EQ(m.z + ez, 9 * 9 - t) << c.rot;          // back face on z = 77
        if (c.back.x < 0) EXPECT_EQ(m.x, t) << c.rot;
        if (c.back.x > 0) EXPECT_EQ(m.x + ex, 7 * 9 - t) << c.rot;          // back face on x = 59
        // The reservation agrees: a flush shallow piece occupies its own cube, no spill.
        const CubeSpan s = placedCubeSpan(fp.microW, fp.microD, c.rot, c.back, t, c.cube.x, c.cube.z);
        EXPECT_EQ(s.width(), 1) << c.rot;
        EXPECT_EQ(s.depth(), 1) << c.rot;
        EXPECT_EQ(s.minX, c.cube.x) << c.rot;
        EXPECT_EQ(s.minZ, c.cube.z) << c.rot;
    }
    // A whole-cube piece on a +side wall keeps today's anchor (cube*9 - inset).
    Footprint whole; whole.microW = 8; whole.microD = 8;
    FurniturePlacement q; q.worldPos = {4, 0, 8}; q.backDir = {0, 0, 1}; q.insetMicroZ = t;
    EXPECT_EQ(FurniturePlacer::microWorldPos(q, t, 0, whole).z, 8 * 9 - t);
    EXPECT_EQ(FurniturePlacer::microWorldPos(q, t, 0).z, 8 * 9 - t) << "no-footprint overload changed";
}
