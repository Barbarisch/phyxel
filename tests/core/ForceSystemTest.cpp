#include <gtest/gtest.h>
#include "core/ForceSystem.h"
#include <glm/glm.hpp>
#include <cmath>
#include <thread>

using namespace Phyxel;

// ============================================================================
// ForceConfig Tests
// ============================================================================

TEST(ForceConfigTest, DefaultValues) {
    ForceSystem::ForceConfig config;
    EXPECT_FLOAT_EQ(config.baseForce, 250.0f);
    EXPECT_FLOAT_EQ(config.velocityMultiplier, 100.0f);
    EXPECT_FLOAT_EQ(config.maxForce, 1000.0f);
    EXPECT_FLOAT_EQ(config.forceFalloffRate, 0.7f);
    EXPECT_EQ(config.maxPropagationDistance, 4);
    EXPECT_FLOAT_EQ(config.propagationThreshold, 50.0f);
    EXPECT_FLOAT_EQ(config.bondBreakingThreshold, 100.0f);
}

TEST(ForceConfigTest, SetAndGetConfig) {
    ForceSystem fs;
    ForceSystem::ForceConfig custom;
    custom.baseForce = 500.0f;
    custom.maxForce = 2000.0f;
    custom.maxPropagationDistance = 8;

    fs.setConfig(custom);
    const auto& retrieved = fs.getConfig();
    EXPECT_FLOAT_EQ(retrieved.baseForce, 500.0f);
    EXPECT_FLOAT_EQ(retrieved.maxForce, 2000.0f);
    EXPECT_EQ(retrieved.maxPropagationDistance, 8);
}

// ============================================================================
// ClickForce Calculation Tests
// ============================================================================

class ClickForceTest : public ::testing::Test {
protected:
    ForceSystem forceSystem;
    glm::vec3 rayOrigin{0.0f, 10.0f, 0.0f};
    glm::vec3 rayDirection{0.0f, -1.0f, 0.0f};  // Pointing down
    glm::vec3 hitPoint{0.0f, 0.0f, 0.0f};
};

TEST_F(ClickForceTest, ZeroVelocityGivesBaseForce) {
    auto force = forceSystem.calculateClickForce(
        glm::vec2(0.0f, 0.0f), rayOrigin, rayDirection, hitPoint);

    EXPECT_FLOAT_EQ(force.magnitude, 250.0f);  // baseForce default
}

TEST_F(ClickForceTest, VelocityIncreasesForce) {
    glm::vec2 mouseVel(10.0f, 0.0f);  // 10 px/s horizontal
    auto force = forceSystem.calculateClickForce(mouseVel, rayOrigin, rayDirection, hitPoint);

    // baseForce (250) + mouseSpeed(10) * velocityMultiplier(100) = 1250, clamped to 1000
    EXPECT_FLOAT_EQ(force.magnitude, 1000.0f);
}

TEST_F(ClickForceTest, SmallVelocityAddsToBase) {
    glm::vec2 mouseVel(2.0f, 0.0f);  // 2 px/s
    auto force = forceSystem.calculateClickForce(mouseVel, rayOrigin, rayDirection, hitPoint);

    // baseForce (250) + 2 * 100 = 450
    EXPECT_FLOAT_EQ(force.magnitude, 450.0f);
}

TEST_F(ClickForceTest, ForceClampsToMax) {
    glm::vec2 mouseVel(100.0f, 0.0f);  // Very fast mouse
    auto force = forceSystem.calculateClickForce(mouseVel, rayOrigin, rayDirection, hitPoint);

    EXPECT_FLOAT_EQ(force.magnitude, 1000.0f);  // Clamped to maxForce
}

TEST_F(ClickForceTest, DirectionIsNormalizedRayDirection) {
    glm::vec3 unnormalized(3.0f, 4.0f, 0.0f);  // length = 5
    auto force = forceSystem.calculateClickForce(
        glm::vec2(0.0f), rayOrigin, unnormalized, hitPoint);

    EXPECT_NEAR(force.direction.x, 0.6f, 1e-5f);
    EXPECT_NEAR(force.direction.y, 0.8f, 1e-5f);
    EXPECT_NEAR(force.direction.z, 0.0f, 1e-5f);
}

