#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Phyxel::Core {

// ---------------------------------------------------------------------------
// Calendar constants — 360-day year (12 months × 30 days)
// ---------------------------------------------------------------------------

constexpr int DAYS_PER_MONTH  = 30;
constexpr int MONTHS_PER_YEAR = 12;
constexpr int DAYS_PER_YEAR   = DAYS_PER_MONTH * MONTHS_PER_YEAR;   // 360
constexpr int DAYS_PER_WEEK   = 7;
constexpr int DAYS_PER_SEASON = 90;    // 3 months per season
constexpr int LUNAR_CYCLE_DAYS = 28;   // Moon repeats every 28 days

// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------

enum class Season { Spring, Summer, Autumn, Winter };

enum class MoonPhase {
    NewMoon,
    WaxingCrescent,
    FirstQuarter,
    WaxingGibbous,
    FullMoon,
    WaningGibbous,
    LastQuarter,
    WaningCrescent
};

// ---------------------------------------------------------------------------
// CalendarDate — a human-readable date in the world calendar
// ---------------------------------------------------------------------------

struct CalendarDate {
    int day;    ///< 1–30 within the month
    int month;  ///< 1–12
    int year;   ///< Absolute year (total_days / DAYS_PER_YEAR + 1 at campaign start)

    /// Month name (e.g. "Deepwinter", "Highsun").
    std::string monthName() const;

    /// Short numeric date string: "15/06/423"
    std::string toShortString() const;

    /// Long descriptive string: "15 Highsun, Year 423"
    std::string toLongString() const;

    bool operator==(const CalendarDate& o) const {
        return day == o.day && month == o.month && year == o.year;
    }
};

// ---------------------------------------------------------------------------
// Holiday — named recurring events keyed to month/day
// ---------------------------------------------------------------------------

struct Holiday {
    std::string name;
    int month;  ///< 1–12
    int day;    ///< 1–30
};

// ---------------------------------------------------------------------------
// WorldClock
// ---------------------------------------------------------------------------

/// Layered world calendar. Does not depend on DayNightCycle — it works from
/// a simple integer day counter (total days elapsed since campaign start).
/// Feed it DayNightCycle::getDayNumber() to keep them in sync.
class WorldClock {
public:
    WorldClock();

    // -----------------------------------------------------------------------
    // Day counter
    // -----------------------------------------------------------------------

    /// Set absolute day number (0 = campaign start).
    void setTotalDays(int days);
    int  getTotalDays() const { return m_totalDays; }

    /// Advance by N in-world days.
    void advanceDays(int count);

    // -----------------------------------------------------------------------
    // Calendar queries
    // -----------------------------------------------------------------------

    CalendarDate getDate() const;

    int getDayOfMonth() const;   ///< 1–30
    int getMonth() const;        ///< 1–12
    int getYear() const;         ///< Campaign year (starts at m_startYear)

    int getDayOfWeek() const;    ///< 0–6 (0 = Firstday)
    Season    getSeason() const;
    MoonPhase getMoonPhase() const;

    // -----------------------------------------------------------------------
    // Time-of-day integration (optional, fed from DayNightCycle)
    // -----------------------------------------------------------------------

    /// Set fractional time of day (0.0–1.0, where 0 = midnight).
    void setTimeOfDayFraction(float t) { m_timeOfDayFraction = t; }
    float getTimeOfDayFraction() const { return m_timeOfDayFraction; }

    /// Approximate hour-of-day (0–23) derived from fraction.
    int getHourOfDay() const { return static_cast<int>(m_timeOfDayFraction * 24.0f) % 24; }

    // -----------------------------------------------------------------------
    // Name helpers
    // -----------------------------------------------------------------------

    static const char* seasonName(Season s);
    static const char* moonPhaseName(MoonPhase p);
    static const char* dayOfWeekName(int dayOfWeek);   ///< 0–6
    static const char* monthName(int month);            ///< 1–12

    // -----------------------------------------------------------------------
    // Holidays
    // -----------------------------------------------------------------------

    /// Register a holiday (recurring annual event).
    void addHoliday(Holiday h);

    /// True if today is a registered holiday.
    bool isHoliday() const;

    /// Name of today's holiday, or "" if none.
    std::string getHolidayName() const;

    const std::vector<Holiday>& getHolidays() const { return m_holidays; }

    // -----------------------------------------------------------------------
    // Rest helpers (for RestSystem integration)
    // -----------------------------------------------------------------------

    /// Returns the campaign start year used for date display.
    int getStartYear() const { return m_startYear; }
    void setStartYear(int year) { m_startYear = year; }

    // -----------------------------------------------------------------------
    // Serialization
    // -----------------------------------------------------------------------

    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

private:
    int   m_totalDays           = 0;
    float m_timeOfDayFraction   = 0.25f;  // 6:00 AM default
    int   m_startYear           = 1;

    std::vector<Holiday> m_holidays;
};

} // namespace Phyxel::Core
