#pragma once

// CPU scope timing for the render path, with the same history and statistics as the GPU scopes
// (docs/PerfProgram2026-09.md, I7). RenderCoordinator owns one; GET /api/debug/cpu_timing reads it.
//
// The editor's PerformanceProfiler lives in Application, so the engine's render path (shared by the
// editor and standalone games) had no CPU scopes at all. This recorder is self-contained: scopes nest
// by a stack, samples are keyed by path + occurrence like the GPU history, and each frame is added
// once under its frame serial. Main-thread only (drawFrame and the API handlers both run there).

#include <chrono>
#include <string>
#include <vector>

#include "utils/GpuTimingHistory.h"

namespace Phyxel {

class CpuTimingRecorder {
public:
    explicit CpuTimingRecorder(size_t capacity = 240) : history_(capacity) {}

    void beginFrame() {
        samples_.clear();
        stack_.clear();
        paths_.clear();
    }

    void push(const char* name) {
        const std::string path = stack_.empty() ? std::string(name) : paths_[stack_.back().slot] + "/" + name;
        const size_t slot = samples_.size();
        samples_.push_back({path, name, static_cast<uint32_t>(stack_.size()), 0.0});
        paths_.push_back(path);
        stack_.push_back({slot, Clock::now()});
    }

    void pop() {
        if (stack_.empty()) return;
        const Open o = stack_.back();
        stack_.pop_back();
        samples_[o.slot].ms = std::chrono::duration<double, std::milli>(Clock::now() - o.start).count();
    }

    // A value measured outside a scope (e.g. the frame-to-frame interval), recorded at the top level.
    void addSample(const char* name, double ms) {
        samples_.push_back({name, name, 0u, ms});
        paths_.push_back(name);
    }

    // This frame's samples, valid after endFrame() until the next beginFrame() (the route recorder's feed).
    const std::vector<GpuTimingSample>& lastFrameSamples() const { return samples_; }

    // Closes any scope left open (an early return) and adds the frame to the history.
    void endFrame(uint64_t frameSerial) {
        while (!stack_.empty()) pop();
        const auto keys = GpuTimingHistory::occurrenceKeys(paths_);
        for (size_t i = 0; i < samples_.size(); ++i) samples_[i].key = keys[i];
        history_.addFrame(frameSerial, samples_);
    }

    const GpuTimingHistory& history() const { return history_; }

    class Scope {
    public:
        Scope(CpuTimingRecorder* r, const char* name) : r_(r) { if (r_) r_->push(name); }
        ~Scope() { if (r_) r_->pop(); }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    private:
        CpuTimingRecorder* r_;
    };

private:
    using Clock = std::chrono::steady_clock;
    struct Open {
        size_t slot;
        Clock::time_point start;
    };
    GpuTimingHistory history_;
    std::vector<GpuTimingSample> samples_;   // in START order
    std::vector<std::string> paths_;
    std::vector<Open> stack_;
};

#define PHX_CPU_CONCAT_INNER(a, b) a##b
#define PHX_CPU_CONCAT(a, b) PHX_CPU_CONCAT_INNER(a, b)
#define CPU_PROFILE_SCOPE(recorder, name) \
    ::Phyxel::CpuTimingRecorder::Scope PHX_CPU_CONCAT(_cpu_scope_, __LINE__)(recorder, name)

}  // namespace Phyxel