TEST_F(ClickForceTest, ImpactPointPassedThrough) {
    glm::vec3 hp(5.0f, 3.0f, -2.0f);
    auto force = forceSystem.calculateClickForce(
        glm::vec2(0.0f), rayOrigin, rayDirection, hp);

    EXPECT_EQ(force.impactPoint, hp);
}

TEST_F(ClickForceTest, DiagonalMouseVelocity) {
    glm::vec2 mouseVel(3.0f, 4.0f);  // speed = 5
    auto force = forceSystem.calculateClickForce(mouseVel, rayOrigin, rayDirection, hitPoint);

    // baseForce (250) + 5 * 100 = 750
    EXPECT_FLOAT_EQ(force.magnitude, 750.0f);
}

TEST_F(ClickForceTest, CustomConfigApplied) {
    ForceSystem::ForceConfig cfg;
    cfg.baseForce = 100.0f;
    cfg.velocityMultiplier = 50.0f;
    cfg.maxForce = 500.0f;
    forceSystem.setConfig(cfg);

    glm::vec2 mouseVel(4.0f, 0.0f);  // speed = 4
    auto force = forceSystem.calculateClickForce(mouseVel, rayOrigin, rayDirection, hitPoint);

    // 100 + 4*50 = 300
    EXPECT_FLOAT_EQ(force.magnitude, 300.0f);
}

// ============================================================================
// Static Utility Tests
// ============================================================================

TEST(ForceSystemStaticTest, WorldPositionToDirection_PositiveX) {
    auto dir = ForceSystem::worldPositionToDirection({0,0,0}, {1,0,0});
    EXPECT_NEAR(dir.x, 1.0f, 1e-5f);
    EXPECT_NEAR(dir.y, 0.0f, 1e-5f);
    EXPECT_NEAR(dir.z, 0.0f, 1e-5f);
}

TEST(ForceSystemStaticTest, WorldPositionToDirection_NegativeY) {
    auto dir = ForceSystem::worldPositionToDirection({5,10,5}, {5,9,5});
    EXPECT_NEAR(dir.x, 0.0f, 1e-5f);
    EXPECT_NEAR(dir.y, -1.0f, 1e-5f);
    EXPECT_NEAR(dir.z, 0.0f, 1e-5f);
}

TEST(ForceSystemStaticTest, WorldPositionToDirection_Diagonal) {
    auto dir = ForceSystem::worldPositionToDirection({0,0,0}, {1,1,0});
    float expected = 1.0f / std::sqrt(2.0f);
    EXPECT_NEAR(dir.x, expected, 1e-5f);
    EXPECT_NEAR(dir.y, expected, 1e-5f);
    EXPECT_NEAR(dir.z, 0.0f, 1e-5f);
}

TEST(ForceSystemStaticTest, WorldPositionToDirection_SamePosition) {
    auto dir = ForceSystem::worldPositionToDirection({3,3,3}, {3,3,3});
    EXPECT_FLOAT_EQ(dir.x, 0.0f);
    EXPECT_FLOAT_EQ(dir.y, 0.0f);
    EXPECT_FLOAT_EQ(dir.z, 0.0f);
}

TEST(ForceSystemStaticTest, GetDirectionBetweenCubes_AllSixDirections) {
    EXPECT_EQ(ForceSystem::getDirectionBetweenCubes({0,0,0}, {1,0,0}), BondDirection::POSITIVE_X);
    EXPECT_EQ(ForceSystem::getDirectionBetweenCubes({0,0,0}, {-1,0,0}), BondDirection::NEGATIVE_X);
    EXPECT_EQ(ForceSystem::getDirectionBetweenCubes({0,0,0}, {0,1,0}), BondDirection::POSITIVE_Y);
    EXPECT_EQ(ForceSystem::getDirectionBetweenCubes({0,0,0}, {0,-1,0}), BondDirection::NEGATIVE_Y);
    EXPECT_EQ(ForceSystem::getDirectionBetweenCubes({0,0,0}, {0,0,1}), BondDirection::POSITIVE_Z);
    EXPECT_EQ(ForceSystem::getDirectionBetweenCubes({0,0,0}, {0,0,-1}), BondDirection::NEGATIVE_Z);
}

