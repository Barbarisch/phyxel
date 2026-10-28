#include <gtest/gtest.h>

#include "ForgeTestUtil.h"
#include "core/FlowField.h"
#include "core/TraversalProbe.h"
#include "core/WorldForgeParams.h"
#include "core/WorldForgePlan.h"
#include "core/WorldGenerator.h"
#include "core/WorldRecipe.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace Phyxel;
using namespace Phyxel::ForgeTest;

// ============================================================================
// BridgeSpanTest — M4c (docs/WorldForgeV2Plan.md M4c, D30–D32): crossings as routing edges on
// the corridor-driven box plan. A carved river is crossed by a BRIDGE only within the grounded
// span (D31: single timber span <= 6 u, piered <= 25 u overall, piers every ~10 u), its approach
// earthwork is bounded by the bank limit (8.4 u, the deepest fill the widest repose shoulder
// carries; the deck meets the LOWER bank and clears the channel's rim), a creek is a FORD
// (D32: channel depth < 0.5 u, the M2b-2 class split), and anything wider or steeper is
// uncrossable for the route search — the road crosses elsewhere or the demand is refused with
// its reason. Never a half-road into the water, never a dam.
//
// Red on the M4b code (measured 2026-10-27, the red run archived in m4c_red_green.txt): decks up
// to 96 u, the approach fill unbounded (the deck sat at the HIGHER bank), carved crossings decided
// after routing.
// ============================================================================
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

// The version-2 worlds bake once per process (1–3 min Debug each): the baseline Perlin civ world
// (the CivLab recipe) and the Mountains civ world (the M4b red: 177 u of approach fill).
WorldGenerator& baselineWorld() {
    static std::unique_ptr<WorldGenerator> g = [] {
        auto w = std::make_unique<WorldGenerator>(WorldGenerator::GenerationType::Perlin, 20260816u);
        w->applyRecipe(civRecipe(20260816u));
        return w;
    }();
    return *g;
}
WorldGenerator& mountainsWorld() {
    static std::unique_ptr<WorldGenerator> g = [] {
        auto w = std::make_unique<WorldGenerator>(WorldGenerator::GenerationType::Mountains, 424242u);
        w->applyRecipe(civRecipe(424242u, "Mountains"));
        return w;
    }();
    return *g;
}

// D31 / D32 as measured constants (GROUNDED, section 10 of the plan): the longest piered medieval
// bridge overall, and the river-vs-creek class depth. The red version held them as literals; the
// green reads them from the shipped defaults and pins the defaults here so a silent default change
// is caught (the literals are the sources' numbers, not the params').
constexpr float kSpanMaxU = 25.0f;
constexpr float kFordMaxDepthU = 0.5f;
constexpr float kDeckShoulderU = 2.0f;   // the span's 2 u shoulder onto each bank (WorldForgePlan)

// The first version-2 world with a bridge (the baseline's one carved crossing may be refused
// under the rule - its bed stands above the banks - in which case Mountains, with 24, serves).
WorldGenerator* worldWithBridge(const char*& tag) {
    for (const auto& pair : std::vector<std::pair<const char*, WorldGenerator*>>{{"baseline Perlin", &baselineWorld()},
                                                                                {"Mountains 424242", &mountainsWorld()}}) {
        const WorldForgePlan* box = pair.second->worldForge();
        if (box && !box->bridges().empty()) { tag = pair.first; return pair.second; }
    }
    tag = "none";
    return nullptr;
}

bool kindsDecided(const WorldForgePlan& box, int& fords, int& bridges, int& other) {
    fords = bridges = other = 0;
    for (const auto& road : box.roads())
        for (const auto& c : road.crossings) {
            if (c.kind == WorldForgeRoad::Crossing::Kind::Ford) ++fords;
            else if (c.kind == WorldForgeRoad::Crossing::Kind::Bridge) ++bridges;
            else ++other;
        }
    return other == 0;
}

struct CrossingCensus {
    int crossings = 0, carved = 0, creeks = 0;
    int undecked = 0;        // carved crossings with no bridge span within 32 u (a half-road)
    int wide = 0;            // carved crossings whose channel is wider than the grounded span
    int deepFords = 0;       // creek crossings deeper than the ford bound
    int bridges = 0, longSpans = 0;
    int shoulderHits = 0;    // channel columns under a deck's 2 u shoulders (the abutments must be natural ground)
    float longestSpanU = 0.0f, widestChannelU = 0.0f;
};

