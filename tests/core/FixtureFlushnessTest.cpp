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
