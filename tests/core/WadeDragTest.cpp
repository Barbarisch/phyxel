// WaterCore 48 (plan A4) - the wading law (core/water/WadeDrag.h). Pure: no engine, no water volume.
// Predictions (48.1, written before building): the default character (half-width 0.25, half-height 0.95) walking at 2.0 m/s
// slows to sqrt(2.5 / k): ankle 0.1 m 2.0 (terminal 2.58), knee 0.3 m 1.49, thigh 0.5 m 1.15, waist 0.8 m 0.91 m/s; the answer
// does not depend on the step length; at 0.8 m a 0.5 m/s current is held, a 1.5 m/s current pushes the wader downstream at
// 1.5 - 0.91 = 0.59 m/s.
#include <gtest/gtest.h>

#include "core/water/WadeDrag.h"

#include <glm/gtc/quaternion.hpp>

using namespace Phyxel::Core::Water;

namespace {
constexpr float kHW = 0.25f, kHH = 0.95f;

// walk `seconds` in steps of `dt` from rest on land speed `want` (+x); returns the final velocity and the distance covered
struct Walk { glm::vec2 v{0.0f}; float dist = 0.0f; };
Walk walk(float depth, float want, float seconds, float dt, glm::vec2 flow = glm::vec2(0.0f), glm::vec2 v0 = glm::vec2(0.0f)) {
    Walk w; w.v = v0;
    const int n = static_cast<int>(std::lround(seconds / dt));
    for (int i = 0; i < n; ++i) {
        w.v = wadeStep(w.v, glm::vec2(want, 0.0f), flow, depth, kHW, kHH, true, dt).velocity;
        w.dist += w.v.x * dt;
    }
    return w;
}
}  // namespace

TEST(WadeDrag, TheDefaultsArePinned) {
    // 48.2: the shipped switch and the law's constants - a change here is deliberate, with its reason in WaterCore 48
    EXPECT_TRUE(kWadeCharactersByDefault);
    const WadeParams p;
    EXPECT_FLOAT_EQ(p.driveAcc, 2.5f);
    EXPECT_FLOAT_EQ(p.dragCd, 1.05f);
    EXPECT_FLOAT_EQ(p.kgPerM3, 147.0f);
    EXPECT_FLOAT_EQ(p.minDepth, 0.05f);
}

TEST(WadeDrag, TerminalSpeedsByDepth) {
    struct Row { float depth, predicted; } rows[] = {{0.1f, 2.0f}, {0.3f, 1.49f}, {0.5f, 1.15f}, {0.8f, 0.91f}};
    for (const auto& r : rows) {
        const Walk w = walk(r.depth, 2.0f, 8.0f, 1.0f / 60.0f);
        EXPECT_NEAR(w.v.x, r.predicted, 0.01f * r.predicted + 0.005f) << "depth " << r.depth;
        EXPECT_NEAR(w.v.y, 0.0f, 1e-6f);
    }
    // running into deep water: the same cap
    EXPECT_NEAR(walk(0.8f, 5.0f, 8.0f, 1.0f / 60.0f, {}, {5.0f, 0.0f}).v.x, 0.91f, 0.01f);
    EXPECT_NEAR(wadeTerminalSpeed(0.8f, kHW, kHH), 0.91f, 0.005f);
}

TEST(WadeDrag, TheAnswerDoesNotDependOnTheStepLength) {
    // one frame integrated as 1, 2, 4 or 8 kinematic sub-steps (dt 1/30 .. 1/240): the same speed and nearly the same distance
    const Walk ref = walk(0.8f, 2.0f, 3.0f, 1.0f / 240.0f, {}, {2.0f, 0.0f});
    for (float dt : {1.0f / 30.0f, 1.0f / 60.0f, 1.0f / 120.0f}) {
        const Walk w = walk(0.8f, 2.0f, 3.0f, dt, {}, {2.0f, 0.0f});
        EXPECT_NEAR(w.v.x, ref.v.x, 0.01f * ref.v.x) << "dt " << dt;
        EXPECT_NEAR(w.dist, ref.dist, 0.02f * ref.dist) << "dt " << dt;
    }
}

