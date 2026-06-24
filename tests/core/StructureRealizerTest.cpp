#include <gtest/gtest.h>

#include <unordered_set>

#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"
#include "core/BuildingProgram.h"

using namespace Phyxel::Core;

namespace {
// The first-slice cottage: hall + kitchen tile a 7x9 rectangle, crawlspace, gable,
// a front door and an interior arch.
const char* kCottage = R"({
    "name": "cottage", "style": "timber_cottage", "footprint": [7, 9],
    "substructure": "crawlspace", "roof_style": "gable",
    "stories": [{
        "height": 3,
        "rooms": [
            { "id": "hall",    "rect": [0,0,4,9], "purpose": "living" },
            { "id": "kitchen", "rect": [4,0,3,9], "purpose": "kitchen" }
        ],
        "portals": [
            { "between": ["exterior","hall"], "pos": [0,3], "width": 1, "height": 2, "kind": "door" },
            { "between": ["hall","kitchen"],  "pos": [4,2], "width": 1, "height": 2, "kind": "arch" }
        ]
    }]
})";

StyleProfile timberCottageStyle() {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"({
        "timber_cottage": {
            "roof_style": "gable", "foundation": "crawlspace",
            "thickness": { "exterior_wall": 0.333, "interior_wall": 0.222,
                           "foundation_wall": 0.667, "floor": 0.333, "ceiling": 0.222 },
            "materials": { "structure": "Wood", "floor": "Wood", "roof": "Wood", "foundation": "Stone" },
            "roof": { "pitch": 0.8 }
        }
    })"));
    return *reg.get("timber_cottage");
}

StructureRealizer::ShellResult build() {
    return StructureRealizer::realizeShell(BuildingProgram::fromJson(nlohmann::json::parse(kCottage)),
                                           timberCottageStyle());
}
} // namespace

TEST(StructureRealizerTest, RealizesAndIsNotEmpty) {
    auto r = build();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_FALSE(r.canvas.empty());
}

// THE headline: walls are SUBCUBE-thin, never 1 m full-cube Minecraft walls. A
// perimeter cell's interior core is hollow (open room), with only a thin edge band
// of wall. Full cubes are allowed ONLY as invisible solid roof mass, and stay a
// small minority — never the walls.
TEST(StructureRealizerTest, WallsAreThinNotFullCube) {
    auto r = build();
    int wallMid = r.floorTopMicro + 9;     // ~1 cube up the wall

    // The core of a perimeter wall cell (cell (0,7), west edge) is OPEN, not solid:
    EXPECT_FALSE(r.canvas.occupiedMicro(0 * 9 + 4, wallMid, 7 * 9 + 4))
        << "perimeter cell interior is solid — that's a full-thickness (1 m) wall";
    // ...while its outer edge band IS the wall:
    EXPECT_TRUE(r.canvas.occupiedMicro(0 * 9 + 0, wallMid, 7 * 9 + 4));

    auto rep = r.canvas.report();
    EXPECT_GT(rep.subcubes, 0);
    // Whatever full cubes exist are solid roof mass only — a small minority of subcubes.
    EXPECT_LT(rep.cubes * 5, rep.subcubes) << rep.summary();
}

TEST(StructureRealizerTest, FloorIsContinuousOverFootprint) {
    auto r = build();
    // The walkable floor surface is just below floorTopMicro; sample the cube row's floor band.
    int floorBand = r.crawlHeightCubes * 9;     // bottom of the finish-floor slab
    auto floored = [&](int cx, int cz) {
        // some micro cell of this cell at the floor band is solid
        for (int mx = 0; mx < 9; ++mx)
            for (int mz = 0; mz < 9; ++mz)
                if (r.canvas.occupiedMicro(cx * 9 + mx, floorBand, cz * 9 + mz)) return true;
        return false;
    };
    for (int x = 0; x < 7; ++x)
        for (int z = 0; z < 9; ++z)
            EXPECT_TRUE(floored(x, z)) << "no floor under cell (" << x << "," << z << ")";
}

