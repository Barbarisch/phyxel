#include <gtest/gtest.h>
#include "graphics/DayNightCycle.h"
#include <cmath>

using namespace Phyxel::Graphics;

TEST(DayNightCycleTest, DefaultState) {
    DayNightCycle cycle;
    EXPECT_FLOAT_EQ(cycle.getTimeOfDay(), 12.0f); // starts at noon
    EXPECT_FLOAT_EQ(cycle.getDayLengthSeconds(), 600.0f);
    EXPECT_FALSE(cycle.isEnabled());
    EXPECT_FALSE(cycle.isPaused());
    EXPECT_FLOAT_EQ(cycle.getTimeScale(), 1.0f);
}

TEST(DayNightCycleTest, SetTimeOfDay) {
    DayNightCycle cycle;
    cycle.setTimeOfDay(6.0f);
    EXPECT_FLOAT_EQ(cycle.getTimeOfDay(), 6.0f);
}

TEST(DayNightCycleTest, SetTimeWraps) {
    DayNightCycle cycle;
    cycle.setTimeOfDay(25.0f);
    EXPECT_FLOAT_EQ(cycle.getTimeOfDay(), 1.0f);
    cycle.setTimeOfDay(-2.0f);
    EXPECT_FLOAT_EQ(cycle.getTimeOfDay(), 22.0f);
}

TEST(DayNightCycleTest, UpdateAdvancesTime) {
    DayNightCycle cycle;
    cycle.setEnabled(true);
    cycle.setTimeOfDay(12.0f);
    cycle.setDayLengthSeconds(24.0f); // 1 second = 1 hour
    cycle.update(1.0f); // advance 1 second = 1 hour
    EXPECT_NEAR(cycle.getTimeOfDay(), 13.0f, 0.01f);
}

TEST(DayNightCycleTest, UpdateWrapsAt24) {
    DayNightCycle cycle;
    cycle.setEnabled(true);
    cycle.setTimeOfDay(23.5f);
    cycle.setDayLengthSeconds(24.0f);
    cycle.update(1.0f); // advance 1 hour
    EXPECT_NEAR(cycle.getTimeOfDay(), 0.5f, 0.01f);
}

TEST(DayNightCycleTest, DisabledDoesNotAdvance) {
    DayNightCycle cycle;
    cycle.setEnabled(false);
    cycle.setTimeOfDay(12.0f);
    cycle.update(100.0f);
    EXPECT_FLOAT_EQ(cycle.getTimeOfDay(), 12.0f);
}

TEST(DayNightCycleTest, PausedDoesNotAdvance) {
    DayNightCycle cycle;
    cycle.setEnabled(true);
    cycle.setPaused(true);
    cycle.setTimeOfDay(12.0f);
    cycle.update(100.0f);
    EXPECT_FLOAT_EQ(cycle.getTimeOfDay(), 12.0f);
}

TEST(DayNightCycleTest, TimeScaleAffectsSpeed) {
    DayNightCycle cycle;
    cycle.setEnabled(true);
    cycle.setTimeOfDay(12.0f);
    cycle.setDayLengthSeconds(24.0f);
    cycle.setTimeScale(2.0f);
    cycle.update(1.0f); // 2x speed, advance 2 hours
    EXPECT_NEAR(cycle.getTimeOfDay(), 14.0f, 0.01f);
}

TEST(DayNightCycleTest, NoonHasBrightAmbient) {
    DayNightCycle cycle;
    cycle.setTimeOfDay(12.0f);
    EXPECT_GT(cycle.getAmbientStrength(), 0.8f);
}

TEST(DayNightCycleTest, MidnightHasDimAmbient) {
    DayNightCycle cycle;
    cycle.setTimeOfDay(0.0f);
    EXPECT_LT(cycle.getAmbientStrength(), 0.15f);
}

TEST(DayNightCycleTest, NoonSunPointsDown) {
    DayNightCycle cycle;
    cycle.setTimeOfDay(12.0f);
    auto dir = cycle.getSunDirection();
    EXPECT_LT(dir.y, 0.0f); // Sun direction points downward
}

TEST(DayNightCycleTest, MidnightSunColorIsBlack) {
    DayNightCycle cycle;
    cycle.setTimeOfDay(0.0f);
    auto color = cycle.getSunColor();
    EXPECT_NEAR(color.r, 0.0f, 0.01f);
    EXPECT_NEAR(color.g, 0.0f, 0.01f);
    EXPECT_NEAR(color.b, 0.0f, 0.01f);
}

TEST(DayNightCycleTest, NoonSunColorIsBright) {
    DayNightCycle cycle;
    cycle.setTimeOfDay(12.0f);
    auto color = cycle.getSunColor();
    EXPECT_GT(color.r, 0.8f);
    EXPECT_GT(color.g, 0.8f);
    EXPECT_GT(color.b, 0.8f);
}

TEST(DayNightCycleTest, DayLengthMinimum) {
    DayNightCycle cycle;
    cycle.setDayLengthSeconds(0.0f);
    EXPECT_GE(cycle.getDayLengthSeconds(), 1.0f);
}