CrossingCensus crossingCensus(const char* tag, WorldGenerator& g, const WorldForgePlan& box) {
    CrossingCensus c;
    c.bridges = static_cast<int>(box.bridges().size());
    for (const auto& b : box.bridges()) {
        const float span = glm::length(b.b - b.a);
        c.longestSpanU = std::max(c.longestSpanU, span);
        const glm::vec2 dir = (b.b - b.a) / std::max(1e-3f, span);
        for (float s = 0.5f; s <= span; s += 1.0f) {   // a HALF-UNIT lattice: not the rule's own sample points
            if (s > kDeckShoulderU && s < span - kDeckShoulderU) continue;
            const glm::vec2 p = b.a + dir * s;
            if (g.channelHitAt(p.x, p.y).hit) ++c.shoulderHits;
        }
        if (span > kSpanMaxU + 0.5f) {   // overall, shoulders included (audit 2026-10-27)
            ++c.longSpans;
            std::printf("[ M4c ] %s: deck of %.1f u (class %d, order %d) at (%.0f,%.0f)-(%.0f,%.0f) deckY %.0f\n",
                        tag, span, b.cls, b.crossingOrder, b.a.x, b.a.y, b.b.x, b.b.y, b.deckY);
        }
    }
    for (const auto& road : box.roads())
        for (const auto& cr : road.crossings) {
            ++c.crossings;
            const FlowField::ChannelHit ch = g.channelHitAt(cr.pos.x, cr.pos.y);
            const float width = 2.0f * (ch.hit ? ch.halfWidth : g.riverNetwork()->halfWidthAt(cr.pos.x, cr.pos.y));
            const float depth = ch.hit ? ch.fullDepth : g.riverNetwork()->fullDepthAt(cr.pos.x, cr.pos.y);
            if (cr.carved) {
                ++c.carved;
                c.widestChannelU = std::max(c.widestChannelU, width);
                if (width > kSpanMaxU + 0.5f) ++c.wide;
                bool decked = false;
                for (const auto& b : box.bridges()) {
                    const glm::vec2 ab = b.b - b.a;
                    const float t = glm::clamp(glm::dot(cr.pos - b.a, ab) / std::max(1e-3f, glm::dot(ab, ab)), 0.0f, 1.0f);
                    if (glm::length(cr.pos - (b.a + ab * t)) <= 32.0f) { decked = true; break; }
                }
                if (!decked) {
                    ++c.undecked;
                    std::printf("[ M4c ] %s: carved order-%d crossing at (%.0f,%.0f) width %.1f u depth %.2f u has NO deck (road %d->%d class %d)\n",
                                tag, cr.riverOrder, cr.pos.x, cr.pos.y, width, depth, road.a, road.b, road.cls);
                }
            } else {
                ++c.creeks;
                if (depth >= kFordMaxDepthU) ++c.deepFords;
            }
        }
    std::printf("[ M4c ] %s: %d crossings (%d carved, %d creeks), %d bridges, longest deck %.1f u overall (%d beyond the grounded %g u), widest carved channel crossed %.1f u (%d wider than the span), %d carved crossings without a deck, %d creek crossings deeper than %g u, %d channel columns under a deck's shoulders\n",
                tag, c.crossings, c.carved, c.creeks, c.bridges, c.longestSpanU, c.longSpans, kSpanMaxU, c.widestChannelU, c.wide, c.undecked, c.deepFords, kFordMaxDepthU, c.shoulderHits);
    return c;
}

struct FillCensus {
    int vertices = 0, dryFills = 0, overLimit = 0;
    float maxDryFill = 0.0f, maxWetFill = 0.0f;   // dry = a bank/approach vertex; wet = over the carved bed (the deck)
    glm::vec2 worstAt{0.0f};
    int worstCls = 0;
    // the EMITTED terrain at the dry vertices within reach of a bridge (sampleSurface - the real
    // output, not the plan's profile): cut = natural - emitted, fill = emitted - natural
    int nearBridge = 0, emittedOver = 0;
    float maxEmittedCut = 0.0f, maxEmittedFill = 0.0f;
    float maxEmittedCutNear = 0.0f;   // within 16 u (one vertex spacing) of the span: the deck's own pins
};

FillCensus fillCensus(const char* tag, WorldGenerator& g, const WorldForgePlan& box) {
    FillCensus f;
    for (const auto& road : box.roads()) {
        const auto& cl = road.centerline;
        if (cl.size() < 2 || road.gradeProfile.size() != cl.size() || road.naturalY.size() != cl.size()) continue;
        const float limitU = WorldForgePlan::bankLimit();
        for (size_t i = 0; i < cl.size(); ++i) {
            ++f.vertices;
            const float fill = road.gradeProfile[i] - road.naturalY[i];
            if (fill <= 0.1f) continue;
            const FlowField::ChannelHit ch = g.channelHitAt(cl[i].x, cl[i].y);
            const bool wet = ch.hit && ch.depth >= 0.15f;
            if (wet) { f.maxWetFill = std::max(f.maxWetFill, fill); continue; }
            ++f.dryFills;
            if (fill > f.maxDryFill) { f.maxDryFill = fill; f.worstAt = cl[i]; f.worstCls = road.cls; }
            if (fill > limitU + 0.5f) ++f.overLimit;
        }
        for (size_t i = 0; i < cl.size(); ++i) {   // the emitted terrain where a bridge's pins reach
            float dist = 1e30f;
            for (const auto& b : box.bridges()) {
                const glm::vec2 ab = b.b - b.a;
                const float t = glm::clamp(glm::dot(cl[i] - b.a, ab) / std::max(1e-3f, glm::dot(ab, ab)), 0.0f, 1.0f);
                dist = std::min(dist, glm::length(cl[i] - (b.a + ab * t)));
            }
            if (dist > 64.0f) continue;
            const FlowField::ChannelHit ch = g.channelHitAt(cl[i].x, cl[i].y);
            if (ch.hit) continue;   // the deck, not the bank
            ++f.nearBridge;
            const float emitted = static_cast<float>(g.sampleSurface(static_cast<int>(std::lround(cl[i].x)), static_cast<int>(std::lround(cl[i].y))).surfaceY);
            const float cut = road.naturalY[i] - emitted, fillE = emitted - road.naturalY[i];
            f.maxEmittedCut = std::max(f.maxEmittedCut, cut);
            if (dist <= 16.0f) f.maxEmittedCutNear = std::max(f.maxEmittedCutNear, cut);
            f.maxEmittedFill = std::max(f.maxEmittedFill, fillE);
            if (fillE > limitU + 1.0f) ++f.emittedOver;   // +1: the surface is emitted on whole cubes
        }
    }
    std::printf("[ M4c ] %s: emitted terrain at %d dry vertices within 64 u of a bridge: max cut %.1f u (%.1f u within 16 u), max fill %.1f u, %d fills above the bank limit + 1\n",
                tag, f.nearBridge, f.maxEmittedCut, f.maxEmittedCutNear, f.maxEmittedFill, f.emittedOver);
    std::printf("[ M4c ] %s: %d vertices, %d dry fills, %d above the bank limit (%.1f u = kShoulderMaxU x repose; class floors %.2f/%.2f/%.2f u); deepest dry fill %.1f u at (%.0f,%.0f) class %d; deepest over-bed deck height %.1f u\n",
                tag, f.vertices, f.dryFills, f.overLimit, WorldForgePlan::bankLimit(), WorldForgePlan::cutLimit(1), WorldForgePlan::cutLimit(2), WorldForgePlan::cutLimit(3),
                f.maxDryFill, f.worstAt.x, f.worstAt.y, f.worstCls, f.maxWetFill);
    return f;
}

}  // namespace

