#pragma once

#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <chrono>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

// ============================================================================
// GameEventLog
//
// A thread-safe, bounded log of game events designed for AI agent polling.
// Each event has a monotonically increasing ID (cursor) so agents can request
// "give me everything since cursor N" without missing or duplicating events.
//
// Usage:
//   eventLog->emit("entity_spawned", {{"id","npc_01"},{"type","physics"}});
//   eventLog->emit("voxel_placed", {{"x",5},{"y",10},{"z",3},{"material","Stone"}});
//
// Polling:
//   auto [events, nextCursor] = eventLog->pollSince(lastCursor);
//
// The log is bounded to maxEvents (default 1000). Oldest events are discarded
// when capacity is exceeded.
// ============================================================================

struct GameEvent {
    uint64_t id = 0;                       // Monotonic cursor ID
    std::string type;                      // Event type name
    nlohmann::json data;                   // Event payload
    double timestamp = 0.0;                // Seconds since engine start
    std::string timestampISO;              // ISO 8601 wall-clock time
};

class GameEventLog {
public:
    /// Create an event log with bounded capacity.
    /// @param maxEvents  Maximum events retained (oldest auto-discarded)
    explicit GameEventLog(size_t maxEvents = 1000);

    // Non-copyable
    GameEventLog(const GameEventLog&) = delete;
    GameEventLog& operator=(const GameEventLog&) = delete;

    /// Emit a new event. Thread-safe.
    /// @param type  Event type (e.g. "entity_spawned", "voxel_placed")
    /// @param data  JSON payload with event details
    void emit(const std::string& type, const nlohmann::json& data = nlohmann::json::object());

    /// Poll all events with ID > sinceId. Thread-safe.
    /// Returns the events and the next cursor to use for subsequent polls.
    struct PollResult {
        std::vector<GameEvent> events;
        uint64_t nextCursor;               // Pass this as sinceId next time
    };
    PollResult pollSince(uint64_t sinceId = 0) const;

    /// Get the current cursor (latest event ID). Thread-safe.
    uint64_t currentCursor() const;

    /// Get total number of events emitted since creation. Thread-safe.
    uint64_t totalEmitted() const;

    /// Get number of events currently in the buffer. Thread-safe.
    size_t size() const;

    /// Clear all events. Thread-safe.
    void clear();

    /// Convert a GameEvent to JSON.
    static nlohmann::json eventToJson(const GameEvent& event);

private:
    mutable std::mutex m_mutex;
    std::deque<GameEvent> m_events;
    size_t m_maxEvents;
    uint64_t m_nextId = 1;
    std::chrono::steady_clock::time_point m_startTime;
};

} // namespace Core
} // namespace Phyxel
