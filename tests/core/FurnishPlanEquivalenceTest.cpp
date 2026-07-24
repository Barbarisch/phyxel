#include <gtest/gtest.h>

#include "core/FurniturePlacer.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"
#include "core/BuildingProgram.h"

using namespace Phyxel::Core;

// ============================================================================
// Claims Ledger increment 3 (docs/structure-generation/ClaimsLedger.md):
// furnishFromPlan() derives the geometric side-channels (exterior/interior wall
// thickness, stair reservations) from the realized AssemblyPlan and must place
// EXACTLY what the legacy call places when the legacy arm is fed the same
// service-side expressions (StructureBuildService's own derivations, replicated
// verbatim here). RED state: furnishFromPlan is a stub returning nothing.
// ============================================================================

namespace {

StyleProfile styleFromJson(const char* id, const std::string& json) {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(json));
    return *reg.get(id);
}

StyleProfile cottageStyle() {
    return styleFromJson("timber_cottage", R"({
        "timber_cottage": {
            "roof_style": "gable", "foundation": "slab",
            "thickness": { "exterior_wall": 0.333, "interior_wall": 0.222,
                           "foundation_wall": 0.667, "floor": 0.333, "ceiling": 0.222 },
            "materials": { "structure": "WoodPlanks", "floor": "Wood", "roof": "Wood",
                           "foundation": "Stone", "trim": "Log" },
            "roof": { "pitch": 0.8 }
        }
    })");
}

// A keep-thick style: exterior 3.0 m — the clamp case the thicknessMicro contract
// documents (must derive 9 micro through the PLAN too, never 27).
StyleProfile keepStyle() {
    return styleFromJson("stone_keep", R"({
        "stone_keep": {
            "roof_style": "flat", "foundation": "slab",
            "thickness": { "exterior_wall": 3.0, "interior_wall": 0.667,
                           "foundation_wall": 1.0, "floor": 0.333, "ceiling": 0.333 },
            "materials": { "structure": "StoneBricks", "floor": "StoneTiles", "roof": "Stone",
                           "foundation": "Stone", "trim": "Sandstone" },
            "roof": { "pitch": 0.1 }
        }
    })");
}

ProgPortal portal(const std::string& a, const std::string& b, int px, int pz,
                  int w, int h, const std::string& kind) {
    ProgPortal p; p.a = a; p.b = b; p.px = px; p.pz = pz;
    p.width = w; p.height = h; p.kind = kind;
    return p;
}

// Multi-room, multi-purpose single story: real recipes fire for living/kitchen/
// bedchamber; interior partitions give partition-inset placements.
BuildingProgram furnishedHouse() {
    BuildingProgram p;
    p.name = "equiv_house"; p.style = "timber_cottage";
    p.footprintW = 10; p.footprintD = 9; p.substructure = "slab";
    ProgStory st; st.height = 3;
    ProgRoom a; a.id = "hall"; a.rect = {0, 0, 5, 9}; a.purpose = "living";
    ProgRoom b; b.id = "kitchen"; b.rect = {5, 0, 5, 5}; b.purpose = "kitchen";
    ProgRoom c; c.id = "bed"; c.rect = {5, 5, 5, 4}; c.purpose = "bedchamber";
    st.rooms.push_back(a); st.rooms.push_back(b); st.rooms.push_back(c);
    st.portals.push_back(portal("exterior", "hall", 0, 4, 1, 2, "door"));
    st.portals.push_back(portal("hall", "kitchen", 5, 2, 1, 2, "door"));
    st.portals.push_back(portal("kitchen", "bed", 7, 5, 1, 2, "door"));
    p.stories.push_back(st);
    return p;
}

