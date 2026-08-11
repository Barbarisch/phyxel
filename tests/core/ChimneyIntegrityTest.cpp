#include <gtest/gtest.h>

#include <climits>
#include <cmath>
#include <deque>
#include <set>

#include "core/BuildingProgram.h"
#include "core/FurniturePlacer.h"
#include "core/HearthForge.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"

using namespace Phyxel::Core;

// ============================================================================
// place_chimney (#14) — L2 on the REALIZED CANVAS.
//
// This replaces ChimneyPlannerTest, which measured a stack the furnish pass
// stamped into the finished world. The stack is now part of the SHELL
// (HearthForge -> MicroCanvas, docs/structure-generation/ChimneyForgePlan.md),
// so the invariants can be measured where they actually matter: on the canvas
// every downstream gate reads.
//
//   F1  the flue is CONTINUOUS AIR from the firebox to the cap — proven by a
//       flood fill through air, not by sampling a column (the old test could
//       not see the solid mantel sitting between fire and flue);
//   F2  the stack CLEARS THE RIDGE by the grounded 2 ft (IRC R1003.9);
//   F3  the stack is a SOLID RING at every course (no smoke into the room);
//   F4  a floor slab crossed by the stack YIELDS to the flue;
//   F5  a stack that would rise through the middle of a room upstairs REFUSES.
// ============================================================================

namespace {

StyleProfileRegistry testStyles() {
    StyleProfileRegistry sreg;
    sreg.loadFromJson(nlohmann::json::parse(R"({
        "timber_cottage": { "roof_style":"gable", "foundation":"crawlspace",
            "thickness": { "exterior_wall":0.333, "interior_wall":0.222, "foundation_wall":0.667,
                           "floor":0.333, "ceiling":0.222 },
            "materials": { "structure":"WoodPlanks", "floor":"Wood", "roof":"Wood",
                           "foundation":"Stone" },
            "roof": { "pitch":0.8 } } })"));
    return sreg;
}

/// A hall with `stories` storeys, laid out by hand (one room per storey) so the
/// test controls the geometry, then hearth-sited exactly the way the floorplan
/// stage does it.
BuildingProgram hallProgram(int W, int D, int stories) {
    BuildingProgram p;
    p.name = "hall"; p.style = "timber_cottage";
    p.footprintW = W; p.footprintD = D; p.substructure = "slab";
    for (int s = 0; s < stories; ++s) {
        ProgStory st;
        st.height = 3;
        ProgRoom rm;
        rm.id = "hall" + std::to_string(s);
        rm.purpose = "hall";
        rm.rect = Rect{0, 0, W, D};
        st.rooms.push_back(rm);
        p.stories.push_back(st);
    }
    return p;
}

int siteHearths(BuildingProgram& p, const StyleProfile& style) {
    const int extT = StructureRealizer::thicknessMicro(style.thicknessOf("exterior_wall", 0.333));
    const int intT = StructureRealizer::thicknessMicro(style.thicknessOf("interior_wall", 0.222));
    int n = 0;
    for (auto& st : p.stories)
        n += HearthForge::siteIntoProgram(st, {}, extT, intT, {}, "");
    return n;
}

/// Can smoke get from `from` to `to` through AIR only, staying inside the box
/// [lo, hi]? The honest test of "the flue draws".
bool airReaches(const MicroCanvas& c, const glm::ivec3& from, const glm::ivec3& to,
                const glm::ivec3& lo, const glm::ivec3& hi) {
    if (c.occupiedMicro(from.x, from.y, from.z)) return false;
    std::set<std::tuple<int, int, int>> seen;
    std::deque<glm::ivec3> q{from};
    seen.insert({from.x, from.y, from.z});
    const glm::ivec3 dirs[6] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    while (!q.empty()) {
        const glm::ivec3 p = q.front(); q.pop_front();
        if (p == to) return true;
        for (const auto& d : dirs) {
            const glm::ivec3 n = p + d;
            if (n.x < lo.x || n.y < lo.y || n.z < lo.z) continue;
            if (n.x > hi.x || n.y > hi.y || n.z > hi.z) continue;
            if (c.occupiedMicro(n.x, n.y, n.z)) continue;
            if (!seen.insert({n.x, n.y, n.z}).second) continue;
            q.push_back(n);
        }
    }
    return false;
}

}  // namespace

