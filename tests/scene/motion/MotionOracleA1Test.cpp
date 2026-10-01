#include <gtest/gtest.h>

#include "scene/motion/MotionOracle.h"
#include "scene/motion/MotionOracleSampling.h"
#include "graphics/AnimationSystem.h"

#include <cmath>
#include <map>
#include <string>

using namespace Phyxel::Scene::Motion;

// ============================================================================
// A1 — MotionOracle as THE oracle (docs/AnimationSystemV3Plan.md §4 A1).
// Every metric added here is shown to FLAG a seeded defect and to PASS the shipped
// mocap set. The humanoid tests are a cross-language pin: the C++ stance-feet estimate
// must reproduce tools/anim_pipeline/anim_lint.py (walk est 1.667 u/s vs authored 1.679,
// residual 0.172 — measured 2026-09-29 at the 3 cm window).
// ============================================================================

namespace {

// Two-joint rig: root (0) above a foot (1). Frames at 60 Hz.
OracleFrame frame(glm::vec3 foot, glm::vec3 root = {0, 1, 0}) {
    OracleFrame f;
    f.localRotations = {glm::quat(1, 0, 0, 0), glm::quat(1, 0, 0, 0)};
    f.worldJointPositions = {root, foot};
    return f;
}

constexpr float kDt = 1.0f / 60.0f;

struct HumanoidRig {
    Phyxel::Skeleton skeleton;
    std::vector<Phyxel::AnimationClip> clips;
    Phyxel::VoxelModel model;
    bool ok = false;
    HumanoidRig() {
        Phyxel::AnimationSystem sys;
        ok = sys.loadFromFile("resources/animated_characters/humanoid.anim", skeleton, clips, model);
    }
    const Phyxel::AnimationClip* clip(const std::string& name) const {
        for (const auto& c : clips) if (c.name == name) return &c;
        return nullptr;
    }
};

const HumanoidRig& humanoid() {
    static HumanoidRig rig;   // parsed once for the whole suite (~seconds)
    return rig;
}

MotionOracleMetrics evalHumanoidClip(const std::string& name, float authoredSpeedOverride = -1.0f,
                                     OracleTerrain ground = {}) {
    const auto& rig = humanoid();
    const auto* clip = rig.clip(name);
    if (!clip) return {};
    SampledClip s = sampleClip(rig.skeleton, *clip, 60.0f);
    OracleOptions opt;
    opt.footJoints = findFeet(rig.skeleton);
    opt.kneeChains = legChainsForFeet(rig.skeleton, opt.footJoints);
    opt.authoredSpeed = authoredSpeedOverride > 0.0f ? authoredSpeedOverride : clip->speed;
    opt.ground = std::move(ground);
    return evaluateMotion(s.frames, s.secondsPerFrame, s.chainEdges, opt);
}

} // namespace

// ---------------------------------------------------------------------------
// Planted derivation: the calibrated 3 cm rule.
// ---------------------------------------------------------------------------
TEST(MotionOracleA1, PlantedJointsDeriveFromFootHeightWindow) {
    std::vector<OracleFrame> frames = {
        frame({0, 0.00f, 0}), frame({0, 0.02f, 0}), frame({0, 0.05f, 0}), frame({0, 0.20f, 0}), frame({0, 0.01f, 0})};
    derivePlantedJoints(frames, {1}, 0.03f);
    EXPECT_TRUE(frames[0].plantedJoints[1]);
    EXPECT_TRUE(frames[1].plantedJoints[1]);
    EXPECT_FALSE(frames[2].plantedJoints[1]) << "0.05 is outside the 3 cm window";
    EXPECT_FALSE(frames[3].plantedJoints[1]);
    EXPECT_TRUE(frames[4].plantedJoints[1]);
    EXPECT_FALSE(frames[0].plantedJoints[0]) << "the root is not a foot";
}

