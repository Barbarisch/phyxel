#include <gtest/gtest.h>

#include "core/BuildingProgram.h"
#include "core/ChunkManager.h"
#include "core/RealizedStructureValidator.h"
#include "core/RoomLayout.h"
#include "core/RoomProgram.h"
#include "core/StructureBuildService.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"

// ============================================================================
// M3 gate teeth — RED tests first.
//
// Defect (live at time of writing): the program validation gate is
// WARN-BUT-ALLOW — a program with hard validator ERRORS (e.g. a footprint wider
// than the typology's grounded max) logs "building anyway" and builds anyway
// (observed live 2026-08-07: an 8-wide tavern vs the 7.00 cruck-span max built
// fine). And nothing at build time verifies the REALIZED shell is traversable —
// the L3 TraversalProbe ran only in tests.
//
// Contract after M3:
//  - validate_program: error severity -> one bounded repair (re-roll the
//    autofilled layout with a salted seed) -> still failing = REFUSED with the
//    structured validation report; {"allow_invalid": true} = test/debug escape.
//  - validate_realized: TraversalProbe over the realized canvas — every room on
//    every story physically reachable from the entrance room — else REFUSED.
// ============================================================================

using namespace Phyxel;
using namespace Phyxel::Core;

namespace {

StyleProfile gateStyle() {
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

// An 8-wide tavern: the tavern typology's grounded width max is 7.00 (cruck
// span) -> BuildingProgramValidator ERROR footprint_too_wide, deterministically.
nlohmann::json overwideTavernParams() {
    return {{"schema", "v2"}, {"type", "tavern"}, {"typology", "tavern"},
            {"style", "timber_cottage"},
            {"position", {{"x", 500}, {"y", 16}, {"z", 500}}},
            {"footprint", nlohmann::json::array({8, 12})},
            {"substructure", "crawlspace"},
            {"stories", nlohmann::json::array({nlohmann::json{{"height", 3}}})}};
}

// Single-story three-room program (hall + kitchen + store, linear doors) —
// the CanvasDigest interiorOpenings shape.
BuildingProgram threeRoom() {
    BuildingProgram p;
    p.name = "traversal_case"; p.style = "timber_cottage";
    p.footprintW = 9; p.footprintD = 9; p.substructure = "crawlspace";
    ProgStory st; st.height = 3;
    ProgRoom a; a.id = "a"; a.rect = {0, 0, 4, 9}; a.purpose = "living";
    ProgRoom b; b.id = "b"; b.rect = {4, 0, 5, 5}; b.purpose = "kitchen";
    ProgRoom c; c.id = "c"; c.rect = {4, 5, 5, 4}; c.purpose = "storage";
    st.rooms = {a, b, c};
    auto portal = [](const std::string& pa, const std::string& pb, int px, int pz) {
        ProgPortal p2; p2.a = pa; p2.b = pb; p2.px = px; p2.pz = pz;
        p2.width = 1; p2.height = 2; p2.kind = "door"; return p2;
    };
    st.portals.push_back(portal("exterior", "a", 0, 4));
    st.portals.push_back(portal("a", "b", 4, 2));   // partition at x=4
    st.portals.push_back(portal("b", "c", 6, 5));   // partition at z=5
    p.stories.push_back(st);
    return p;
}

// Two-story with a proven switchback (the StairFallShaft guard fixture).
BuildingProgram twoStoryStair() {
    BuildingProgram p;
    p.name = "stair_case"; p.style = "timber_cottage";
    p.footprintW = 7; p.footprintD = 9; p.substructure = "crawlspace";
    for (int s = 0; s < 2; ++s) {
        ProgStory st; st.height = 3;
        ProgRoom r; r.id = s ? "upper" : "hall"; r.rect = {0, 0, 7, 9};
        r.purpose = s ? "bedchamber" : "living";
        st.rooms.push_back(r);
        p.stories.push_back(st);
    }
    ProgPortal door; door.a = "exterior"; door.b = "hall"; door.kind = "door";
    door.px = 0; door.pz = 4; door.width = 1; door.height = 2;
    p.stories[0].portals.push_back(door);
    ProgStair sr; sr.fromStory = 0; sr.toStory = 1; sr.rect = {2, 3, 2, 4};
    sr.form = "switchback";
    p.stories[0].stairs.push_back(sr);
    return p;
}

}  // namespace

// ---------------------------------------------------------------------------
// checkShellTraversal — the detector the validate_realized stage wires in.
// Detector-first: proven to PASS an intact shell and FIRE on the same shell
// with an opening sealed / the stair filled (defect shapes the realizer could
// produce through a carve or stair regression).
// ---------------------------------------------------------------------------

TEST(ShellTraversalGate, IntactShellsPass) {
    const StyleProfile style = gateStyle();
    for (const auto& prog : {threeRoom(), twoStoryStair()}) {
        auto sh = StructureRealizer::realizeShell(prog, style);
        ASSERT_TRUE(sh.ok) << sh.error;
        auto rep = RealizedStructureValidator::checkShellTraversal(
            sh.canvas, sh.floorTopByStory, prog);
        EXPECT_TRUE(rep.ok()) << prog.name << ": " << rep.summary();
    }
}

TEST(ShellTraversalGate, SealedInteriorDoorFires) {
    BuildingProgram prog = threeRoom();
    auto sh = StructureRealizer::realizeShell(prog, gateStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    // Seal the a<->b partition (the band straddles cubes x=3..4) over the full
    // room depth and height — the carved doorway at (4,2) is walled shut.
    const int floor0 = sh.floorTopByStory[0];
    sh.canvas.fillMicroBox(3 * 9, floor0, 0, 18, 27, 9 * 9, "WoodPlanks");
    auto rep = RealizedStructureValidator::checkShellTraversal(
        sh.canvas, sh.floorTopByStory, prog);
    EXPECT_FALSE(rep.ok())
        << "a walled-shut doorway went undetected — the realized gate has no teeth";
    EXPECT_GE(rep.errorCount(), 2u) << rep.summary();   // b AND c become unreachable
}

TEST(ShellTraversalGate, FilledStairwellFires) {
    BuildingProgram prog = twoStoryStair();
    auto sh = StructureRealizer::realizeShell(prog, gateStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    ASSERT_GE(sh.floorTopByStory.size(), 2u);
    // Fill the whole stair well solid from the lower floor to the upper —
    // the flight is entombed, the upper story sealed.
    const int floor0 = sh.floorTopByStory[0];
    const int rise   = sh.floorTopByStory[1] - floor0;
    sh.canvas.fillMicroBox(2 * 9, floor0, 3 * 9, 2 * 9, rise, 4 * 9, "Wood");
    auto rep = RealizedStructureValidator::checkShellTraversal(
        sh.canvas, sh.floorTopByStory, prog);
    EXPECT_FALSE(rep.ok())
        << "an entombed stair went undetected — upper story sealed with no diagnostics";
}

// The REAL generated tavern — the case that exposed a false positive in this
// gate at L4 (2026-08-08). The tavern's taproom is room 0, and the auto-placed
// stair well sits at the low end of the long axis, so the taproom's CENTRE is
// inside the stairwell shaft. Seeding the flood at that centre made the agent
// unstandable, and the gate reported EVERY other room unreachable — refusing a
// building that is in fact fully navigable. The start must be a standable spot,
// not a centre point.
TEST(ShellTraversalGate, GeneratedTavernWhoseEntranceCentreIsTheStairwellPasses) {
    RoomProgramRegistry reg;
    ASSERT_TRUE(reg.loadFromFile("resources/room_program.json") ||
                reg.loadFromFile("../resources/room_program.json") ||
                reg.loadFromFile("../../resources/room_program.json") ||
                reg.loadFromFile("../../../resources/room_program.json"));
    const RoomProgram* tavern = reg.get("tavern");
    ASSERT_NE(tavern, nullptr);

    nlohmann::json j;
    j["name"] = "tavern"; j["style"] = "timber_cottage";
    j["footprint"] = nlohmann::json::array({7, 12});
    j["substructure"] = "crawlspace"; j["typology"] = "tavern";
    j["stories"] = nlohmann::json::array({nlohmann::json{{"height", 3}}});
    BuildingProgram prog = BuildingProgram::fromJson(j);
    ASSERT_TRUE(autofillRoomLayout(prog, 1234u, tavern));
    ASSERT_GE(prog.stories.size(), 2u) << "tavern typology should grow to 2 stories";

    auto sh = StructureRealizer::realizeShell(prog, gateStyle());
    ASSERT_TRUE(sh.ok) << sh.error;

    auto rep = RealizedStructureValidator::checkShellTraversal(
        sh.canvas, sh.floorTopByStory, prog);
    EXPECT_TRUE(rep.ok())
        << "the traversal gate refused a NAVIGABLE generated tavern: " << rep.summary();
}

// The false-positive guard proper. The bug was: seeding the traversal flood at a
// room's geometric CENTRE fails when that centre is the stairwell SHAFT — the
// agent has nothing to stand on, floods nowhere, and every other room reads
// "unreachable". The generated tavern used to have that shape; M6 re-sited the
// well, so this fixture pins it SYNTHETICALLY and cannot drift with the
// generator: a single-room storey whose centre is deliberately inside the well.
TEST(ShellTraversalGate, RoomWhoseCentreIsTheStairwellStillPasses) {
    BuildingProgram prog = twoStoryStair();          // well {2,3,2,4} in a 7x9 room
    const Rect& well = prog.stories[0].stairs[0].rect;
    const Rect& r0 = prog.stories[0].rooms[0].rect;
    const int cx = r0.x + r0.w / 2, cz = r0.z + r0.d / 2;
    ASSERT_TRUE(cx >= well.x && cx < well.x1() && cz >= well.z && cz < well.z1())
        << "fixture no longer places room 0's centre inside the well — it would stop "
           "guarding the centre-seeded-flood false positive";

    auto sh = StructureRealizer::realizeShell(prog, gateStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    auto rep = RealizedStructureValidator::checkShellTraversal(
        sh.canvas, sh.floorTopByStory, prog);
    EXPECT_TRUE(rep.ok())
        << "a navigable building was refused because its room centre is the stair shaft: "
        << rep.summary();
}

// ---------------------------------------------------------------------------
// The build-path gates. Headless ChunkManager (StructureGroundingTest pattern);
// both assertions observe REFUSALS, which return before place() (the only part
// that is not headless-testable).
// ---------------------------------------------------------------------------

// RED today: warn-but-allow lets the invalid program continue until the
// GROUNDING gate refuses it ("ungrounded footprint" — nothing to do with the
// program defect). GREEN: refused AT validate_program with the report attached.
TEST(ForgeGateTeeth, ProgramErrorRefusesAtTheProgramGate) {
    ChunkManager cm;
    cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
    StructureBuildService::Deps deps;
    deps.chunkManager = &cm;

    auto res = StructureBuildService::buildV2(overwideTavernParams(), deps);
    ASSERT_TRUE(res.contains("error")) << res.dump();
    EXPECT_EQ(res.value("refused_at", std::string()), "validate_program")
        << "an error-severity program was not refused at the program gate: " << res.dump();
    EXPECT_TRUE(res.contains("validation")) << "refusal must carry the validation report";
}

// The escape hatch: allow_invalid skips program-gate ENFORCEMENT (mirroring
// allow_ungrounded) — the build then proceeds and, on this terrain-less world,
// meets the grounding refusal exactly as before. Green both pre- and post-M3.
TEST(ForgeGateTeeth, AllowInvalidSkipsProgramGateEnforcement) {
    ChunkManager cm;
    cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
    StructureBuildService::Deps deps;
    deps.chunkManager = &cm;

    auto params = overwideTavernParams();
    params["allow_invalid"] = true;
    auto res = StructureBuildService::buildV2(params, deps);
    ASSERT_TRUE(res.contains("error")) << res.dump();
    // The INVARIANT: the program gate proceeded (enforcement skipped) and the
    // refusal came from a LATER stage. Do not pin WHICH later gate refuses —
    // the original assertion pinned the grounding gate's "ungrounded" message
    // and went stale the day the realize-stage chimney-siting check started
    // refusing this rig first (still a later gate, invariant intact).
    EXPECT_NE(res.value("refused_at", std::string()), "validate_program")
        << "allow_invalid should defer to the LATER gates, not fail at the program gate: "
        << res.dump();
    bool programProceeded = false;
    if (res.contains("gates") && res["gates"].is_array())
        for (const auto& g : res["gates"])
            if (g.value("stage", "") == "validate_program")
                programProceeded = (g.value("outcome", "") == "proceeded");
    EXPECT_TRUE(programProceeded)
        << "gate trail must show validate_program proceeded under allow_invalid: " << res.dump();
}