// F1 — the flue is continuous AIR from the fire to the cap. Red before the forge:
// the hearth used to be a template with a SOLID mantel and the stack started above
// it, so no air path existed at all.
TEST(ChimneyIntegrityTest, FlueIsContinuousAirFromFireboxToCap) {
    FurniturePlacer::clearRecipes();
    auto sreg = testStyles();
    const StyleProfile& style = *sreg.get("timber_cottage");
    BuildingProgram p = hallProgram(12, 8, 1);
    ASSERT_EQ(siteHearths(p, style), 1) << "the hall recipe must site exactly one hearth";

    auto shell = StructureRealizer::realizeShell(p, style);
    ASSERT_TRUE(shell.ok) << shell.error;
    ASSERT_EQ(shell.plan.hearths.size(), 1u) << "the realizer recorded no hearth";
    const HearthRecord& h = shell.plan.hearths[0];

    // The fire itself must sit in air (inside the firebox, not embedded in masonry).
    EXPECT_FALSE(shell.canvas.occupiedMicro(h.fireMicroX, h.fireMicroY + 1, h.fireMicroZ))
        << "no void above the fuel bed — the firebox is solid";

    const int capTop = h.stackTopMicroY - HearthForge::kCapRows;   // last AIR course
    const glm::ivec3 from(h.fireMicroX, h.fireMicroY + 1, h.fireMicroZ);
    const glm::ivec3 to(h.flueX + 1, capTop, h.flueZ + 1);
    // Confine the flood fill to the hearth's own column so it cannot "escape" into
    // the room and come back down the chimney from outside.
    const glm::ivec3 lo(std::min(h.x * 9, h.flueX), h.baseMicroY, std::min(h.z * 9, h.flueZ));
    const glm::ivec3 hi(std::max((h.x + h.w) * 9 - 1, h.flueX + h.flueW - 1), capTop,
                        std::max((h.z + h.d) * 9 - 1, h.flueZ + h.flueD - 1));
    EXPECT_TRUE(airReaches(shell.canvas, from, to, lo, hi))
        << "smoke cannot reach the cap from the fire: the flue is blocked (a hearth "
           "whose flue does not reach its own firebox is a chimney-shaped decoration)";
}

// F3 — the stack is a solid masonry RING at every course. A gap is smoke in the
// room it passes through.
TEST(ChimneyIntegrityTest, StackIsASolidRingAtEveryCourse) {
    FurniturePlacer::clearRecipes();
    auto sreg = testStyles();
    const StyleProfile& style = *sreg.get("timber_cottage");
    BuildingProgram p = hallProgram(12, 8, 1);
    ASSERT_EQ(siteHearths(p, style), 1);
    auto shell = StructureRealizer::realizeShell(p, style);
    ASSERT_TRUE(shell.ok) << shell.error;
    ASSERT_EQ(shell.plan.hearths.size(), 1u);
    const HearthRecord& h = shell.plan.hearths[0];

    for (int y = h.mantelMicroY; y <= h.stackTopMicroY; ++y)
        for (int x = h.stackX; x < h.stackX + h.stackW; ++x)
            for (int z = h.stackZ; z < h.stackZ + h.stackD; ++z) {
                const bool ring = (x == h.stackX || x == h.stackX + h.stackW - 1 ||
                                   z == h.stackZ || z == h.stackZ + h.stackD - 1);
                const bool cap = y > h.stackTopMicroY - HearthForge::kCapRows;
                if (ring || cap)
                    EXPECT_TRUE(shell.canvas.occupiedMicro(x, y, z))
                        << "stack GAP at (" << x << "," << y << "," << z << ")";
                else
                    EXPECT_FALSE(shell.canvas.occupiedMicro(x, y, z))
                        << "the flue is BLOCKED at (" << x << "," << y << "," << z << ")";
            }
}