// D31: no deck is longer than the grounded span (25 u overall + the 2 u shoulders). Red on M4b:
// the span builder marched up to 96 u either way.
TEST(BridgeSpanTest, NoDeckBeyondTheGroundedSpan) {
    for (const auto& pair : std::vector<std::pair<const char*, WorldGenerator*>>{{"baseline Perlin", &baselineWorld()},
                                                                                {"Mountains 424242", &mountainsWorld()}}) {
        WorldGenerator& g = *pair.second;
        const WorldForgePlan* box = g.worldForge();
        ASSERT_TRUE(box && g.corridors()) << pair.first << ": no corridor-driven box plan";
        const WorldForgeParams P = g.getWorldForgeParams().clamped();
        EXPECT_FLOAT_EQ(P.bridgeMaxSpanU, kSpanMaxU) << "the shipped span default moved off the grounded figure";
        EXPECT_FLOAT_EQ(P.fordMaxDepthU, kFordMaxDepthU) << "the shipped ford bound moved off the class split";
        EXPECT_FLOAT_EQ(P.fordMaxDepthU, FlowField::kRiverCarveDepth) << "the ford bound is the river/creek class split (D32)";
        const CrossingCensus c = crossingCensus(pair.first, g, *box);
        EXPECT_EQ(c.longSpans, 0) << pair.first << ": a deck longer than the grounded span";
        EXPECT_EQ(c.shoulderHits, 0) << pair.first << ": a deck's shoulder stands on a channel column (a gap inside the channel mask was taken for a bank)";
        for (const auto& b : box->bridges()) {
            EXPECT_FLOAT_EQ(b.pierSpacingU, P.pierSpacingU) << pair.first << ": a corridor-driven span without the pier rule";
            EXPECT_FLOAT_EQ(b.singleSpanU, P.bridgeSingleSpanU);
        }
        std::printf("[ M4c ] %s: %zu refused demands\n", pair.first, box->refusedDemands().size());
        for (const auto& r : box->refusedDemands())
            std::printf("[ M4c ]   demand %d -> %d class %d at (%.0f,%.0f): %s\n", r.a, r.b, r.cls, r.pos.x, r.pos.y, r.reason.c_str());
    }
}

// The approach earthwork is bounded: at every dry centreline vertex the fill (profile above the
// natural surface) stays within the bank limit (8.4 u: the deepest fill the widest repose shoulder
// carries, the same bank physics as a cutting) — the deck meets the LOWER bank and the higher bank
// is cut to it, never the lower bank filled up to the higher. Red on M4b: 44 vertices above the
// bound on Mountains, 30 u deepest (the deck sat at the higher bank; the pin cone filled a dam).
// The first green bounded fills at the class floor (3.15-4.2 u) and bridged nothing: see the plan.
TEST(BridgeSpanTest, ApproachFillIsBounded) {
    for (const auto& pair : std::vector<std::pair<const char*, WorldGenerator*>>{{"baseline Perlin", &baselineWorld()},
                                                                                {"Mountains 424242", &mountainsWorld()}}) {
        WorldGenerator& g = *pair.second;
        const WorldForgePlan* box = g.worldForge();
        ASSERT_TRUE(box && g.corridors()) << pair.first << ": no corridor-driven box plan";
        const FillCensus f = fillCensus(pair.first, g, *box);
        EXPECT_GT(f.vertices, 1000) << pair.first;
        EXPECT_EQ(f.overLimit, 0) << pair.first << ": a dry approach vertex is filled above the bank limit";
        EXPECT_LE(f.maxDryFill, WorldForgePlan::bankLimit() + 0.5f) << pair.first;
        EXPECT_EQ(f.emittedOver, 0) << pair.first << ": the EMITTED surface near a bridge is filled above the bank limit";
        EXPECT_LE(f.maxEmittedFill, WorldForgePlan::bankLimit() + 1.0f) << pair.first;
        EXPECT_LE(f.maxEmittedCutNear, WorldForgePlan::bankLimit() + 1.0f) << pair.first << ": the EMITTED cut at a bridge's own pins exceeds the bank limit";
        if (!box->bridges().empty()) EXPECT_GT(f.nearBridge, 0) << pair.first << ": no dry vertex near a bridge was measured";
    }
}

