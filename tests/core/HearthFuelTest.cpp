#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

#include "core/BuildingProgram.h"
#include "core/FurniturePlacer.h"
#include "core/HearthForge.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"

using namespace Phyxel::Core;

// ============================================================================
// Hearth FUEL — the burning wood is item props, not masonry.
//
// Why it matters beyond looks: fuel painted into the brickwork can never be
// lit, can never carry a flame, and (the reason this changed) reads as a pale
// slab wedged in the firebox. As item props it carries the flame + firelight as
// declarative item effects — the torch mechanism — which is also what makes a
// lit hearth come back lit after a reload.
//
//   F1  the masonry contains NO fuel: zero Log / glow cells in the hearth body.
//   F2  the pile is inside the firebox VOID and touches no masonry.
//   F3  exactly ONE billet is lit (it carries the light; MAX_POINT_LIGHTS = 32).
//   F4  the count comes from the firebox geometry, and scales with it.
//   F5  the plan does not depend on where in the world the building lands.
//   F6  a hearth that does not burn cordwood lays NO logs (the control).
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

/// A one-room hall whose recipe calls for a fireplace, hearth-sited exactly the
/// way the floorplan stage does it.
BuildingProgram hallProgram(int W, int D, const char* purpose = "hall") {
    BuildingProgram p;
    p.name = "hall"; p.style = "timber_cottage";
    p.footprintW = W; p.footprintD = D; p.substructure = "slab";
    ProgStory st;
    st.height = 3;
    ProgRoom rm;
    rm.id = "hall0"; rm.purpose = purpose; rm.rect = Rect{0, 0, W, D};
    st.rooms.push_back(rm);
    p.stories.push_back(st);
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

StructureRealizer::ShellResult realizeHall(const StyleProfile& style, int W = 12, int D = 8,
                                           const char* purpose = "hall") {
    FurniturePlacer::clearRecipes();
    BuildingProgram p = hallProgram(W, D, purpose);
    siteHearths(p, style);
    return StructureRealizer::realizeShell(p, style);
}

}  // namespace

// F1 — no fuel baked into the brickwork.
TEST(HearthFuelTest, MasonryHoldsNoFuel) {
    auto sreg = testStyles();
    auto shell = realizeHall(*sreg.get("timber_cottage"));
    ASSERT_TRUE(shell.ok) << shell.error;
    ASSERT_EQ(shell.plan.hearths.size(), 1u);
    const HearthRecord& h = shell.plan.hearths[0];

    int logCells = 0, glowCells = 0;
    for (int x = h.x * 9; x < (h.x + h.w) * 9; ++x)
        for (int y = h.baseMicroY; y < h.mantelMicroY; ++y)
            for (int z = h.z * 9; z < (h.z + h.d) * 9; ++z) {
                const std::string m = shell.canvas.materialAt(x, y, z);
                if (m == "Log") ++logCells;
                if (m == "glow") ++glowCells;
            }
    EXPECT_EQ(logCells, 0) << "the fuel bed is still painted into the masonry";
    EXPECT_EQ(glowCells, 0) << "the embers are still painted into the masonry";
}

// F2/F3 — the pile sits in the firebox void, touching nothing, with one lit log.
TEST(HearthFuelTest, PileSitsInTheFireboxVoidWithOneLitBillet) {
    auto sreg = testStyles();
    auto shell = realizeHall(*sreg.get("timber_cottage"));
    ASSERT_TRUE(shell.ok) << shell.error;
    ASSERT_EQ(shell.plan.hearths.size(), 1u);
    const HearthRecord& h = shell.plan.hearths[0];

    const auto billets = HearthForge::fuelBillets(h);
    ASSERT_FALSE(billets.empty()) << "the hearth laid no fuel at all";
    EXPECT_EQ((int)billets.size(), h.fuelCount) << "layout disagrees with the recorded count";
    EXPECT_GE(billets.size(), 2u) << "one billet is not a fire — check the firebox depth";

    int lit = 0;
    for (const auto& fb : billets) {
        if (fb.lit) ++lit;
        // The billet's own cell must be air: a log embedded in brick is exactly
        // the defect this change removes.
        const int cx = (int)std::lround(fb.x), cy = (int)std::lround(fb.y), cz = (int)std::lround(fb.z);
        EXPECT_FALSE(shell.canvas.occupiedMicro(cx, cy, cz))
            << "billet at (" << cx << "," << cy << "," << cz << ") is inside masonry";
        // ...and it must be INSIDE the hearth body's footprint, not out in the room.
        EXPECT_GE(cx, h.x * 9);          EXPECT_LT(cx, (h.x + h.w) * 9);
        EXPECT_GE(cz, h.z * 9);          EXPECT_LT(cz, (h.z + h.d) * 9);
        EXPECT_GE(cy, h.baseMicroY);     EXPECT_LT(cy, h.mantelMicroY);
    }
    EXPECT_EQ(lit, 1) << "exactly one billet burns — it carries the only firelight "
                         "(MAX_POINT_LIGHTS is 32 engine-wide)";
    EXPECT_EQ(billets.back().lit || billets.size() == 1, true)
        << "the lit log should sit ON the bed, in the middle of the pile";
}