// F2 — the stack clears the RIDGE by the grounded 2 ft. Teeth: the apex is measured
// on the SAME shell without a hearth, and the constant must meet ceil(0.610*9) = 6.
TEST(ChimneyIntegrityTest, StackClearsTheRealRidgeByTheGroundedClearance) {
    const int minClearanceMicro = (int)std::ceil(0.610 * 9.0);   // 2 ft, IRC R1003.9
    EXPECT_GE(HearthForge::kRidgeClearanceMicro, minClearanceMicro)
        << "ridge clearance " << HearthForge::kRidgeClearanceMicro
        << " micro is BELOW the 2 ft (" << minClearanceMicro << " micro) IRC R1003.9 floor";

    FurniturePlacer::clearRecipes();
    auto sreg = testStyles();
    const StyleProfile& style = *sreg.get("timber_cottage");

    BuildingProgram bare = hallProgram(12, 8, 1);
    auto bareShell = StructureRealizer::realizeShell(bare, style);
    ASSERT_TRUE(bareShell.ok) << bareShell.error;
    glm::ivec3 lo, hi;
    ASSERT_TRUE(bareShell.canvas.microBounds(lo, hi));
    const int apex = hi.y;   // the ridge, with no chimney in the measurement

    BuildingProgram p = hallProgram(12, 8, 1);
    ASSERT_EQ(siteHearths(p, style), 1);
    auto shell = StructureRealizer::realizeShell(p, style);
    ASSERT_TRUE(shell.ok) << shell.error;
    ASSERT_EQ(shell.plan.hearths.size(), 1u);
    EXPECT_GE(shell.plan.hearths[0].stackTopMicroY - apex, minClearanceMicro)
        << "stack top " << shell.plan.hearths[0].stackTopMicroY
        << " does not clear the roof apex " << apex << " by 2 ft (downdraught)";

    glm::ivec3 lo2, hi2;
    ASSERT_TRUE(shell.canvas.microBounds(lo2, hi2));
    EXPECT_EQ(hi2.y, shell.plan.hearths[0].stackTopMicroY)
        << "the chimney should now be the highest thing on the building";
}

// F4 — where the stack crosses an upper floor, the SLAB YIELDS to the flue: air
// inside the flue, intact slab immediately outside it. This is the "runs up through
// the middle of a floor" defect, measured.
TEST(ChimneyIntegrityTest, UpperFloorSlabYieldsToTheFlue) {
    FurniturePlacer::clearRecipes();
    auto sreg = testStyles();
    const StyleProfile& style = *sreg.get("timber_cottage");
    BuildingProgram p = hallProgram(12, 8, 2);
    ASSERT_GE(siteHearths(p, style), 1);

    auto shell = StructureRealizer::realizeShell(p, style);
    ASSERT_TRUE(shell.ok) << shell.error;
    ASSERT_FALSE(shell.plan.hearths.empty());
    const HearthRecord* ground = nullptr;
    for (const auto& h : shell.plan.hearths) if (h.story == 0) ground = &h;
    ASSERT_NE(ground, nullptr) << "no ground-story hearth to run a flue upward";
    ASSERT_GE(shell.floorTopByStory.size(), 2u);

    // The upper story's floor SLAB sits just below its walkable surface.
    const int slabY = shell.floorTopByStory[1] - 1;
    ASSERT_GT(slabY, ground->mantelMicroY) << "the stack does not reach the upper floor";
    for (int x = ground->flueX; x < ground->flueX + ground->flueW; ++x)
        for (int z = ground->flueZ; z < ground->flueZ + ground->flueD; ++z)
            EXPECT_FALSE(shell.canvas.occupiedMicro(x, slabY, z))
                << "the upper floor slab is still inside the flue at (" << x << "," << z << ")";
    // Control: the slab is intact one cell outside the stack.
    EXPECT_TRUE(shell.canvas.occupiedMicro(ground->stackX - 1, slabY, ground->flueZ))
        << "control failed — no floor slab beside the stack, so the test above proves nothing";
}

// F5 — a stack that would come up in the MIDDLE of a room upstairs is refused, not
// silently leaned. Teeth: the same program with the hearth against the wall builds.
TEST(ChimneyIntegrityTest, RefusesAStackThroughTheMiddleOfAnUpstairsRoom) {
    FurniturePlacer::clearRecipes();
    auto sreg = testStyles();
    const StyleProfile& style = *sreg.get("timber_cottage");

    BuildingProgram p = hallProgram(12, 8, 2);
    ProgFixture fx;
    fx.type = "fireplace";
    fx.room = "hall0";
    fx.rotation = 0;
    fx.rect = Rect{5, 4, 2, 1};          // dead centre of the ground room
    p.stories[0].fixtures.push_back(fx);
    auto refused = StructureRealizer::realizeShell(p, style);
    EXPECT_FALSE(refused.ok)
        << "a chimney rising through the middle of the room upstairs was accepted";
    EXPECT_NE(refused.error.find("middle of room"), std::string::npos) << refused.error;

    // Control: against the wall, the SAME building realizes.
    BuildingProgram ok = hallProgram(12, 8, 2);
    ASSERT_GE(siteHearths(ok, style), 1);
    auto okShell = StructureRealizer::realizeShell(ok, style);
    EXPECT_TRUE(okShell.ok) << okShell.error;
}