TEST(StructureRealizerTest, FrontDoorIsCarvedThroughTheWall) {
    auto r = build();
    // Door at pos (0,3): west wall, cell (0,3), opening from floor up. The wall band on the
    // -x edge (micro x 0..2) should be AIR across the opening height.
    int oy = r.floorTopMicro + 4;     // mid-door height
    bool anySolid = false;
    for (int mx = 0; mx < 3; ++mx)
        for (int mz = 0; mz < 9; ++mz)
            if (r.canvas.occupiedMicro(0 * 9 + mx, oy, 3 * 9 + mz)) anySolid = true;
    EXPECT_FALSE(anySolid) << "the front-door opening was not carved through the wall";
}

TEST(StructureRealizerTest, ExteriorWallExistsWhereThereIsNoDoor) {
    auto r = build();
    // The west wall away from the door (cell (0,7)) must be solid on its -x edge.
    int oy = r.floorTopMicro + 4;
    bool solid = false;
    for (int mx = 0; mx < 3; ++mx)
        for (int mz = 0; mz < 9; ++mz)
            if (r.canvas.occupiedMicro(0 * 9 + mx, oy, 7 * 9 + mz)) solid = true;
    EXPECT_TRUE(solid) << "expected a solid west wall away from the door";
}

TEST(StructureRealizerTest, CeilingAndRoofExistAboveTheRooms) {
    auto r = build();
    glm::ivec3 lo, hi;
    ASSERT_TRUE(r.canvas.microBounds(lo, hi));
    // Roof rises above the wall tops -> the build is taller than just floor+walls.
    int wallTop = r.floorTopMicro + 3 * 9;
    EXPECT_GT(hi.y, wallTop) << "ceiling/roof should rise above the wall tops";
    // Plan captured the anatomy.
    EXPECT_FALSE(r.plan.walls.empty());
    EXPECT_FALSE(r.plan.floors.empty());
    EXPECT_FALSE(r.plan.openings.empty());
    EXPECT_FALSE(r.plan.roof.empty());
}

// stack_stories (#36): realizeShell must build EVERY story, stacked — not just
// stories[0]. A 2-story cottage has real exterior walls in the upper story's band
// (where the old single-story code had only roof), and is ~a story taller.
TEST(StructureRealizerTest, StacksMultipleStories) {
    const char* kTwoStory = R"({
        "name": "cottage2", "style": "timber_cottage", "footprint": [7, 9],
        "substructure": "crawlspace", "roof_style": "gable",
        "stories": [
            { "height": 3,
              "rooms": [ {"id":"hall","rect":[0,0,4,9],"purpose":"living"},
                         {"id":"kitchen","rect":[4,0,3,9],"purpose":"kitchen"} ],
              "portals": [ {"between":["exterior","hall"],"pos":[0,3],"width":1,"height":2,"kind":"door"} ] },
            { "height": 3,
              "rooms": [ {"id":"solar","rect":[0,0,7,9],"purpose":"solar"} ],
              "portals": [ {"between":["exterior","solar"],"pos":[0,3],"width":1,"height":1,"kind":"window"} ] }
        ]
    })";
    auto two = StructureRealizer::realizeShell(
        BuildingProgram::fromJson(nlohmann::json::parse(kTwoStory)), timberCottageStyle());
    ASSERT_TRUE(two.ok) << two.error;

    // Ground wall-top = floorTop + a 3-cube wall. The SECOND story's walls live above
    // that (floor + into its wall band); cell (0,7) is solid wall in BOTH stories (no
    // opening there). The old single-story code left nothing solid this high at the edge.
    const int groundWallTop = two.floorTopMicro + 3 * 9;
    const int upperWallMid  = groundWallTop + 3 + 13;     // 2nd-story floor + into its wall
    bool upperWall = false;
    for (int mz = 0; mz < 9; ++mz)
        if (two.canvas.occupiedMicro(0 * 9 + 0, upperWallMid, 7 * 9 + mz)) upperWall = true;
    EXPECT_TRUE(upperWall) << "second-story exterior wall was not built (multi-story loop missing)";

    // ...and the 2-story build is ~a story taller than the 1-story cottage.
    auto one = build();
    glm::ivec3 lo1, hi1, lo2, hi2;
    ASSERT_TRUE(one.canvas.microBounds(lo1, hi1));
    ASSERT_TRUE(two.canvas.microBounds(lo2, hi2));
    EXPECT_GT(hi2.y, hi1.y + 20) << "2-story build should be ~a story taller than 1-story";
}

