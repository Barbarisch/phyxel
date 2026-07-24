#include <gtest/gtest.h>

#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"
#include "core/BuildingProgram.h"

using namespace Phyxel::Core;

// ============================================================================
// Claims Ledger increment 1 (docs/structure-generation/ClaimsLedger.md):
// stairs become FIRST-CLASS AssemblyPlan data. Before this, stair geometry was
// planned three independent times from the same ProgStair.rect (realizer,
// BuildingProgramValidator, and a hand-rolled rect re-scan feeding furniture
// reservedRects in StructureBuildService) and recorded NOWHERE — featureAt
// could not say "stair", and a stairwell hole cut through the upper slab still
// classified as slab (a real misclassification consumed by the destruction
// system). RED state: realizeShell builds treads but plan.stairs stays empty.
// ============================================================================

namespace {

StyleProfile timberCottageStyle() {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"({
        "timber_cottage": {
            "roof_style": "gable", "foundation": "slab",
            "thickness": { "exterior_wall": 0.333, "interior_wall": 0.222,
                           "foundation_wall": 0.667, "floor": 0.333, "ceiling": 0.222 },
            "materials": { "structure": "Wood", "floor": "Wood", "roof": "Wood", "foundation": "Stone" },
            "roof": { "pitch": 0.8 }
        }
    })"));
    return *reg.get("timber_cottage");
}

// Two stories, one switchback stair in an INTERIOR well (rect kept off the
// perimeter so featureAt's wall answer never shadows the stair probe cells).
BuildingProgram twoStoryWithStair() {
    BuildingProgram p;
    p.name = "stair_house"; p.style = "timber_cottage";
    p.footprintW = 7; p.footprintD = 9; p.substructure = "slab";
    for (int s = 0; s < 2; ++s) {
        ProgStory st; st.height = 3;
        ProgRoom r; r.id = s ? "loft" : "hall"; r.rect = {0, 0, 7, 9};
        r.purpose = s ? "bedroom" : "living";
        st.rooms.push_back(r);
        p.stories.push_back(st);
    }
    ProgPortal door; door.a = "exterior"; door.b = "hall";
    door.px = 0; door.pz = 3; door.width = 1; door.height = 2; door.kind = "door";
    p.stories[0].portals.push_back(door);
    ProgStair sr; sr.fromStory = 0; sr.toStory = 1;
    sr.rect = {2, 3, 2, 4}; sr.form = "switchback";
    p.stories[0].stairs.push_back(sr);
    return p;
}

} // namespace

// The realizer's stair pass must RECORD what it builds, with the record's
// y-anchors equal to the shell's own walkable surfaces (no re-derivation).
TEST(AssemblyPlanStairTest, RealizedStairIsRecordedInPlan) {
    auto r = StructureRealizer::realizeShell(twoStoryWithStair(), timberCottageStyle());
    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_EQ(r.floorTopByStory.size(), 2u);

    ASSERT_EQ(r.plan.stairs.size(), 1u)
        << "realizer built a stair flight but recorded nothing in the plan";
    const StairRecord& s = r.plan.stairs[0];
    EXPECT_EQ(s.x, 2); EXPECT_EQ(s.z, 3); EXPECT_EQ(s.w, 2); EXPECT_EQ(s.d, 4);
    EXPECT_EQ(s.fromStory, 0); EXPECT_EQ(s.toStory, 1);
    EXPECT_EQ(s.botWalkMicro, r.floorTopByStory[0]);
    EXPECT_EQ(s.topWalkMicro, r.floorTopByStory[1]);
    EXPECT_EQ(s.baseY, r.floorTopByStory[0] / 9);
    EXPECT_EQ(s.topY, (r.floorTopByStory[1] + 8) / 9);
    EXPECT_GT(s.holeW, 0); EXPECT_GT(s.holeD, 0)
        << "well hole missing - emergence through the upper slab unrecorded";
}

