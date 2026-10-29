// EarthworkTest — M4b-4 (docs/WorldForgeV2Plan.md D29/D33/D34/D35): retaining walls and rock faces
// on corridor-driven roads. A cut deeper than the bank limit (8.4 u = the widest repose shoulder
// x repose) is held by a dry-stone tier of at most wallTierU at the road edge, the rest of the
// face is rock up to rockCutMaxU (the Pisco Montano precedent), the bank above stands at repose;
// a cut beyond wall + precedent is a refused EDGE (D33 b): the demand is re-routed or refused
// with the reason. Red on the M4c code: no wall column exists, banks beyond 8.4 u stand steeper
// than repose (a dirt cliff), the Mountains world cuts 80 u.
#include <gtest/gtest.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "ForgeTestUtil.h"
#include "core/WorldForgeParams.h"
#include "core/WorldForgePlan.h"
#include "core/WorldGenerator.h"
#include "core/WorldRecipe.h"

using namespace Phyxel;
using namespace Phyxel::ForgeTest;

namespace {

WorldRecipe civRecipe(uint32_t seed, const char* type = "Perlin") {
    WorldRecipe r;
    r.version = 2;
    r.type = type;
    r.seed = seed;
    r.worldforge.enabled = true;
    r.worldforge.civ = true;
    r.worldforge.siteCount = 8;
    r.worldforge.regionRadius = 2048;
    r.worldforge.minSpacing = 400;
    return r;
}

WorldGenerator& baselineWorld() {
    static WorldGenerator* g = [] {
        auto* w = new WorldGenerator(WorldGenerator::GenerationType::Perlin, 20260816u);
        w->applyRecipe(civRecipe(20260816u));
        return w;
    }();
    return *g;
}

WorldGenerator& mountainsWorld() {
    static WorldGenerator* g = [] {
        auto* w = new WorldGenerator(WorldGenerator::GenerationType::Mountains, 424242u);
        w->applyRecipe(civRecipe(424242u, "Mountains"));
        return w;
    }();
    return *g;
}

// The NATURAL surface: the same recipe with WorldForge off - every column exactly as the terrain
// stages emit it, no road grading. (baseHeightFn() is the coarse height the plan routes on, not
// the emitted surface: measured 2026-10-28, flat 407 u across ten columns whose emitted natural
// rose 394 -> 408, and 290 u one column from a vertex whose surface is 305 - the first green's
// 30 "steep banks" were that reference, not the bank.)
WorldGenerator& naturalOf(WorldGenerator& graded) {
    static WorldGenerator* perlin = nullptr;
    static WorldGenerator* mountains = nullptr;
    const bool isMountains = graded.getSeed() == 424242u;
    WorldGenerator*& slot = isMountains ? mountains : perlin;
    if (!slot) {
        WorldRecipe r = civRecipe(isMountains ? 424242u : 20260816u, isMountains ? "Mountains" : "Perlin");
        r.worldforge.enabled = false;
        slot = new WorldGenerator(isMountains ? WorldGenerator::GenerationType::Mountains : WorldGenerator::GenerationType::Perlin, isMountains ? 424242u : 20260816u);
        slot->applyRecipe(r);
    }
    return *slot;
}

float naturalAt(WorldGenerator& graded, const glm::vec2& q) {
    return static_cast<float>(naturalOf(graded).sampleSurface(static_cast<int>(std::lround(q.x)), static_cast<int>(std::lround(q.y))).surfaceY);
}

// A single-road cut vertex: the deepest cuts on shared trunks are bundles whose composite bank is
// not this claim (the M4b audit), so only vertices with exactly one road in reach are measured.
struct CutVertex { glm::vec2 p, n; float cut, grade, natural; int cls; int road = -1; size_t index = 0; };

std::vector<CutVertex> singleRoadCuts(WorldGenerator& g, const WorldForgePlan& box, float minCut, size_t maxCount) {
    std::vector<CutVertex> all;
    for (size_t r = 0; r < box.roads().size(); ++r) {
        const auto& road = box.roads()[r];
        const auto& cl = road.centerline; const auto& pr = road.gradeProfile; const auto& nat = road.naturalY;
        if (cl.size() < 3 || pr.size() != cl.size() || nat.size() != cl.size()) continue;
        for (size_t i = 1; i + 1 < cl.size(); ++i) {
            const float cut = nat[i] - pr[i];
            if (cut < minCut) continue;
            const glm::vec2 d = glm::normalize(cl[i + 1] - cl[i - 1]);
            if (!std::isfinite(d.x)) continue;
            all.push_back({cl[i], glm::vec2(-d.y, d.x), cut, pr[i], nat[i], road.cls, static_cast<int>(r), i});
        }
    }
    std::sort(all.begin(), all.end(), [](const CutVertex& a, const CutVertex& b) { return a.cut > b.cut; });
    std::vector<CutVertex> out;
    for (const CutVertex& v : all) {
        if (out.size() >= maxCount) break;
        std::vector<int> roads;
        box.gradeBlendAt(v.p.x, v.p.y, v.natural, &roads);
        if (roads.size() == 1) out.push_back(v);
    }
    return out;
}

int roundI(float v) { return static_cast<int>(std::lround(v)); }

// The cross-section on one side of a cut vertex, sampled every 1 u from the road edge outward.
struct Section {
    int wallCols = 0;        // columns carrying a wall (wallTopY set, the wall material on top)
    int rockCols = 0;        // columns carrying a rock face
    float wallHeight = 0.0f; // the tallest wall column's height above the grade
    float rockBand = 0.0f;   // the tallest emitted rock band (rockTopY - rockBaseY)
    float bankSlope = 0.0f;  // the realized ABSOLUTE rise per u over the bank (>= 6 columns), less one cube of rounding
    int bankSpan = 0;        // columns the bank was measured over
    int maxStep = 0;         // the largest single-column rise along the bank (a jump = a cliff)
    glm::vec2 jumpAt{0.0f};  // where the largest step was measured
    bool expectWall = false; // the ground at the first column past the edge stands above grade + the bank limit
    bool valid = false;
    std::string dump;        // d:surface/natural[W wallTop|R rockTop] per column, for the diagnostic
};

Section sectionAt(WorldGenerator& g, const WorldForgePlan& box, const CutVertex& v, float side) {
    Section s;
    const float half = WorldForgePlan::roadHalfWidth(v.cls);
    const float reach = half + 1.0f + 2.0f + WorldForgePlan::kShoulderMaxU + 2.0f;
    int firstBankCol = -1; float firstBankSurf = 0.0f, firstBankNat = 0.0f;
    int lastCol = -1; float lastSurf = 0.0f, lastNat = 0.0f;
    int prevSurf = INT_MIN; bool pastBand = false;
    for (float d = half + 0.5f; d <= reach; d += 1.0f) {
        const glm::vec2 q = v.p + v.n * (side * d);
        const auto col = g.sampleSurface(roundI(q.x), roundI(q.y));
        if (col.riverOrder > 0 || col.bridgeDeckY != INT_MIN) return s;   // a channel or a deck in the section: not this claim
        const float natural = naturalAt(g, q);
        std::vector<int> roads;
        box.gradeBlendAt(q.x, q.y, natural, &roads);
        if (roads.size() > 1) return s;                                     // a bundle's composite bank
        const int idx = static_cast<int>(d - half);
        {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%.1f:%d/%.0f%s%s ", d, col.surfaceY, natural,
                          col.wallTopY != INT_MIN ? "W" : "", col.rockTopY != INT_MIN ? "R" : "");
            s.dump += buf;
        }
        if (idx == 0) s.expectWall = natural > v.grade + WorldForgePlan::bankLimit() + 0.5f;
        if (col.wallTopY != INT_MIN) { ++s.wallCols; s.wallHeight = std::max(s.wallHeight, static_cast<float>(col.wallTopY) - v.grade); continue; }
        if (col.rockTopY != INT_MIN) { ++s.rockCols; s.rockBand = std::max(s.rockBand, static_cast<float>(col.rockTopY - col.rockBaseY)); pastBand = true; continue; }
        // the bank starts past the wall/rock band; where no band exists (an unwalled section)
        // it starts at the first column that is not the road's own edge (a bend's normal)
        if (!pastBand && s.wallCols == 0 && col.roadClass > 0) continue;
        pastBand = true;
        if (firstBankCol < 0) { firstBankCol = idx; firstBankSurf = static_cast<float>(col.surfaceY); firstBankNat = natural; }
        else if (prevSurf != INT_MIN && std::abs(col.surfaceY - prevSurf) > s.maxStep) { s.maxStep = std::abs(col.surfaceY - prevSurf); s.jumpAt = q; }   // along the bank only: the wall and rock faces are vertical by design
        prevSurf = col.surfaceY;
        lastCol = idx; lastSurf = static_cast<float>(col.surfaceY); lastNat = natural;
        if (std::fabs(static_cast<float>(col.surfaceY) - natural) < 1.0f && idx > firstBankCol) break;   // the bank met the natural surface
    }
    s.valid = true;   // every section with a cut side counts, walled or not (audit 2026-10-28: the walled == sections assertion was tautological)
    if (firstBankCol < 0 || lastCol <= firstBankCol) return s;
    s.bankSpan = lastCol - firstBankCol;
    // the bank's absolute rise per u (it climbs at repose from the face top until it meets the
    // ground), less ONE cube of rounding over the span (each end rounds by up to 0.5)
    (void)firstBankNat; (void)lastNat;
    const float realized = lastSurf - firstBankSurf;
    s.bankSlope = (std::fabs(realized) - 1.0f) / static_cast<float>(lastCol - firstBankCol);
    return s;
}

