#include <gtest/gtest.h>

#include "core/FurniturePlacer.h"
#include "core/BuildingProgram.h"

using namespace Phyxel::Core;

// ============================================================================
// KI-5d â€” stairs overlap furniture (USER observation, 2026-07-23): furnish()
// reserved doorway thresholds but NOT stair cells, so pieces landed straight on
// the stair base / the arriving well. The red premise is proven in-test: the
// legacy call (no reservedRects) places into the stair rect; the fixed call
// (rects passed) must not touch rect + 1-cell landing margin.
// ============================================================================

namespace {
ProgStory hallWithStair(Rect& stairOut) {
    ProgStory s;
    s.height = 3;
    ProgRoom r;
    r.id = "hall";
    r.rect = {0, 0, 6, 6};
    r.purpose = "chamber";        // recipe leads with a wall-backed bed
    s.rooms.push_back(r);
    ProgStair st;
    st.fromStory = 0; st.toStory = 1;
    // Occupies the WEST wall band — the first wall the placer packs, so the legacy
    // (unreserved) call deterministically drops the first piece onto the stair.
    st.rect = {0, 0, 2, 6};
    stairOut = st.rect;
    s.stairs.push_back(st);
    return s;
}

bool overlapsRect(const FurniturePlacement& p, const Footprint& fp, const Rect& r, int margin) {
    // Placement cell + its footprint span (width/depth swap by rotation is placement
    // detail; test conservatively with the max span both ways).
    const int span = std::max(std::max(1, fp.width), std::max(1, fp.depth));
    for (int dx = 0; dx < span; ++dx)
        for (int dz = 0; dz < span; ++dz) {
            const int x = p.worldPos.x + dx, z = p.worldPos.z + dz;
            if (x >= r.x - margin && x < r.x + r.w + margin &&
                z >= r.z - margin && z < r.z + r.d + margin)
                return true;
        }
    return false;
}
} // namespace

TEST(StairReservationTest, FurnitureNeverCoversStairCells) {
    Rect stair;
    const ProgStory story = hallWithStair(stair);
    std::map<std::string, Footprint> fps;   // default 1x1 footprints

    // RED premise (legacy behavior, no reservation): at least one piece lands inside
    // the stair rect â€” the defect this test exists to pin. If the placer someday
    // avoids stairs without reservation, this premise check tells us the test needs
    // a new red vehicle rather than silently passing.
    auto legacy = FurniturePlacer::furnish(story, glm::ivec3(0), 17, fps);
    bool legacyOverlap = false;
    for (const auto& p : legacy)
        if (overlapsRect(p, Footprint{}, stair, 0)) legacyOverlap = true;
    EXPECT_TRUE(legacyOverlap)
        << "red premise gone: legacy furnish no longer overlaps stairs - update the test";

    // GREEN: with the stair rect reserved, no placement touches rect + 1-cell margin.
    auto fixed = FurniturePlacer::furnish(story, glm::ivec3(0), 17, fps, nullptr, 0, "",
                                          {stair});
    ASSERT_FALSE(fixed.empty()) << "room should still furnish outside the stair";
    for (const auto& p : fixed)
        EXPECT_FALSE(overlapsRect(p, Footprint{}, stair, 1))
            << p.type << " at (" << p.worldPos.x << "," << p.worldPos.z
            << ") covers the reserved stair (+margin)";
}