// featureAt learns "stair": the flight volume answers "stair", and at the
// emergence level the well HOLE answers "stair" while intact slab around it
// stays "floor". Every plan answer is cross-checked against the REAL canvas
// (hole micro = air, intact slab micro = solid) so a record that lies about
// geometry fails here, not in a downstream consumer.
TEST(AssemblyPlanStairTest, FeatureAtClassifiesFlightAndWellHole) {
    auto r = StructureRealizer::realizeShell(twoStoryWithStair(), timberCottageStyle());
    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_FALSE(r.plan.stairs.empty()) << "no stair record - see RealizedStairIsRecordedInPlan";
    const StairRecord& s = r.plan.stairs[0];

    // Mid-flight volume cell inside the well.
    const int midY = (s.baseY + s.topY) / 2;
    EXPECT_EQ(r.plan.featureAt({s.x, midY, s.z}), "stair")
        << "well volume cell at y=" << midY << " not classified as stair";

    // Emergence level: probe the hole's center cube and cross-check the canvas.
    const int emergY = s.topY - 1;
    const int holeCx = (s.holeX + s.holeW / 2) / 9;
    const int holeCz = (s.holeZ + s.holeD / 2) / 9;
    EXPECT_EQ(r.plan.featureAt({holeCx, emergY, holeCz}), "stair")
        << "stairwell hole in the upper slab still classifies as slab";
    // The canvas agrees the slab is CUT there (top-most slab micro is air):
    EXPECT_FALSE(r.canvas.occupiedMicro(s.holeX + s.holeW / 2,
                                        s.topWalkMicro - 1,
                                        s.holeZ + s.holeD / 2))
        << "record claims a hole where the canvas has solid slab";

    // An intact-slab cell inside the well rect (outside the hole), if one exists,
    // must STAY "floor" - the stair claim must not swallow surrounding slab.
    const int hx0 = s.holeX / 9, hx1 = (s.holeX + s.holeW - 1) / 9;
    const int hz0 = s.holeZ / 9, hz1 = (s.holeZ + s.holeD - 1) / 9;
    for (int cx = s.x; cx < s.x + s.w; ++cx)
        for (int cz = s.z; cz < s.z + s.d; ++cz) {
            if (cx >= hx0 && cx <= hx1 && cz >= hz0 && cz <= hz1) continue;
            EXPECT_EQ(r.plan.featureAt({cx, emergY, cz}), "floor")
                << "intact slab at (" << cx << "," << emergY << "," << cz
                << ") over-claimed as stair";
            EXPECT_TRUE(r.canvas.occupiedMicro(cx * 9 + 4, s.topWalkMicro - 1, cz * 9 + 4))
                << "expected intact slab micro at cube (" << cx << "," << cz << ")";
        }
}

// The plan is persisted as assembly_plan metadata on every placed object (the
// destruction system reads it back) - stair records must round-trip.
TEST(AssemblyPlanStairTest, StairRecordJsonRoundTrip) {
    auto r = StructureRealizer::realizeShell(twoStoryWithStair(), timberCottageStyle());
    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_FALSE(r.plan.stairs.empty());

    AssemblyPlan back = AssemblyPlan::fromJson(r.plan.toJson());
    ASSERT_EQ(back.stairs.size(), r.plan.stairs.size());
    const StairRecord& a = r.plan.stairs[0];
    const StairRecord& b = back.stairs[0];
    EXPECT_EQ(a.x, b.x); EXPECT_EQ(a.z, b.z); EXPECT_EQ(a.w, b.w); EXPECT_EQ(a.d, b.d);
    EXPECT_EQ(a.fromStory, b.fromStory); EXPECT_EQ(a.toStory, b.toStory);
    EXPECT_EQ(a.baseY, b.baseY); EXPECT_EQ(a.topY, b.topY);
    EXPECT_EQ(a.botWalkMicro, b.botWalkMicro); EXPECT_EQ(a.topWalkMicro, b.topWalkMicro);
    EXPECT_EQ(a.form, b.form);
    EXPECT_EQ(a.holeX, b.holeX); EXPECT_EQ(a.holeZ, b.holeZ);
    EXPECT_EQ(a.holeW, b.holeW); EXPECT_EQ(a.holeD, b.holeD);
}

// Consumer-swap equivalence FOR ADJACENT, WELL-FORMED STAIRS (the only kind
// current typologies author - RoomLayout::generateStoryStairs): the plan-derived
// rect set must equal what the old StructureBuildService re-scan produced, so
// StairReservationTest's audited behavior carries over unchanged. The general
// case is NOT equivalent by design - see
// SkippedStairsReserveNothingBecauseNothingWasBuilt below.
TEST(AssemblyPlanStairTest, PlanStairRectsMatchProgramDerivedRects) {
    BuildingProgram p = twoStoryWithStair();
    auto r = StructureRealizer::realizeShell(p, timberCottageStyle());
    ASSERT_TRUE(r.ok) << r.error;

    for (size_t si = 0; si < p.stories.size(); ++si) {
        std::vector<Rect> fromProgram;   // the OLD StructureBuildService re-scan
        for (const auto& st2 : p.stories)
            for (const auto& sr : st2.stairs)
                if (sr.fromStory == static_cast<int>(si) || sr.toStory == static_cast<int>(si))
                    fromProgram.push_back(sr.rect);
        std::vector<Rect> fromPlan;      // the NEW plan-record derivation
        for (const auto& sr : r.plan.stairs)
            if (sr.fromStory == static_cast<int>(si) || sr.toStory == static_cast<int>(si))
                fromPlan.push_back(Rect{sr.x, sr.z, sr.w, sr.d});

        ASSERT_EQ(fromPlan.size(), fromProgram.size()) << "story " << si;
        for (size_t k = 0; k < fromPlan.size(); ++k) {
            EXPECT_EQ(fromPlan[k].x, fromProgram[k].x) << "story " << si;
            EXPECT_EQ(fromPlan[k].z, fromProgram[k].z) << "story " << si;
            EXPECT_EQ(fromPlan[k].w, fromProgram[k].w) << "story " << si;
            EXPECT_EQ(fromPlan[k].d, fromProgram[k].d) << "story " << si;
        }
    }
}