// A second leg of the same road within 24 u of a point (a switchback hairpin): the earthwork's
// governing segment can flip to the other leg there - the arc helpers' stated limit (legs
// closer than twice the band offset), reported apart from a cliff.
bool nearOtherLeg(const WorldForgePlan& box, const CutVertex& v, const glm::vec2& q) {
    if (v.road < 0) return false;
    const auto& cl = box.roads()[static_cast<size_t>(v.road)].centerline;
    for (size_t i = 0; i < cl.size(); ++i) {
        if (i + 3 >= v.index && i <= v.index + 3) continue;   // the vertex's own neighbourhood
        if (glm::length(cl[i] - q) < 24.0f) return true;
    }
    return false;
}

// the repose slope the bank may read as: 0.70 + 0.05 per u for the far anchor's drift between the
// road's 16 u vertices (the bank line is interpolated along the segment, so a rising far natural
// tilts it slightly along the section). The 0.05 is EMPIRICAL, not derived: the worst bank measured
// over 119 Mountains sections after the bisector blend reads 0.75; a bound from the vertex spacing
// and the largest anchor change per segment would be looser (a 10 u change over 16 u at a sharp bend
// is 0.6), so the allowance is the observed drift, stated here so a reader can judge it.
constexpr float kBankSlopeTolerance = 0.05f;
float reposeBound() { return WorldForgePlan::kReposeSlope + kBankSlopeTolerance; }

}  // namespace

