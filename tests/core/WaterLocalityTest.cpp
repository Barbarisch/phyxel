// WaterCore §40 (L1) - the viewer distance every water system reads. A pure function of the viewer and a world position: the
// distance to a box is 0 inside and exact outside (negative coordinates too), the detail fade never rises with distance and never
// jumps, and the defaults - the two distances L1 took over unchanged - are pinned (40.1 items 1, 3, 4).
#include <gtest/gtest.h>

#include "core/water/WaterCoreManager.h"
#include "core/water/WaterLocality.h"

#include <cmath>
#include <random>

using namespace Phyxel::Core::Water;

TEST(WaterLocality, TheDefaultsAreTheTwoDistancesItTookOver) {
    // 27.10's record draw radius and 25.29's ripple window: L1 changes nothing when it lands
    EXPECT_FLOAT_EQ(WaterLocality::kDrawMDefault, 48.0f);
    EXPECT_EQ(WaterLocality::kRippleWindowCellsDefault, 384);
    EXPECT_EQ(WaterCoreManager::kColumnRippleWindow, WaterLocality::kRippleWindowCellsDefault);
    const WaterLocality fresh;
    EXPECT_FLOAT_EQ(fresh.drawM, WaterLocality::kDrawMDefault);
    EXPECT_EQ(fresh.rippleWindowCells, WaterLocality::kRippleWindowCellsDefault);
    EXPECT_FALSE(fresh.testViewerOn);
    EXPECT_FLOAT_EQ(WaterCoreManager::s_locality.drawM, WaterLocality::kDrawMDefault) << "a test left the draw radius changed";
    EXPECT_EQ(WaterCoreManager::s_locality.rippleWindowCells, WaterLocality::kRippleWindowCellsDefault) << "a test left the window changed";
    EXPECT_FALSE(WaterCoreManager::s_locality.testViewerOn) << "a test left the test viewer on";
}

// L3 (41.4 item 8): the kept-memory reach and cap
TEST(WaterLocality, TheMemoryDefaultsArePinned) {
    EXPECT_FLOAT_EQ(WaterLocality::kMemoryMDefault, 48.0f);
    EXPECT_FLOAT_EQ(WaterLocality::kMemoryMDefault, WaterLocality::kDrawMDefault) << "41.2: a pond you can see keeps its memory";
    EXPECT_FLOAT_EQ(WaterLocality::kMemoryCapMbDefault, 256.0f);
    const WaterLocality fresh;
    EXPECT_FLOAT_EQ(fresh.memoryM, WaterLocality::kMemoryMDefault);
    EXPECT_FLOAT_EQ(fresh.memoryCapMb, WaterLocality::kMemoryCapMbDefault);
    EXPECT_FALSE(fresh.testKeepMemoryOff);
    EXPECT_FLOAT_EQ(WaterCoreManager::s_locality.memoryM, WaterLocality::kMemoryMDefault) << "a test left the memory reach changed";
    EXPECT_FLOAT_EQ(WaterCoreManager::s_locality.memoryCapMb, WaterLocality::kMemoryCapMbDefault) << "a test left the cap changed";
    EXPECT_FALSE(WaterCoreManager::s_locality.testKeepMemoryOff) << "a test left kept memory off";
}