// Every crossing on a realized road is a ford (a creek, depth < 0.5 u), a bridge within the grounded
// span, or does not exist (the route crossed elsewhere / the demand was refused with its reason).
// Red on M4b: carved crossings over channels wider than the span are crossed anyway, and a crossing
// the deck builder could not clear is a road ending in the river (undecked).
TEST(BridgeSpanTest, EveryCarvedCrossingIsFordBridgeOrRefused) {
    for (const auto& pair : std::vector<std::pair<const char*, WorldGenerator*>>{{"baseline Perlin", &baselineWorld()},
                                                                                {"Mountains 424242", &mountainsWorld()}}) {
        WorldGenerator& g = *pair.second;
        const WorldForgePlan* box = g.worldForge();
        ASSERT_TRUE(box && g.corridors()) << pair.first << ": no corridor-driven box plan";
        const CrossingCensus c = crossingCensus(pair.first, g, *box);
        EXPECT_GT(c.crossings, 0) << pair.first << ": no crossing at all - the fixture cannot judge the rule";
        EXPECT_EQ(c.undecked, 0) << pair.first << ": a carved crossing without a deck (a half-road into the river)";
        EXPECT_EQ(c.wide, 0) << pair.first << ": a carved crossing over a channel wider than the grounded span";
        EXPECT_EQ(c.deepFords, 0) << pair.first << ": a creek crossing deeper than the ford bound";
        int fords = 0, bridges = 0, other = 0;
        EXPECT_TRUE(kindsDecided(*box, fords, bridges, other)) << pair.first << ": " << other << " crossings on realized roads are neither ford nor bridge";
        EXPECT_EQ(bridges, c.carved) << pair.first << ": every carved crossing on a realized road is a bridge";
        EXPECT_EQ(fords, c.creeks) << pair.first << ": every creek crossing on a realized road is a ford";
    }
}

// D31 as the routing EDGE, with its control: the span knob set ONE UNIT UNDER the overall length the
// fixture's own bridge needed (13 u on the baseline: a 5.8 u channel + 3 u of bank each side + the
// 2 u shoulders — the width formula, not the clamp floor; audit 2026-10-27) makes that channel
// uncrossable, so the world bakes ZERO bridges and every demand that needed one is routed around
// (no carved crossing survives on any road) or refused with its reason — each demand yields exactly
// one road or one refusal, so roads + refusals match the default plan's. Control: the default knob
// bakes the bridge on the same world. One variable moves: bridgeMaxSpanU.
TEST(BridgeSpanTest, UncrossableChannelsAreRoutedAround) {
    const char* tag = nullptr;
    WorldGenerator* gp = worldWithBridge(tag);
    ASSERT_TRUE(gp) << "control: no version-2 fixture bakes a bridge with the default span, so the knob cannot be judged";
    WorldGenerator& g = *gp;
    const WorldForgePlan* box = g.worldForge();
    ASSERT_TRUE(box && g.corridors());
    const bool mountains = std::string(tag) == "Mountains 424242";
    WorldRecipe r = civRecipe(mountains ? 424242u : 20260816u, mountains ? "Mountains" : "Perlin");
    float need = 0.0f;
    for (const auto& b : box->bridges()) need = std::max(need, glm::length(b.b - b.a));
    const float knob = std::max(3.0f, std::floor(need) - 1.0f);
    std::printf("[ M4c ] the longest bridge on %s needs %.1f u overall; the knob goes to %.0f u\n", tag, need, knob);
    r.worldforge.bridgeMaxSpanU = knob;
    WorldGenerator h(mountains ? WorldGenerator::GenerationType::Mountains : WorldGenerator::GenerationType::Perlin, mountains ? 424242u : 20260816u);
    h.applyRecipe(r);
    const WorldForgePlan* tight = h.worldForge();
    ASSERT_TRUE(tight && h.corridors());
    EXPECT_FLOAT_EQ(tight->params().bridgeMaxSpanU, knob) << "the knob did not reach the plan";
    const CrossingCensus c = crossingCensus((std::string(tag) + ", span knob " + std::to_string(static_cast<int>(knob)) + " u").c_str(), h, *tight);
    EXPECT_TRUE(tight->bridges().empty()) << "a deck was built with no crossable span";
    EXPECT_EQ(c.carved, 0) << "a carved crossing survived on a realized road with no crossable span";
    EXPECT_EQ(tight->roads().size() + tight->refusedDemands().size(), box->roads().size() + box->refusedDemands().size())
        << "a demand neither became a road nor a refusal";
    std::printf("[ M4c ] span knob %.0f u: %zu roads, %zu refused (default: %zu roads, %zu refused, %zu bridges)\n",
                knob, tight->roads().size(), tight->refusedDemands().size(), box->roads().size(), box->refusedDemands().size(), box->bridges().size());
    for (const auto& rd : tight->refusedDemands())
        std::printf("[ M4c ]   refused %d -> %d class %d at (%.0f,%.0f): %s\n", rd.a, rd.b, rd.cls, rd.pos.x, rd.pos.y, rd.reason.c_str());
}

