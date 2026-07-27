#include <gtest/gtest.h>

#include <fstream>

#include "core/RoomLayout.h"
#include "core/RoomProgram.h"
#include "core/BuildingProgram.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"
#include "core/TraversalProbe.h"

using namespace Phyxel::Core;

// ============================================================================
// L-PLAN Ã— THICK WALLS (KI-5h decisive experiment, 2026-07-23): stone_keep's
// 3.0 m exterior walls clamp to a FULL CUBE. On an L-plan the wing's notch-side
// wall is exterior-thick, and the hallâ†”solar door must carve through it â€” the
// user suspects such doors can stay blocked. This test measures it: a
// character-box must be able to WALK hallâ†’service and hallâ†’solar on the
// realized voxels. If it fails, the interior door carve doesn't clear
// cube-thick walls; if it passes, that suspicion is falsified for this
// geometry and KI-5h reduces to the (already-fixed-by-reorder) L-typology
// interaction plus any yet-unfound case.
// ============================================================================

namespace {
StyleProfile thickStyle() {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"({
        "stone_thick": { "roof_style":"gable", "foundation":"slab",
            "thickness": { "exterior_wall":3.0, "interior_wall":0.222, "foundation_wall":0.667,
                           "floor":0.333, "ceiling":0.222 },
            "materials": { "structure":"StoneBricks", "floor":"Wood", "roof":"WoodShingle",
                           "foundation":"Stone" },
            "roof": { "pitch":0.8 } } })"));
    return *reg.get("stone_thick");
}

const ProgRoom* byId(const ProgStory& s, const std::string& id) {
    for (const auto& r : s.rooms) if (r.id == id) return &r;
    return nullptr;
}

bool walkBetween(const StructureRealizer::ShellResult& sh, const ProgRoom& from,
                 const ProgRoom& to, int W, int D) {
    const int floorY = sh.floorTopByStory.empty() ? 12 : sh.floorTopByStory[0];
    TraversalProbe probe([&](int x, int y, int z) { return sh.canvas.occupiedMicro(x, y, z); },
                         AgentBox{2, 16, 4});
    const glm::ivec3 start((from.rect.x + from.rect.w / 2) * 9 + 4, floorY,
                           (from.rect.z + from.rect.d / 2) * 9 + 4);
    const int gx = (to.rect.x + to.rect.w / 2) * 9 + 4, gz = (to.rect.z + to.rect.d / 2) * 9 + 4;
    return probe.reachable(start, glm::ivec3(gx - 2, floorY - 1, gz - 2),
                           glm::ivec3(gx + 2, floorY + 1, gz + 2),
                           glm::ivec3(0, floorY - 2, 0), glm::ivec3(W * 9, floorY + 28, D * 9));
}
} // namespace

// The L-typology interaction (found in the same hunt): footprintShape "L" used to
// REPLACE the typology's room program with generic hall/service/solar â€” an L-shaped
// TAVERN had no taproom. The typology plan must win; the shape request is skipped
// (surfaced in the log) until winged typology programs exist.
TEST(LPlanThickWallTest, TypologyBeatsWingedShape) {
    RoomProgramRegistry reg;
    bool loaded = false;
    for (const char* path : {"resources/room_program.json", "../resources/room_program.json",
                             "../../resources/room_program.json"}) {
        std::ifstream f(path);
        if (f.good()) { loaded = reg.loadFromFile(path); break; }
    }
    if (!loaded) GTEST_SKIP() << "room_program.json not reachable from CWD";
    const RoomProgram* tavern = reg.get("tavern");
    ASSERT_NE(tavern, nullptr);

    BuildingProgram p;
    p.name = "ltavern"; p.style = "stone_thick"; p.typology = "tavern";
    p.footprintW = 16; p.footprintD = 7;          // fits the tavern program
    p.substructure = "slab"; p.footprintShape = "L";
    ProgStory s; s.height = 3; p.stories.push_back(s);
    ASSERT_TRUE(autofillRoomLayout(p, 7u, tavern));

    bool hasTaproom = false, hasSolar = false;
    for (const auto& r : p.stories[0].rooms) {
        if (r.purpose == "taproom") hasTaproom = true;
        if (r.id == "solar") hasSolar = true;
    }
    EXPECT_TRUE(hasTaproom) << "L-shaped tavern lost its taproom (typology replaced by winged)";
    EXPECT_FALSE(hasSolar) << "generic winged rooms overrode the typology";
}

// The exact footprint of the live defective building (the seed-3 village's L-plan
// stone_keep tavern was 7x16): rooms are only 3 cubes wide, so cube-thick walls
// leave 1-cube interiors â€” the tightest case the carve must still keep walkable.
TEST(LPlanThickWallTest, LiveTavernFootprint7x16IsWalkable) {
    const int W = 7, D = 16;
    BuildingProgram p;
    p.name = "lplan_live"; p.style = "stone_thick";
    p.footprintW = W; p.footprintD = D;
    p.substructure = "slab"; p.footprintShape = "L";
    ProgStory s; s.height = 3; p.stories.push_back(s);
    if (!autofillRoomLayout(p, 7u, nullptr) || p.stories[0].rooms.size() < 3)
        GTEST_SKIP() << "winged layout does not fit 7x16 with current minDim";
    const ProgStory& st = p.stories[0];
    const ProgRoom* hall = byId(st, "hall");
    const ProgRoom* service = byId(st, "service");
    const ProgRoom* solar = byId(st, "solar");
    ASSERT_TRUE(hall && service && solar);
    auto shell = StructureRealizer::realizeShell(p, thickStyle());
    ASSERT_TRUE(shell.ok) << shell.error;
    EXPECT_TRUE(walkBetween(shell, *hall, *service, W, D))
        << "hall->service blocked at the LIVE tavern footprint";
    EXPECT_TRUE(walkBetween(shell, *hall, *solar, W, D))
        << "hall->solar blocked at the LIVE tavern footprint (KI-5h)";
}

TEST(LPlanThickWallTest, InteriorDoorsAreWalkableThroughCubeThickWalls) {
    const int W = 12, D = 16;
    BuildingProgram p;
    p.name = "lplan_keep"; p.style = "stone_thick";
    p.footprintW = W; p.footprintD = D;
    p.substructure = "slab"; p.footprintShape = "L";   // no typology -> winged layout applies
    ProgStory s; s.height = 3; p.stories.push_back(s);
    ASSERT_TRUE(autofillRoomLayout(p, 7u, nullptr)) << "winged layout did not fit";

    const ProgStory& st = p.stories[0];
    const ProgRoom* hall = byId(st, "hall");
    const ProgRoom* service = byId(st, "service");
    const ProgRoom* solar = byId(st, "solar");
    ASSERT_TRUE(hall && service && solar) << "winged rooms missing";

    auto shell = StructureRealizer::realizeShell(p, thickStyle());
    ASSERT_TRUE(shell.ok) << shell.error;

    EXPECT_TRUE(walkBetween(shell, *hall, *service, W, D))
        << "hall->service blocked (main-range partition carve failed vs thick walls)";
    EXPECT_TRUE(walkBetween(shell, *hall, *solar, W, D))
        << "hall->solar blocked: the wing-joint door through the CUBE-THICK notch wall "
           "is not passable (KI-5h)";
}