// place_stairs (#12): a ProgStair must (1) cut a stairwell hole through the upper
// story's floor slab and (2) build a flight of steps climbing into it. Previously
// ProgStair was parsed but never realized, sealing the upper floor off.
TEST(StructureRealizerTest, StairsCutUpperFloorAndBuildFlight) {
    const char* kStairHouse = R"({
        "name": "stairhouse", "style": "timber_cottage", "footprint": [7, 9],
        "substructure": "crawlspace", "roof_style": "gable",
        "stories": [
            { "height": 3,
              "rooms": [ {"id":"hall","rect":[0,0,7,9],"purpose":"living"} ],
              "portals": [ {"between":["exterior","hall"],"pos":[0,3],"width":1,"height":2,"kind":"door"} ],
              "stairs": [ {"from_story":0, "to_story":1, "rect":[1,2,2,5], "kind":"straight"} ] },
            { "height": 3, "rooms": [ {"id":"upper","rect":[0,0,7,9],"purpose":"solar"} ], "portals": [] }
        ]
    })";
    auto r = StructureRealizer::realizeShell(
        BuildingProgram::fromJson(nlohmann::json::parse(kStairHouse)), timberCottageStyle());
    ASSERT_TRUE(r.ok) << r.error;

    // crawl 1 + floor 3 -> story-0 walkable 12, +3-cube wall -> story-1 floor slab ~[39,42).
    const int slabY = 40;
    // (1) the stairwell HOLE: at a low stair-rect cell (2,2) the upper slab is CUT -> air.
    EXPECT_FALSE(r.canvas.occupiedMicro(2 * 9 + 4, slabY, 2 * 9 + 4))
        << "the upper floor slab was not cut for the stairwell";
    // ...while away from the stairwell the upper floor is intact -> solid.
    EXPECT_TRUE(r.canvas.occupiedMicro(5 * 9 + 4, slabY, 7 * 9 + 4))
        << "upper floor missing away from the stairwell";
    // (2) a solid STEP climbs in the lower story within the stair rect (above the lower floor).
    EXPECT_TRUE(r.canvas.occupiedMicro(2 * 9 + 4, 15, 2 * 9 + 4))
        << "no stair step built in the stairwell";
}

namespace {
StyleProfile cottageStyleWithPitchDeg(double deg) {
    StyleProfileRegistry reg;
    std::string js = std::string(R"json({ "timber_cottage": {
        "roof_style": "gable", "foundation": "crawlspace",
        "thickness": { "exterior_wall": 0.222, "interior_wall": 0.111,
                       "foundation_wall": 0.444, "floor": 0.333, "ceiling": 0.111 },
        "materials": { "structure": "Wood", "floor": "Wood", "roof": "Wood", "foundation": "Stone" },
        "roof": { "pitch_deg": )json") + std::to_string(deg) + R"json( } } })json";
    reg.loadFromJson(nlohmann::json::parse(js));
    return *reg.get("timber_cottage");
}
} // namespace