// L2 on the real output: at the deepest single-road cuts beyond the bank limit the emitted
// cross-section holds a dry-stone tier at the road edge (wall columns with the wall material, as
// tall as min(cut - 8.4, wallTierU)), a rock face above it where the cut exceeds the tier, and a
// bank at repose above that. Red on M4c: 0 wall columns, banks steeper than repose.
TEST(EarthworkTest, DeepCutsAreWalledNotCliffed) {
    for (const auto& pair : std::vector<std::pair<const char*, WorldGenerator*>>{{"baseline Perlin", &baselineWorld()},
                                                                                {"Mountains 424242", &mountainsWorld()}}) {
        WorldGenerator& g = *pair.second;
        const WorldForgePlan* box = g.worldForge();
        ASSERT_TRUE(box && g.corridors()) << pair.first << ": no corridor-driven box plan";
        const WorldForgeParams P = g.getWorldForgeParams().clamped();
        const float limit = WorldForgePlan::bankLimit();
        const auto cuts = singleRoadCuts(g, *box, limit + 0.5f, 60);
        int sections = 0, cutSides = 0, unwalled = 0, steep = 0, shortBank = 0, shortWall = 0, jumps = 0, hairpinJumps = 0, tallRock = 0;
        float worstSlope = 0.0f, deepest = 0.0f, tallestRock = 0.0f;
        for (const CutVertex& v : cuts) {
            deepest = std::max(deepest, v.cut);
            for (const float side : {-1.0f, 1.0f}) {
                const Section s = sectionAt(g, *box, v, side);
                if (!s.valid) continue;
                ++sections;
                if (s.expectWall) { ++cutSides; if (s.wallCols == 0) ++unwalled; }
                const float expectWall = std::min(v.cut - limit, P.wallTierU);
                if (s.wallCols > 0 && std::fabs(s.wallHeight - expectWall) > 1.0f) ++shortWall;
                tallestRock = std::max(tallestRock, s.rockBand);
                if (s.rockBand > P.rockCutMaxU + 0.5f) ++tallRock;
                if (s.maxStep > 2) {   // a 0.7 bank rounds to steps of 0 or 1 (2 with the anchor's drift); 3+ along the bank is a cliff
                    if (nearOtherLeg(*box, v, s.jumpAt)) ++hairpinJumps;
                    else { if (++jumps <= 4) std::printf("[ M4b-4-diag ] %s cliff step %d at (%.0f,%.0f) side %+.0f cut %.1f: %s\n", pair.first, s.maxStep, v.p.x, v.p.y, side, v.cut, s.dump.c_str()); }
                }
                if (s.bankSpan < 6) { ++shortBank; continue; }   // too short to judge a slope honestly (reported)
                worstSlope = std::max(worstSlope, s.bankSlope);
                if (s.bankSlope > reposeBound() + 1e-3f) {
                    if (++steep <= 6)
                        std::printf("[ M4b-4-diag ] %s steep bank %.2f over %d columns at (%.0f,%.0f) side %+.0f cut %.1f grade %.1f natural %.1f cls %d: %s\n",
                                    pair.first, s.bankSlope, s.bankSpan, v.p.x, v.p.y, side, v.cut, v.grade, v.natural, v.cls, s.dump.c_str());
                }
            }
        }
        std::printf("[ M4b-4 ] %s: %zu single-road cuts beyond %.1f u (deepest %.1f u): %d sections, %d cut sides, %d of them UNWALLED, %d walls off their expected height, %d banks (>= 6 columns) steeper than repose (worst %.2f vs %.2f + %.2f drift), %d banks too short to judge, %d sections with a 3+ cube step along the bank (+%d beside a hairpin's other leg, reported), tallest rock band %.1f u (%d beyond the precedent)\n",
                    pair.first, cuts.size(), limit, deepest, sections, cutSides, unwalled, shortWall, steep, worstSlope, WorldForgePlan::kReposeSlope, kBankSlopeTolerance, shortBank, jumps, hairpinJumps, tallestRock, tallRock);
        if (cuts.empty()) { std::printf("[ M4b-4 ] %s: no cut beyond the bank limit - nothing to wall\n", pair.first); continue; }
        EXPECT_GT(cutSides, 0) << pair.first;
        EXPECT_EQ(unwalled, 0) << pair.first << ": a cut side beyond the bank limit without a retaining wall";
        EXPECT_EQ(shortWall, 0) << pair.first << ": a wall tier not as tall as the cut needs (up to wallTierU)";
        EXPECT_EQ(steep, 0) << pair.first << ": a bank above the wall stands steeper than repose";
        EXPECT_EQ(jumps, 0) << pair.first << ": a step of 3+ cubes along the bank (a cliff where the bank should be)";
        EXPECT_EQ(tallRock, 0) << pair.first << ": an emitted rock face taller than the precedent";
        EXPECT_GT(sections - shortBank, 20) << pair.first << ": too few banks long enough to judge";
    }
}