TEST(ForceSystemStaticTest, CalculateDistanceFalloff_DistanceZero) {
    // 0.7^0 = 1.0
    EXPECT_FLOAT_EQ(ForceSystem::calculateDistanceFalloff(0, 0.7f), 1.0f);
}

TEST(ForceSystemStaticTest, CalculateDistanceFalloff_DistanceOne) {
    EXPECT_FLOAT_EQ(ForceSystem::calculateDistanceFalloff(1, 0.7f), 0.7f);
}

TEST(ForceSystemStaticTest, CalculateDistanceFalloff_DistanceThree) {
    float expected = std::pow(0.7f, 3.0f);  // 0.343
    EXPECT_NEAR(ForceSystem::calculateDistanceFalloff(3, 0.7f), expected, 1e-5f);
}

TEST(ForceSystemStaticTest, CalculateDistanceFalloff_FullFalloff) {
    // 0.0^1 = 0
    EXPECT_FLOAT_EQ(ForceSystem::calculateDistanceFalloff(1, 0.0f), 0.0f);
}

TEST(ForceSystemStaticTest, CalculateDistanceFalloff_NoFalloff) {
    // 1.0^5 = 1.0
    EXPECT_FLOAT_EQ(ForceSystem::calculateDistanceFalloff(5, 1.0f), 1.0f);
}

// ============================================================================
// PropagationResult Tests
// ============================================================================

TEST(PropagationResultTest, DefaultsEmpty) {
    ForceSystem::PropagationResult result;
    EXPECT_TRUE(result.brokenCubes.empty());
    EXPECT_TRUE(result.damagedCubes.empty());
    EXPECT_FLOAT_EQ(result.totalEnergyDissipated, 0.0f);
}

// ============================================================================
// MouseVelocityTracker Tests
// ============================================================================

TEST(MouseVelocityTrackerTest, InitialVelocityIsZero) {
    MouseVelocityTracker tracker;
    EXPECT_FLOAT_EQ(tracker.getSpeed(), 0.0f);
    EXPECT_EQ(tracker.getVelocity(), glm::vec2(0.0f));
}

TEST(MouseVelocityTrackerTest, SingleSampleVelocityIsZero) {
    MouseVelocityTracker tracker;
    tracker.updatePosition(100.0, 200.0);
    EXPECT_FLOAT_EQ(tracker.getSpeed(), 0.0f);
}