// Two stories + stair: exercises the stair-reservation channel on BOTH stories.
BuildingProgram stairHouse() {
    BuildingProgram p;
    p.name = "equiv_stair"; p.style = "timber_cottage";
    p.footprintW = 7; p.footprintD = 9; p.substructure = "slab";
    for (int s = 0; s < 2; ++s) {
        ProgStory st; st.height = 3;
        ProgRoom r; r.id = s ? "loft" : "hall"; r.rect = {0, 0, 7, 9};
        r.purpose = s ? "bedchamber" : "living";
        st.rooms.push_back(r);
        p.stories.push_back(st);
    }
    p.stories[0].portals.push_back(portal("exterior", "hall", 0, 3, 1, 2, "door"));
    ProgStair sr; sr.fromStory = 0; sr.toStory = 1;
    sr.rect = {2, 3, 2, 4}; sr.form = "switchback";
    p.stories[0].stairs.push_back(sr);
    return p;
}

std::map<std::string, Footprint> someFootprints() {
    std::map<std::string, Footprint> f;
    f["bed"]   = {2, 3, 17, 26, 9};
    f["table"] = {2, 2, 17, 17, 8};
    return f;
}

void expectSamePlacements(const std::vector<FurniturePlacement>& legacy,
                          const std::vector<FurniturePlacement>& fromPlan,
                          const char* tag) {
    ASSERT_EQ(fromPlan.size(), legacy.size()) << tag;
    for (size_t i = 0; i < legacy.size(); ++i) {
        const auto& a = legacy[i];
        const auto& b = fromPlan[i];
        EXPECT_EQ(a.type, b.type) << tag << " #" << i;
        EXPECT_EQ(a.worldPos, b.worldPos) << tag << " #" << i << " (" << a.type << ")";
        EXPECT_EQ(a.rotation, b.rotation) << tag << " #" << i << " (" << a.type << ")";
        EXPECT_EQ(a.room, b.room) << tag << " #" << i;
        EXPECT_EQ(a.backDir, b.backDir) << tag << " #" << i << " (" << a.type << ")";
        EXPECT_EQ(a.insetMicroX, b.insetMicroX) << tag << " #" << i << " (" << a.type << ")";
        EXPECT_EQ(a.insetMicroZ, b.insetMicroZ) << tag << " #" << i << " (" << a.type << ")";
    }
}

// The LEGACY arm. Thickness expressions replicate the service's pre-increment-3
// code verbatim (thicknessMicro over style values). The stair arm deliberately
// replicates the ORIGINAL pre-increment-1 program re-scan — one step further
// back than the commit under test (which already scanned plan.stairs) — so this
// equivalence reaches all the way to the pre-ledger behavior; increment 1's
// divergence test pinned program-vs-plan equality for well-formed adjacent
// stairs, which is all these fixtures author. (Auditor round-3 finding: the
// earlier "replicated verbatim" wording overclaimed the stair portion.)
std::vector<FurniturePlacement> legacyFurnish(const BuildingProgram& p, size_t si,
                                              const StyleProfile& style,
                                              const glm::ivec3& origin, int floorY,
                                              const std::map<std::string, Footprint>& fps) {
    const int extT = StructureRealizer::thicknessMicro(
        style.thicknessOf("exterior_wall", 0.333));
    const int intT = StructureRealizer::thicknessMicro(
        style.thicknessOf("interior_wall", 0.222));
    std::vector<Rect> stairRects;
    for (const auto& st2 : p.stories)
        for (const auto& sr : st2.stairs)
            if (sr.fromStory == static_cast<int>(si) || sr.toStory == static_cast<int>(si))
                stairRects.push_back(sr.rect);
    return FurniturePlacer::furnish(p.stories[si], origin, floorY, fps, nullptr,
                                    extT, "", stairRects, intT);
}

} // namespace

// The plan-derivation helpers reproduce the service's numbers exactly —
// including the documented clamp (3.0 m keep wall -> 9 micro, never 27).
TEST(FurnishPlanEquivalenceTest, PlanDerivedThicknessMatchesServiceDerivation) {
    auto cottage = StructureRealizer::realizeShell(furnishedHouse(), cottageStyle());
    ASSERT_TRUE(cottage.ok) << cottage.error;
    EXPECT_EQ(FurniturePlacer::planExteriorThicknessMicro(cottage.plan), 3);
    EXPECT_EQ(FurniturePlacer::planInteriorThicknessMicro(cottage.plan), 2);

    BuildingProgram keepProg = furnishedHouse();
    keepProg.style = "stone_keep";
    auto keep = StructureRealizer::realizeShell(keepProg, keepStyle());
    ASSERT_TRUE(keep.ok) << keep.error;
    EXPECT_EQ(FurniturePlacer::planExteriorThicknessMicro(keep.plan), 9)
        << "3.0 m keep wall must clamp to 9 micro through the plan too";
    EXPECT_EQ(FurniturePlacer::planInteriorThicknessMicro(keep.plan), 6);
}