// D32 as a KNOB with its control: at the shipped default the ford bound IS the river/creek class
// split (0.5 u = FlowField::kRiverCarveDepth), so "no creek deeper than the bound is forded" is a
// tautology there (audit 2026-10-27). Set the bound to 0.2 u on the baseline: every creek crossing
// on a realized road is then shallower than 0.2 u, and the default plan — the control — had creek
// crossings between 0.2 and 0.5 u that the knob removed (routed around or refused with the reason).
TEST(BridgeSpanTest, FordBoundIsAKnobWithItsControl) {
    WorldGenerator& g = baselineWorld();
    const WorldForgePlan* box = g.worldForge();
    ASSERT_TRUE(box && g.corridors());
    auto deepCreeks = [&](WorldGenerator& w, const WorldForgePlan& plan, float bound) {
        int n = 0;
        for (const auto& road : plan.roads())
            for (const auto& cr : road.crossings) {
                if (cr.carved) continue;
                const FlowField::ChannelHit ch = w.channelHitAt(cr.pos.x, cr.pos.y);
                const float depth = ch.hit ? ch.fullDepth : w.riverNetwork()->fullDepthAt(cr.pos.x, cr.pos.y);
                if (depth >= bound) ++n;
            }
        return n;
    };
    const int controlDeep = deepCreeks(g, *box, 0.2f);
    ASSERT_GT(controlDeep, 0) << "control: the default plan fords no creek deeper than 0.2 u, so the knob cannot be judged";
    WorldRecipe r = civRecipe(20260816u);
    r.worldforge.fordMaxDepthU = 0.2f;
    WorldGenerator h(WorldGenerator::GenerationType::Perlin, 20260816u);
    h.applyRecipe(r);
    const WorldForgePlan* shallow = h.worldForge();
    ASSERT_TRUE(shallow && h.corridors());
    EXPECT_FLOAT_EQ(shallow->params().fordMaxDepthU, 0.2f) << "the knob did not reach the plan";
    const int deep = deepCreeks(h, *shallow, 0.2f);
    std::printf("[ M4c ] ford bound 0.2 u: %d creek crossings at or above 0.2 u on realized roads (default plan: %d), %zu roads + %zu refused (default %zu + %zu)\n",
                deep, controlDeep, shallow->roads().size(), shallow->refusedDemands().size(), box->roads().size(), box->refusedDemands().size());
    for (const auto& rd : shallow->refusedDemands())
        std::printf("[ M4c ]   refused %d -> %d class %d at (%.0f,%.0f): %s\n", rd.a, rd.b, rd.cls, rd.pos.x, rd.pos.y, rd.reason.c_str());
    EXPECT_EQ(deep, 0) << "a creek deeper than the ford bound is forded on a realized road";
    EXPECT_EQ(shallow->roads().size() + shallow->refusedDemands().size(), box->roads().size() + box->refusedDemands().size())
        << "a demand neither became a road nor a refusal";
}

// The approach check walks the road's own arc. The helpers are unit-tested on a hairpin (M4b-3
// switchbacks exist): a bank point on leg 1 projects to leg 1's arc, walking on from it stays on
// leg 1, a point on leg 2 projects to leg 2, and an arc past the end clamps to the last vertex.
// The known limit (header): legs closer than twice the point's offset would misproject - here the
// legs are 8 u apart and the bank offset 3 u. Not a red/green of plan behaviour: the "road curving
// away from a canyon wall" case has no fixture (recorded as unfalsified in the plan).
TEST(BridgeSpanTest, ArcHelpersProjectOntoTheOwnLegOfAHairpin) {
    const std::vector<glm::vec2> cl = {{0, 0}, {16, 0}, {32, 0}, {40, 0}, {40, 8}, {32, 8}, {16, 8}, {0, 8}};
    const std::vector<float> cum = WorldForgePlan::cumulativeArc(cl);
    ASSERT_EQ(cum.size(), cl.size());
    EXPECT_NEAR(cum.back(), 88.0f, 1e-3f);
    EXPECT_NEAR(WorldForgePlan::arcOfPoint(cl, cum, {23, 0}), 23.0f, 1e-3f) << "a bank on leg 1";
    EXPECT_NEAR(WorldForgePlan::arcOfPoint(cl, cum, {23, 3}), 23.0f, 1e-3f) << "3 u off leg 1 towards leg 2 still projects to leg 1";
    EXPECT_NEAR(WorldForgePlan::arcOfPoint(cl, cum, {23, 8}), 65.0f, 1e-3f) << "a point on leg 2";
    EXPECT_NEAR(WorldForgePlan::arcOfPoint(cl, cum, {23, 6}), 65.0f, 1e-3f) << "2 u off leg 2 projects to leg 2 (the nearest)";
    EXPECT_NEAR(glm::length(WorldForgePlan::pointAtArc(cl, cum, 33.0f) - glm::vec2(33, 0)), 0.0f, 1e-3f) << "10 u on from the bank stays on leg 1";
    EXPECT_NEAR(glm::length(WorldForgePlan::pointAtArc(cl, cum, 65.0f) - glm::vec2(23, 8)), 0.0f, 1e-3f) << "the inverse on leg 2";
    EXPECT_NEAR(glm::length(WorldForgePlan::pointAtArc(cl, cum, 44.0f) - glm::vec2(40, 4)), 0.0f, 1e-3f) << "inside the hairpin";
    EXPECT_NEAR(glm::length(WorldForgePlan::pointAtArc(cl, cum, 1000.0f) - glm::vec2(0, 8)), 0.0f, 1e-3f) << "past the end clamps";
    EXPECT_NEAR(glm::length(WorldForgePlan::pointAtArc(cl, cum, -5.0f) - glm::vec2(0, 0)), 0.0f, 1e-3f) << "before the start clamps";
}

