#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace Phyxel {

// One timed GPU scope from one frame.
struct GpuTimingSample {
    std::string key;     // scope path plus occurrence, e.g. "Shadow Pass/Shadow Mid/Character Shadows"
    std::string name;    // the scope's own name, e.g. "Character Shadows"
    uint32_t depth = 0;  // nesting depth, 0 = top level
    double ms = 0.0;
};

// Statistics for one scope over a window of accepted frames.
struct GpuTimingStats {
    std::string key;
    std::string name;
    uint32_t depth = 0;
    size_t n = 0;          // frames in the window that recorded this scope
    double median = 0.0;
    double p90 = 0.0;
    double p99 = 0.0;
    double mean = 0.0;
    double max = 0.0;      // worst frame in the window (a hitch is a max, not a percentile)
    double last = 0.0;     // most recent sample
};

// A ring of the last `capacity` frames of GPU scope timings (docs/PerfProgram2026-09.md, item I1).
//
// Two rules keep the numbers honest:
//   * Samples are keyed by scope PATH plus occurrence index, never by name. The same name appears
//     more than once per frame ("Character Shadows" under each shadow cascade), and a name key
//     would average different passes together.
//   * A frame is accepted at most once, identified by the serial number of the frame that RECORDED
//     the queries. Re-delivering an old frame (what a NOT_READY readback used to do by keeping the
//     previous results) is counted in staleSkipped() and adds nothing, so it cannot inflate n.
//
// Pure data, no Vulkan: GpuProfiler feeds it, the API reads it, and it is unit-tested directly.
class GpuTimingHistory {
public:
    explicit GpuTimingHistory(size_t capacity = 240);

    // Adds one frame's samples. Returns false, adds nothing and counts a stale skip when
    // frameSerial is not newer than the last accepted frame.
    bool addFrame(uint64_t frameSerial, const std::vector<GpuTimingSample>& samples);

    // Statistics over the most recent `frames` accepted frames (clamped to what is held), one entry
    // per key, in the order keys were first seen.
    std::vector<GpuTimingStats> stats(size_t frames) const;

    // The raw per-frame values behind stats() (docs/PerfProgram2026-09.md section 16, I11): the most
    // recent `frames` accepted frames, oldest first, each with every key recorded in that frame, in
    // first-seen key order. Statistics say how often something is slow; the series says WHICH frame.
    struct FrameSeries {
        uint64_t serial = 0;
        std::vector<std::pair<std::string, double>> values;   // (key, ms)
    };
    std::vector<FrameSeries> series(size_t frames) const;

    size_t capacity() const { return capacity_; }
    size_t framesHeld() const { return serials_.size(); }
    uint64_t framesAccepted() const { return accepted_; }
    uint64_t staleSkipped() const { return stale_; }
    uint64_t lastSerial() const { return lastSerial_; }

    // Turns one frame's scope paths (in recording order) into unique keys: the first use of a path
    // keeps it as is, later uses in the same frame become "path#1", "path#2", ...
    static std::vector<std::string> occurrenceKeys(const std::vector<std::string>& paths);

private:
    struct Entry {
        std::string name;
        uint32_t depth = 0;
        std::deque<std::pair<uint64_t, double>> samples;   // (frame serial, ms), oldest first
    };

    size_t capacity_;
    std::deque<uint64_t> serials_;                 // accepted frame serials, oldest first
    std::unordered_map<std::string, Entry> entries_;
    std::vector<std::string> order_;               // keys in first-seen order
    uint64_t accepted_ = 0;
    uint64_t stale_ = 0;
    uint64_t lastSerial_ = 0;
    bool any_ = false;
};

}  // namespace Phyxel