// Guard (green before and after): the wall and rock bands never intrude on the road itself —
// every column inside the half-width at a walled cut keeps the road class, the road material and
// the graded surface (within a cube of rounding).
TEST(EarthworkTest, WallNeverIntrudesOnTheRoad) {
    WorldGenerator& g = mountainsWorld();
    const WorldForgePlan* box = g.worldForge();
    ASSERT_TRUE(box && g.corridors());
    const auto cuts = singleRoadCuts(g, *box, WorldForgePlan::bankLimit() + 0.5f, 60);
    int cols = 0, bad = 0, offRoad = 0;
    for (const CutVertex& v : cuts) {
        const float half = WorldForgePlan::roadHalfWidth(v.cls);
        for (float d = -half + 0.5f; d < half; d += 1.0f) {
            const glm::vec2 q = v.p + v.n * d;
            const auto col = g.sampleSurface(roundI(q.x), roundI(q.y));
            if (col.riverOrder > 0 || col.bridgeDeckY != INT_MIN) continue;
            // the road's own edge: at a bend a column on the vertex's normal can lie beyond the
            // polyline's half-width (measured 2026-10-28: 2 of 181) - such a column is the
            // shoulder, where a wall belongs; it must still stand OUTSIDE the true edge
            const WorldForgePlan::RoadHit rh = box->roadAt(static_cast<float>(roundI(q.x)), static_cast<float>(roundI(q.y)));   // the column the sampler judged
            if (rh.cls == 0 || rh.dist > half) {
                ++offRoad;
                if (col.wallTopY != INT_MIN && rh.cls > 0 && rh.dist <= half) ++bad;
                continue;
            }
            ++cols;
            if (col.wallTopY != INT_MIN || col.rockTopY != INT_MIN || col.roadClass == 0 ||
                col.surfaceMat != WorldForgePlan::roadMaterial(v.cls) || std::fabs(static_cast<float>(col.surfaceY) - v.grade) > 1.5f) {
                ++bad;
                std::printf("[ M4b-4-diag ] intruded road column at (%.0f,%.0f) d %+.1f of half %.1f: class %d mat %s surface %d grade %.1f wallTop %d rockTop %d roadDist %.2f\n",
                            q.x, q.y, d, half, col.roadClass, col.surfaceMat.c_str(), col.surfaceY, v.grade, col.wallTopY, col.rockTopY, col.roadDist);
            }
        }
    }
    std::printf("[ M4b-4 ] Mountains: %d road columns at %zu walled cuts (%d samples beyond the polyline's own edge at bends), %d intruded or off-grade\n", cols, cuts.size(), offRoad, bad);
    EXPECT_EQ(bad, 0);
}

// D33 (b): no realized road cuts deeper than the bank limit + the wall tier + the rock-cut
// precedent; a demand that needs such a cut is re-routed or refused with the reason. Red on
// M4c: Mountains cuts 80 u on realized roads.
TEST(EarthworkTest, CutsBeyondThePrecedentAreRefused) {
    for (const auto& pair : std::vector<std::pair<const char*, WorldGenerator*>>{{"baseline Perlin", &baselineWorld()},
                                                                                {"Mountains 424242", &mountainsWorld()}}) {
        WorldGenerator& g = *pair.second;
        const WorldForgePlan* box = g.worldForge();
        ASSERT_TRUE(box && g.corridors());
        const WorldForgeParams P = g.getWorldForgeParams().clamped();
        const float bound = WorldForgePlan::bankLimit() + P.wallTierU + P.rockCutMaxU;
        int vertices = 0, beyond = 0; float deepest = 0.0f, deepestFace = 0.0f;
        float walledLen = 0.0f, rockLen = 0.0f;
        for (const auto& road : box->roads()) {
            if (road.gradeProfile.size() != road.naturalY.size()) continue;
            for (size_t i = 0; i < road.gradeProfile.size(); ++i) {
                ++vertices;
                const float cut = road.naturalY[i] - road.gradeProfile[i];
                deepest = std::max(deepest, cut);
                if (cut > bound + 0.5f) ++beyond;
            }
            walledLen += road.walledLengthU; rockLen += road.rockLengthU;
            EXPECT_LE(road.deepestCutU, bound + 0.5f) << pair.first << ": a road's deepest FACE on the final profile exceeds the bound (road " << road.a << " -> " << road.b << ")";
            EXPECT_LE(road.rockLengthU, road.sectionLengthU + 1e-3f) << pair.first << ": rock counted outside an earthwork section";
            if (road.naturalSideY.size() == road.gradeProfile.size())
                for (size_t i = 0; i < road.gradeProfile.size(); ++i) {
                    const float face = std::max(road.naturalY[i], std::max(road.naturalSideY[i].x, road.naturalSideY[i].y)) - road.gradeProfile[i];
                    deepestFace = std::max(deepestFace, face);
                }
        }
        int cutRefusals = 0;
        for (const auto& rd : box->refusedDemands())
            // the D33 reason, not a crossing's "cut beyond the bank limit"
            if (rd.reason.rfind("a walkable line needs a cut", 0) == 0) { ++cutRefusals; std::printf("[ M4b-4 ]   %s refused %d -> %d class %d at (%.0f,%.0f): %s\n", pair.first, rd.a, rd.b, rd.cls, rd.pos.x, rd.pos.y, rd.reason.c_str()); }
        std::printf("[ M4b-4 ] %s: %d vertices, deepest centreline cut %.1f u, deepest FACE %.1f u, %d beyond the bound %.1f u (bank %.1f + tier %.0f + precedent %.0f); walled %.0f u, rock-faced %.0f u of road; %d demands refused for a cut, %zu refused in all\n",
                    pair.first, vertices, deepest, deepestFace, beyond, bound, WorldForgePlan::bankLimit(), P.wallTierU, P.rockCutMaxU, walledLen, rockLen, cutRefusals, box->refusedDemands().size());
        EXPECT_EQ(beyond, 0) << pair.first << ": a realized road cuts deeper than the wall tier + the rock-cut precedent";
        EXPECT_LE(deepestFace, bound + 0.5f) << pair.first << ": the deepest FACE on the final profile exceeds the bound";
    }
}