// furnishFromPlan == legacy furnish, field-by-field, for a furnished multi-room
// house in both styles (normal + clamped thickness).
TEST(FurnishPlanEquivalenceTest, PlanFurnishMatchesLegacySingleStory) {
    const glm::ivec3 origin{20, 0, 30};
    const int floorY = 17;
    for (const auto& sc : {std::pair<const char*, StyleProfile>{"cottage", cottageStyle()},
                           std::pair<const char*, StyleProfile>{"keep", keepStyle()}}) {
        BuildingProgram p = furnishedHouse();
        if (std::string(sc.first) == "keep") p.style = "stone_keep";
        auto shell = StructureRealizer::realizeShell(p, sc.second);
        ASSERT_TRUE(shell.ok) << shell.error;

        auto legacy = legacyFurnish(p, 0, sc.second, origin, floorY, someFootprints());
        ASSERT_FALSE(legacy.empty()) << sc.first << ": legacy arm placed nothing - fixture broken";
        auto fromPlan = FurniturePlacer::furnishFromPlan(
            p.stories[0], 0, origin, floorY, shell.plan, someFootprints(), nullptr, "");
        expectSamePlacements(legacy, fromPlan, sc.first);
    }
}

// Both stories of a stair house: the stair-reservation channel must flow from
// plan.stairs identically (ground story = departing base, upper = arriving well).
TEST(FurnishPlanEquivalenceTest, PlanFurnishMatchesLegacyAcrossStoriesWithStairs) {
    const glm::ivec3 origin{0, 0, 0};
    BuildingProgram p = stairHouse();
    auto shell = StructureRealizer::realizeShell(p, cottageStyle());
    ASSERT_TRUE(shell.ok) << shell.error;
    ASSERT_EQ(shell.plan.stairs.size(), 1u) << "stair record missing (increment 1 regressed?)";

    for (size_t si = 0; si < p.stories.size(); ++si) {
        const int floorY = (si < shell.floorTopByStory.size())
                               ? shell.floorTopByStory[si] / 9 : 1;
        auto legacy = legacyFurnish(p, si, cottageStyle(), origin, floorY, someFootprints());
        ASSERT_FALSE(legacy.empty()) << "story " << si << ": legacy arm placed nothing";
        auto fromPlan = FurniturePlacer::furnishFromPlan(
            p.stories[si], static_cast<int>(si), origin, floorY, shell.plan,
            someFootprints(), nullptr, "");
        expectSamePlacements(legacy, fromPlan, si == 0 ? "story0" : "story1");
    }
}

// The unplaced out-param flows through identically (honest-drop reporting).
TEST(FurnishPlanEquivalenceTest, UnplacedReportingFlowsThrough) {
    BuildingProgram p = furnishedHouse();
    auto shell = StructureRealizer::realizeShell(p, cottageStyle());
    ASSERT_TRUE(shell.ok) << shell.error;

    const int extT = StructureRealizer::thicknessMicro(0.333);
    const int intT = StructureRealizer::thicknessMicro(0.222);
    std::vector<UnplacedFixture> legacyUn, planUn;
    FurniturePlacer::furnish(p.stories[0], {0, 0, 0}, 1, someFootprints(), &legacyUn,
                             extT, "", {}, intT);
    FurniturePlacer::furnishFromPlan(p.stories[0], 0, {0, 0, 0}, 1, shell.plan,
                                     someFootprints(), &planUn, "");
    ASSERT_EQ(planUn.size(), legacyUn.size());
    for (size_t i = 0; i < legacyUn.size(); ++i) {
        EXPECT_EQ(planUn[i].room, legacyUn[i].room);
        EXPECT_EQ(planUn[i].type, legacyUn[i].type);
    }
}