TEST(WadeDrag, TheOldTableCompoundsWithSubSteps) {
    // the red this law replaces (AnimatedVoxelCharacter, small-scale water plan 4.3): v.xz *= mix(1, 0.55, depth / hip) once per
    // kinematic sub-step, with v.xz re-derived from input only once per frame - a 1/15 s frame (2 sub-steps) waded slower than
    // a 1/60 s frame (1 sub-step). Kept as the control: the measure must see it.
    auto oldFrame = [](float depth, float want, int subSteps) {
        const float hip = std::max(0.6f, kHH), slow = 1.0f + (0.55f - 1.0f) * std::clamp(depth / hip, 0.0f, 1.0f);
        float v = want, dist = 0.0f;
        const float dt = 1.0f / 30.0f;
        for (int i = 0; i < subSteps; ++i) { v *= slow; dist += v * dt; }
        return dist / (subSteps * dt);   // the frame's mean speed
    };
    const float one = oldFrame(0.8f, 2.0f, 1), four = oldFrame(0.8f, 2.0f, 4);
    EXPECT_GT(one - four, 0.2f) << "the old table did not compound: " << one << " vs " << four;
}

TEST(WadeDrag, ACurrentIsHeldOrPushes) {
    // standing (want 0) in 0.8 m: drive holds against flow up to sqrt(2.5 / 3.0) = 0.91 m/s
    const Walk held = walk(0.8f, 0.0f, 6.0f, 1.0f / 60.0f, {0.5f, 0.0f});
    EXPECT_NEAR(held.v.x, 0.0f, 1e-4f) << "a 0.5 m/s current moved a standing wader";
    const Walk pushed = walk(0.8f, 0.0f, 6.0f, 1.0f / 60.0f, {1.5f, 0.0f});
    EXPECT_NEAR(pushed.v.x, 1.5f - 0.91f, 0.02f) << "a 1.5 m/s current should push the wader downstream";
    // airborne: no drive, the water carries the body toward its own velocity
    glm::vec2 v(2.0f, 0.0f);
    for (int i = 0; i < 600; ++i) v = wadeStep(v, {2.0f, 0.0f}, {0.0f, 0.0f}, 0.8f, kHW, kHH, false, 1.0f / 60.0f).velocity;
    EXPECT_LT(v.x, 0.2f) << "drag only, no drive in the air";
    EXPECT_GE(v.x, 0.0f) << "drag never reverses a body";
}

TEST(WadeDrag, DryLeavesTheVelocityAlone) {
    const auto r = wadeStep({0.3f, 0.1f}, {2.0f, -1.0f}, {5.0f, 0.0f}, 0.04f, kHW, kHH, true, 1.0f / 60.0f);
    EXPECT_FALSE(r.wet);
    EXPECT_EQ(r.velocity, glm::vec2(2.0f, -1.0f));
    EXPECT_FALSE(wadeStep({}, {1.0f, 0.0f}, {}, 0.0f, kHW, kHH, true, 1.0f / 60.0f).wet);
}

TEST(WadeDrag, ABiggerBodyWadesFaster) {
    // a wolf (0.15 x 0.4) and an ogre (0.5 x 1.6) in the same 0.5 m creek: drag per mass falls with size
    const float wolf = wadeTerminalSpeed(0.5f, 0.15f, 0.4f), adult = wadeTerminalSpeed(0.5f, kHW, kHH), ogre = wadeTerminalSpeed(0.5f, 0.5f, 1.6f);
    EXPECT_LT(wolf, adult);
    EXPECT_LT(adult, ogre);
}

TEST(WadeDrag, ALimbSolidKeepsItsVolume) {
    const glm::vec3 he(0.07f, 0.25f, 0.07f);   // a shin
    EXPECT_EQ(limbSolidHalfExtents(he, glm::quat(1, 0, 0, 0)), he);
    for (float deg : {10.0f, 30.0f, 45.0f, 80.0f}) {
        const glm::quat q = glm::angleAxis(glm::radians(deg), glm::normalize(glm::vec3(1.0f, 0.0f, 0.3f)));
        const glm::vec3 a = limbSolidHalfExtents(he, q);
        EXPECT_NEAR(a.x * a.y * a.z, he.x * he.y * he.z, 1e-7f) << deg;
        EXPECT_LT(a.y, he.y + 1e-6f) << "a leaning shin is shorter in y";
    }
    // a quarter turn about y swaps x and z
    const glm::vec3 t = limbSolidHalfExtents(glm::vec3(0.1f, 0.2f, 0.3f), glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0)));
    EXPECT_NEAR(t.x, 0.3f, 1e-5f); EXPECT_NEAR(t.z, 0.1f, 1e-5f);
}

