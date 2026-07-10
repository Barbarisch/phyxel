#pragma once

// ============================================================================
// MainThreadJobs — heavy MAIN-THREAD work as tracked, budgeted jobs with
// visible progress ([no-frozen-engine], user directive 2026-07-09).
//
// The JobSystem runs work on a background thread; world-BUILDING commands
// can't move there wholesale — they touch managers with main-thread affinity
// (chunk writes interleaved with placed-object registry, undo snapshots,
// template spawns, nav). This queue keeps that work ON the main thread but
// SLICES it: the caller splits a long operation into UNITS (a building, a
// paving batch, a parcel), and the main loop runs a bounded number of units
// per frame — rendering, input, and the HTTP API stay alive BETWEEN units,
// and {phase, done, total} progress is visible via /api/jobs and the in-engine
// Active Jobs overlay. The old behavior (28 buildings inside ONE
// processAPICommands drain = a ~25-minute frozen window) is the anti-pattern
// this replaces.
//
// Threading: units run on the main thread only (tick()). start/addUnit/seal
// are main-thread too (handlers run on the game loop). listJson/statusJson/
// cancel are safe from the HTTP thread (mutex; the mutex is NOT held while a
// unit runs, so status stays readable mid-unit).
// Ids start at 1,000,000 so they never collide with JobSystem ids in the
// merged /api/jobs listing.
// ============================================================================

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

class MainThreadJobs {
public:
    using Id = uint64_t;

    /// Create a job record (state "running"). `type` mirrors JobStatus ("build_settlement").
    Id start(const std::string& type, const std::string& label);

    /// Append a work unit. `phase` is what the progress UI shows while this unit runs
    /// (e.g. "building 12/28"). Units run FIFO, at most `maxUnitsPerTick` per frame.
    void addUnit(Id id, const std::string& phase, std::function<void()> fn);

    /// No more units coming — the job completes when its queue drains.
    void seal(Id id);

    /// Merge fields into the job's result payload (returned by statusJson when complete).
    void mergeResult(Id id, const nlohmann::json& fields);

    /// Drop all remaining units (the current unit, if any, finishes — units are not preempted).
    bool cancel(Id id);

    /// MAIN THREAD, once per frame: run up to `maxUnitsPerTick` pending units.
    void tick(int maxUnitsPerTick = 1);

    bool anyActive() const;

    /// JobStatus-shaped snapshots for the merged /api/jobs listing + the overlay.
    nlohmann::json listJson() const;
    /// Status for one id, or a null json if unknown.
    nlohmann::json statusJson(Id id) const;

private:
    struct Unit {
        std::string phase;
        std::function<void()> run;
    };
    struct Job {
        Id id = 0;
        std::string type;
        std::string label;
        std::string phase;       ///< the CURRENT (or next) unit's phase
        size_t done = 0;
        size_t total = 0;        ///< grows as units are added (progress is honest, not guessed)
        bool sealed = false;
        bool cancelled = false;
        bool complete = false;
        std::deque<Unit> units;
        nlohmann::json result = nlohmann::json::object();
        std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    };

    nlohmann::json jobJson(const Job& j) const;
    Job* find(Id id);

    mutable std::mutex m_mutex;
    std::vector<Job> m_jobs;             ///< small (few concurrent + short history), linear scan
    Id m_nextId = 1000000;
    static constexpr size_t kMaxHistory = 20;
};

} // namespace Core
} // namespace Phyxel