// The wall tier as a KNOB with its control (the red of the mechanism on the CURRENT test): wallTierU 0
// on the Mountains world switches the earthwork section off - every cut side beyond the bank limit
// is then unwalled and its bank is the plain widened shoulder (a cliff beyond 8.4 u); the default
// world (the control) walls them all. One variable moves: wallTierU.
TEST(EarthworkTest, WallTierKnobWithItsControl) {
    WorldGenerator& g = mountainsWorld();
    const WorldForgePlan* box = g.worldForge();
    ASSERT_TRUE(box && g.corridors());
    WorldRecipe r = civRecipe(424242u, "Mountains");
    r.worldforge.wallTierU = 0.0f;
    WorldGenerator h(WorldGenerator::GenerationType::Mountains, 424242u);
    h.applyRecipe(r);
    const WorldForgePlan* off = h.worldForge();
    ASSERT_TRUE(off && h.corridors());
    EXPECT_FLOAT_EQ(off->params().wallTierU, 0.0f) << "the knob did not reach the plan";
    auto measure = [&](WorldGenerator& w, const WorldForgePlan& plan, const char* tag, int& cutSides, int& walled, int& steepOrJump) {
        cutSides = walled = steepOrJump = 0;
        const auto cuts = singleRoadCuts(w, plan, WorldForgePlan::bankLimit() + 0.5f, 40);
        for (const CutVertex& v : cuts)
            for (const float side : {-1.0f, 1.0f}) {
                const Section s = sectionAt(w, plan, v, side);
                if (!s.valid || !s.expectWall) continue;
                ++cutSides;
                if (s.wallCols > 0) ++walled;
                if ((s.maxStep > 2 && !nearOtherLeg(plan, v, s.jumpAt)) || (s.bankSpan >= 6 && s.bankSlope > reposeBound() + 1e-3f)) ++steepOrJump;
            }
        std::printf("[ M4b-4 ] %s: %d cut sides, %d walled, %d with a cliff or a bank steeper than repose\n", tag, cutSides, walled, steepOrJump);
    };
    int cs0, w0, s0, cs1, w1, s1;
    measure(g, *box, "Mountains wallTierU 3 (control)", cs0, w0, s0);
    measure(h, *off, "Mountains wallTierU 0", cs1, w1, s1);
    ASSERT_GT(cs0, 0); ASSERT_GT(cs1, 0);
    EXPECT_EQ(w0, cs0) << "control: the default walls every cut side";
    EXPECT_EQ(w1, 0) << "wallTierU 0 still walls";
    EXPECT_GT(s1, 0) << "with the section off, no cut side shows the cliff the section replaces - the measure cannot see it";
    EXPECT_EQ(s0, 0);
}