TEST(MouseVelocityTrackerTest, ResetClearsVelocity) {
    MouseVelocityTracker tracker;
    tracker.updatePosition(0.0, 0.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    tracker.updatePosition(100.0, 0.0);
    // Velocity should be non-zero now
    tracker.reset();
    EXPECT_FLOAT_EQ(tracker.getSpeed(), 0.0f);
    EXPECT_EQ(tracker.getVelocity(), glm::vec2(0.0f));
}

TEST(MouseVelocityTrackerTest, TwoSamplesProducesVelocity) {
    MouseVelocityTracker tracker;
    tracker.updatePosition(0.0, 0.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    tracker.updatePosition(100.0, 0.0);
    // Velocity should be positive in X
    EXPECT_GT(tracker.getVelocity().x, 0.0f);
    EXPECT_GT(tracker.getSpeed(), 0.0f);
}

TEST(MouseVelocityTrackerTest, SpeedReturnsLengthOfVelocity) {
    MouseVelocityTracker tracker;
    tracker.updatePosition(0.0, 0.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    tracker.updatePosition(60.0, 80.0);  // displacement (60, 80), magnitude 100
    float speed = tracker.getSpeed();
    float velLen = glm::length(tracker.getVelocity());
    EXPECT_NEAR(speed, velLen, 1e-3f);
}

TEST(MouseVelocityTrackerTest, ResetThenNewSamplesWork) {
    MouseVelocityTracker tracker;
    tracker.updatePosition(0.0, 0.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    tracker.updatePosition(50.0, 0.0);
    tracker.reset();

    // After reset, single sample should give zero velocity
    tracker.updatePosition(200.0, 200.0);
    EXPECT_FLOAT_EQ(tracker.getSpeed(), 0.0f);

    // Second sample after reset should produce velocity
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    tracker.updatePosition(300.0, 200.0);
    EXPECT_GT(tracker.getSpeed(), 0.0f);
}

// ============================================================================
// Bond Tests (using Cube directly)
// ============================================================================

TEST(BondTest, DefaultBondStrength) {
    Bond bond;
    EXPECT_FLOAT_EQ(bond.strength, 100.0f);
    EXPECT_FLOAT_EQ(bond.accumulatedForce, 0.0f);
    EXPECT_FALSE(bond.isBroken);
}

TEST(BondTest, ShouldBreakWhenForceExceedsStrength) {
    Bond bond(80.0f);
    bond.addForce(80.0f);
    EXPECT_TRUE(bond.shouldBreak());
}

TEST(BondTest, ShouldNotBreakBelowStrength) {
    Bond bond(100.0f);
    bond.addForce(50.0f);
    EXPECT_FALSE(bond.shouldBreak());
}

TEST(BondTest, BrokenBondShouldNotBreakAgain) {
    Bond bond(50.0f);
    bond.breakBond();
    bond.addForce(999.0f);
    EXPECT_FALSE(bond.shouldBreak());  // Already broken
}

TEST(BondTest, RepairResetsBond) {
    Bond bond(50.0f);
    bond.addForce(100.0f);
    bond.breakBond();
    bond.repair();
    EXPECT_FALSE(bond.isBroken);
    EXPECT_FLOAT_EQ(bond.accumulatedForce, 0.0f);
}

// ============================================================================
// Cube Bond Integration Tests
// ============================================================================

TEST(CubeBondTest, InitialBrokenBondCountIsZero) {
    Cube cube({0,0,0});
    EXPECT_EQ(cube.getNumberOfBrokenBonds(), 0);
    EXPECT_FALSE(cube.hasAnyBrokenBonds());
}

TEST(CubeBondTest, BreakingBondsIncrementsCount) {
    Cube cube({0,0,0});
    cube.breakBond(BondDirection::POSITIVE_X);
    EXPECT_EQ(cube.getNumberOfBrokenBonds(), 1);
    EXPECT_TRUE(cube.hasAnyBrokenBonds());

    cube.breakBond(BondDirection::NEGATIVE_Y);
    EXPECT_EQ(cube.getNumberOfBrokenBonds(), 2);
}

TEST(CubeBondTest, ResetBondForcesClearsAllForces) {
    Cube cube({0,0,0});
    cube.addForceToDirection(BondDirection::POSITIVE_X, 50.0f);
    cube.addForceToDirection(BondDirection::NEGATIVE_Z, 30.0f);
    cube.resetBondForces();
    EXPECT_FLOAT_EQ(cube.getAccumulatedForce(BondDirection::POSITIVE_X), 0.0f);
    EXPECT_FLOAT_EQ(cube.getAccumulatedForce(BondDirection::NEGATIVE_Z), 0.0f);
}

TEST(CubeBondTest, InitializeBondsSetsUniformStrength) {
    Cube cube({0,0,0});
    cube.initializeBonds(200.0f);
    for (int i = 0; i < 6; ++i) {
        EXPECT_FLOAT_EQ(cube.getBondStrength(static_cast<BondDirection>(i)), 200.0f);
    }
}

TEST(CubeBondTest, GetBrokenBondDirections) {
    Cube cube({0,0,0});
    cube.breakBond(BondDirection::POSITIVE_Y);
    cube.breakBond(BondDirection::NEGATIVE_Z);
    auto dirs = cube.getBrokenBondDirections();
    EXPECT_EQ(dirs.size(), 2u);
}
