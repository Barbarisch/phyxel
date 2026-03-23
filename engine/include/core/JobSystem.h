#pragma once

#include <string>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <queue>
#include <atomic>
#include <condition_variable>
#include <unordered_map>
#include <chrono>
#include <optional>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

using json = nlohmann::json;

// ============================================================================
// Job System
//
// Runs long-running engine operations (fill_region, generate_world, etc.)
// on a dedicated worker thread so the main game loop stays responsive.
//
// Pattern:
//   1. HTTP handler submits a job via JobSystem::submitJob()
//   2. Returns a JobId immediately — HTTP response sent without blocking
//   3. Worker thread picks up the job and executes backgroundWork()
//   4. Worker reports progress via JobContext::setProgress()
//   5. HTTP clients poll GET /api/job/{id} for status, progress, result
//   6. When backgroundWork() finishes, result is stored
//   7. Main loop calls processCompletedJobs() each frame to run
//      mainThreadFinalize() for any completed jobs (e.g., GPU upload)
//
// Thread safety:
//   - submitJob() is thread-safe (called from HTTP thread)
//   - getJobStatus() is thread-safe (called from HTTP thread)
//   - processCompletedJobs() must be called from main thread only
//   - cancelJob() is thread-safe (cooperative cancellation)
// ============================================================================

using JobId = uint64_t;

// ============================================================================
// JobState — lifecycle states for a job
// ============================================================================
enum class JobState {
    Pending,     // Queued, waiting for worker thread
    Running,     // Worker thread is executing backgroundWork
    Completing,  // Background done, waiting for main-thread finalization
    Complete,    // Fully done — result available
    Failed,      // backgroundWork threw an exception or returned error
    Cancelled    // Cancelled via cancelJob()
};

/// Convert JobState to string for JSON serialization.
inline const char* jobStateToString(JobState s) {
    switch (s) {
        case JobState::Pending:    return "pending";
        case JobState::Running:    return "running";
        case JobState::Completing: return "completing";
        case JobState::Complete:   return "complete";
        case JobState::Failed:     return "failed";
        case JobState::Cancelled:  return "cancelled";
        default:                   return "unknown";
    }
}

// ============================================================================
// JobContext — passed to backgroundWork for progress reporting & cancellation
// ============================================================================
struct JobContext {
    /// Check this periodically in your background loop.
    /// If true, abort work and return early.
    std::atomic<bool>& cancelled;

    /// Report progress. pct should be 0.0..1.0.
    /// message is a short human-readable description of current step.
    void setProgress(float pct, const std::string& msg = "") {
        progress.store(std::min(1.0f, std::max(0.0f, pct)));
        if (!msg.empty()) {
            std::lock_guard<std::mutex> lock(messageMutex);
            message = msg;
        }
    }

    float getProgress() const { return progress.load(); }

    std::string getMessage() const {
        std::lock_guard<std::mutex> lock(messageMutex);
        return message;
    }

    // References to JobRecord fields — set by JobSystem, not by user code
    std::atomic<float>& progress;
    std::mutex& messageMutex;
    std::string& message;
};

// ============================================================================
// JobDescriptor — describes the work to be done
// ============================================================================
struct JobDescriptor {
    /// Human-readable type name (e.g., "fill_region", "generate_world").
    std::string type;

    /// Original request parameters (stored for status queries).
    json params;

    /// The heavy work that runs on the worker thread.
    /// Must check ctx.cancelled periodically and return early if set.
    /// Returns JSON result on success.
    std::function<json(JobContext& ctx)> backgroundWork;

    /// Optional: runs on the main thread after backgroundWork completes.
    /// Receives the result from backgroundWork (mutable — can augment it).
    /// Use for GPU uploads, face rebuilds, etc.
    std::function<void(json& result)> mainThreadFinalize;
};

// ============================================================================
// JobStatus — queryable snapshot of a job's current state
// ============================================================================
struct JobStatus {
    JobId id = 0;
    JobState state = JobState::Pending;
    std::string type;
    float progress = 0.0f;         // 0.0..1.0
    std::string message;           // Current step description
    json result;                   // Available when Complete or Failed
    double elapsedSeconds = 0.0;   // Wall time since submission
    json params;                   // Original parameters

    json toJson() const {
        json j = {
            {"id", id},
            {"state", jobStateToString(state)},
            {"type", type},
            {"progress", progress},
            {"message", message},
            {"elapsed_seconds", elapsedSeconds}
        };
        if (state == JobState::Complete || state == JobState::Failed) {
            j["result"] = result;
        }
        return j;
    }
};

// ============================================================================
// JobSystem — manages background job execution
// ============================================================================
class JobSystem {
public:
    JobSystem();
    ~JobSystem();

    // Non-copyable
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    /// Submit a job for background execution.
    /// Returns a unique JobId immediately (thread-safe).
    JobId submitJob(JobDescriptor descriptor);

    /// Get the current status of a job (thread-safe).
    /// Returns nullopt if the job ID is unknown.
    std::optional<JobStatus> getJobStatus(JobId id) const;

    /// Get the result of a completed job (thread-safe).
    /// Returns empty json if not complete.
    json getJobResult(JobId id) const;

    /// Request cancellation of a running/pending job (thread-safe).
    /// Cooperative — the backgroundWork must check ctx.cancelled.
    /// Returns true if job was found and cancellation was requested.
    bool cancelJob(JobId id);

    /// List all jobs (active + recent completed). Thread-safe.
    std::vector<JobStatus> listJobs() const;

    /// Process completed jobs that need main-thread finalization.
    /// Call this once per frame from the main game loop.
    /// Returns the number of jobs finalized this frame.
    size_t processCompletedJobs();

    /// Check if any jobs are currently running or pending.
    bool hasActiveJobs() const;

    /// Get count of pending + running jobs.
    size_t activeJobCount() const;

    /// Shut down the worker thread. Waits for current job to finish.
    void shutdown();

private:
    // Internal job record — full mutable state
    struct JobRecord {
        JobId id = 0;
        std::string type;
        json params;
        JobState state = JobState::Pending;
        std::atomic<float> progress{0.0f};
        std::mutex messageMutex;
        std::string message;
        json result;
        std::chrono::steady_clock::time_point submitTime;
        std::atomic<bool> cancelled{false};

        // The work functions
        std::function<json(JobContext& ctx)> backgroundWork;
        std::function<void(json& result)> mainThreadFinalize;
    };

    void workerLoop();
    void cleanupOldJobs();  // Remove old completed jobs (keep last N)

    // Worker thread
    std::thread m_workerThread;
    std::atomic<bool> m_shutdown{false};

    // Pending job queue (submitted but not yet picked up by worker)
    std::mutex m_queueMutex;
    std::condition_variable m_queueCV;
    std::queue<JobId> m_pendingQueue;

    // Job records — all jobs (pending, running, completed)
    mutable std::shared_mutex m_jobsMutex;
    std::unordered_map<JobId, std::shared_ptr<JobRecord>> m_jobs;

    // Completed jobs needing main-thread finalization
    std::mutex m_finalizeMutex;
    std::queue<JobId> m_finalizeQueue;

    // Job ID counter
    std::atomic<uint64_t> m_nextJobId{1};

    // Max completed jobs to keep in history
    static constexpr size_t MAX_JOB_HISTORY = 50;
};

} // namespace Core
} // namespace Phyxel
