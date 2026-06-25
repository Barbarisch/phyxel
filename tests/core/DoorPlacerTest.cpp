#include <gtest/gtest.h>

#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"
#include "core/BuildingProgram.h"
#include "core/TraversalProbe.h"
#include "core/BuildingProgramValidator.h"

using namespace Phyxel::Core;

// ============================================================================
// place_doors (#09) — L3: a character-box must physically PASS THROUGH the carved
// interior opening. The validator's door-scale gates (doorWidthMin / doorClearMin,
// grounded to IRC R311.2 + the 1.751 m character) are TOPOLOGY: they assert the
// declared opening is large enough on paper. This file proves that gate corresponds
// to physical reality on the REAL realized voxels — a door at the gate's minimum is
// walkable, and a door BELOW it is both rejected by the gate AND impassable to the
// TraversalProbe (the same character-box used for stairs/rooms).
// ============================================================================

namespace {
StyleProfile cottageStyle() {
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

// A 2-room single-story cottage with ONE configurable interior door between hall and
// kitchen (shared wall at x=4). An exterior entrance into the hall is always present.
// doorWidth <= 0 => OMIT the interior door (sealed partition).
BuildingProgram twoRoom(int doorWidth, int doorHeight) {
    nlohmann::json j;
    j["name"] = "doors"; j["style"] = "timber_cottage";
    j["footprint"] = nlohmann::json::array({7, 9});
    j["substructure"] = "crawlspace"; j["roof_style"] = "gable";
    nlohmann::json hall, kitchen, story;
    hall["id"] = "hall"; hall["rect"] = nlohmann::json::array({0, 0, 4, 9}); hall["purpose"] = "living";
    kitchen["id"] = "kitchen"; kitchen["rect"] = nlohmann::json::array({4, 0, 3, 9}); kitchen["purpose"] = "kitchen";
    story["height"] = 3;
    story["rooms"] = nlohmann::json::array({hall, kitchen});
    story["portals"] = nlohmann::json::array();
    nlohmann::json ent;
    ent["between"] = nlohmann::json::array({"exterior", "hall"});
    ent["pos"] = nlohmann::json::array({0, 3}); ent["width"] = 1; ent["height"] = 2; ent["kind"] = "door";
    story["portals"].push_back(ent);
    if (doorWidth > 0) {
        nlohmann::json d;
        d["between"] = nlohmann::json::array({"hall", "kitchen"});
        d["pos"] = nlohmann::json::array({4, 4});
        d["width"] = doorWidth; d["height"] = doorHeight; d["kind"] = "door";
        story["portals"].push_back(d);
    }
    j["stories"] = nlohmann::json::array({story});
    return BuildingProgram::fromJson(j);
}

// Realize the program and BFS-walk the character-box from the hall centre to the kitchen
// centre. The only way across is the carved interior opening — no path otherwise (the
// partition is solid). Returns true iff the box reaches the kitchen.
bool characterCrossesDoor(const BuildingProgram& p) {
    auto sh = StructureRealizer::realizeShell(p, cottageStyle());
    EXPECT_TRUE(sh.ok) << sh.error;
    if (!sh.ok || sh.floorTopByStory.empty()) return false;
    const int floorY = sh.floorTopByStory[0];
    TraversalProbe probe([&](int x, int y, int z) { return sh.canvas.occupiedMicro(x, y, z); },
                         AgentBox{2, 16, 4});   // 0.5 m wide, 1.75 m tall, 0.44 m step-up
    const glm::ivec3 start(2 * 9 + 4, floorY, 4 * 9 + 4);                       // hall centre
    const glm::ivec3 gLo(5 * 9 + 2, floorY - 1, 4 * 9 + 2);                     // kitchen centre ±2
    const glm::ivec3 gHi(5 * 9 + 6, floorY + 1, 4 * 9 + 6);
    const glm::ivec3 bLo(0, floorY - 2, 0), bHi(7 * 9, floorY + 28, 9 * 9);     // whole footprint
    return probe.reachable(start, gLo, gHi, bLo, bHi);
}

bool hasError(const ValidationReport& r, const std::string& code) {
    for (const auto& i : r.issues())
        if (i.severity == Severity::Error && i.code == code) return true;
    return false;
}

// Tallest contiguous air column found IN the hall/kitchen partition PLANE, measured from the floor
// up. The interior wall sits at x = 4*9 - intT/2 = 35 (intT=2 => band x in [35,37)); scanning only
// the wall plane (not the open room interiors) so an opening is the door, not the room's ceiling
// space. 0 = the partition is solid (no opening); ~9 = a 1-cube door; ~18 = a 2-cube door.
// Distinguishes "blocked by no hole" from "blocked by a too-short hole".
int tallestOpeningInPartition(const StructureRealizer::ShellResult& sh, int floorY) {
    int best = 0;
    for (int x = 35; x < 37; ++x)
        for (int z = 0; z < 81; ++z) {
            int run = 0;
            for (int y = floorY; y < floorY + 30; ++y) {
                if (sh.canvas.occupiedMicro(x, y, z)) break;
                ++run;
            }
            best = std::max(best, run);
        }
    return best;
}
} // namespace

// GREEN: a door at the gate's exact minimum (width doorWidthMin=1, height doorClearMin=2) is
// BOTH legal (validator passes) AND physically walkable (the character crosses it). The gate's
// floor and the character's reality agree at the boundary.
TEST(DoorPlacerTest, DoorAtGateMinimumIsLegalAndWalkable) {
    const CharacterScale scale;
    const auto p = twoRoom(scale.doorWidthMin, (int)scale.doorClearMin);   // 1 wide, 2 tall
    EXPECT_TRUE(BuildingProgramValidator::validate(p).ok())
        << "a minimum-spec door should pass the scale + reachability gates";
    EXPECT_TRUE(characterCrossesDoor(p))
        << "character-box could not pass through a gate-legal interior door";
}

// TEETH #1 — the door is load-bearing for traversal: seal the partition (no interior portal) and
// the SAME probe must fail. If this still 'reached', the positive test is hollow (the box would be
// walking through walls). The topology gate also catches it: the kitchen is unreachable.
TEST(DoorPlacerTest, SealedWallBlocksTheCharacter) {
    const auto p = twoRoom(0, 0);   // no interior door
    EXPECT_FALSE(characterCrossesDoor(p))
        << "character crossed a SEALED wall — the door-passability probe has no teeth";
    EXPECT_TRUE(hasError(BuildingProgramValidator::validate(p), "room_unreachable"))
        << "a sealed-off kitchen should fail the topology reachability gate";
}

// TEETH #2 + GROUNDING — a sub-clearance 'cat-flap' door (width 1, height 1 = 9 micro, below the
// 16-micro character) is impassable in the REAL voxels AND rejected by the height gate. This proves
// doorClearMin is not an arbitrary number: a door below it corresponds to a door the character
// physically cannot pass through (no head-room in the opening).
TEST(DoorPlacerTest, CatFlapDoorBlocksCharacterAndFailsHeightGate) {
    const auto p = twoRoom(1, 1);   // full width, but only 1 cube (9 micro) tall
    EXPECT_FALSE(characterCrossesDoor(p))
        << "character squeezed through a 9-micro opening shorter than itself — no teeth";
    EXPECT_TRUE(hasError(BuildingProgramValidator::validate(p), "door_too_short"))
        << "a sub-clearance door should fail the height gate (doorClearMin)";

    // Disambiguate WHY it blocks: there IS a real carved hole (so this is not just the sealed case
    // again) but the hole is shorter than the 16-micro character — the block is HEAD-ROOM, which is
    // exactly what doorClearMin protects. Contrast the gate-min door (a 2-cube opening admits it).
    auto catFlap = StructureRealizer::realizeShell(p, cottageStyle());
    auto fullDoor = StructureRealizer::realizeShell(twoRoom(1, 2), cottageStyle());
    ASSERT_TRUE(catFlap.ok && fullDoor.ok && !catFlap.floorTopByStory.empty());
    const int flapAir = tallestOpeningInPartition(catFlap, catFlap.floorTopByStory[0]);
    const int fullAir = tallestOpeningInPartition(fullDoor, fullDoor.floorTopByStory[0]);
    EXPECT_GE(flapAir, 6)  << "the cat-flap carved no hole — would block for the wrong reason (== sealed)";
    EXPECT_LT(flapAir, 16) << "the cat-flap hole was tall enough for the character — it shouldn't block";
    EXPECT_GE(fullAir, 16) << "the gate-min door's opening must clear the character (sanity on the scan)";
}
