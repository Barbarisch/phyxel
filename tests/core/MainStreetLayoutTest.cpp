#include <gtest/gtest.h>

#include <cmath>
#include <set>
#include <sstream>

#include "core/RoomProgram.h"
#include "core/SettlementLayout.h"
#include "core/SettlementProgram.h"

using namespace Phyxel::Core;

// ============================================================================
// planMainStreetLayout — the row-village / burgage morphology (#39 lay_street_
// network / #38 site_settlement partial). L2 invariants:
//   * plots on BOTH sides of the spine, every plot ABUTS + FRONTS it
//   * no plot overlaps a street or another plot
//   * plot frontage is sized FROM its assigned typology (the burgage principle)
//     — the fix for the uniform-grid plot/typology mismatch (footprint_too_wide)
//   * building orientation follows the typology's entrance rule: "long_wall"
//     dwellings present the LONG wall to the street; shops/tavern the GABLE
//   * buildings are their typology's NATURAL size — never stretched to the plot
//   * deterministic in seed; the weighted draw covers the whole tier palette
// RED baseline: the uniform-frontage stub (today's grid behavior transplanted)
// fails the frontage/orientation/natural-size invariants.
// ============================================================================

namespace {
bool loadShipped(SettlementProgramRegistry& reg) {
    for (const char* p : {"resources/settlement_program.json", "../resources/settlement_program.json",
                          "../../resources/settlement_program.json",
                          "../../../resources/settlement_program.json"})
        if (reg.loadFromFile(p)) return true;
    return false;
}
bool loadRooms(RoomProgramRegistry& reg) {
    for (const char* p : {"resources/room_program.json", "../resources/room_program.json",
                          "../../resources/room_program.json", "../../../resources/room_program.json"})
        if (reg.loadFromFile(p)) return true;
    return false;
}
bool overlaps(const Rect& a, const Rect& b) {
    return a.x < b.x1() && b.x < a.x1() && a.z < b.z1() && b.z < a.z1();
}
bool contains(const Rect& outer, const Rect& inner) {
    return inner.x >= outer.x && inner.x1() <= outer.x1() &&
           inner.z >= outer.z && inner.z1() <= outer.z1();
}
// Natural building dims from the typology canon (the same derivation the build service uses).
void naturalDims(const RoomProgram& rp, int& natLong, int& natShort) {
    natLong  = std::max(1, (int)std::lround(rp.bays * rp.bayLength));
    natShort = std::max(1, (int)std::lround(rp.widthMax > 0 ? rp.widthMax : rp.widthMin));
}
// The plot/footprint dimension ALONG the street vs PERPENDICULAR to it, given the street side.
int alongStreet(const Rect& r, char side) { return (side == 'N' || side == 'S') ? r.w : r.d; }
int acrossStreet(const Rect& r, char side) { return (side == 'N' || side == 'S') ? r.d : r.w; }

std::string serialize(const MainStreetLayout& l) {
    std::ostringstream os;
    for (const auto& a : l.assigned)
        os << a.typology << ':' << a.plot.rect.x << ',' << a.plot.rect.z << ','
           << a.plot.rect.w << ',' << a.plot.rect.d << '|' << a.footprint.x << ','
           << a.footprint.z << ',' << a.footprint.w << ',' << a.footprint.d << '|'
           << a.streetSide << a.setback << ';';
    return os.str();
}

struct Fixture {
    SettlementProgramRegistry sreg;
    RoomProgramRegistry rreg;
    const SettlementTierPreset* village = nullptr;
    bool ok = false;
    Fixture() {
        if (loadShipped(sreg) && loadRooms(rreg)) {
            village = sreg.get("medieval", "village");
            ok = village != nullptr;
        }
    }
};
} // namespace

TEST(MainStreetLayoutTest, PlotsOnBothSidesAndAbutTheSpine) {
    Fixture f;
    if (!f.ok) GTEST_SKIP() << "canon files not reachable from CWD";
    const auto l = planMainStreetLayout(*f.village, 80, 40, f.rreg, 3);
    ASSERT_TRUE(l.ok);
    ASSERT_GE(l.assigned.size(), 4u);
    int sideA = 0, sideB = 0;
    for (const auto& a : l.assigned) {
        const Rect& p = a.plot.rect;
        const Rect& s = l.mainStreet;
        // ABUTS: the plot edge on its street side coincides with the street edge.
        switch (a.streetSide) {
            case 'S': EXPECT_EQ(p.z, s.z1()) << "plot must sit on the street's +z edge"; ++sideA; break;
            case 'N': EXPECT_EQ(p.z1(), s.z) << "plot must sit on the street's -z edge"; ++sideB; break;
            case 'W': EXPECT_EQ(p.x, s.x1()); ++sideA; break;
            case 'E': EXPECT_EQ(p.x1(), s.x); ++sideB; break;
            default: FAIL() << "assigned plot has no street side";
        }
    }
    EXPECT_GT(sideA, 0) << "no plots on the first side of the main street";
    EXPECT_GT(sideB, 0) << "no plots on the second side of the main street";
}

