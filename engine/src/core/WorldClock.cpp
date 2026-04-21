#include "core/WorldClock.h"
#include <sstream>
#include <iomanip>

namespace Phyxel::Core {

// ---------------------------------------------------------------------------
// Month names (fantasy calendar)
// ---------------------------------------------------------------------------

static const char* s_monthNames[12] = {
    "Deepwinter",   // 1
    "Firstthaw",    // 2
    "Budding",      // 3
    "Greenleaf",    // 4
    "Bloomtide",    // 5
    "Highsun",      // 6
    "Fireharvest",  // 7
    "Goldfall",     // 8
    "Harvestend",   // 9
    "Frostmere",    // 10
    "Snowmantle",   // 11
    "Midwinter",    // 12
};

static const char* s_dayNames[7] = {
    "Firstday",   // 0
    "Moonday",    // 1
    "Towerday",   // 2
    "Wanderday",  // 3
    "Fireday",    // 4
    "Starday",    // 5
    "Sunday",     // 6
};

// ---------------------------------------------------------------------------
// CalendarDate
// ---------------------------------------------------------------------------

std::string CalendarDate::monthName() const {
    return WorldClock::monthName(month);
}

std::string CalendarDate::toShortString() const {
    std::ostringstream oss;
    oss << std::setw(2) << std::setfill('0') << day   << "/"
        << std::setw(2) << std::setfill('0') << month << "/"
        << year;
    return oss.str();
}

std::string CalendarDate::toLongString() const {
    std::ostringstream oss;
    oss << day << " " << WorldClock::monthName(month) << ", Year " << year;
    return oss.str();
}

// ---------------------------------------------------------------------------
// WorldClock
// ---------------------------------------------------------------------------

WorldClock::WorldClock() {
    // Default holidays
    m_holidays = {
        { "Midwinter Festival",  12,  1 },
        { "Spring Equinox",       3, 20 },
        { "Midsummer Feast",      6, 21 },
        { "Harvest Day",          9, 22 },
        { "Day of the Dead",     10, 31 },
        { "New Year's Dawn",      1,  1 },
    };
}

void WorldClock::setTotalDays(int days) {
    m_totalDays = (days < 0) ? 0 : days;
}

void WorldClock::advanceDays(int count) {
    m_totalDays += (count > 0) ? count : 0;
}

// ---------------------------------------------------------------------------
// Calendar queries
// ---------------------------------------------------------------------------

int WorldClock::getDayOfMonth() const {
    return (m_totalDays % DAYS_PER_YEAR) % DAYS_PER_MONTH + 1;
}

int WorldClock::getMonth() const {
    return (m_totalDays % DAYS_PER_YEAR) / DAYS_PER_MONTH + 1;
}

int WorldClock::getYear() const {
    return m_startYear + m_totalDays / DAYS_PER_YEAR;
}

CalendarDate WorldClock::getDate() const {
    return { getDayOfMonth(), getMonth(), getYear() };
}

int WorldClock::getDayOfWeek() const {
    return m_totalDays % DAYS_PER_WEEK;
}

Season WorldClock::getSeason() const {
    int dayOfYear = m_totalDays % DAYS_PER_YEAR;  // 0-based
    // Spring: days 60–149 (months 3–5)
    // Summer: days 150–239 (months 6–8)
    // Autumn: days 240–329 (months 9–11)
    // Winter: days 0–59 and 330–359 (months 1–2, 12)
    if (dayOfYear < 60)  return Season::Winter;
    if (dayOfYear < 150) return Season::Spring;
    if (dayOfYear < 240) return Season::Summer;
    if (dayOfYear < 330) return Season::Autumn;
    return Season::Winter;
}

MoonPhase WorldClock::getMoonPhase() const {
    int lunarDay = m_totalDays % LUNAR_CYCLE_DAYS;  // 0–27
    if (lunarDay < 2)  return MoonPhase::NewMoon;
    if (lunarDay < 6)  return MoonPhase::WaxingCrescent;
    if (lunarDay < 8)  return MoonPhase::FirstQuarter;
    if (lunarDay < 13) return MoonPhase::WaxingGibbous;
    if (lunarDay < 16) return MoonPhase::FullMoon;
    if (lunarDay < 20) return MoonPhase::WaningGibbous;
    if (lunarDay < 22) return MoonPhase::LastQuarter;
    return MoonPhase::WaningCrescent;
}

// ---------------------------------------------------------------------------
// Name helpers
// ---------------------------------------------------------------------------

const char* WorldClock::seasonName(Season s) {
    switch (s) {
        case Season::Spring: return "Spring";
        case Season::Summer: return "Summer";
        case Season::Autumn: return "Autumn";
        case Season::Winter: return "Winter";
    }
    return "Unknown";
}

const char* WorldClock::moonPhaseName(MoonPhase p) {
    switch (p) {
        case MoonPhase::NewMoon:        return "New Moon";
        case MoonPhase::WaxingCrescent: return "Waxing Crescent";
        case MoonPhase::FirstQuarter:   return "First Quarter";
        case MoonPhase::WaxingGibbous:  return "Waxing Gibbous";
        case MoonPhase::FullMoon:       return "Full Moon";
        case MoonPhase::WaningGibbous:  return "Waning Gibbous";
        case MoonPhase::LastQuarter:    return "Last Quarter";
        case MoonPhase::WaningCrescent: return "Waning Crescent";
    }
    return "Unknown";
}

const char* WorldClock::dayOfWeekName(int dayOfWeek) {
    if (dayOfWeek < 0 || dayOfWeek >= DAYS_PER_WEEK) return "Unknown";
    return s_dayNames[dayOfWeek];
}

const char* WorldClock::monthName(int month) {
    if (month < 1 || month > 12) return "Unknown";
    return s_monthNames[month - 1];
}

// ---------------------------------------------------------------------------
// Holidays
// ---------------------------------------------------------------------------

void WorldClock::addHoliday(Holiday h) {
    m_holidays.push_back(std::move(h));
}

bool WorldClock::isHoliday() const {
    int dom = getDayOfMonth();
    int mon = getMonth();
    for (const auto& h : m_holidays)
        if (h.month == mon && h.day == dom) return true;
    return false;
}

std::string WorldClock::getHolidayName() const {
    int dom = getDayOfMonth();
    int mon = getMonth();
    for (const auto& h : m_holidays)
        if (h.month == mon && h.day == dom) return h.name;
    return "";
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

nlohmann::json WorldClock::toJson() const {
    nlohmann::json j;
    j["totalDays"]          = m_totalDays;
    j["timeOfDayFraction"]  = m_timeOfDayFraction;
    j["startYear"]          = m_startYear;

    nlohmann::json holidays = nlohmann::json::array();
    for (const auto& h : m_holidays) {
        holidays.push_back({ {"name", h.name}, {"month", h.month}, {"day", h.day} });
    }
    j["holidays"] = holidays;
    return j;
}

void WorldClock::fromJson(const nlohmann::json& j) {
    m_totalDays         = j.value("totalDays", 0);
    m_timeOfDayFraction = j.value("timeOfDayFraction", 0.25f);
    m_startYear         = j.value("startYear", 1);

    if (j.contains("holidays") && j["holidays"].is_array()) {
        m_holidays.clear();
        for (const auto& hj : j["holidays"]) {
            m_holidays.push_back({
                hj.value("name",  ""),
                hj.value("month", 1),
                hj.value("day",   1)
            });
        }
    }
}

} // namespace Phyxel::Core