// ---------------------------------------------------------------------------
// Penetration / float against a declared terrain. Seeded: a foot 0.10 below ground fails
// penetration; a planted foot resting 0.30 above ground fails float; a grounded foot passes.
// ---------------------------------------------------------------------------
TEST(MotionOracleA1, PenetrationAndFloatAreMeasuredAgainstTerrain) {
    OracleOptions opt;
    opt.footJoints = {1};
    opt.ground = [](float, float) { return 0.0f; };

    std::vector<OracleFrame> sunk = {frame({0, -0.10f, 0}), frame({0, -0.10f, 0}), frame({0, -0.10f, 0})};
    auto m = evaluateMotion(sunk, kDt, {{0, 1}}, opt);
    ASSERT_TRUE(m.valid);
    EXPECT_NEAR(m.maxPenetration, 0.10f, 1e-5f);

    std::vector<OracleFrame> floating = {frame({0, 0.30f, 0}), frame({0, 0.30f, 0}), frame({0, 0.30f, 0})};
    m = evaluateMotion(floating, kDt, {{0, 1}}, opt);
    EXPECT_NEAR(m.maxStanceFloat, 0.30f, 1e-5f) << "a constant-height foot is planted by definition, 0.30 above ground";
    EXPECT_FLOAT_EQ(m.maxPenetration, 0.0f);

    std::vector<OracleFrame> grounded = {frame({0, 0.0f, 0}), frame({0, 0.0f, 0}), frame({0, 0.0f, 0})};
    m = evaluateMotion(grounded, kDt, {{0, 1}}, opt);
    EXPECT_FLOAT_EQ(m.maxPenetration, 0.0f);
    EXPECT_FLOAT_EQ(m.maxStanceFloat, 0.0f);
    EXPECT_TRUE(m.terrainEvaluated);
}

// ---------------------------------------------------------------------------
// Terrain seam equality (FloraMarginTest shape): the ground function presented whole and
// presented as two halves split at an arbitrary seam must give identical metrics.
// ---------------------------------------------------------------------------
TEST(MotionOracleA1, TerrainSplitAtASeamDoesNotChangeTheMetrics) {
    auto slope = [](float x, float) { return 0.25f * x; };
    auto split = [](float x, float z) {                    // same slope, evaluated per "chunk"
        return x < 0.37f ? 0.25f * x : 0.25f * (x - 0.37f) + 0.25f * 0.37f;
    };
    std::vector<OracleFrame> frames;
    for (int i = 0; i < 40; ++i) {
        const float x = -0.5f + i * 0.03f;
        frames.push_back(frame({x, 0.25f * x - 0.02f * (i % 3), 0}, {x, 1.0f, 0}));
    }
    OracleOptions a; a.footJoints = {1}; a.ground = slope;
    OracleOptions b; b.footJoints = {1}; b.ground = split;
    const auto ma = evaluateMotion(frames, kDt, {{0, 1}}, a);
    const auto mb = evaluateMotion(frames, kDt, {{0, 1}}, b);
    ASSERT_TRUE(ma.valid && mb.valid);
    EXPECT_FLOAT_EQ(ma.maxPenetration, mb.maxPenetration);
    EXPECT_FLOAT_EQ(ma.maxStanceFloat, mb.maxStanceFloat);
    EXPECT_GT(ma.maxPenetration, 0.0f) << "control: the seeded 2 cm dips must register";
}