// The Mountains refusals measured INDEPENDENTLY of the rule's own strings (audit 2026-10-27): at
// every refused demand the generator's emitted surface and channel hit are dumped every 1 u along
// the deck's line (the recorded tangent) 30 u each side, and the rim the rule judged on must equal
// the dump's highest channel-hit column (the rule read the same generator; a mismatch would be a
// rule bug, not terrain). The dump itself is the evidence that the terrain is what the reason says.
TEST(BridgeSpanTest, RefusalsMatchTheEmittedTerrain) {
    for (const auto& pair : std::vector<std::pair<const char*, WorldGenerator*>>{{"baseline Perlin", &baselineWorld()},
                                                                                {"Mountains 424242", &mountainsWorld()}}) {
        WorldGenerator& g = *pair.second;
        const WorldForgePlan* box = g.worldForge();
        ASSERT_TRUE(box && g.corridors());
        for (const auto& rd : box->refusedDemands()) {
            if (glm::length(rd.tangent) < 0.5f) { std::printf("[ M4c-refusal ] %s: demand %d -> %d at (%.0f,%.0f) refused before a deck line existed: %s\n", pair.first, rd.a, rd.b, rd.pos.x, rd.pos.y, rd.reason.c_str()); continue; }
            float hitMax = -1e30f, dryMin = 1e30f, dryMax = -1e30f;
            int shoulderHits = 0;
            if (rd.spanAU > -1e29f && rd.spanBU > -1e29f)   // the shoulders on a HALF-UNIT lattice (not the rule's sample points)
                for (float s = 0.5f; s < kDeckShoulderU; s += 1.0f) {
                    const glm::vec2 pa = rd.pos - rd.tangent * (rd.spanAU + s), pb = rd.pos + rd.tangent * (rd.spanBU + s);
                    if (g.channelHitAt(pa.x, pa.y).hit) ++shoulderHits;
                    if (g.channelHitAt(pb.x, pb.y).hit) ++shoulderHits;
                }
            std::string line;
            for (float s = -30.0f; s <= 30.0f; s += 1.0f) {
                const glm::vec2 p = rd.pos + rd.tangent * s;
                const int x = static_cast<int>(std::lround(p.x)), z = static_cast<int>(std::lround(p.y));
                const auto col = g.sampleSurface(x, z);
                const FlowField::ChannelHit ch = g.channelHitAt(p.x, p.y);
                // the rim the rule judged is the highest channel column BETWEEN THE BANKS (s in
                // [-spanA, spanB]); the dump runs 30 u each way and may cross another channel
                const bool inSpan = rd.spanAU > -1e29f && rd.spanBU > -1e29f && s >= -rd.spanAU - 1e-3f && s <= rd.spanBU + 1e-3f;
                if (ch.hit && inSpan) hitMax = std::max(hitMax, static_cast<float>(col.surfaceY));
                const bool inShoulder = rd.spanAU > -1e29f && rd.spanBU > -1e29f &&
                                        ((s < -rd.spanAU - 1e-3f && s >= -rd.spanAU - kDeckShoulderU - 1e-3f) || (s > rd.spanBU + 1e-3f && s <= rd.spanBU + kDeckShoulderU + 1e-3f));
                if (ch.hit && inShoulder) ++shoulderHits;   // the integer lattice too
                else { dryMin = std::min(dryMin, static_cast<float>(col.surfaceY)); dryMax = std::max(dryMax, static_cast<float>(col.surfaceY)); }
                char buf[24];
                std::snprintf(buf, sizeof buf, "%s%s%d ", inSpan ? "" : ".", ch.hit ? "~" : "", col.surfaceY);
                line += buf;
            }
            std::printf("[ M4c-refusal ] %s: demand %d -> %d class %d at (%.0f,%.0f): banks %.0f / %.0f at -%.0f / +%.0f u, rim %.0f, deck %.1f | dump (~ = channel column, . = outside the banks) hit max between the banks %.0f, dry %.0f..%.0f | %s\n",
                        pair.first, rd.a, rd.b, rd.cls, rd.pos.x, rd.pos.y, rd.bankLowU, rd.bankHighU, rd.spanAU, rd.spanBU, rd.rimU, rd.deckU, hitMax, dryMin, dryMax, rd.reason.c_str());
            std::printf("[ M4c-refusal ]   %s\n", line.c_str());
            if (rd.rimU > -1e29f) {   // the rule judged a rim: the dump must find one, and the same one
                EXPECT_GT(hitMax, -1e29f) << pair.first << ": the rule judged a rim but the dump finds no channel column between the banks";
                EXPECT_NEAR(rd.rimU, hitMax, 0.01f) << pair.first << ": the rim the rule judged on is not the dump's highest channel column";
            }
            if (rd.spanAU > -1e29f && rd.spanBU > -1e29f)
                EXPECT_EQ(shoulderHits, 0) << pair.first << ": a channel column within the 2 u shoulder beyond a bank (a gap inside the channel mask was taken for a bank)";
        }
    }
}

