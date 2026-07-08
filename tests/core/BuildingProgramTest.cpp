#include <gtest/gtest.h>

#include "core/BuildingProgram.h"
#include "core/AssemblyPlan.h"

using namespace Phyxel::Core;

TEST(BuildingProgramTest, RectArrayConvention) {
    Rect r = Rect::fromJson(nlohmann::json::parse("[1,2,3,4]"));
    EXPECT_EQ(r.x, 1);
    EXPECT_EQ(r.z, 2);
    EXPECT_EQ(r.w, 3);
    EXPECT_EQ(r.d, 4);
    EXPECT_EQ(r.x1(), 4);   // x + w
    EXPECT_EQ(r.z1(), 6);   // z + d
}

TEST(BuildingProgramTest, RoundTripsThroughJson) {
    auto j = nlohmann::json::parse(R"({
        "name": "cottage_a",
        "style": "timber_cottage",
        "function": "house",
        "footprint": [7, 9],
        "substructure": "crawlspace",
        "roof_style": "gable",
        "stories": [{
            "height": 3,
            "rooms": [
                { "id": "hall", "rect": [0,0,4,9], "purpose": "living", "floor_mat": "Wood" },
                { "id": "kitchen", "rect": [4,0,3,9], "purpose": "kitchen" }
            ],
            "portals": [
                { "between": ["exterior","hall"], "pos": [0,3], "width": 1, "height": 2,
                  "kind": "door", "door": { "lockable": true, "key": "brass_key" } },
                { "between": ["hall","kitchen"], "pos": [4,2], "width": 1, "height": 2, "kind": "arch" }
            ],
            "fixtures": [
                { "type": "table_dining", "rect": [1,4,2,1], "facing": "north", "room": "hall" }
            ]
        }]
    })");

    BuildingProgram p1 = BuildingProgram::fromJson(j);
    // round-trip: serialize and re-parse, then assert the structure survived
    BuildingProgram p = BuildingProgram::fromJson(p1.toJson());

    EXPECT_EQ(p.name, "cottage_a");
    EXPECT_EQ(p.style, "timber_cottage");
    EXPECT_EQ(p.function, "house");
    EXPECT_EQ(p.footprintW, 7);
    EXPECT_EQ(p.footprintD, 9);
    EXPECT_EQ(p.substructure, "crawlspace");
    EXPECT_EQ(p.roofStyle, "gable");

    ASSERT_EQ(p.stories.size(), 1u);
    const ProgStory& s = p.stories[0];
    EXPECT_EQ(s.height, 3);
    ASSERT_EQ(s.rooms.size(), 2u);
    EXPECT_EQ(s.rooms[0].id, "hall");
    EXPECT_EQ(s.rooms[0].purpose, "living");
    EXPECT_EQ(s.rooms[1].rect.w, 3);

    ASSERT_EQ(s.portals.size(), 2u);
    EXPECT_EQ(s.portals[0].a, "exterior");
    EXPECT_EQ(s.portals[0].kind, "door");
    EXPECT_TRUE(s.portals[0].lockable);
    EXPECT_EQ(s.portals[0].key, "brass_key");
    EXPECT_TRUE(s.portals[1].passable());     // arch is passable
    EXPECT_EQ(s.portals[1].kind, "arch");

    ASSERT_EQ(s.fixtures.size(), 1u);
    EXPECT_EQ(s.fixtures[0].type, "table_dining");
    EXPECT_EQ(s.fixtures[0].facing, "north");
}