// ---------------------------------------------------------------------------
// A5: planting is TERRAIN-RELATIVE when a ground function is given. A foot walking DOWN a ramp
// plants at ever-lower heights; the height-only rule marks only the lowest stance as planted
// (the control shows it), the terrain rule marks every stance.
// ---------------------------------------------------------------------------
TEST(MotionOracleA1, PlantingIsTerrainRelativeWhenAGroundFunctionIsGiven) {
    auto ramp = [](float x, float) { return -0.3f * std::floor(x); };   // 0.3 u risers down along +x
    std::vector<OracleFrame> frames;
    // three stances (x = 0.5, 1.5, 2.5) of 4 frames each, foot exactly on its riser, with a
    // swing frame 0.2 u up between them
    for (int cell = 0; cell < 3; ++cell) {
        const float x = cell + 0.5f, g = -0.3f * cell;
        for (int i = 0; i < 4; ++i) frames.push_back(frame({x, g, 0}, {x, g + 1.0f, 0}));
        frames.push_back(frame({x + 0.5f, g + 0.2f, 0}, {x + 0.5f, g + 1.2f, 0}));
    }
    std::vector<OracleFrame> byHeight = frames, byTerrain = frames;
    derivePlantedJoints(byHeight, {1}, 0.03f);          // joint 1 = the foot (0 = root)
    derivePlantedJointsOnTerrain(byTerrain, {1}, 0.03f, ramp);
    int plantedH = 0, plantedT = 0;
    for (const auto& f : byHeight)  plantedH += f.plantedJoints[1] ? 1 : 0;
    for (const auto& f : byTerrain) plantedT += f.plantedJoints[1] ? 1 : 0;
    EXPECT_EQ(plantedH, 4)  << "control: height-only planting keeps only the lowest stance";
    EXPECT_EQ(plantedT, 12) << "terrain-relative planting keeps all three stances";
    // and the clearance reference removes a constant ankle height from float/penetration
    OracleOptions o; o.footJoints = {1}; o.ground = ramp; o.footClearanceRef = 0.0f;
    std::vector<OracleFrame> lifted = frames;
    for (auto& f : lifted) for (auto& p : f.worldJointPositions) p.y += 0.1f;     // ankle 0.1 above the sole
    const auto raw = evaluateMotion(lifted, kDt, {{0, 1}}, o);
    o.footClearanceRef = 0.1f;
    const auto ref = evaluateMotion(lifted, kDt, {{0, 1}}, o);
    ASSERT_TRUE(raw.valid && ref.valid);
    EXPECT_NEAR(raw.maxStanceFloat, 0.1f, 1e-4f);
    EXPECT_NEAR(ref.maxStanceFloat, 0.0f, 1e-4f);
}

// ---------------------------------------------------------------------------
// Knee inversion. Reference: knee forward (+Z) of the hip–ankle line. Seeded: a frame with the
// knee behind the line by 0.05 → inversion 0.05; a leg that only bends forward → 0.
// ---------------------------------------------------------------------------
TEST(MotionOracleA1, KneeBehindTheHipAnkleLineIsAnInversion) {
    auto leg = [](float kneeZ) {
        OracleFrame f;
        f.localRotations = {glm::quat(1,0,0,0), glm::quat(1,0,0,0), glm::quat(1,0,0,0)};
        f.worldJointPositions = {{0, 1.0f, 0}, {0, 0.5f, kneeZ}, {0, 0.0f, 0}};
        return f;
    };
    OracleOptions opt;
    opt.kneeChains = {{0, 1, 2}};
    auto good = evaluateMotion({leg(0.05f), leg(0.08f), leg(0.05f)}, kDt, {}, opt);
    ASSERT_TRUE(good.valid);
    EXPECT_FLOAT_EQ(good.maxKneeInversion, 0.0f);
    auto bad = evaluateMotion({leg(0.05f), leg(-0.05f), leg(0.05f)}, kDt, {}, opt);
    EXPECT_NEAR(bad.maxKneeInversion, 0.05f, 1e-5f);
}

// ---------------------------------------------------------------------------
// Self-intersection: non-adjacent boxes overlapping is flagged; adjacent pairs are allowed.
// ---------------------------------------------------------------------------
TEST(MotionOracleA1, NonAdjacentBoxOverlapIsFlagged) {
    OracleFrame f = frame({0, 0, 0});
    f.boxes = {{{0, 0, 0}, {1, 1, 1}}, {{0.5f, 0.5f, 0.5f}, {1.5f, 1.5f, 1.5f}}};   // overlap 0.125
    OracleOptions opt;
    auto m = evaluateMotion({f, f}, kDt, {}, opt);
    EXPECT_NEAR(m.maxBoxOverlap, 0.125f, 1e-5f);
    opt.boxAdjacency = {{0, 1}};
    m = evaluateMotion({f, f}, kDt, {}, opt);
    EXPECT_FLOAT_EQ(m.maxBoxOverlap, 0.0f) << "adjacent segments may overlap at the joint";
}