// A steep hillside beside a SHALLOW cut (the centreline cut within the bank limit, the natural at
// the reach more than the bank limit above the grade): the section is gated on the face, so the
// bank is anchored at the reach there too - no wall (none is needed at the toe), no cliff, the bank
// at repose. Audit 2026-10-28: the first gate (the centreline cut) left this case to the plain
// shoulder, capped at 15 u, standing steeper than repose.
TEST(EarthworkTest, SteepHillsidesBesideShallowCutsAreBanked) {
    WorldGenerator& g = mountainsWorld();
    const WorldForgePlan* box = g.worldForge();
    ASSERT_TRUE(box && g.corridors());
    const float limit = WorldForgePlan::bankLimit();
    std::vector<std::pair<CutVertex, float>> cases;   // the vertex and the side (+1 left / -1 right) whose far natural is high
    for (const auto& road : box->roads()) {
        const auto& cl = road.centerline; const auto& pr = road.gradeProfile; const auto& nat = road.naturalY; const auto& far = road.naturalSideY;
        if (cl.size() < 3 || pr.size() != cl.size() || nat.size() != cl.size() || far.size() != cl.size()) continue;
        for (size_t i = 1; i + 1 < cl.size(); ++i) {
            const float cut = nat[i] - pr[i];
            if (cut > limit || cut < 0.0f) continue;
            const glm::vec2 d = glm::normalize(cl[i + 1] - cl[i - 1]);
            if (!std::isfinite(d.x)) continue;
            const CutVertex v{cl[i], glm::vec2(-d.y, d.x), cut, pr[i], nat[i], road.cls};
            if (far[i].x - pr[i] > limit + 4.0f) cases.push_back({v, 1.0f});
            if (far[i].y - pr[i] > limit + 4.0f) cases.push_back({v, -1.0f});
        }
    }
    std::sort(cases.begin(), cases.end(), [](const auto& a, const auto& b) { return a.first.natural < b.first.natural; });
    int measured = 0, steep = 0, jumps = 0, walled = 0, bundled = 0;
    float worst = 0.0f;
    for (const auto& c : cases) {
        if (measured >= 40) break;
        std::vector<int> roads;
        box->gradeBlendAt(c.first.p.x, c.first.p.y, c.first.natural, &roads);
        if (roads.size() != 1) { ++bundled; continue; }
        const Section s = sectionAt(g, *box, c.first, c.second);
        if (!s.valid) continue;
        ++measured;
        if (s.wallCols > 0) ++walled;
        if (s.maxStep > 2) { ++jumps; if (jumps <= 4) std::printf("[ M4b-4-diag ] hillside cliff step %d at (%.0f,%.0f) side %+.0f cut %.1f: %s\n", s.maxStep, c.first.p.x, c.first.p.y, c.second, c.first.cut, s.dump.c_str()); }
        if (s.bankSpan >= 6) { worst = std::max(worst, s.bankSlope); if (s.bankSlope > reposeBound() + 1e-3f) { if (++steep <= 4) std::printf("[ M4b-4-diag ] hillside steep %.2f at (%.0f,%.0f) side %+.0f cut %.1f: %s\n", s.bankSlope, c.first.p.x, c.first.p.y, c.second, c.first.cut, s.dump.c_str()); } }
    }
    std::printf("[ M4b-4 ] Mountains: %zu shallow-cut vertices with a far natural > %.1f u above the grade, %d single-road sections measured (%d bundled skipped): %d walled, %d cliffs, %d banks steeper than repose (worst %.2f)\n",
                cases.size(), limit + 4.0f, measured, bundled, walled, jumps, steep, worst);
    ASSERT_GT(measured, 10) << "no steep hillside beside a shallow cut on the Mountains world";
    EXPECT_EQ(jumps, 0) << "a cliff beside a shallow cut on a steep hillside";
    EXPECT_EQ(steep, 0) << "a hillside bank steeper than repose";
}