TEST(MainStreetLayoutTest, NoPlotOverlapsAStreetOrAnotherPlot) {
    Fixture f;
    if (!f.ok) GTEST_SKIP() << "canon files not reachable from CWD";
    const auto l = planMainStreetLayout(*f.village, 80, 40, f.rreg, 7);
    ASSERT_TRUE(l.ok);
    for (size_t i = 0; i < l.assigned.size(); ++i) {
        for (const auto& s : l.base.streets)
            EXPECT_FALSE(overlaps(l.assigned[i].plot.rect, s)) << "plot " << i << " overlaps a street";
        for (size_t j = i + 1; j < l.assigned.size(); ++j)
            EXPECT_FALSE(overlaps(l.assigned[i].plot.rect, l.assigned[j].plot.rect))
                << "plots " << i << "," << j << " overlap";
        EXPECT_TRUE(contains(Rect{0, 0, 80, 40}, l.assigned[i].plot.rect))
            << "plot " << i << " pokes out of the settlement footprint";
    }
}

// THE burgage invariant (RED on the uniform-frontage stub): a plot's street frontage is sized from
// its ASSIGNED typology — building frontage + 2*setback — so a croft gets a narrow plot and a hall
// a wide one, and the footprint gate (footprint_too_wide/square) can't trip by construction.
TEST(MainStreetLayoutTest, PlotFrontageIsSizedFromItsTypology) {
    Fixture f;
    if (!f.ok) GTEST_SKIP() << "canon files not reachable from CWD";
    const auto l = planMainStreetLayout(*f.village, 120, 44, f.rreg, 3);
    ASSERT_TRUE(l.ok);
    std::set<int> frontages;
    for (const auto& a : l.assigned) {
        const RoomProgram* rp = f.rreg.get(a.typology);
        ASSERT_NE(rp, nullptr) << "assigned unknown typology " << a.typology;
        EXPECT_EQ(alongStreet(a.plot.rect, a.streetSide),
                  alongStreet(a.footprint, a.streetSide) + 2 * a.setback)
            << a.typology << " plot frontage != building frontage + 2*setback";
        EXPECT_GE(a.setback, f.village->setback.min);
        EXPECT_LE(a.setback, f.village->setback.max);
        frontages.insert(alongStreet(a.plot.rect, a.streetSide));
    }
    // Mixed typologies (croft 4-6 m wide vs 16 m-long halls) MUST produce varied frontages —
    // uniform frontage is exactly the old grid defect.
    EXPECT_GT(frontages.size(), 1u) << "all plots share one frontage — uniform-grid behavior";
}

// Orientation follows the typology's entrance rule (RED on the stub): "long_wall" dwellings
// (croft/longhouse/hall_house) present their LONG wall to the street; gable/shop typologies
// (tavern, blacksmith, ...) present the GABLE — the burgage narrow-frontage read. Buildings are
// their NATURAL size, never stretched to fill the plot.
TEST(MainStreetLayoutTest, BuildingOrientationAndNaturalSizeFollowTheTypology) {
    Fixture f;
    if (!f.ok) GTEST_SKIP() << "canon files not reachable from CWD";
    const auto l = planMainStreetLayout(*f.village, 120, 44, f.rreg, 5);
    ASSERT_TRUE(l.ok);
    for (const auto& a : l.assigned) {
        const RoomProgram* rp = f.rreg.get(a.typology);
        ASSERT_NE(rp, nullptr);
        int natLong = 0, natShort = 0;
        naturalDims(*rp, natLong, natShort);
        const int fAlong = alongStreet(a.footprint, a.streetSide);
        const int fAcross = acrossStreet(a.footprint, a.streetSide);
        if (rp->entrance == "long_wall") {
            EXPECT_EQ(fAlong, natLong) << a.typology << " long wall must face the street";
            EXPECT_EQ(fAcross, natShort) << a.typology;
        } else {
            EXPECT_EQ(fAlong, natShort) << a.typology << " gable must face the street (burgage)";
            EXPECT_EQ(fAcross, natLong) << a.typology;
        }
        // Front wall sits `setback` in from the street edge; the rest of the plot is the rear toft.
        const Rect& p = a.plot.rect;
        switch (a.streetSide) {
            case 'S': EXPECT_EQ(a.footprint.z, p.z + a.setback); break;
            case 'N': EXPECT_EQ(a.footprint.z1(), p.z1() - a.setback); break;
            case 'W': EXPECT_EQ(a.footprint.x, p.x + a.setback); break;
            case 'E': EXPECT_EQ(a.footprint.x1(), p.x1() - a.setback); break;
        }
        EXPECT_TRUE(contains(p, a.footprint)) << "footprint must stay inside its plot";
    }
}

