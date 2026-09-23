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

// ---------------------------------------------------------------------------------------------
// applySettings (G-165): the authored game.json block reaches the cycle. Partial blocks must
// touch ONLY the fields they set, and the field order must let time-of-day see the day number.
// ---------------------------------------------------------------------------------------------

TEST(DayNightCycleTest, ApplySettingsEnablesTheCycleWithOnlyEnabledSet) {
    DayNightCycle cycle;
    ASSERT_FALSE(cycle.isEnabled()) << "the whole G-165 bug is that this default is never changed";
    Phyxel::Core::DayNightSettings s;
    s.enabled = true;
    cycle.applySettings(s);
    EXPECT_TRUE(cycle.isEnabled());
    // Everything else must be untouched by a block that did not mention it.
    EXPECT_FLOAT_EQ(cycle.getTimeOfDay(), 12.0f);
    EXPECT_FLOAT_EQ(cycle.getDayLengthSeconds(), 600.0f);
    EXPECT_FLOAT_EQ(cycle.getTimeScale(), 1.0f);
    EXPECT_FALSE(cycle.isPaused());
}

TEST(DayNightCycleTest, ApplySettingsSetsEveryAuthoredField) {
    DayNightCycle cycle;
    Phyxel::Core::DayNightSettings s;
    s.enabled = true; s.timeOfDay = 9.0f; s.dayLengthSeconds = 1200.0f;
    s.timeScale = 2.0f; s.paused = true; s.dayNumber = 3;
    cycle.applySettings(s);
    EXPECT_TRUE(cycle.isEnabled());
    EXPECT_FLOAT_EQ(cycle.getTimeOfDay(), 9.0f);
    EXPECT_FLOAT_EQ(cycle.getDayLengthSeconds(), 1200.0f);
    EXPECT_FLOAT_EQ(cycle.getTimeScale(), 2.0f);
    EXPECT_TRUE(cycle.isPaused());
    EXPECT_EQ(cycle.getDayNumber(), 3);
}

TEST(DayNightCycleTest, AnAppliedEnabledCycleActuallyAdvances) {
    // The observable the user reported: the clock does not move. After applying the Ravenmere
    // block (1200 s day) and ticking 60 s, the clock must read 1.2 hours later.
    DayNightCycle cycle;
    Phyxel::Core::DayNightSettings s;
    s.enabled = true; s.timeOfDay = 9.0f; s.dayLengthSeconds = 1200.0f;
    cycle.applySettings(s);
    for (int i = 0; i < 60; ++i) cycle.update(1.0f);
    EXPECT_NEAR(cycle.getTimeOfDay(), 9.0f + 24.0f * 60.0f / 1200.0f, 1e-3f);
}

TEST(DayNightCycleTest, ApplySettingsRoundTripsThroughJson) {
    Phyxel::Core::DayNightSettings s;
    s.enabled = true; s.timeOfDay = 18.25f; s.dayLengthSeconds = 900.0f;
    const auto back = Phyxel::Core::DayNightSettings::fromJson(s.toJson());
    EXPECT_EQ(back.enabled, s.enabled);
    EXPECT_EQ(back.timeOfDay, s.timeOfDay);
    EXPECT_EQ(back.dayLengthSeconds, s.dayLengthSeconds);
    EXPECT_FALSE(back.paused.has_value());
}

// ---------------------------------------------------------------------------------------------
// tick() (G-165 part 2): the cycle times ITSELF. The frame loop used to feed it
// `now - frameStartTime` with a 0.1 s clamp, and only the editor ever set frameStartTime, so a
// shipped game clamped EVERY frame and the clock ran at 0.1 s x FPS - measured 2.89x real time
// at 28.2 FPS on Ravenmere. These pin real-time behaviour against a fake clock.
// ---------------------------------------------------------------------------------------------

namespace {
using Tp = DayNightCycle::Clock::time_point;
Tp at(double seconds) {
    return Tp{} + std::chrono::duration_cast<DayNightCycle::Clock::duration>(
                      std::chrono::duration<double>(seconds));
}
DayNightCycle runningCycle() {
    DayNightCycle c;
    c.setEnabled(true);
    c.setTimeOfDay(9.0f);
    c.setDayLengthSeconds(1200.0f);   // Ravenmere's authored day
    return c;
}
}  // namespace

TEST(DayNightCycleTest, TheFirstTickAdvancesNothing) {
    DayNightCycle c = runningCycle();
    c.tick(at(1000.0));   // there is no previous stamp to measure from
    EXPECT_FLOAT_EQ(c.getTimeOfDay(), 9.0f);
}

TEST(DayNightCycleTest, TicksAdvanceByRealElapsedTimeNotByTheClamp) {
    // 60 frames at 60 FPS = 1.0 real second = 24/1200 h. The pre-fix defect would have charged
    // 0.1 s per frame = 6.0 s of game time - six times too fast.
    DayNightCycle c = runningCycle();
    double t = 5.0;
    c.tick(at(t));
    for (int i = 0; i < 60; ++i) { t += 1.0 / 60.0; c.tick(at(t)); }
    const float expected = 9.0f + 24.0f * 1.0f / 1200.0f;
    EXPECT_NEAR(c.getTimeOfDay(), expected, 1e-4f)
        << "a stale/absent frame stamp must not turn every frame into the 0.1 s cap";
}

TEST(DayNightCycleTest, TheClockRateDoesNotDependOnFrameRate) {
    // Same real second, 10 FPS vs 120 FPS: the clock must land in the same place.
    DayNightCycle slow = runningCycle(), fast = runningCycle();
    double t = 0.0; slow.tick(at(t));
    for (int i = 0; i < 10; ++i) { t += 0.1; slow.tick(at(t)); }
    t = 0.0; fast.tick(at(t));
    for (int i = 0; i < 120; ++i) { t += 1.0 / 120.0; fast.tick(at(t)); }
    EXPECT_NEAR(slow.getTimeOfDay(), fast.getTimeOfDay(), 1e-4f)
        << "the shipped bug was exactly this: 0.1 s x FPS";
}

TEST(DayNightCycleTest, AStallAdvancesAtMostTheCap) {
    DayNightCycle c = runningCycle();
    c.tick(at(0.0));
    c.tick(at(5.0));   // a 5 s hitch (debugger, load) must not skip an hour
    EXPECT_NEAR(c.getTimeOfDay(), 9.0f + 24.0f * DayNightCycle::kMaxTickSeconds / 1200.0f, 1e-5f);
}

TEST(DayNightCycleTest, AClockThatGoesBackwardsAdvancesNothing) {
    DayNightCycle c = runningCycle();
    c.tick(at(10.0));
    c.tick(at(9.0));
    EXPECT_FLOAT_EQ(c.getTimeOfDay(), 9.0f);
}

TEST(DayNightCycleTest, TickRespectsEnabledAndPaused) {
    DayNightCycle c;   // disabled by default
    c.setTimeOfDay(9.0f);
    c.tick(at(0.0)); c.tick(at(0.05));
    EXPECT_FLOAT_EQ(c.getTimeOfDay(), 9.0f) << "disabled: no advance";
    c.setEnabled(true); c.setPaused(true);
    c.tick(at(0.10)); c.tick(at(0.15));
    EXPECT_FLOAT_EQ(c.getTimeOfDay(), 9.0f) << "paused: no advance";
    c.setPaused(false);
    c.tick(at(0.20));
    EXPECT_GT(c.getTimeOfDay(), 9.0f) << "resumed: advances again";
}
