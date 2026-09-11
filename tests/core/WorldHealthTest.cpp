#include <gtest/gtest.h>

#include "core/NavGraph.h"
#include "core/WorldHealth.h"

#include <memory>
#include <unordered_set>

using namespace Phyxel;
using namespace Phyxel::Core;

// ============================================================================
// WorldHealth (WalkabilityGateAndPlaytestLoop layer B, increment 5). Ravenmere G-84: a
// world scene whose database yielded 0 chunks spawned the player into the void and the
// harness steered a falling body for minutes - nothing in the runtime said "there is no
// world under the spawn". And a generator defect that escapes the settlement gate (an
// anchor walled off) must be reported at load, before anyone plays. RED on the old code:
// no such check existed.
// ============================================================================

namespace {

struct FlatWorld {
    std::unordered_set<int64_t> solid;
    static int64_t key(int x, int y, int z) {
        return (static_cast<int64_t>(x + 100000) << 42) | (static_cast<int64_t>(y + 100000) << 21) |
               static_cast<int64_t>(z + 100000);
    }
    void fillCube(int cx, int cy, int cz) {
        for (int x = 0; x < 9; ++x) for (int y = 0; y < 9; ++y) for (int z = 0; z < 9; ++z)
            solid.insert(key(cx * 9 + x, cy * 9 + y, cz * 9 + z));
    }
    bool micro(int x, int y, int z) const { return solid.count(key(x, y, z)) > 0; }
    CellFill fill(const glm::ivec3& c) const {
        int n = 0;
        for (int x = 0; x < 9; ++x) for (int y = 0; y < 9; ++y) for (int z = 0; z < 9; ++z)
            n += micro(c.x * 9 + x, c.y * 9 + y, c.z * 9 + z);
        return n == 0 ? CellFill::Empty : (n == 729 ? CellFill::Solid : CellFill::Partial);
    }
    bool hasVoxel(const glm::ivec3& c) const { return fill(c) != CellFill::Empty; }
    std::unique_ptr<NavGraph> graph() const {
        return std::make_unique<NavGraph>(CellFillFunc([this](const glm::ivec3& c) { return fill(c); }),
                                          MicroQueryFunc([this](const glm::ivec3& m) { return micro(m.x, m.y, m.z); }));
    }
};

// A 12x12 floor at y=0 (stand at y=1) with a 3x3 walled room whose anchor sits inside.
FlatWorld roomWorld() {
    FlatWorld w;
    for (int x = 0; x < 12; ++x) for (int z = 0; z < 12; ++z) w.fillCube(x, 0, z);
    for (int x = 7; x <= 9; ++x) for (int z = 7; z <= 9; ++z)
        if (x == 7 || x == 9 || z == 7 || z == 9) for (int y = 1; y <= 3; ++y) w.fillCube(x, y, z);
    return w;
}

}  // namespace

TEST(WorldHealthTest, ReportsTheWalledOffAnchorByName) {
    const FlatWorld w = roomWorld();
    auto g = w.graph();
    g->buildRegion({0, 0}, {11, 11}, NavAgentProfile{});
    std::vector<WorldHealthAnchor> anchors = {
        {"npc_open", "npc", glm::vec3(2.5f, 1.0f, 2.5f), false, 0},
        {"npc_walled", "npc", glm::vec3(8.5f, 1.0f, 8.5f), false, 0},
        {"to_cellar", "trigger", glm::vec3(4.5f, 1.0f, 9.5f), false, 0},
    };
    const auto rep = WorldHealth::check(g.get(), [&](const glm::ivec3& c) { return w.hasVoxel(c); },
                                        glm::vec3(1.5f, 1.0f, 1.5f), anchors);
    EXPECT_TRUE(rep.terrainUnderSpawn);
    EXPECT_TRUE(rep.graphAvailable);
    EXPECT_EQ(rep.total, 3);
    EXPECT_EQ(rep.reachable, 2) << rep.summary();
    EXPECT_FALSE(rep.ok());
    ASSERT_EQ(rep.anchors.size(), 3u);
    EXPECT_TRUE(rep.anchors[0].reachable);
    EXPECT_FALSE(rep.anchors[1].reachable) << "the walled-off NPC must be reported";
    EXPECT_TRUE(rep.anchors[2].reachable);
    EXPECT_NE(rep.summary().find("UNREACHABLE npc 'npc_walled'"), std::string::npos) << rep.summary();
    const auto j = rep.toJson();
    EXPECT_EQ(j["reachable"], 2); EXPECT_EQ(j["total"], 3); EXPECT_FALSE(j["ok"].get<bool>());
}

