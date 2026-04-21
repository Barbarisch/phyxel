#include <gtest/gtest.h>
#include "core/WorldClock.h"

using namespace Phyxel::Core;

// ---------------------------------------------------------------------------
// Day 0 baseline
// ---------------------------------------------------------------------------

TEST(WorldClockTest, Day0IsFirstOfFirstMonthYear1) {
    WorldClock clock;
    EXPECT_EQ(clock.getDayOfMonth(), 1);
    EXPECT_EQ(clock.getMonth(),      1);
    EXPECT_EQ(clock.getYear(),       1);
}

TEST(WorldClockTest, Day0DayOfWeekIsFirstday) {
    WorldClock clock;
    EXPECT_EQ(clock.getDayOfWeek(), 0);
    EXPECT_STREQ(WorldClock::dayOfWeekName(0), "Firstday");
}

// ---------------------------------------------------------------------------
// Day-of-month rollover
// ---------------------------------------------------------------------------

TEST(WorldClockTest, Day29IsLastOfFirstMonth) {
    WorldClock clock;
    clock.setTotalDays(29);
    EXPECT_EQ(clock.getDayOfMonth(), 30);
    EXPECT_EQ(clock.getMonth(),       1);
}

TEST(WorldClockTest, Day30IsFirstOfSecondMonth) {
    WorldClock clock;
    clock.setTotalDays(30);
    EXPECT_EQ(clock.getDayOfMonth(), 1);
    EXPECT_EQ(clock.getMonth(),      2);
}

TEST(WorldClockTest, Month12LastDay) {
    WorldClock clock;
    clock.setTotalDays(DAYS_PER_YEAR - 1);  // day 359
    EXPECT_EQ(clock.getDayOfMonth(), 30);
    EXPECT_EQ(clock.getMonth(),      12);
    EXPECT_EQ(clock.getYear(),        1);
}

TEST(WorldClockTest, NewYearRollover) {
    WorldClock clock;
    clock.setTotalDays(DAYS_PER_YEAR);  // day 360 = start of year 2
    EXPECT_EQ(clock.getDayOfMonth(), 1);
    EXPECT_EQ(clock.getMonth(),      1);
    EXPECT_EQ(clock.getYear(),       2);
}

// ---------------------------------------------------------------------------
// advanceDays
// ---------------------------------------------------------------------------

TEST(WorldClockTest, AdvanceDaysWorks) {
    WorldClock clock;
    clock.setTotalDays(0);
    clock.advanceDays(35);
    EXPECT_EQ(clock.getTotalDays(), 35);
    EXPECT_EQ(clock.getMonth(),      2);
    EXPECT_EQ(clock.getDayOfMonth(), 6);
}

TEST(WorldClockTest, AdvanceNegativeIgnored) {
    WorldClock clock;
    clock.setTotalDays(10);
    clock.advanceDays(-5);
    EXPECT_EQ(clock.getTotalDays(), 10);
}

// ---------------------------------------------------------------------------
// Seasons
// ---------------------------------------------------------------------------

TEST(WorldClockTest, WinterAtYearStart) {
    WorldClock clock;
    clock.setTotalDays(0);   // month 1
    EXPECT_EQ(clock.getSeason(), Season::Winter);
}

TEST(WorldClockTest, WinterEarlyYear) {
    WorldClock clock;
    clock.setTotalDays(59);  // day 60, last day of month 2
    EXPECT_EQ(clock.getSeason(), Season::Winter);
}

TEST(WorldClockTest, SpringMonth3) {
    WorldClock clock;
    clock.setTotalDays(60);  // day 61, first day of month 3
    EXPECT_EQ(clock.getSeason(), Season::Spring);
}

TEST(WorldClockTest, SummerMonth6) {
    WorldClock clock;
    clock.setTotalDays(150); // month 6
    EXPECT_EQ(clock.getSeason(), Season::Summer);
}

TEST(WorldClockTest, AutumnMonth9) {
    WorldClock clock;
    clock.setTotalDays(240); // month 9
    EXPECT_EQ(clock.getSeason(), Season::Autumn);
}

