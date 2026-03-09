#include "core/GameEventLog.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace Phyxel {
namespace Core {

GameEventLog::GameEventLog(size_t maxEvents)
    : m_maxEvents(maxEvents)
    , m_startTime(std::chrono::steady_clock::now())
{
}

void GameEventLog::emit(const std::string& type, const nlohmann::json& data) {
    std::lock_guard<std::mutex> lock(m_mutex);

    GameEvent event;
    event.id = m_nextId++;
    event.type = type;
    event.data = data;

    // Elapsed seconds since engine start
    auto now = std::chrono::steady_clock::now();
    event.timestamp = std::chrono::duration<double>(now - m_startTime).count();

    // ISO 8601 wall-clock timestamp
    auto wallNow = std::chrono::system_clock::now();
    auto wallTime = std::chrono::system_clock::to_time_t(wallNow);
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &wallTime);
#else
    localtime_r(&wallTime, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S");
    event.timestampISO = oss.str();

    m_events.push_back(std::move(event));

    // Evict oldest if over capacity
    while (m_events.size() > m_maxEvents) {
        m_events.pop_front();
    }
}

GameEventLog::PollResult GameEventLog::pollSince(uint64_t sinceId) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    PollResult result;
    result.nextCursor = m_nextId - 1; // Current latest ID

    if (m_events.empty()) {
        result.nextCursor = 0;
        return result;
    }

    // Binary search: find first event with id > sinceId
    // Events are always in ascending ID order
    for (const auto& event : m_events) {
        if (event.id > sinceId) {
            result.events.push_back(event);
        }
    }

    result.nextCursor = m_events.back().id;
    return result;
}

uint64_t GameEventLog::currentCursor() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_events.empty()) return 0;
    return m_events.back().id;
}

uint64_t GameEventLog::totalEmitted() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_nextId - 1;
}

size_t GameEventLog::size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_events.size();
}

void GameEventLog::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_events.clear();
}

nlohmann::json GameEventLog::eventToJson(const GameEvent& event) {
    return {
        {"id", event.id},
        {"type", event.type},
        {"data", event.data},
        {"timestamp", event.timestamp},
        {"time", event.timestampISO}
    };
}

} // namespace Core
} // namespace Phyxel