// L3 (41.3): the release rule - beyond the reach released; over the cap the nearest are kept first; ties by position, never by order
TEST(WaterLocality, KeptMemoryIsReleasedByDistanceThenByTheCap) {
    using KB = WaterLocality::KeptBox;
    const unsigned long long MB = 1024ull * 1024ull;
    const glm::vec3 viewer(0.0f, 10.0f, 0.0f);
    // boxes at 0 (the viewer inside), ~10, ~30 and ~60 m east, negative z included
    const std::vector<KB> kept = {{{-4, 0, -4}, {3, 2, 3}, 40 * MB}, {{10, 0, -8}, {17, 2, -1}, 40 * MB},
                                  {{30, 0, 0}, {37, 2, 7}, 40 * MB}, {{60, 0, -20}, {67, 2, -13}, 40 * MB}};
    EXPECT_TRUE(WaterLocality::keptToRelease(kept, viewer, 1024.0f, 4096.0f).empty()) << "all within the reach and the cap";
    EXPECT_EQ(WaterLocality::keptToRelease(kept, viewer, 48.0f, 4096.0f), (std::vector<size_t>{3})) << "beyond 48 m";
    EXPECT_EQ(WaterLocality::keptToRelease(kept, viewer, 1024.0f, 100.0f), (std::vector<size_t>{2, 3})) << "the cap holds two: the farthest go";
    EXPECT_EQ(WaterLocality::keptToRelease(kept, viewer, 0.0f, 4096.0f).size(), 4u) << "a reach of 0 keeps nothing";
    EXPECT_EQ(WaterLocality::keptToRelease(kept, viewer, 1024.0f, 0.0f).size(), 4u) << "a cap of 0 keeps nothing";
    // two at the same distance, room for one: the same one wins in either list order (the lower x)
    const std::vector<KB> tie = {{{20, 0, 0}, {27, 2, 7}, 40 * MB}, {{-28, 0, 0}, {-21, 2, 7}, 40 * MB}};
    const std::vector<KB> tieSwapped = {tie[1], tie[0]};
    const glm::vec3 mid(0.0f, 0.0f, 4.0f);
    EXPECT_EQ(WaterLocality::keptToRelease(tie, mid, 1024.0f, 50.0f), (std::vector<size_t>{0})) << "the box at x 20 goes (x -28 wins the tie)";
    EXPECT_EQ(WaterLocality::keptToRelease(tieSwapped, mid, 1024.0f, 50.0f), (std::vector<size_t>{1})) << "the list order picked the winner";
}

TEST(WaterLocality, TheDistanceToABoxIsZeroInsideAndExactOutside) {
    const glm::vec2 lo(-40.0f, -8.0f), hi(-32.0f, 0.0f);   // an 8 m tile at negative coordinates
    EXPECT_FLOAT_EQ(WaterLocality::horizontalDistanceToBox(glm::vec3(-36.0f, 99.0f, -4.0f), lo, hi), 0.0f) << "inside (height ignored)";
    EXPECT_FLOAT_EQ(WaterLocality::horizontalDistanceToBox(glm::vec3(-29.0f, 0.0f, -4.0f), lo, hi), 3.0f) << "east of it";
    EXPECT_FLOAT_EQ(WaterLocality::horizontalDistanceToBox(glm::vec3(-43.0f, 0.0f, 4.0f), lo, hi), 5.0f) << "a corner: 3-4-5";
    EXPECT_FLOAT_EQ(WaterLocality::horizontalDistanceSqToBox(glm::vec3(-43.0f, 0.0f, 4.0f), lo, hi), 25.0f);
    EXPECT_FLOAT_EQ(WaterLocality::horizontalDistance(glm::vec3(1.0f, 7.0f, 1.0f), glm::vec2(4.0f, 5.0f)), 5.0f);
}

TEST(WaterLocality, TheDetailFadeNeverRisesAndNeverJumps) {
    const float nearM = 20.0f, farM = 40.0f;
    EXPECT_FLOAT_EQ(WaterLocality::detail(0.0f, nearM, farM), 1.0f);
    EXPECT_FLOAT_EQ(WaterLocality::detail(nearM, nearM, farM), 1.0f);
    EXPECT_FLOAT_EQ(WaterLocality::detail(farM, nearM, farM), 0.0f);
    EXPECT_FLOAT_EQ(WaterLocality::detail(1000.0f, nearM, farM), 0.0f);
    EXPECT_NEAR(WaterLocality::detail(30.0f, nearM, farM), 0.5f, 1e-6f);
    float prev = 1.0f, worstStep = 0.0f;
    for (int mm = 0; mm <= 50000; ++mm) {   // 0..50 m in 1 mm steps
        const float v = WaterLocality::detail(static_cast<float>(mm) * 1e-3f, nearM, farM);
        EXPECT_LE(v, prev + 1e-7f) << "the fade rose at " << mm << " mm";
        worstStep = std::max(worstStep, std::abs(v - prev));
        prev = v;
    }
    EXPECT_LT(worstStep, 1e-3f) << "the fade jumped (a band edge, not a fade)";
    EXPECT_FLOAT_EQ(WaterLocality::detail(25.0f, 30.0f, 30.0f), 1.0f) << "far <= near: a hard reach at near";
    EXPECT_FLOAT_EQ(WaterLocality::detail(31.0f, 30.0f, 30.0f), 0.0f);
}

// ---- L2 (43): ripple detail by distance -------------------------------------------------------------------------------------