TEST(DayNightCycleTest, ToJsonAndBack) {
    DayNightCycle cycle;
    cycle.setTimeOfDay(8.5f);
    cycle.setDayLengthSeconds(300.0f);
    cycle.setTimeScale(3.0f);
    cycle.setEnabled(true);
    cycle.setPaused(true);

    auto j = cycle.toJson();

    DayNightCycle cycle2;
    cycle2.fromJson(j);
    EXPECT_FLOAT_EQ(cycle2.getTimeOfDay(), 8.5f);
    EXPECT_FLOAT_EQ(cycle2.getDayLengthSeconds(), 300.0f);
    EXPECT_FLOAT_EQ(cycle2.getTimeScale(), 3.0f);
    EXPECT_TRUE(cycle2.isEnabled());
    EXPECT_TRUE(cycle2.isPaused());
}

TEST(DayNightCycleTest, ToJsonContainsFields) {
    DayNightCycle cycle;
    cycle.setTimeOfDay(12.0f);
    auto j = cycle.toJson();

    EXPECT_TRUE(j.contains("timeOfDay"));
    EXPECT_TRUE(j.contains("dayLengthSeconds"));
    EXPECT_TRUE(j.contains("enabled"));
    EXPECT_TRUE(j.contains("paused"));
    EXPECT_TRUE(j.contains("timeScale"));
    EXPECT_TRUE(j.contains("sunDirection"));
    EXPECT_TRUE(j.contains("sunColor"));
    EXPECT_TRUE(j.contains("ambientStrength"));
}

TEST(DayNightCycleTest, DawnHasWarmSunColor) {
    DayNightCycle cycle;
    cycle.setTimeOfDay(6.5f); // just after dawn
    auto color = cycle.getSunColor();
    // Sun should be warm (more red than blue) at dawn
    EXPECT_GT(color.r, color.b);
}

TEST(DayNightCycleTest, FullDayCycleMonotonic) {
    // Ambient should rise from midnight to noon and fall from noon to midnight
    DayNightCycle cycle;
    cycle.setTimeOfDay(0.0f);
    float midnight = cycle.getAmbientStrength();
    cycle.setTimeOfDay(12.0f);
    float noon = cycle.getAmbientStrength();
    EXPECT_GT(noon, midnight);
}

// ============================================================================
// Day counter and world time helpers
// ============================================================================

TEST(DayNightCycleTest, DefaultDayNumber) {
    DayNightCycle cycle;
    EXPECT_EQ(cycle.getDayNumber(), 1);
}

TEST(DayNightCycleTest, DayNumberIncrements) {
    DayNightCycle cycle;
    cycle.setEnabled(true);
    cycle.setTimeOfDay(23.5f);
    cycle.setDayLengthSeconds(24.0f); // 1 real second = 1 game hour
    EXPECT_EQ(cycle.getDayNumber(), 1);
    cycle.update(1.0f); // advance 1 hour -> wraps past midnight
    EXPECT_NEAR(cycle.getTimeOfDay(), 0.5f, 0.01f);
    EXPECT_EQ(cycle.getDayNumber(), 2);
}

TEST(DayNightCycleTest, DayNumberMultipleWraps) {
    DayNightCycle cycle;
    cycle.setEnabled(true);
    cycle.setTimeOfDay(0.0f);
    cycle.setDayLengthSeconds(24.0f); // 1 sec = 1 hour
    cycle.update(48.0f); // advance 48 hours = 2 full days
    EXPECT_EQ(cycle.getDayNumber(), 3); // started at day 1, +2 wraps
}

TEST(DayNightCycleTest, SetDayNumber) {
    DayNightCycle cycle;
    cycle.setDayNumber(10);
    EXPECT_EQ(cycle.getDayNumber(), 10);
}

TEST(DayNightCycleTest, GetHourAndMinute) {
    DayNightCycle cycle;
    cycle.setTimeOfDay(14.5f); // 2:30 PM
    EXPECT_EQ(cycle.getHour(), 14);
    EXPECT_EQ(cycle.getMinute(), 30);
}

TEST(DayNightCycleTest, IsNight) {
    DayNightCycle cycle;
    cycle.setTimeOfDay(2.0f); // 2 AM
    EXPECT_TRUE(cycle.isNight());
    EXPECT_FALSE(cycle.isDay());

    cycle.setTimeOfDay(12.0f); // noon
    EXPECT_FALSE(cycle.isNight());
    EXPECT_TRUE(cycle.isDay());

    cycle.setTimeOfDay(20.0f); // 8 PM
    EXPECT_TRUE(cycle.isNight());
}

TEST(DayNightCycleTest, DayNumberSerializesRoundTrip) {
    DayNightCycle cycle;
    cycle.setTimeOfDay(8.5f);
    cycle.setDayNumber(5);
    cycle.setEnabled(true);

    auto j = cycle.toJson();
    EXPECT_TRUE(j.contains("dayNumber"));
    EXPECT_TRUE(j.contains("hour"));
    EXPECT_TRUE(j.contains("minute"));
    EXPECT_TRUE(j.contains("isNight"));
    EXPECT_EQ(j["dayNumber"].get<int>(), 5);

    DayNightCycle cycle2;
    cycle2.fromJson(j);
    EXPECT_EQ(cycle2.getDayNumber(), 5);
}