// The realizer must honor the GROUNDED roof pitch: a thatch-steep 55deg roof rises
// higher than a shallow 30deg roof on the same footprint.
TEST(StructureRealizerTest, HonorsRoofPitchDegree) {
    auto program = BuildingProgram::fromJson(nlohmann::json::parse(kCottage));
    auto steep = StructureRealizer::realizeShell(program, cottageStyleWithPitchDeg(55.0));
    auto shallow = StructureRealizer::realizeShell(program, cottageStyleWithPitchDeg(30.0));
    ASSERT_TRUE(steep.ok);
    ASSERT_TRUE(shallow.ok);
    glm::ivec3 lo1, hi1, lo2, hi2;
    ASSERT_TRUE(steep.canvas.microBounds(lo1, hi1));
    ASSERT_TRUE(shallow.canvas.microBounds(lo2, hi2));
    EXPECT_GT(hi1.y, hi2.y) << "steeper grounded pitch should produce a taller roof";
}

TEST(StructureRealizerTest, ToStructureResultOffsetsAndPreservesCount) {
    auto r = build();
    glm::ivec3 origin(100, 16, -50);
    auto sr = StructureRealizer::toStructureResult(r, origin);

    // One placement per exported voxel, levels mapped, positions world-offset.
    EXPECT_EQ(sr.voxels.size(), r.canvas.exportVoxels().size());
    ASSERT_FALSE(sr.voxels.empty());
    glm::ivec3 lo(INT_MAX), hi(INT_MIN);
    for (const auto& v : sr.voxels) { lo = glm::min(lo, v.position); hi = glm::max(hi, v.position); }
    EXPECT_GE(lo.x, origin.x);
    EXPECT_GE(lo.y, origin.y);     // canvas y=0 maps to origin.y (foundation bottom)
    EXPECT_GE(lo.z, origin.z);
    // levels actually used (it's a multi-resolution structure, not all cubes)
    bool sawSub = false;
    for (const auto& v : sr.voxels) if (v.level == VoxelLevel::Subcube) sawSub = true;
    EXPECT_TRUE(sawSub);
}

// Does the realizer emit any two voxels claiming the same space? Expand every
// placement to its micro cells and check for a double-claim. If this passes, any
// live placement "failures" are collisions with the PRE-EXISTING world (terrain /
// other structures), not a generator overlap bug — which is the P2 (seating) fix.
TEST(StructureRealizerTest, NoInternalVoxelOverlap) {
    auto r = build();
    auto sr = StructureRealizer::toStructureResult(r, glm::ivec3(0, 0, 0));

    std::unordered_set<long long> claimed;
    auto key = [](int x, int y, int z) {
        return (static_cast<long long>(x + (1 << 20)) ) |
               (static_cast<long long>(y + (1 << 20)) << 21) |
               (static_cast<long long>(z + (1 << 20)) << 42);
    };
    int collisions = 0;
    auto claim = [&](int mx, int my, int mz) {
        if (!claimed.insert(key(mx, my, mz)).second) ++collisions;
    };
    for (const auto& v : sr.voxels) {
        int bx = v.position.x * 9, by = v.position.y * 9, bz = v.position.z * 9;
        if (v.level == VoxelLevel::Cube) {
            for (int i = 0; i < 9; ++i) for (int j = 0; j < 9; ++j) for (int k = 0; k < 9; ++k)
                claim(bx + i, by + j, bz + k);
        } else if (v.level == VoxelLevel::Subcube) {
            int sx = v.subcubePos.x * 3, sy = v.subcubePos.y * 3, sz = v.subcubePos.z * 3;
            for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) for (int k = 0; k < 3; ++k)
                claim(bx + sx + i, by + sy + j, bz + sz + k);
        } else {
            claim(bx + v.subcubePos.x * 3 + v.microcubePos.x,
                  by + v.subcubePos.y * 3 + v.microcubePos.y,
                  bz + v.subcubePos.z * 3 + v.microcubePos.z);
        }
    }
    EXPECT_EQ(collisions, 0) << collisions << " micro cells double-claimed by the realizer";
}

TEST(StructureRealizerTest, VoxelCountIsReasonable) {
    auto r = build();
    auto rep = r.canvas.report();
    // A small cottage shell should be well under the asset/structure budget and
    // dramatically cheaper than a naive all-micro encoding.
    EXPECT_LT(rep.total(), 20000) << rep.summary();
    EXPECT_LT(rep.total(), rep.microCells() / 4);
}