TEST(WaterLocality, TheRippleReachDefaultsArePinned) {
    EXPECT_TRUE(WaterLocality::kRippleReachByDefault) << "43.5: on by default (the owner, 2026-10-29)";
    EXPECT_FLOAT_EQ(WaterLocality::kRippleMDefault, 21.0f);
    EXPECT_FLOAT_EQ(WaterLocality::kRippleFadeMDefault, 8.0f);
    EXPECT_FLOAT_EQ(WaterLocality::kRippleMMax, 24.0f);
    EXPECT_EQ(WaterLocality::rippleWindowSide(WaterLocality::kRippleMMax), WaterLocality::kRippleWindowCellsMax) << "24 m = the 512 atlas";
    EXPECT_EQ(WaterLocality::rippleWindowSide(WaterLocality::kRippleMDefault), 2 * (189 + 40));
    EXPECT_EQ(WaterLocality::rippleWindowSide(100.0f), WaterLocality::kRippleWindowCellsMax) << "clamped";
    EXPECT_EQ(WaterLocality::rippleWindowSide(0.0f), 80);
    const WaterLocality fresh;
    EXPECT_EQ(fresh.rippleReach, WaterLocality::kRippleReachByDefault);
    EXPECT_FLOAT_EQ(fresh.rippleM, WaterLocality::kRippleMDefault);
    EXPECT_FLOAT_EQ(fresh.rippleFadeM, WaterLocality::kRippleFadeMDefault);
    EXPECT_EQ(WaterCoreManager::s_locality.rippleReach, WaterLocality::kRippleReachByDefault) << "a test left the reach changed";
    EXPECT_FLOAT_EQ(WaterCoreManager::s_locality.rippleM, WaterLocality::kRippleMDefault) << "a test left ripple_m changed";
    EXPECT_FLOAT_EQ(WaterCoreManager::s_locality.rippleFadeM, WaterLocality::kRippleFadeMDefault) << "a test left the fade changed";
}

namespace {
// One axis of the window against the reach: every lattice cell of the box within rippleM of the viewer (the bounding square of
// the disc - stricter than the disc) lies inside the window and outside its 2-cell absorbing rim, except where the window's edge
// is the box's own edge (as a pond that fits its window always was). Returns false on a violation.
bool axisCovered(double v, double r, int boxLo, int boxHi /*last cell*/, int cur, int n) {
    const int need0 = std::max(boxLo, static_cast<int>(std::floor(9.0 * (v - r)))), need1 = std::min(boxHi, static_cast<int>(std::floor(9.0 * (v + r))));
    if (need0 > need1) return true;   // the box is beyond the reach on this axis
    const bool loOk = cur == boxLo ? need0 >= cur : need0 >= cur + 2;
    const bool hiOk = cur + n - 1 == boxHi ? need1 <= cur + n - 1 : need1 <= cur + n - 3;
    return loOk && hiOk;
}
struct WalkResult { long steps = 0, violations = 0, moves = 0; };
// A random walk of the viewer over and around the box; the window placed by `reachRule` with a side of 2 (ceil(9 r) + margin)
WalkResult walk(uint32_t seed, const glm::ivec3& minV, const glm::ivec3& maxV, float r, int margin, bool reachRule, int steps) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> step(0.1f, 6.0f), angle(0.0f, 6.2831853f), u(0.0f, 1.0f);
    const int boxX = (maxV.x - minV.x + 1) * 9, boxZ = (maxV.z - minV.z + 1) * 9;
    const int side = WaterLocality::rippleWindowSide(r, margin);
    const int lnx = std::min(side, boxX), lnz = std::min(side, boxZ);
    // start somewhere within 30 m of the box
    glm::vec3 v(minV.x - 30.0f + u(rng) * (maxV.x - minV.x + 61.0f), 20.0f, minV.z - 30.0f + u(rng) * (maxV.z - minV.z + 61.0f));
    bool has = false; glm::ivec2 cur(0);
    WalkResult w;
    for (int s = 0; s < steps; ++s) {
        const float a = angle(rng), d = (s % 97 == 96) ? 40.0f * u(rng) : step(rng);   // now and then a jump (a teleport)
        v.x += d * std::cos(a); v.z += d * std::sin(a);
        // keep it near the box (walks that leave stay outside and test nothing)
        v.x = std::clamp(v.x, minV.x - 40.0f, maxV.x + 41.0f); v.z = std::clamp(v.z, minV.z - 40.0f, maxV.z + 41.0f);
        const glm::ivec2 next = WaterLocality::rippleWindowOrigin(has, cur, v, minV, maxV, lnx, lnz, reachRule);
        if (has && next != cur) ++w.moves;
        cur = next; has = true;
        ++w.steps;
        const bool ok = axisCovered(v.x, r, minV.x * 9, (maxV.x + 1) * 9 - 1, cur.x, lnx) && axisCovered(v.z, r, minV.z * 9, (maxV.z + 1) * 9 - 1, cur.y, lnz);
        if (!ok) ++w.violations;
    }
    return w;
}
}  // namespace

