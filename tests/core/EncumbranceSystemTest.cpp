#include <gtest/gtest.h>
#include "core/EncumbranceSystem.h"

using namespace Phyxel::Core;

// ---------------------------------------------------------------------------
// Capacity thresholds
// ---------------------------------------------------------------------------

TEST(EncumbranceSystemTest, CarryCapacity) {
    EXPECT_FLOAT_EQ(EncumbranceSystem::carryCapacity(10), 150.0f);  // STR 10 × 15
    EXPECT_FLOAT_EQ(EncumbranceSystem::carryCapacity(15), 225.0f);
    EXPECT_FLOAT_EQ(EncumbranceSystem::carryCapacity(20), 300.0f);
}

TEST(EncumbranceSystemTest, EncumberedThreshold) {
    EXPECT_FLOAT_EQ(EncumbranceSystem::encumberedThreshold(10), 50.0f);   // STR 10 × 5
    EXPECT_FLOAT_EQ(EncumbranceSystem::encumberedThreshold(15), 75.0f);
}

TEST(EncumbranceSystemTest, HeavilyEncumberedThreshold) {
    EXPECT_FLOAT_EQ(EncumbranceSystem::heavilyEncumberedThreshold(10), 100.0f);  // STR 10 × 10
    EXPECT_FLOAT_EQ(EncumbranceSystem::heavilyEncumberedThreshold(15), 150.0f);
}

// ---------------------------------------------------------------------------
// getLevel
// ---------------------------------------------------------------------------

TEST(EncumbranceSystemTest, UnencumberedAtOrBelowThreshold) {
    // STR 10: threshold = 50 lbs
    EXPECT_EQ(EncumbranceSystem::getLevel(0.0f,  10), EncumbranceLevel::Unencumbered);
    EXPECT_EQ(EncumbranceSystem::getLevel(50.0f, 10), EncumbranceLevel::Unencumbered);
}

TEST(EncumbranceSystemTest, EncumberedAboveFirstThreshold) {
    // STR 10: 50 < x ≤ 100
    EXPECT_EQ(EncumbranceSystem::getLevel(51.0f,  10), EncumbranceLevel::Encumbered);
    EXPECT_EQ(EncumbranceSystem::getLevel(100.0f, 10), EncumbranceLevel::Encumbered);
}

TEST(EncumbranceSystemTest, HeavilyEncumberedAboveSecondThreshold) {
    // STR 10: x > 100
    EXPECT_EQ(EncumbranceSystem::getLevel(100.1f, 10), EncumbranceLevel::HeavilyEncumbered);
    EXPECT_EQ(EncumbranceSystem::getLevel(150.0f, 10), EncumbranceLevel::HeavilyEncumbered);
}

TEST(EncumbranceSystemTest, HighStrengthMoveThresholds) {
    // STR 20: thresholds at 100 / 200 lbs
    EXPECT_EQ(EncumbranceSystem::getLevel(100.0f, 20), EncumbranceLevel::Unencumbered);
    EXPECT_EQ(EncumbranceSystem::getLevel(101.0f, 20), EncumbranceLevel::Encumbered);
    EXPECT_EQ(EncumbranceSystem::getLevel(200.1f, 20), EncumbranceLevel::HeavilyEncumbered);
}

// ---------------------------------------------------------------------------
// speedPenalty
// ---------------------------------------------------------------------------

TEST(EncumbranceSystemTest, SpeedPenaltyUnencumbered) {
    EXPECT_EQ(EncumbranceSystem::speedPenalty(EncumbranceLevel::Unencumbered), 0);
}

TEST(EncumbranceSystemTest, SpeedPenaltyEncumbered) {
    EXPECT_EQ(EncumbranceSystem::speedPenalty(EncumbranceLevel::Encumbered), -10);
}

TEST(EncumbranceSystemTest, SpeedPenaltyHeavilyEncumbered) {
    EXPECT_EQ(EncumbranceSystem::speedPenalty(EncumbranceLevel::HeavilyEncumbered), -20);
}

// ---------------------------------------------------------------------------
// hasPhysicalDisadvantage
// ---------------------------------------------------------------------------

TEST(EncumbranceSystemTest, NormalEncumbranceNoDisadvantage) {
    EXPECT_FALSE(EncumbranceSystem::hasPhysicalDisadvantage(EncumbranceLevel::Unencumbered));
    EXPECT_FALSE(EncumbranceSystem::hasPhysicalDisadvantage(EncumbranceLevel::Encumbered));
}

TEST(EncumbranceSystemTest, HeavilyEncumberedHasDisadvantage) {
    EXPECT_TRUE(EncumbranceSystem::hasPhysicalDisadvantage(EncumbranceLevel::HeavilyEncumbered));
}

// ---------------------------------------------------------------------------
// adjustedSpeed
// ---------------------------------------------------------------------------

TEST(EncumbranceSystemTest, AdjustedSpeedNoPenalty) {
    EXPECT_EQ(EncumbranceSystem::adjustedSpeed(30, 20.0f, 10), 30);
}

TEST(EncumbranceSystemTest, AdjustedSpeedEncumberedMinus10) {
    // STR 10, carry 60 lbs → Encumbered → -10 ft
    EXPECT_EQ(EncumbranceSystem::adjustedSpeed(30, 60.0f, 10), 20);
}

TEST(EncumbranceSystemTest, AdjustedSpeedHeavilyEncumberedMinus20) {
    // STR 10, carry 110 lbs → HeavilyEncumbered → -20 ft
    EXPECT_EQ(EncumbranceSystem::adjustedSpeed(30, 110.0f, 10), 10);
}

TEST(EncumbranceSystemTest, AdjustedSpeedClampsToZero) {
    // STR 1, carry 50 lbs → HeavilyEncumbered (threshold 10 lbs) → -20, base=15 → max(0,−5)=0
    EXPECT_EQ(EncumbranceSystem::adjustedSpeed(15, 50.0f, 1), 0);
}

// ---------------------------------------------------------------------------
// pushDragLift
// ---------------------------------------------------------------------------

TEST(EncumbranceSystemTest, PushDragLiftMediumCreature) {
    // STR 10: 10 × 15 = 150 lbs
    EXPECT_FLOAT_EQ(EncumbranceSystem::pushDragLift(10, false), 150.0f);
}

TEST(EncumbranceSystemTest, PushDragLiftLargeCreature) {
    // Large+ creatures: STR × 30
    EXPECT_FLOAT_EQ(EncumbranceSystem::pushDragLift(10, true), 300.0f);
}

TEST(EncumbranceSystemTest, PushDragLiftHighStrength) {
    EXPECT_FLOAT_EQ(EncumbranceSystem::pushDragLift(20, false), 300.0f);
    EXPECT_FLOAT_EQ(EncumbranceSystem::pushDragLift(20, true),  600.0f);
}