// The weighted draw is deterministic and covers the whole tier palette over enough plots.
TEST(MainStreetLayoutTest, WeightedDrawCoversThePaletteAndIsDeterministic) {
    Fixture f;
    if (!f.ok) GTEST_SKIP() << "canon files not reachable from CWD";
    std::set<std::string> drawn;
    for (int i = 0; i < 64; ++i) drawn.insert(drawTypology(f.village->typologyWeights, i, 11));
    for (const auto& [typ, w] : f.village->typologyWeights)
        if (w > 0) EXPECT_TRUE(drawn.count(typ)) << "palette typology never drawn: " << typ;
    for (int i = 0; i < 8; ++i)
        EXPECT_EQ(drawTypology(f.village->typologyWeights, i, 11),
                  drawTypology(f.village->typologyWeights, i, 11));
    EXPECT_EQ(drawTypology({}, 0, 1), "hall_house");   // empty weights -> sane fallback
}

TEST(MainStreetLayoutTest, DeterministicInSeedAndRespectsBuildingCap) {
    Fixture f;
    if (!f.ok) GTEST_SKIP() << "canon files not reachable from CWD";
    const auto a = planMainStreetLayout(*f.village, 100, 44, f.rreg, 42);
    const auto b = planMainStreetLayout(*f.village, 100, 44, f.rreg, 42);
    const auto c = planMainStreetLayout(*f.village, 100, 44, f.rreg, 43);
    ASSERT_TRUE(a.ok);
    EXPECT_EQ(serialize(a), serialize(b)) << "same seed must reproduce the identical layout";
    EXPECT_NE(serialize(a), serialize(c)) << "different seed should vary the layout";
    EXPECT_LE((int)a.assigned.size(), f.village->buildingsMax);
}

// Degenerate inputs: a footprint too small for the spine (or a zero-width street tier) returns
// ok=false — the caller surfaces it, nothing silently placed.
TEST(MainStreetLayoutTest, TooSmallFootprintReturnsNotOk) {
    Fixture f;
    if (!f.ok) GTEST_SKIP() << "canon files not reachable from CWD";
    EXPECT_FALSE(planMainStreetLayout(*f.village, 4, 3, f.rreg, 1).ok);
    SettlementTierPreset noStreet = *f.village;
    noStreet.street.mainWidth = 0;   // cluster-form tier — not a main-street morphology
    EXPECT_FALSE(planMainStreetLayout(noStreet, 80, 40, f.rreg, 1).ok);
}

// chooseStreetAxis picks the flattest band: on a site with a flat valley running along Z at x=8..12
// and steep ground elsewhere, the spine must run along Z inside the valley.
TEST(MainStreetLayoutTest, ChooseStreetAxisFindsTheFlatValley) {
    auto terrain = [](int x, int z) {
        (void)z;
        return (x >= 8 && x < 13) ? 10 : 10 + std::abs(x - 10);   // V-valley along Z, floor at x 8-12
    };
    const auto site = analyzeSite(24, 24, 3, terrain, {}, 1, 1);
    const auto pick = chooseStreetAxis(site, 5);
    EXPECT_EQ(pick.axis, 'Z');
    EXPECT_GE(pick.crossOffset, 7);
    EXPECT_LE(pick.crossOffset, 9);   // band [offset, offset+5) inside/centred on the valley floor
}

