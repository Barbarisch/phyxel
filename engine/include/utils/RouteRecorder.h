#pragma once

// RouteRecorder -- captures a whole benchmark route, one row per frame (docs/PerfProgram2026-09.md
// section 16, I15).
//
// Why it exists: the timing rings (I1 GPU, I7 CPU, I11 frame pacing) hold 240 frames. A ~60 s route at
// ~100 fps is ~6,000 frames, so a ring wraps ~25 times, and READING a ring mid-route is itself
// main-thread work (the main loop drains API commands every frame), added to the very frames being
// measured. The recorder is started before a route, stopped after it, and read once afterwards.
//
// Rules that keep a recording honest:
//   * Rows are allocated ONCE at start(). A row holds fixed arrays of floats; scope names are interned
//     into small name tables. Nothing is allocated per frame.
//   * When the buffer is full, recording STOPS and reports truncated() -- it never wraps (a wrapped
//     route silently loses its first frames).
//   * GPU timings resolve a few frames after the frame that recorded them. Each row stores the GPU
//     frame serial it rendered; onGpuFrame() attaches the scopes when that serial arrives. A row still
//     unresolved at read time is reported as pending, never guessed.
// Pure data, main-thread only; unit-tested directly (RouteRecorderTest).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "utils/GpuTimingHistory.h"

namespace Phyxel {

class RouteRecorder {
public:
    static constexpr size_t kMaxFrames = 65536;   // ~11 min at 100 fps
    // Frame-pacing scopes (main loop, I11) PLUS the render path's CPU scopes (drawFrame > Dirty Chunk
    // Flush, Fence Wait, Record, ..., I7): a hitch inside rendering (a re-mesh) must be attributable
    // to its cause, not just to "render".
    static constexpr size_t kMaxPhases = 64;
    static constexpr size_t kMaxGpuScopes = 48;    // GPU scopes per frame

    struct Row {
        uint64_t frame = 0;          // main-loop frame counter
        uint64_t gpuSerial = 0;      // the GPU profiler serial this frame recorded (0 = none)
        float frameMs = 0.0f;        // wall-clock frame time
        std::array<float, kMaxPhases> phaseMs{};       // NaN = phase not run this frame
        std::array<float, kMaxGpuScopes> gpuMs{};      // NaN = scope not recorded / unresolved
        bool gpuResolved = false;
        glm::vec3 cameraPos{0.0f};
        float cameraYaw = 0.0f, cameraPitch = 0.0f;
        float pathProgress = -1.0f;  // -1 = no path playing
        uint32_t residentChunks = 0, pendingGeneration = 0, pendingRemesh = 0;
        int8_t cameraChunkResident = -1;   // is the chunk under the camera loaded? -1 = unknown
    };

    // Per-frame input. `phases` are the frame-pacing samples of this frame (key = scope path).
    struct FrameInput {
        uint64_t frame = 0;
        uint64_t gpuSerial = 0;
        float frameMs = 0.0f;
        const std::vector<GpuTimingSample>* phases = nullptr;
        const std::vector<GpuTimingSample>* renderPhases = nullptr;   // I7 render-path CPU scopes
        glm::vec3 cameraPos{0.0f};
        float cameraYaw = 0.0f, cameraPitch = 0.0f;
        float pathProgress = -1.0f;
        uint32_t residentChunks = 0, pendingGeneration = 0, pendingRemesh = 0;
        int8_t cameraChunkResident = -1;
    };

    // Starts a recording of up to `maxFrames` rows (clamped to 1..kMaxFrames; the value used is
    // returned via framesCapacity()). Refused (returns false, nothing changes) while recording.
    bool start(size_t maxFrames) {
        if (recording_) return false;
        const size_t cap = std::clamp<size_t>(maxFrames, 1, kMaxFrames);
        rows_.assign(cap, Row{});
        count_ = 0;
        truncated_ = false;
        phaseKeys_.clear();
        gpuKeys_.clear();
        droppedKeys_ = 0;
        bySerial_.clear();
        bySerial_.reserve(cap);
        recording_ = true;
        return true;
    }

    void stop() { recording_ = false; }

    // Adds one frame. When the buffer is full the recording stops and truncated() becomes true.
    void addFrame(const FrameInput& in) {
        if (!recording_) return;
        if (count_ >= rows_.size()) { truncated_ = true; recording_ = false; return; }
        Row& r = rows_[count_];
        r = Row{};
        r.frame = in.frame;
        r.gpuSerial = in.gpuSerial;
        r.frameMs = in.frameMs;
        r.phaseMs.fill(kNaN);
        r.gpuMs.fill(kNaN);
        for (const auto* list : {in.phases, in.renderPhases})
            if (list)
                for (const auto& s : *list) {
                    const int k = intern(phaseKeys_, kMaxPhases, s.key);
                    if (k >= 0) r.phaseMs[k] = static_cast<float>(s.ms);
                }
        r.cameraPos = in.cameraPos;
        r.cameraYaw = in.cameraYaw;
        r.cameraPitch = in.cameraPitch;
        r.pathProgress = in.pathProgress;
        r.residentChunks = in.residentChunks;
        r.pendingGeneration = in.pendingGeneration;
        r.pendingRemesh = in.pendingRemesh;
        r.cameraChunkResident = in.cameraChunkResident;
        if (in.gpuSerial != 0) bySerial_[in.gpuSerial] = count_;
        ++count_;
    }

    // GPU scopes for `gpuSerial` have resolved: attach them to the row that rendered that frame.
    // Keeps working after stop() so the last frames of a route still resolve.
    void onGpuFrame(uint64_t gpuSerial, const std::vector<GpuTimingSample>& samples) {
        auto it = bySerial_.find(gpuSerial);
        if (it == bySerial_.end()) return;
        Row& r = rows_[it->second];
        for (const auto& s : samples) {
            const int k = intern(gpuKeys_, kMaxGpuScopes, s.key);
            if (k >= 0) r.gpuMs[k] = static_cast<float>(s.ms);
        }
        r.gpuResolved = true;
        bySerial_.erase(it);
    }

    bool recording() const { return recording_; }
    bool truncated() const { return truncated_; }
    size_t framesCapacity() const { return rows_.size(); }
    size_t frames() const { return count_; }
    const Row& row(size_t i) const { return rows_[i]; }
    const std::vector<std::string>& phaseKeys() const { return phaseKeys_; }
    const std::vector<std::string>& gpuKeys() const { return gpuKeys_; }
    // Scope names beyond the fixed table (a table too small would otherwise drop them silently).
    size_t droppedKeys() const { return droppedKeys_; }

private:
    static constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

    int intern(std::vector<std::string>& table, size_t cap, const std::string& key) {
        for (size_t i = 0; i < table.size(); ++i)
            if (table[i] == key) return static_cast<int>(i);
        if (table.size() >= cap) { ++droppedKeys_; return -1; }
        table.push_back(key);
        return static_cast<int>(table.size() - 1);
    }

    std::vector<Row> rows_;
    size_t count_ = 0;
    bool recording_ = false;
    bool truncated_ = false;
    std::vector<std::string> phaseKeys_, gpuKeys_;
    size_t droppedKeys_ = 0;
    std::unordered_map<uint64_t, size_t> bySerial_;   // unresolved GPU serial -> row
};

}  // namespace Phyxel