TEST(AssemblyPlanTest, RoundTripsThroughJson) {
    AssemblyPlan plan;
    plan.foundation.push_back({2, 3, 12, 16, "Stone"});
    plan.walls.push_back({0, 0, 7, 0, 16, 3, 0.333, "Wood", "exterior"});
    plan.floors.push_back({0, 0, 7, 9, 16, 0.333, "Wood", "floor"});
    plan.openings.push_back({0, 17, 3, 1, 2, 1, "door", "open"});
    FixturePlacement f;
    f.archetype = "table_dining"; f.templateName = "table_oak"; f.worldPos = {3, 17, 4}; f.rotation = 90;
    plan.fixtures.push_back(f);
    plan.lights.push_back({});

    AssemblyPlan p = AssemblyPlan::fromJson(plan.toJson());

    ASSERT_EQ(p.foundation.size(), 1u);
    EXPECT_EQ(p.foundation[0].bearingY, 12);
    EXPECT_EQ(p.foundation[0].topY, 16);
    ASSERT_EQ(p.walls.size(), 1u);
    EXPECT_DOUBLE_EQ(p.walls[0].thickness, 0.333);
    EXPECT_EQ(p.walls[0].type, "exterior");
    ASSERT_EQ(p.openings.size(), 1u);
    EXPECT_EQ(p.openings[0].kind, "door");
    ASSERT_EQ(p.fixtures.size(), 1u);
    EXPECT_EQ(p.fixtures[0].archetype, "table_dining");
    EXPECT_EQ(p.fixtures[0].worldPos.y, 17);
    EXPECT_EQ(p.fixtures[0].rotation, 90);
    ASSERT_EQ(p.lights.size(), 1u);
}

// ============================================================================
// featureAt — the structural-feature classifier. Consumers ask the building's
// anatomy ("is this cell a wall / floor / ceiling?") instead of sniffing voxel
// materials, so queries keep working whatever the style palette names.
// ============================================================================

TEST(AssemblyPlanTest, FeatureAtClassifiesExteriorWall) {
    AssemblyPlan plan;
    // per-edge-cell exterior segment: wall band in cube (0,4), outside neighbor (-1,4)
    plan.walls.push_back({0, 4, -1, 4, 2, 3, 0.333, "WoodPlanks", "exterior"});
    EXPECT_EQ(plan.featureAt({0, 2, 4}), "wall");   // base course
    EXPECT_EQ(plan.featureAt({0, 4, 4}), "wall");   // top course
    EXPECT_EQ(plan.featureAt({0, 5, 4}), "");       // above the wall
    EXPECT_EQ(plan.featureAt({1, 3, 4}), "");       // interior cell beside it
    EXPECT_EQ(plan.featureAt({-1, 3, 4}), "");      // outside neighbor is NOT the wall
}

TEST(AssemblyPlanTest, FeatureAtClassifiesInteriorPartition) {
    AssemblyPlan plan;
    // partition plane on the cube boundary at x=3, spanning z in [1,5)
    plan.walls.push_back({3, 1, 3, 5, 2, 3, 0.222, "WoodPlanks", "interior"});
    EXPECT_EQ(plan.featureAt({2, 3, 2}), "wall");   // straddles both adjacent cubes
    EXPECT_EQ(plan.featureAt({3, 3, 2}), "wall");
    EXPECT_EQ(plan.featureAt({4, 3, 2}), "");       // one cube further is open room
    EXPECT_EQ(plan.featureAt({3, 3, 5}), "");       // past the segment's z-run
}

TEST(AssemblyPlanTest, FeatureAtClassifiesFloorCeilingFoundationRoof) {
    AssemblyPlan plan;
    plan.floors.push_back({0, 0, 6, 5, 2, 0.333, "Wood", "floor"});
    plan.floors.push_back({0, 0, 6, 5, 6, 0.222, "WoodPlanks", "ceiling"});
    plan.foundation.push_back({0, 0, 0, 2, "Stone"});
    plan.roof.push_back({0, 0, 6, 5, 7, 50.0, "gable", "Thatch"});
    EXPECT_EQ(plan.featureAt({3, 2, 2}), "floor");
    EXPECT_EQ(plan.featureAt({3, 6, 2}), "ceiling");
    EXPECT_EQ(plan.featureAt({0, 1, 0}), "foundation");
    EXPECT_EQ(plan.featureAt({3, 8, 2}), "roof");
    EXPECT_EQ(plan.featureAt({3, 4, 2}), "");       // open room air between floor and ceiling
}