// L3: a character-box walks the first version-2 bridge bank to bank (the version-1 twin lives in
// RoadFieldSeamTest). The deck at the LOWER bank with the higher approach cut to it must still be
// mountable from both sides with a 1-cube hop, and the walkway strictly walkable between the
// parapets; the sensitivity control (a parapet-height wall across mid-span) proves the probe can fail.
// Named for what it measures (audit 2026-10-28): the ROAD across the crossing, not the deck -
// on this fixture the channel is 1 u deep and dry, so the hop can pass without the plank.
TEST(BridgeSpanTest, RoadIsHopWalkableAcrossTheVersionTwoCrossing) {
    const char* tag = nullptr;
    WorldGenerator* gp = worldWithBridge(tag);
    if (!gp) GTEST_SKIP() << "no version-2 fixture baked a bridge";
    WorldGenerator& g = *gp;
    const WorldForgePlan* box = g.worldForge();
    ASSERT_TRUE(box && g.corridors());
    std::printf("[ M4c ] walking the first bridge of %s (%zu bridges)\n", tag, box->bridges().size());
    const auto& b = box->bridges()[0];
    const glm::vec2 ab = b.b - b.a;
    const float len = glm::length(ab);
    ASSERT_GT(len, 3.0f);
    const glm::vec2 dir = ab / len;
    const int deckY = static_cast<int>(b.deckY);

    ChunkMicroWorld world(g);
    const glm::vec2 A = b.a - dir * 8.0f, B = b.b + dir * 8.0f;
    const auto colA = g.sampleSurface(static_cast<int>(std::lround(A.x)), static_cast<int>(std::lround(A.y)));
    const auto colB = g.sampleSurface(static_cast<int>(std::lround(B.x)), static_cast<int>(std::lround(B.y)));
    const int minSurf = std::min({colA.surfaceY, colB.surfaceY, deckY});
    const int maxSurf = std::max({colA.surfaceY, colB.surfaceY, deckY});
    const glm::ivec3 boundLo(static_cast<int>((std::min(A.x, B.x) - 6.0f) * 9.0f), (minSurf - 2) * 9,
                             static_cast<int>((std::min(A.y, B.y) - 6.0f) * 9.0f));
    const glm::ivec3 boundHi(static_cast<int>((std::max(A.x, B.x) + 6.0f) * 9.0f), (maxSurf + 5) * 9,
                             static_cast<int>((std::max(A.y, B.y) + 6.0f) * 9.0f));
    auto occ = [&](int x, int y, int z) { return world.solidMicro(x, y, z); };
    Core::AgentBox hopBox;
    hopBox.maxStepUpMicro = 9;
    Core::TraversalProbe hopProbe(occ, hopBox);
    auto feetAt = [&](const glm::vec2& p, int surfY, const Core::TraversalProbe& pr) {
        const int fx = static_cast<int>(std::floor(p.x * 9.0f)) + 4;
        const int fz = static_cast<int>(std::floor(p.y * 9.0f)) + 4;
        return glm::ivec3(fx, pr.settle(fx, (surfY + 3) * 9, fz, boundLo.y), fz);
    };
    const glm::ivec3 start = feetAt(A, colA.surfaceY, hopProbe);
    const glm::ivec3 goal = feetAt(B, colB.surfaceY, hopProbe);
    ASSERT_NE(start.y, INT_MIN) << "agent cannot stand at the A-side approach";
    ASSERT_NE(goal.y, INT_MIN) << "agent cannot stand at the B-side approach";
    const auto bankA = g.sampleSurface(static_cast<int>(std::lround(b.a.x)), static_cast<int>(std::lround(b.a.y)));
    const auto bankB = g.sampleSurface(static_cast<int>(std::lround(b.b.x)), static_cast<int>(std::lround(b.b.y)));
    std::cout << "[ M4c ] v2 bridge walk: deckY=" << deckY << " bankA.surfaceY=" << bankA.surfaceY
              << " bankB.surfaceY=" << bankB.surfaceY << " approachA=" << colA.surfaceY << " approachB=" << colB.surfaceY
              << " span=" << len << " class=" << b.cls << " order=" << b.crossingOrder << std::endl;
    EXPECT_TRUE(hopProbe.reachable(start, goal - glm::ivec3(9, 18, 9), goal + glm::ivec3(9, 18, 9), boundLo, boundHi))
        << "bank-to-bank version-2 bridge crossing is not walkable (with 1-cube hop)";
    {   // REPORTED, not asserted: the strict 4/9 u walker bank to bank inside the road's own width
        // (lateral +-1.5 u). The deck plank is a whole cube at deckY on a bank whose surface is
        // deckY - 1, so every bridge end has a 1 u lip for a strict walker - M4b's road surface
        // standard is "no step beyond 1 cube" (RealizedRoadsAreStepWalkable), which the hop above
        // measures; a flush plank is an open item (plan M4c, audit 2026-10-27).
        Core::AgentBox strictWalk;
        Core::TraversalProbe strictWalker(occ, strictWalk);
        auto narrow = [&](int x, int y, int z) {
            const glm::vec2 p((x + 0.5f) / 9.0f, (z + 0.5f) / 9.0f);
            const float lat = std::abs(glm::dot(p - b.a, glm::vec2(-dir.y, dir.x)));
            return lat > 1.5f || world.solidMicro(x, y, z);
        };
        Core::TraversalProbe narrowWalker(narrow, strictWalk);
        const glm::ivec3 s0 = feetAt(A, colA.surfaceY, strictWalker), g0 = feetAt(B, colB.surfaceY, strictWalker);
        const bool strictOk = s0.y != INT_MIN && g0.y != INT_MIN &&
                              narrowWalker.reachable(s0, g0 - glm::ivec3(9, 18, 9), g0 + glm::ivec3(9, 18, 9), boundLo, boundHi);
        std::printf("[ M4c ] strict 4/9 u walker bank to bank within +-1.5 u of the deck line: %s (reported; the 1 u lip at the plank ends)\n", strictOk ? "walkable" : "NOT walkable");
    }

    Core::AgentBox strictBox;
    Core::TraversalProbe strictProbe(occ, strictBox);
    const glm::ivec3 deckStart = feetAt(b.a + dir * 2.0f, deckY, strictProbe);
    const glm::ivec3 deckGoal = feetAt(b.b - dir * 2.0f, deckY, strictProbe);
    ASSERT_NE(deckStart.y, INT_MIN);
    ASSERT_NE(deckGoal.y, INT_MIN);
    EXPECT_TRUE(strictProbe.reachable(deckStart, deckGoal - glm::ivec3(9, 18, 9), deckGoal + glm::ivec3(9, 18, 9), boundLo, boundHi))
        << "deck walkway not strictly walkable between the parapets";

    const glm::vec2 mid = (b.a + b.b) * 0.5f;
    auto blockedOcc = [&](int x, int y, int z) {
        const glm::vec2 p((x + 0.5f) / 9.0f, (z + 0.5f) / 9.0f);
        const float along = glm::dot(p - b.a, dir);
        const float lat = std::abs(glm::dot(p - b.a, glm::vec2(-dir.y, dir.x)));
        const float midAlong = glm::dot(mid - b.a, dir);
        // The control wall crosses the WHOLE bounded box at mid-span, bed to the box top: on the
        // baseline the channel under this deck is 1 u deep and dry in the test world, so a wall
        // only as wide as the deck was walked around through the bed, and a parapet-high one was
        // walked OVER from the graded road surface beside the deck end, which sits 2/3 u above
        // the plank (measured 2026-10-27, the lateral-top dump) - the control must remove every
        // route, so it proves only that the probe can fail here, not that the deck is the one way.
        (void)lat;
        if (std::abs(along - midAlong) <= 0.5f && y >= (deckY - 10) * 9 && y <= boundHi.y) return true;
        return world.solidMicro(x, y, z);
    };
    Core::TraversalProbe blockedProbe(blockedOcc, strictBox);
    EXPECT_FALSE(blockedProbe.reachable(deckStart, deckGoal - glm::ivec3(9, 18, 9), deckGoal + glm::ivec3(9, 18, 9), boundLo, boundHi))
        << "probe failed to sense a parapet-height wall across the walkway";
}