// 43.2's invariant: with the reach on, every point the fade can show is simulated, after every move of the viewer. Two reds in
// the same walk: the window without the hysteresis term (margin 4), and the move rule before L2 (the clamped wanted origin).
TEST(WaterLocality, EveryPointTheRippleFadeCanShowIsInsideTheWindow) {
    struct Box { glm::ivec3 lo, hi; const char* what; };
    const Box boxes[] = {{{-100, 0, -60}, {-1, 2, 19}, "100 x 80 m at negative coordinates (larger than any window)"},
                         {{-8, 0, -8}, {7, 2, 7}, "16 x 16 m (fits a 21 m window on both axes)"},
                         {{0, 0, -3}, {99, 2, 2}, "a 100 x 6 m creek"},
                         {{-30, 0, 5}, {29, 2, 32}, "the creek bench's 60 x 28 m lake shape"}};
    const float reaches[] = {0.0f, 5.0f, 13.0f, 21.0f, 24.0f};
    long steps = 0, moves = 0, redHyst = 0, redOldRule = 0;
    uint32_t seed = 1;
    for (const Box& b : boxes)
        for (float r : reaches) {
            for (int k = 0; k < 4; ++k, ++seed) {
                const WalkResult g = walk(seed, b.lo, b.hi, r, WaterLocality::kRippleWindowMarginCells, true, 1500);
                EXPECT_EQ(g.violations, 0) << b.what << ", ripple_m " << r << ", seed " << seed;
                steps += g.steps; moves += g.moves;
                redHyst += walk(seed, b.lo, b.hi, r, WaterLocality::kRippleWindowMarginCells - WaterLocality::kRippleMoveCells, true, 1500).violations;
                redOldRule += walk(seed, b.lo, b.hi, r, WaterLocality::kRippleWindowMarginCells, false, 1500).violations;
            }
        }
    std::printf("  %ld steps, %ld window moves: 0 uncovered; red without the hysteresis term %ld, red with the pre-L2 move rule %ld\n",
                steps, moves, redHyst, redOldRule);
    EXPECT_GT(moves, 1000) << "the walks moved the window (else they test nothing)";
    EXPECT_GT(redHyst, 0) << "the red: a window without the hysteresis term must uncover the reach";
    EXPECT_GT(redOldRule, 0) << "the red: the clamped move rule must uncover the reach near a box edge";
}

TEST(WaterLocality, RippleSlotsGoNearestFirstNeverByListOrder) {
    using B = std::pair<glm::ivec3, glm::ivec3>;
    const glm::vec3 viewer(0.0f, 0.0f, 0.0f);
    const std::vector<B> boxes = {{{30, 0, 0}, {33, 1, 3}}, {{-6, 0, -2}, {-3, 1, 1}}, {{10, 0, 10}, {13, 1, 13}},
                                  {{-14, 0, 0}, {-11, 1, 3}}, {{10, 0, -14}, {13, 1, -11}}};
    const auto o = WaterLocality::nearestFirst(boxes, viewer);
    ASSERT_EQ(o.size(), boxes.size());
    EXPECT_EQ(o[0], 1u) << "the box 2 m away";
    // distances (to the box's far faces at hi + 1): [0] 30, [1] 2, [2] sqrt(200), [3] 10, [4] sqrt(200) - [2] and [4] tie at the
    // same x: the lower z (-14) wins
    EXPECT_EQ(o[1], 3u); EXPECT_EQ(o[2], 4u); EXPECT_EQ(o[3], 2u); EXPECT_EQ(o[4], 0u);
    std::vector<B> rev(boxes.rbegin(), boxes.rend());
    const auto o2 = WaterLocality::nearestFirst(rev, viewer);
    for (size_t i = 0; i < o.size(); ++i) EXPECT_EQ(rev[o2[i]], boxes[o[i]]) << "the list order changed who goes first at " << i;
}