// AUDITOR-PRESCRIBED divergence pin: the old program re-scan and the plan-record
// derivation are NOT equivalent in general. Stairs the realizer SKIPS - duplicate
// keys, degenerate rects (w<1 or d<1), non-adjacent story spans (b != a+1) - get
// no treads AND no StairRecord, so the new derivation reserves nothing there
// while the old re-scan reserved a rect for a stair that never physically
// existed. The divergence is the FIX (reservation matches built reality), pinned
// here deliberately: each skip branch is constructed, the plan is asserted to
// exclude it, the old re-scan is asserted to INCLUDE it (divergence is real),
// and the canvas is asserted empty in the skipped well (nothing to reserve).
TEST(AssemblyPlanStairTest, SkippedStairsReserveNothingBecauseNothingWasBuilt) {
    BuildingProgram p;
    p.name = "stair_tower"; p.style = "timber_cottage";
    p.footprintW = 7; p.footprintD = 9; p.substructure = "slab";
    for (int s = 0; s < 3; ++s) {
        ProgStory st; st.height = 3;
        ProgRoom r; r.id = "room" + std::to_string(s); r.rect = {0, 0, 7, 9};
        r.purpose = "generic";
        st.rooms.push_back(r);
        p.stories.push_back(st);
    }
    ProgPortal door; door.a = "exterior"; door.b = "room0";
    door.px = 0; door.pz = 3; door.width = 1; door.height = 2; door.kind = "door";
    p.stories[0].portals.push_back(door);

    auto addStair = [&](int from, int to, Rect rc) {
        ProgStair sr; sr.fromStory = from; sr.toStory = to;
        sr.rect = rc; sr.form = "switchback";
        p.stories[0].stairs.push_back(sr);
    };
    addStair(0, 1, Rect{2, 3, 2, 4});   // VALID - the only one that builds
    addStair(0, 1, Rect{2, 3, 2, 4});   // skip: duplicate key (seenStairs dedup)
    addStair(0, 1, Rect{5, 3, 0, 2});   // skip: degenerate rect (w < 1)
    addStair(0, 2, Rect{4, 3, 2, 4});   // skip: non-adjacent (b != a+1)

    auto r = StructureRealizer::realizeShell(p, timberCottageStyle());
    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_GE(r.floorTopByStory.size(), 2u);

    // The plan records ONLY the flight that was built.
    ASSERT_EQ(r.plan.stairs.size(), 1u)
        << "skipped stairs (dup/degenerate/non-adjacent) must not produce records";
    EXPECT_EQ(r.plan.stairs[0].x, 2); EXPECT_EQ(r.plan.stairs[0].z, 3);
    EXPECT_EQ(r.plan.stairs[0].w, 2); EXPECT_EQ(r.plan.stairs[0].d, 4);
    EXPECT_EQ(r.plan.stairs[0].fromStory, 0); EXPECT_EQ(r.plan.stairs[0].toStory, 1);

    // The divergence vs the OLD re-scan is REAL: for story 0 the old code
    // derived 4 rects (all authored stairs), the plan derives 1.
    int oldCount = 0;
    for (const auto& st2 : p.stories)
        for (const auto& sr : st2.stairs)
            if (sr.fromStory == 0 || sr.toStory == 0) ++oldCount;
    EXPECT_EQ(oldCount, 4) << "fixture drifted - divergence no longer constructed";
    int newCount = 0;
    for (const auto& sr : r.plan.stairs)
        if (sr.fromStory == 0 || sr.toStory == 0) ++newCount;
    EXPECT_EQ(newCount, 1);

    // And reserving nothing there is CORRECT because nothing exists there: the
    // skipped non-adjacent well (rect {4,3,2,4}) has no treads - its flight
    // volume above the ground floor is pure air...
    const int botWalk = r.floorTopByStory[0];
    const int upperWalk = r.floorTopByStory[1];
    for (int cx = 4; cx <= 5; ++cx)
        for (int cz = 3; cz <= 6; ++cz)
            for (int myy = botWalk + 2; myy < upperWalk - 3; myy += 3)
                EXPECT_FALSE(r.canvas.occupiedMicro(cx * 9 + 4, myy, cz * 9 + 4))
                    << "treads found in a SKIPPED stair well at cube (" << cx << ","
                    << cz << ") micro y=" << myy;
    // ...and the story-1 slab above it is INTACT (no hole was cut).
    for (int cx = 4; cx <= 5; ++cx)
        for (int cz = 3; cz <= 6; ++cz)
            EXPECT_TRUE(r.canvas.occupiedMicro(cx * 9 + 4, upperWalk - 1, cz * 9 + 4))
                << "hole cut over a SKIPPED stair well at cube (" << cx << "," << cz << ")";
}