// The EMITTED voxels (generateChunk through ChunkMicroWorld): at walled sections the wall columns
// are Cobblestone from the grade up to the wall top with the top course a 2/3 subcube coping whose
// road-side subcube column is empty; the band is ceil(0.67 h) columns thick; the rock column's
// face is Stone. And the chunking contract: the chunk's top solid at every section column equals
// the column sampler's surface (a coping column tops at surfaceY + 2/3).
TEST(EarthworkTest, WallVoxelsAreCoursedCobbleWithACopingAndRockIsStone) {
    WorldGenerator& g = mountainsWorld();
    const WorldForgePlan* box = g.worldForge();
    ASSERT_TRUE(box && g.corridors());
    ChunkMicroWorld world(g);
    const auto cuts = singleRoadCuts(g, *box, WorldForgePlan::bankLimit() + 0.5f, 12);
    int wallCols = 0, badMat = 0, badCoping = 0, rockCols = 0, badRock = 0, thickOk = 0, thickOff = 0, cols = 0, seamDiff = 0;
    for (const CutVertex& v : cuts)
        for (const float side : {-1.0f, 1.0f}) {
            const float half = WorldForgePlan::roadHalfWidth(v.cls);
            int band = 0; float wallH = 0.0f;
            for (float d = half + 0.5f; d <= half + 17.0f; d += 1.0f) {
                const glm::vec2 q = v.p + v.n * (side * d);
                const int x = roundI(q.x), z = roundI(q.y);
                const auto col = g.sampleSurface(x, z);
                if (col.riverOrder > 0 || col.bridgeDeckY != INT_MIN) break;
                ++cols;
                Chunk& c = world.chunkAt(floorDiv32(x), floorDiv32(col.surfaceY), floorDiv32(z));
                auto cubeAt = [&](int wy) -> const Cube* {
                    Chunk& cc = world.chunkAt(floorDiv32(x), floorDiv32(wy), floorDiv32(z));
                    return cc.getCubeAt(glm::ivec3(floorModN(x, 32), floorModN(wy, 32), floorModN(z, 32)));
                };
                (void)c;
                // the chunking contract: the top solid micro-cell vs the sampler's surface
                int top = INT_MIN;
                for (int my = (col.surfaceY + 2) * 9; my >= (col.surfaceY - 2) * 9; --my)
                    if (world.solidMicro(x * 9 + 4, my, z * 9 + 4)) { top = my + 1; break; }
                const int expectTop = col.wallTopY != INT_MIN ? col.surfaceY * 9 + 6 : (col.surfaceY + 1) * 9;
                if (top != expectTop && !(col.wallTopY != INT_MIN && top == col.surfaceY * 9)) ++seamDiff;   // the coping's road-side column may be the probed centre: empty there
                if (col.wallTopY != INT_MIN) {
                    ++wallCols; ++band; wallH = std::max(wallH, static_cast<float>(col.wallTopY - col.wallBaseY));
                    for (int wy = col.wallBaseY + 1; wy < col.wallTopY; ++wy) {
                        const Cube* cb = cubeAt(wy);
                        if (!cb || cb->getMaterialName() != "Cobblestone") { ++badMat; break; }
                    }
                    // the coping: a subcube shelf at wallTopY, the road-side column (wallFace) empty, the far side present
                    Chunk& ct = world.chunkAt(floorDiv32(x), floorDiv32(col.wallTopY), floorDiv32(z));
                    const glm::ivec3 lp(floorModN(x, 32), floorModN(col.wallTopY, 32), floorModN(z, 32));
                    const glm::ivec3 roadSide = col.wallFace == 0 ? glm::ivec3(0, 0, 1) : col.wallFace == 1 ? glm::ivec3(2, 0, 1) : col.wallFace == 2 ? glm::ivec3(1, 0, 0) : glm::ivec3(1, 0, 2);
                    const glm::ivec3 farSide = col.wallFace == 0 ? glm::ivec3(2, 0, 1) : col.wallFace == 1 ? glm::ivec3(0, 0, 1) : col.wallFace == 2 ? glm::ivec3(1, 0, 2) : glm::ivec3(1, 0, 0);
                    if (ct.getCubeAt(lp) || ct.hasSubcubeAt(lp, roadSide) || !ct.hasSubcubeAt(lp, farSide)) ++badCoping;
                } else if (band > 0 && wallH > 0.0f) {
                    const int expectThick = static_cast<int>(std::ceil(0.67f * wallH));
                    if (std::abs(band - expectThick) <= 1) ++thickOk; else ++thickOff;   // +-1: the band's edge is not on the column lattice
                    band = 0; wallH = 0.0f;
                }
                if (col.rockTopY != INT_MIN) {
                    ++rockCols;
                    const Cube* cb = cubeAt(col.rockTopY);
                    if (!cb || cb->getMaterialName() != "Stone") ++badRock;
                }
            }
        }
    std::printf("[ M4b-4 ] Mountains voxels: %d section columns, %d wall columns (%d with a non-cobble course, %d with a wrong coping), %d bands at the expected thickness (%d off), %d rock columns (%d not Stone at the face top), %d columns whose chunk top differs from the sampler\n",
                cols, wallCols, badMat, badCoping, thickOk, thickOff, rockCols, badRock, seamDiff);
    EXPECT_GT(wallCols, 10);
    EXPECT_EQ(badMat, 0) << "a wall course that is not Cobblestone";
    EXPECT_EQ(badCoping, 0) << "a wall top without the set-back subcube coping";
    EXPECT_EQ(thickOff, 0) << "a wall band not ceil(0.67 h) columns thick";
    EXPECT_GT(rockCols, 0);
    EXPECT_EQ(badRock, 0) << "a rock face whose top voxel is not Stone";
    EXPECT_EQ(seamDiff, 0) << "the chunk's emitted top differs from the column sampler (chunking changed the answer)";
}