// 40.1 item 8 / 43.3 item 3: one pond against the same water as two volumes abutting across x = 0 (a chunk seam at negative
// coordinates). The drawn WEIGHT of every column (the fade x whether its volume's layer simulates it) is equal along a viewer walk,
// and 0 wherever a pond has no layer. (Ring STATE does not cross two volumes - each layer's border absorbs: an existing defect,
// logged in 43.3 item 3, not covered here.)
TEST(WaterLocality, TheRippleWeightIsTheSameForOnePondAndTwo) {
    const float r = WaterLocality::kRippleMDefault, fade = WaterLocality::kRippleFadeMDefault;
    struct Vol { glm::ivec3 lo, hi; bool has = false; glm::ivec2 cur{0}; int lnx = 0, lnz = 0; };
    auto setup = [&](Vol& v) {
        const int side = WaterLocality::rippleWindowSide(r);
        v.lnx = std::min(side, (v.hi.x - v.lo.x + 1) * 9); v.lnz = std::min(side, (v.hi.z - v.lo.z + 1) * 9);
    };
    std::vector<Vol> one = {{{-30, 0, -10}, {29, 2, 9}}};
    std::vector<Vol> two = {{{-30, 0, -10}, {-1, 2, 9}}, {{0, 0, -10}, {29, 2, 9}}};
    for (auto& v : one) setup(v);
    for (auto& v : two) setup(v);
    // the manager's rule: a layer made within r of the box, dropped beyond r + kRippleDropM; the window placed by the reach rule
    auto place = [&](std::vector<Vol>& vs, const glm::vec3& view) {
        for (Vol& v : vs) {
            const float d = WaterLocality::horizontalDistanceToBox(view, glm::vec2(v.lo.x, v.lo.z), glm::vec2(v.hi.x + 1, v.hi.z + 1));
            if (v.has && d > r + WaterLocality::kRippleDropM) v.has = false;
            else if (v.has || d <= r) { v.cur = WaterLocality::rippleWindowOrigin(v.has, v.cur, view, v.lo, v.hi, v.lnx, v.lnz, true); v.has = true; }
        }
    };
    auto weight = [&](const std::vector<Vol>& vs, const glm::vec3& view, int cx, int cz) {
        for (const Vol& v : vs) {
            if (cx < v.lo.x * 9 || cx >= (v.hi.x + 1) * 9 || cz < v.lo.z * 9 || cz >= (v.hi.z + 1) * 9) continue;
            if (!v.has) return 0.0f;
            const bool in = cx >= v.cur.x && cx < v.cur.x + v.lnx && cz >= v.cur.y && cz < v.cur.y + v.lnz;
            const float d = WaterLocality::horizontalDistance(view, glm::vec2((cx + 0.5f) / 9.0f, (cz + 0.5f) / 9.0f));
            return in ? WaterLocality::detail(d, r - fade, r) : 0.0f;
        }
        return -1.0f;
    };
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> step(0.1f, 6.0f), angle(0.0f, 6.2831853f);
    glm::vec3 view(-60.0f, 20.0f, 0.0f);
    long cols = 0, unequal = 0, weighted = 0;
    for (int s = 0; s < 400; ++s) {
        const float a = angle(rng), d = step(rng);
        view.x = std::clamp(view.x + d * std::cos(a), -70.0f, 70.0f); view.z = std::clamp(view.z + d * std::sin(a), -40.0f, 40.0f);
        place(one, view); place(two, view);
        for (int cz = -90; cz < 90; cz += 3) for (int cx = -270; cx < 270; cx += 3) {   // every third column
            const float w1 = weight(one, view, cx, cz), w2 = weight(two, view, cx, cz);
            ++cols; if (w1 > 0.0f) ++weighted;
            if (std::abs(w1 - w2) > 1e-6f) ++unequal;
        }
    }
    std::printf("  %ld columns compared (%ld with weight), %ld unequal\n", cols, weighted, unequal);
    EXPECT_GT(weighted, 10000) << "the walk saw ripples (else it tests nothing)";
    EXPECT_EQ(unequal, 0);
}