// G-84: no terrain under the spawn is the void, reported as such (not as a pathing fact).
TEST(WorldHealthTest, AnEmptyWorldIsReportedAsNoTerrainUnderTheSpawn) {
    FlatWorld empty;
    auto g = empty.graph();
    g->buildRegion({0, 0}, {3, 3}, NavAgentProfile{});
    const auto rep = WorldHealth::check(g.get(), [&](const glm::ivec3& c) { return empty.hasVoxel(c); },
                                        glm::vec3(1.5f, 18.0f, 1.5f), {{"aldric", "npc", glm::vec3(2.5f, 18.0f, 2.5f), false, 0}});
    EXPECT_FALSE(rep.terrainUnderSpawn);
    EXPECT_FALSE(rep.ok());
    EXPECT_NE(rep.summary().find("terrain under spawn NO"), std::string::npos) << rep.summary();
}

// No graph at all (scene ready before navigation exists) is reported, never silently "all fine".
TEST(WorldHealthTest, AMissingGraphIsNotHealth) {
    const FlatWorld w = roomWorld();
    const auto rep = WorldHealth::check(nullptr, [&](const glm::ivec3& c) { return w.hasVoxel(c); },
                                        glm::vec3(1.5f, 1.0f, 1.5f), {{"npc_open", "npc", glm::vec3(2.5f, 1.0f, 2.5f), false, 0}});
    EXPECT_TRUE(rep.terrainUnderSpawn);
    EXPECT_FALSE(rep.graphAvailable);
    EXPECT_FALSE(rep.ok());
    EXPECT_EQ(rep.reachable, 0);
}

// Run 43: Bram stands at the tavern's stair rail - his own cell resolves to a tread the
// graph cannot enter, but the floor a metre away is open and every playthrough talked to
// him from there. An anchor is reachable when any cell within a metre is.
TEST(WorldHealthTest, AnAnchorBesideAnOpenCellCountsAsReachable) {
    FlatWorld w = roomWorld();
    // A 1-cube pillar at (3,3): an anchor ON it (feet y=2) has no graph surface a walker can enter.
    w.fillCube(3, 1, 3);
    auto g = w.graph();
    g->buildRegion({0, 0}, {11, 11}, NavAgentProfile{});
    const auto rep = WorldHealth::check(g.get(), [&](const glm::ivec3& c) { return w.hasVoxel(c); },
                                        glm::vec3(1.5f, 1.0f, 1.5f), {{"bram", "npc", glm::vec3(3.5f, 2.0f, 3.5f), false, 0}});
    EXPECT_EQ(rep.reachable, 1) << rep.summary();
    EXPECT_TRUE(rep.ok());
}

// Ravenmere G-88: the east exit's waystone stood on the last generated column and the road
// led into a black void. A trigger anchor within kExitMarginCubes of the world's edge is
// reported with the directions that have no ground; an interior one is not.
TEST(WorldHealthTest, AnExitAtTheWorldsEdgeIsReportedAsFacingAVoid) {
    FlatWorld w;                       // ground x 0..39, z 0..39 - wider than the 12-cube exit margin
    for (int x = 0; x < 40; ++x) for (int z = 0; z < 40; ++z) w.fillCube(x, 0, z);
    auto g = w.graph();
    g->buildRegion({0, 0}, {39, 39}, NavAgentProfile{});
    std::vector<WorldHealthAnchor> anchors(2);
    anchors[0].id = "to_farm";   anchors[0].kind = "trigger"; anchors[0].pos = glm::vec3(38.5f, 1.0f, 20.5f);   // 1 cube from the east edge
    anchors[1].id = "to_cellar"; anchors[1].kind = "trigger"; anchors[1].pos = glm::vec3(20.5f, 1.0f, 20.5f);   // the middle
    const auto rep = WorldHealth::check(g.get(), [&](const glm::ivec3& c) { return w.hasVoxel(c); },
                                        glm::vec3(1.5f, 1.0f, 1.5f), anchors);
    ASSERT_EQ(rep.anchors.size(), 2u);
    EXPECT_EQ(rep.anchors[0].voidBeyond, (std::vector<std::string>{"+x"})) << "the edge exit faces a void to +x only";
    EXPECT_TRUE(rep.anchors[1].voidBeyond.empty()) << "an interior region faces no void";
    EXPECT_EQ(rep.exitsFacingVoid, 1);
    EXPECT_FALSE(rep.ok());
    EXPECT_NE(rep.summary().find("VOID beyond exit 'to_farm'"), std::string::npos) << rep.summary();
}