TEST(WadeDrag, TheWakeBoxLeadsAWalker) {
    // 48.5 item 9: a walker at 2 m/s - the box reaches 1 m (0.5 s) past the controller's front, nothing behind
    const MovingSolid m = wadeWakeBox({10.0f, 16.0f, 5.0f}, kHW, kHH, {2.0f, 0.0f, 0.0f});
    EXPECT_NEAR(m.centre.x + m.halfExtents.x, 10.0f + kHW + 1.0f, 1e-5f);
    EXPECT_NEAR(m.centre.x - m.halfExtents.x, 10.0f - kHW, 1e-5f);
    EXPECT_NEAR(m.centre.y - m.halfExtents.y, 16.0f, 1e-5f);
    EXPECT_NEAR(m.halfExtents.z, kHW, 1e-6f);
}

TEST(WadeDrag, TheFeetColumnIsFloorAcrossSeams) {
    // 48.5 item 2: the column a character stands in, both sides of the chunk seams at 0 and 32 and at negative coordinates
    EXPECT_EQ(wadeColumn(-0.01f), -1);
    EXPECT_EQ(wadeColumn(0.0f), 0);
    EXPECT_EQ(wadeColumn(31.99f), 31);
    EXPECT_EQ(wadeColumn(32.0f), 32);
    EXPECT_EQ(wadeColumn(-32.01f), -33);
    EXPECT_EQ(wadeColumn(-31.99f), -32);
}

TEST(WadeDrag, TheFeetReadTheRunTheyAreIn) {
    // a pond 20..21 over a cave pool 10..10.5 (runs highest first): the depth is the run the feet are in, never the one above
    const float top[2] = {21.0f, 10.5f}, bottom[2] = {20.0f, 10.0f};
    EXPECT_NEAR(wadeDepthFromRuns(2, top, bottom, 20.05f), 1.0f, 1e-6f) << "standing on the pond floor";
    EXPECT_NEAR(wadeDepthFromRuns(2, top, bottom, 10.05f), 0.5f, 1e-6f) << "in the cave pool";
    EXPECT_EQ(wadeDepthFromRuns(2, top, bottom, 15.05f), 0.0f) << "a dry cave under the pond reads dry";
    EXPECT_EQ(wadeDepthFromRuns(2, top, bottom, 22.05f), 0.0f) << "above the pond";
    EXPECT_EQ(wadeDepthFromRuns(2, top, bottom, 5.05f), 0.0f) << "under every run";
}

TEST(WadeDrag, TheDepthComesFromTheRingNotTheWadersOwnCrest) {
    // 48.6: the centre column holds the crest the legs pile up (1.2 m in 0.8 m of water); the ring reads the pond round the body
    const float ring[4] = {0.84f, 0.76f, 0.80f, 0.80f};   // ahead (bow), behind (trough), the two sides
    EXPECT_NEAR(wadeRingDepth(1.2f, ring), 0.80f, 1e-6f);
    const float bank[4] = {0.0f, 0.3f, 0.0f, 0.3f};        // half the ring on dry land: only the wet points count
    EXPECT_NEAR(wadeRingDepth(0.3f, bank), 0.3f, 1e-6f);
    const float dryRing[4] = {0.0f, 0.0f, 0.0f, 0.0f};     // a puddle under the body only: its own reading
    EXPECT_NEAR(wadeRingDepth(0.2f, dryRing), 0.2f, 1e-6f);
    const float wetRing[4] = {0.5f, 0.5f, 0.5f, 0.5f};     // standing on a dry rock in a pond: the centre decides - dry
    EXPECT_EQ(wadeRingDepth(0.0f, wetRing), 0.0f);
}