// The plan's earthwork census against the EMITTED terrain (audit 2026-10-28: the census was only
// checked against itself): on the Mountains road with the most walled length, walk the centreline
// at 1 u and look at the wall band and the rock column on both sides - the steps that carry an
// emitted wall must match walled_u, the steps that carry an emitted rock face must match rock_u,
// and the steps where the section
// function (gradeBlendAt) yields an earthwork section at any band column - a bank-only section is
// plain terrain, so it can only be seen through the function - must match section_u.
// Walled within 10 %; rock within 25 % (the census counts rock where the face top stands above the
// wall, the generator emits it only where the ground at the first column past the wall stands at
// the face top - a dip there is natural ground, not rock); sections within 10 % (prediction
// written 2026-10-29 before the run: x1.0 +- 0.1, the walls' figure, since the walk and the census
// sample the same function at 1 u and at the vertices). The three checks are consistency checks
// between the vertex census and a 1 u walk of the emitted columns (audit 3 2026-10-29) - and a
// drift smaller than a bound passes them; the mutations show a rule swap does not.
TEST(EarthworkTest, CensusMatchesTheEmittedTerrain) {
    WorldGenerator& g = mountainsWorld();
    const WorldForgePlan* box = g.worldForge();
    ASSERT_TRUE(box && g.corridors());
    const WorldForgeRoad* best = nullptr;
    for (const auto& road : box->roads()) if (!best || road.walledLengthU > best->walledLengthU) best = &road;
    ASSERT_TRUE(best && best->walledLengthU > 100.0f) << "no walled road on Mountains";
    const auto& cl = best->centerline;
    const std::vector<float> cum = WorldForgePlan::cumulativeArc(cl);
    const float half = WorldForgePlan::roadHalfWidth(best->cls);
    float walledSteps = 0.0f, rockSteps = 0.0f, wallOrRockSteps = 0.0f, sectionSteps = 0.0f; int steps = 0;
    for (float arc = 0.5f; arc < cum.back(); arc += 1.0f) {
        const glm::vec2 p = WorldForgePlan::pointAtArc(cl, cum, arc);
        const glm::vec2 q = WorldForgePlan::pointAtArc(cl, cum, std::min(arc + 1.0f, cum.back()));
        const glm::vec2 d = q - p;
        const float len = glm::length(d);
        if (len < 1e-4f) continue;
        const glm::vec2 n(-d.y / len, d.x / len);
        ++steps;
        bool wall = false, rock = false, wallOrRock = false, section = false;
        for (const float side : {-1.0f, 1.0f})
            for (float off = half + 0.5f; off <= half + 5.5f; off += 1.0f) {
                const glm::vec2 c = p + n * (side * off);
                const auto col = g.sampleSurface(roundI(c.x), roundI(c.y));
                if (col.wallTopY != INT_MIN) { wall = true; wallOrRock = true; }
                if (col.rockTopY != INT_MIN) { rock = true; wallOrRock = true; }
                if (!section) {
                    const auto gb = box->gradeBlendAt(c.x, c.y, naturalAt(g, c));
                    if (gb.surfaceY > -1e29f) section = true;   // a wall, a rock face or a bank-only section
                }
            }
        if (wall) walledSteps += 1.0f;
        if (rock) rockSteps += 1.0f;
        if (wallOrRock) wallOrRockSteps += 1.0f;
        if (section) sectionSteps += 1.0f;
    }
    const float walledRatio = walledSteps / std::max(1.0f, best->walledLengthU), rockRatio = rockSteps / std::max(1.0f, best->rockLengthU);
    const float sectionRatio = sectionSteps / std::max(1.0f, best->sectionLengthU);
    std::printf("[ M4b-4 ] Mountains road %d -> %d: %d steps; emitted walls on %.0f steps vs walled_u %.0f (x%.2f), emitted rock on %.0f steps vs rock_u %.0f (x%.2f), a wall or a rock face on %.0f steps; earthwork sections (gradeBlendAt at the band, banks included) on %.0f steps vs section_u %.0f (x%.2f)\n",
                best->a, best->b, steps, walledSteps, best->walledLengthU, walledRatio, rockSteps, best->rockLengthU, rockRatio, wallOrRockSteps, sectionSteps, best->sectionLengthU, sectionRatio);
    EXPECT_NEAR(walledRatio, 1.0f, 0.10f) << "walled_u does not match the emitted walls";
    EXPECT_NEAR(rockRatio, 1.0f, 0.25f) << "rock_u does not match the emitted rock faces";
    EXPECT_NEAR(sectionRatio, 1.0f, 0.10f) << "section_u does not match the sections the section function yields along the road";
}

// The chunk-independence contract (FeatureDesignKeys): the earthwork fields are a pure function of
// world position - a second generator of the same recipe samples identical columns across the
// walled band of the deepest baseline cuts (the GeneratorCopyStampsIdenticalRoadColumns shape).
TEST(EarthworkTest, EarthworkFieldsIdenticalAcrossGenerators) {
    WorldGenerator& g = baselineWorld();
    const WorldForgePlan* box = g.worldForge();
    ASSERT_TRUE(box && g.corridors());
    WorldGenerator h(WorldGenerator::GenerationType::Perlin, 20260816u);
    h.applyRecipe(civRecipe(20260816u));
    ASSERT_TRUE(h.worldForge());
    std::vector<CutVertex> cuts;   // the deepest cuts, bundled or not (the baseline's 11 u cuts are all on shared trunks)
    for (const auto& road : box->roads()) {
        const auto& cl = road.centerline; const auto& pr = road.gradeProfile; const auto& nat = road.naturalY;
        if (cl.size() < 3 || pr.size() != cl.size() || nat.size() != cl.size()) continue;
        for (size_t i = 1; i + 1 < cl.size(); ++i) {
            const glm::vec2 dd = glm::normalize(cl[i + 1] - cl[i - 1]);
            if (std::isfinite(dd.x)) cuts.push_back({cl[i], glm::vec2(-dd.y, dd.x), nat[i] - pr[i], pr[i], nat[i], road.cls});
        }
    }
    std::sort(cuts.begin(), cuts.end(), [](const CutVertex& a, const CutVertex& b) { return a.cut > b.cut; });
    if (cuts.size() > 40) cuts.resize(40);
    int cols = 0, diff = 0, walls = 0;
    for (const CutVertex& v : cuts)
        for (const float side : {-1.0f, 1.0f})
            for (float d = 0.5f; d <= 18.0f; d += 1.0f) {
                const glm::vec2 q = v.p + v.n * (side * d);
                const auto a = g.sampleSurface(roundI(q.x), roundI(q.y)), b = h.sampleSurface(roundI(q.x), roundI(q.y));
                ++cols;
                if (a.wallTopY != INT_MIN) ++walls;
                if (a.surfaceY != b.surfaceY || a.surfaceMat != b.surfaceMat || a.wallTopY != b.wallTopY || a.wallBaseY != b.wallBaseY ||
                    a.rockTopY != b.rockTopY || a.rockBaseY != b.rockBaseY || a.wallFace != b.wallFace || a.roadClass != b.roadClass) ++diff;
            }
    std::printf("[ M4b-4 ] baseline: %d columns across %zu cut sections compared between two generators, %d wall columns, %d differ\n", cols, cuts.size(), walls, diff);
    EXPECT_GT(cols, 100);
    EXPECT_GT(walls, 0) << "no wall column in the deepest baseline cuts - the seam claim is vacuous";
    EXPECT_EQ(diff, 0) << "the earthwork is not a pure function of world position";
}