TEST(WorldClockTest, WinterMonth12) {
    WorldClock clock;
    clock.setTotalDays(330); // month 12
    EXPECT_EQ(clock.getSeason(), Season::Winter);
}

TEST(WorldClockTest, SeasonNamesNonEmpty) {
    EXPECT_NE(std::string(WorldClock::seasonName(Season::Spring)), "");
    EXPECT_NE(std::string(WorldClock::seasonName(Season::Summer)), "");
    EXPECT_NE(std::string(WorldClock::seasonName(Season::Autumn)), "");
    EXPECT_NE(std::string(WorldClock::seasonName(Season::Winter)), "");
}

// ---------------------------------------------------------------------------
// Moon phases
// ---------------------------------------------------------------------------

TEST(WorldClockTest, Day0IsNewMoon) {
    WorldClock clock;
    EXPECT_EQ(clock.getMoonPhase(), MoonPhase::NewMoon);
}

TEST(WorldClockTest, Day14IsFullMoon) {
    WorldClock clock;
    clock.setTotalDays(14);
    EXPECT_EQ(clock.getMoonPhase(), MoonPhase::FullMoon);
}

TEST(WorldClockTest, LunarCycleRepeats) {
    WorldClock clock;
    clock.setTotalDays(0);
    MoonPhase phase0 = clock.getMoonPhase();
    clock.setTotalDays(LUNAR_CYCLE_DAYS);
    EXPECT_EQ(clock.getMoonPhase(), phase0);
}

TEST(WorldClockTest, AllMoonPhasesReachable) {
    WorldClock clock;
    bool seen[8] = {};
    for (int d = 0; d < LUNAR_CYCLE_DAYS; ++d) {
        clock.setTotalDays(d);
        seen[static_cast<int>(clock.getMoonPhase())] = true;
    }
    for (int i = 0; i < 8; ++i)
        EXPECT_TRUE(seen[i]) << "Moon phase " << i << " never seen";
}

TEST(WorldClockTest, MoonPhaseNamesNonEmpty) {
    EXPECT_NE(std::string(WorldClock::moonPhaseName(MoonPhase::FullMoon)),    "");
    EXPECT_NE(std::string(WorldClock::moonPhaseName(MoonPhase::NewMoon)),     "");
    EXPECT_NE(std::string(WorldClock::moonPhaseName(MoonPhase::FirstQuarter)),"");
}

// ---------------------------------------------------------------------------
// Day of week
// ---------------------------------------------------------------------------

TEST(WorldClockTest, WeekCycles) {
    WorldClock clock;
    for (int d = 0; d < 14; ++d) {
        clock.setTotalDays(d);
        EXPECT_EQ(clock.getDayOfWeek(), d % 7);
    }
}

TEST(WorldClockTest, DayOfWeekNamesAllNonEmpty) {
    for (int i = 0; i < DAYS_PER_WEEK; ++i)
        EXPECT_NE(std::string(WorldClock::dayOfWeekName(i)), "") << "Day " << i;
}

// ---------------------------------------------------------------------------
// Month names
// ---------------------------------------------------------------------------

TEST(WorldClockTest, MonthNamesAll12NonEmpty) {
    for (int m = 1; m <= 12; ++m)
        EXPECT_NE(std::string(WorldClock::monthName(m)), "") << "Month " << m;
}

TEST(WorldClockTest, MonthNameOutOfRange) {
    EXPECT_STREQ(WorldClock::monthName(0),  "Unknown");
    EXPECT_STREQ(WorldClock::monthName(13), "Unknown");
}

// ---------------------------------------------------------------------------
// CalendarDate
// ---------------------------------------------------------------------------

TEST(WorldClockTest, CalendarDateFieldsMatchQueries) {
    WorldClock clock;
    clock.setTotalDays(195);  // into month 7 of year 1
    auto date = clock.getDate();
    EXPECT_EQ(date.day,   clock.getDayOfMonth());
    EXPECT_EQ(date.month, clock.getMonth());
    EXPECT_EQ(date.year,  clock.getYear());
}