// Road-arrival bias (WorldForge street orientation): on comparable terrain the preferred
// axis (the one the arriving road runs along) wins over the long-axis tiebreak — but never
// over genuinely hostile terrain. Red-before-green against the ignored-parameter stub.
TEST(MainStreetLayoutTest, ChooseStreetAxisHonorsRoadPreference) {
    // Uniform terrain, LONG axis X: unbiased pick is X (the tiebreak); preferring Z flips it.
    auto flat = [](int x, int z) { (void)x; (void)z; return 10; };
    const auto site = analyzeSite(40, 20, 3, flat, {}, 1, 1);
    EXPECT_EQ(chooseStreetAxis(site, 5).axis, 'X') << "baseline: long axis without preference";
    EXPECT_EQ(chooseStreetAxis(site, 5, 0, 'Z').axis, 'Z')
        << "the arriving road's axis must win on comparable terrain";
    // Terrain still wins: a flat valley along Z with steep ground elsewhere ignores an X
    // preference (the street must not climb the walls to meet the road).
    auto valley = [](int x, int z) {
        (void)z;
        return (x >= 8 && x < 13) ? 10 : 10 + 3 * std::abs(x - 10);
    };
    const auto vsite = analyzeSite(24, 24, 3, valley, {}, 1, 1);
    EXPECT_EQ(chooseStreetAxis(vsite, 5, 0, 'X').axis, 'Z')
        << "hostile terrain overrides the road preference";
}

// Lateral arrival preference: on comparable terrain the band lands where the road arrives
// (so street and road meet head-on), but a flat valley elsewhere still wins. Red-first
// against the ignored-parameter stub.
TEST(MainStreetLayoutTest, ChooseStreetAxisHonorsArrivalOffset) {
    auto flat = [](int x, int z) { (void)x; (void)z; return 10; };
    const auto site = analyzeSite(40, 20, 3, flat, {}, 1, 1);
    // Baseline on flat ground: the offset search keeps the FIRST (lowest) band.
    const auto base = chooseStreetAxis(site, 5, 0, 'X');
    ASSERT_EQ(base.axis, 'X');
    ASSERT_EQ(base.crossOffset, 0);
    // Preferring band start 9 moves the band there on flat ground.
    const auto biased = chooseStreetAxis(site, 5, 0, 'X', 9);
    EXPECT_EQ(biased.axis, 'X');
    EXPECT_EQ(biased.crossOffset, 9) << "flat ground must defer to the arrival offset";
    // Terrain still wins: a flat valley band at z=2..6 with steep ground elsewhere ignores
    // a far-away preferred offset.
    auto valley = [](int x, int z) { (void)x; return (z >= 2 && z < 7) ? 10 : 10 + 3 * std::abs(z - 4); };
    const auto vsite = analyzeSite(40, 20, 3, valley, {}, 1, 1);
    const auto vpick = chooseStreetAxis(vsite, 5, 0, 'X', 14);
    EXPECT_LE(std::abs(vpick.crossOffset - 2), 1) << "hostile terrain overrides the offset bias";
}

// RED (found live 2026-07-09, 80x44 Perlin village -> axis Z offset 0, a 2-building one-sided strip):
// the scorer compared TOTAL band relief, so the SHORT axis (fewer cells) always won on uniformly
// noisy terrain. Score must be PER-CELL; on uniform terrain the LONG axis wins (more street = more
// plots), never the short one by cell-count arithmetic.
TEST(MainStreetLayoutTest, ChooseStreetAxisIsNotBiasedTowardTheShortAxis) {
    auto checker = [](int x, int z) { return 10 + ((x + z) % 2); };   // uniform 1-cube noise everywhere
    const auto site = analyzeSite(40, 20, 3, checker, {}, 1, 1);      // W=40 long, D=20 short
    const auto pick = chooseStreetAxis(site, 5);
    EXPECT_EQ(pick.axis, 'X') << "uniform terrain must pick the LONG axis, not the short one";
}

// RED (same live find): offset 0 pinned the street to the site EDGE, leaving zero plot depth on one
// side (a one-sided village). With minPlotDepth the offset search must keep at least that much room
// on BOTH sides of the band.
TEST(MainStreetLayoutTest, ChooseStreetAxisLeavesPlotRoomOnBothSides) {
    // Flattest ground hugs the -x edge (relief grows with x): unrestricted search would pick offset 0.
    auto terrain = [](int x, int z) { (void)z; return 10 + x / 2; };
    const auto site = analyzeSite(20, 20, 6, terrain, {}, 1, 1);
    const int mainWidth = 4, minDepth = 6;
    const auto pick = chooseStreetAxis(site, mainWidth, minDepth);
    const int cross = (pick.axis == 'X') ? site.D : site.W;
    EXPECT_GE(pick.crossOffset, minDepth) << "no plot room on the near side";
    EXPECT_LE(pick.crossOffset + mainWidth, cross - minDepth) << "no plot room on the far side";
}