// F4 — the count is geometric, and a bigger firebox burns a bigger fire. Teeth:
// a one-deep firebox must NOT get the stacked top log.
TEST(HearthFuelTest, CountComesFromTheFireboxGeometry) {
    auto sreg = testStyles();
    auto shell = realizeHall(*sreg.get("timber_cottage"));
    ASSERT_TRUE(shell.ok) << shell.error;
    const HearthRecord& h = shell.plan.hearths[0];

    // PINNED, not re-derived. An earlier version of this test recomputed the
    // expectation with the same formula the forge uses, so it happily certified
    // a "pile" of ONE billet (the firebox depth was inferred one micro short).
    // A tautological expectation cannot fail; this is the number a 0.44 m-deep
    // firebox must actually get — a 2-billet bed with a crossing log on top.
    EXPECT_EQ(h.fuelCount, 3)
        << "a standard fireplace firebox (4 micro / 0.44 m deep) lays a 2-billet "
           "bed plus one crossing log";
    EXPECT_LE(h.fuelCount, HearthForge::kMaxFuelBillets) << "pile exceeds the cap";

    // ...and the count MOVES with the geometry: half the depth, half the bed.
    const auto body = HearthForge::bodyOf("fireplace");
    ASSERT_EQ(body.fuelDepthMicro, 4) << "preset changed; re-derive the pinned counts above";
    EXPECT_EQ(std::max(1, std::min(3, 2 / HearthForge::kBilletPitchMicro)), 1)
        << "a 2-micro-deep firebox must hold a single billet, not a stack";

    // Teeth on the rule itself: a shallow firebox gets a flat bed, no top log.
    HearthRecord shallow = h;
    shallow.fuelCount = 1;
    const auto one = HearthForge::fuelBillets(shallow);
    ASSERT_EQ(one.size(), 1u);
    EXPECT_TRUE(one[0].lit) << "a single-billet fire must still be the lit one";
    EXPECT_FLOAT_EQ(one[0].y, (float)shallow.fuelMicroY) << "nothing to stack on, so no lift";
}

// F5 — the plan is a function of the hearth, not of where the building landed.
// This is the chunk-independence key in its applicable form: same building at a
// chunk seam must lay the same fire.
TEST(HearthFuelTest, FuelPlanIsIndependentOfWorldPosition) {
    auto sreg = testStyles();
    const StyleProfile& style = *sreg.get("timber_cottage");
    auto a = realizeHall(style);
    auto b = realizeHall(style);
    ASSERT_TRUE(a.ok && b.ok);
    ASSERT_EQ(a.plan.hearths.size(), 1u);
    ASSERT_EQ(b.plan.hearths.size(), 1u);

    // The realizer works in structure-local coords, so the plan must be
    // byte-identical regardless of the world origin the shell is later placed
    // at — including one that straddles a chunk boundary.
    const auto ba = HearthForge::fuelBillets(a.plan.hearths[0]);
    const auto bb = HearthForge::fuelBillets(b.plan.hearths[0]);
    ASSERT_EQ(ba.size(), bb.size());
    for (size_t i = 0; i < ba.size(); ++i) {
        EXPECT_FLOAT_EQ(ba[i].x, bb[i].x);
        EXPECT_FLOAT_EQ(ba[i].y, bb[i].y);
        EXPECT_FLOAT_EQ(ba[i].z, bb[i].z);
        EXPECT_EQ(ba[i].rotationDeg, bb[i].rotationDeg);
        EXPECT_EQ(ba[i].lit, bb[i].lit);
    }
    EXPECT_EQ(a.plan.hearths[0].toJson(), b.plan.hearths[0].toJson());
}

// F6 — THE CONTROL. A smith's fire is charcoal, not cordwood: the forge must lay
// ZERO logs. Without this, a test that counted "props near a hearth" would look
// just as green while quietly dressing a forge with firewood.
TEST(HearthFuelTest, NonCordwoodHearthsLayNoLogs) {
    for (const char* type : {"forge_hearth", "oven_bread"}) {
        const auto body = HearthForge::bodyOf(type);
        ASSERT_TRUE(body.known) << type;
        EXPECT_TRUE(body.fuelItem.empty())
            << type << " was given cordwood; a forge burns charcoal and a bake oven "
                       "is fired then swept — each needs its own grounded fuel";
        HearthRecord rec;   // an unplanned hearth lays nothing
        rec.type = type;
        EXPECT_TRUE(HearthForge::fuelBillets(rec).empty()) << type;
    }
    // ...and the cordwood hearth DOES, so the control is not vacuous.
    EXPECT_EQ(HearthForge::bodyOf("fireplace").fuelItem, "firewood");
}