TEST(WorldClockTest, CalendarDateLongStringContainsMonthAndYear) {
    WorldClock clock;
    clock.setTotalDays(180);  // month 7 = Fireharvest
    auto date = clock.getDate();
    std::string s = date.toLongString();
    EXPECT_NE(s.find("Year"), std::string::npos);
    EXPECT_NE(s.find(WorldClock::monthName(date.month)), std::string::npos);
}

TEST(WorldClockTest, CalendarDateShortStringFormat) {
    WorldClock clock;
    clock.setTotalDays(0);
    auto date = clock.getDate();
    std::string s = date.toShortString();
    // Should contain "/" separators
    EXPECT_NE(s.find("/"), std::string::npos);
}

// ---------------------------------------------------------------------------
// StartYear
// ---------------------------------------------------------------------------

TEST(WorldClockTest, StartYearOffsets) {
    WorldClock clock;
    clock.setStartYear(400);
    EXPECT_EQ(clock.getYear(), 400);

    clock.setTotalDays(DAYS_PER_YEAR);
    EXPECT_EQ(clock.getYear(), 401);
}

// ---------------------------------------------------------------------------
// Time of day
// ---------------------------------------------------------------------------

TEST(WorldClockTest, HourOfDayFromFraction) {
    WorldClock clock;
    clock.setTimeOfDayFraction(0.5f);  // noon
    EXPECT_EQ(clock.getHourOfDay(), 12);

    clock.setTimeOfDayFraction(0.0f);  // midnight
    EXPECT_EQ(clock.getHourOfDay(), 0);

    clock.setTimeOfDayFraction(0.75f); // 18:00
    EXPECT_EQ(clock.getHourOfDay(), 18);
}

// ---------------------------------------------------------------------------
// Holidays
// ---------------------------------------------------------------------------

TEST(WorldClockTest, DefaultHolidaysPresent) {
    WorldClock clock;
    EXPECT_FALSE(clock.getHolidays().empty());
}

TEST(WorldClockTest, IsHolidayOnNewYearsDay) {
    WorldClock clock;
    clock.setTotalDays(0);  // month 1, day 1 = New Year's Dawn
    EXPECT_TRUE(clock.isHoliday());
    EXPECT_FALSE(clock.getHolidayName().empty());
}

TEST(WorldClockTest, IsNotHolidayOnNormalDay) {
    WorldClock clock;
    clock.setTotalDays(2);  // month 1, day 3 — no default holiday
    EXPECT_FALSE(clock.isHoliday());
    EXPECT_EQ(clock.getHolidayName(), "");
}

TEST(WorldClockTest, AddCustomHoliday) {
    WorldClock clock;
    clock.addHoliday({"Dragon Festival", 6, 15});

    // Navigate to month 6, day 15
    // month 6 starts at day 150; day 15 → total day 164
    clock.setTotalDays(150 + 14);  // day 165 (1-indexed day 15 of month 6)
    EXPECT_TRUE(clock.isHoliday());
    EXPECT_EQ(clock.getHolidayName(), "Dragon Festival");
}

// ---------------------------------------------------------------------------
// JSON round-trip
// ---------------------------------------------------------------------------

TEST(WorldClockTest, JsonRoundTrip) {
    WorldClock clock;
    clock.setTotalDays(423);
    clock.setTimeOfDayFraction(0.6f);
    clock.setStartYear(300);
    clock.addHoliday({"Test Day", 3, 10});

    auto j = clock.toJson();
    WorldClock clock2;
    clock2.fromJson(j);

    EXPECT_EQ(clock2.getTotalDays(),          423);
    EXPECT_FLOAT_EQ(clock2.getTimeOfDayFraction(), 0.6f);
    EXPECT_EQ(clock2.getStartYear(),          300);
    // Holidays restored
    bool found = false;
    for (const auto& h : clock2.getHolidays())
        if (h.name == "Test Day") found = true;
    EXPECT_TRUE(found);
}

TEST(WorldClockTest, JsonRestoresDate) {
    WorldClock clock;
    clock.setTotalDays(200);
    auto original = clock.getDate();

    auto j = clock.toJson();
    WorldClock clock2;
    clock2.fromJson(j);

    EXPECT_EQ(clock2.getDate(), original);
}