// Diagnostic (prints only): the columns along the first version-2 bridge, 1 u apart — what the
// generator samples (surface, deck, rail, pier, class, order) and the highest solid micro-cell the
// chunk actually holds at the centreline. The red walk (2026-10-27) needed this to be read.
TEST(BridgeSpanTest, DiagVersionTwoBridgeColumns) {
    const char* tag = nullptr;
    WorldGenerator* gp = worldWithBridge(tag);
    if (!gp) GTEST_SKIP();
    WorldGenerator& g = *gp;
    const WorldForgePlan* box = g.worldForge();
    ASSERT_TRUE(box);
    const auto& b = box->bridges()[0];
    const float len = glm::length(b.b - b.a);
    const glm::vec2 dir = (b.b - b.a) / len;
    const int deckY = static_cast<int>(b.deckY);
    ChunkMicroWorld world(g);
    const glm::vec2 nrm(-dir.y, dir.x);
    auto topAt = [&](const glm::vec2& q) {   // the highest solid micro-cell in the column, as a height in u
        const int x = static_cast<int>(std::lround(q.x)), z = static_cast<int>(std::lround(q.y));
        for (int my = (deckY + 6) * 9; my >= (deckY - 10) * 9; --my)
            if (world.solidMicro(x * 9 + 4, my, z * 9 + 4)) return (my + 1) / 9.0f;
        return -1.0f;
    };
    auto occ = [&](int x, int y, int z) { return world.solidMicro(x, y, z); };
    Core::AgentBox strictBox;
    Core::TraversalProbe probe(occ, strictBox);
    const glm::ivec3 lo(static_cast<int>((std::min(b.a.x, b.b.x) - 20.0f) * 9.0f), (deckY - 12) * 9, static_cast<int>((std::min(b.a.y, b.b.y) - 20.0f) * 9.0f));
    const glm::ivec3 hi(static_cast<int>((std::max(b.a.x, b.b.x) + 20.0f) * 9.0f), (deckY + 8) * 9, static_cast<int>((std::max(b.a.y, b.b.y) + 20.0f) * 9.0f));
    auto feet = [&](const glm::vec2& q) {
        const int fx = static_cast<int>(std::floor(q.x * 9.0f)) + 4, fz = static_cast<int>(std::floor(q.y * 9.0f)) + 4;
        return glm::ivec3(fx, probe.settle(fx, (deckY + 4) * 9, fz, lo.y), fz);
    };
    for (float s = -12.0f; s <= len + 12.0f; s += 1.0f) {
        const glm::vec2 p = b.a + dir * s;
        const int x = static_cast<int>(std::lround(p.x)), z = static_cast<int>(std::lround(p.y));
        const auto col = g.sampleSurface(x, z);
        const float wl = g.hydrology() ? g.hydrology()->waterLevelAt(p.x, p.y) : -1.0f;
        // headroom: the first solid ABOVE the floor at the centreline, up to deckY + 6
        const int floorMicro = static_cast<int>(std::lround(topAt(p) * 9.0f));
        float ceil = -1.0f;
        for (int my = floorMicro; my < (deckY + 6) * 9; ++my)
            if (world.solidMicro(x * 9 + 4, my, z * 9 + 4)) { ceil = my / 9.0f; break; }
        const glm::ivec3 f0 = feet(p), f1 = feet(p + dir);
        const bool leg = f0.y != INT_MIN && f1.y != INT_MIN && probe.reachable(f0, f1 - glm::ivec3(4, 18, 4), f1 + glm::ivec3(4, 18, 4), lo, hi);
        std::printf("[ M4c-diag ] s=%5.0f (%d,%d) surf=%d deck=%d rail=%d pierTop=%d cls=%d order=%d water=%.1f top[-2..2]=%.2f %.2f %.2f %.2f %.2f ceil=%.2f feetY=%.2f leg->%s\n",
                    s, x, z, col.surfaceY, col.bridgeDeckY, col.bridgeRail ? 1 : 0, col.bridgePierTopY, col.roadClass, col.riverOrder, wl,
                    topAt(p - nrm * 2.0f), topAt(p - nrm), topAt(p), topAt(p + nrm), topAt(p + nrm * 2.0f), ceil,
                    f0.y == INT_MIN ? -1.0f : f0.y / 9.0f, leg ? "ok" : "FAIL");
    }
}