// ---------------------------------------------------------------------------
// Cross-language pin on the shipped humanoid walk (headless sampling of the .anim file).
// Python (anim_lint.foot_slide_metrics, 2026-09-29): est 1.667 u/s, residual 0.172 u/s,
// authored Speed 1.679 → 1 % mismatch. The C++ port must agree to three decimals.
// ---------------------------------------------------------------------------
TEST(MotionOracleA1, HumanoidWalkStanceEstimateMatchesThePythonLinter) {
    ASSERT_TRUE(humanoid().ok) << "humanoid.anim must parse (run from the repo root)";
    const auto m = evalHumanoidClip("walk");
    ASSERT_TRUE(m.valid);
    ASSERT_GE(m.stanceSamples, 6);
    EXPECT_NEAR(m.stanceBodySpeed, 1.667f, 0.005f) << "stance-feet body speed diverged from anim_lint";
    EXPECT_NEAR(m.stanceResidual, 0.172f, 0.01f);
    EXPECT_GT(m.stanceBodyVelocity.y, 1.0f) << "walk travels +Z (model forward)";
    EXPECT_LT(std::fabs(m.stanceBodyVelocity.x), 0.2f);
    EXPECT_LT(m.speedMismatch, 0.05f);
}

TEST(MotionOracleA1, HumanoidDoubledSpeedLineIsFlaggedAsSkate) {
    ASSERT_TRUE(humanoid().ok);
    const auto* walk = humanoid().clip("walk");
    ASSERT_NE(walk, nullptr);
    const auto m = evalHumanoidClip("walk", walk->speed * 2.0f);
    ASSERT_TRUE(m.valid);
    EXPECT_GT(m.speedMismatch, 0.45f) << "the RED case: a Speed line twice what the feet imply";
}

TEST(MotionOracleA1, ShippedLocomotionClipsAgreeWithTheirSpeedLines) {
    ASSERT_TRUE(humanoid().ok);
    // Control set identical to tests/test_anim_foot_slide.py (mismatch 1–14 %, residual ≤ 0.40×speed).
    for (const char* name : {"walk", "run", "fast_run", "unarmed_walk", "crouched_walking",
                             "left_strafe", "right_strafe", "walking_backward"}) {
        SCOPED_TRACE(name);
        const auto m = evalHumanoidClip(name);
        ASSERT_TRUE(m.valid);
        ASSERT_GE(m.stanceSamples, 6);
        EXPECT_LE(m.speedMismatch, 0.25f) << "est " << m.stanceBodySpeed;
        EXPECT_LE(m.stanceResidual / m.stanceBodySpeed, 0.40f);
        // Mocap legs never invert. Strafes bend the knee sideways (≈ zero forward component), so
        // allow sub-threshold sign flicker; 0.02 is the live FAIL band.
        EXPECT_LT(m.maxKneeInversion, 0.02f) << "mocap legs never invert (knee chain = hip, knee, ankle)";
        EXPECT_LT(m.maxPoseDeltaRadians, glm::radians(120.0f));
    }
}

TEST(MotionOracleA1, HumanoidWalkOnFlatGroundHasNoPenetrationAndReportsFloatForCalibration) {
    ASSERT_TRUE(humanoid().ok);
    // Model-space clip over a flat floor at the clip's own lowest foot sample: penetration must
    // be ~0; the float figure is REPORTED (the calibration input for the WARN band), not judged.
    const auto& rig = humanoid();
    const auto* walk = rig.clip("walk");
    SampledClip s = sampleClip(rig.skeleton, *walk, 60.0f);
    const auto feet = findFeet(rig.skeleton);
    float floorY = 1e9f;
    for (const auto& f : s.frames) for (auto j : feet) floorY = std::min(floorY, f.worldJointPositions[j].y);
    OracleOptions opt;
    opt.footJoints = feet;
    opt.ground = [floorY](float, float) { return floorY; };
    const auto m = evaluateMotion(s.frames, s.secondsPerFrame, s.chainEdges, opt);
    ASSERT_TRUE(m.valid && m.terrainEvaluated);
    EXPECT_LT(m.maxPenetration, 1e-4f);
    EXPECT_LT(m.maxStanceFloat, 0.03f + 1e-4f) << "planted feet sit inside the window by construction";
}
