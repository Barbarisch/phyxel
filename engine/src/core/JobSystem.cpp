#include "core/JobSystem.h"
#include "utils/Logger.h"
#include <algorithm>

namespace Phyxel {
namespace Core {

// ============================================================================
// Construction / Destruction
// ============================================================================

JobSystem::JobSystem() {
    m_workerThread = std::thread(&JobSystem::workerLoop, this);
    LOG_INFO("JobSystem", "Worker thread started");
}

JobSystem::~JobSystem() {
    shutdown();
}

void JobSystem::shutdown() {
    if (m_shutdown.exchange(true)) return;  // Already shut down

    m_queueCV.notify_all();

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
    LOG_INFO("JobSystem", "Worker thread stopped");
}

// ============================================================================
// Submit
// ============================================================================

JobId JobSystem::submitJob(JobDescriptor descriptor) {
    JobId id = m_nextJobId.fetch_add(1);

    auto record = std::make_shared<JobRecord>();
    record->id = id;
    record->type = descriptor.type;
    record->params = std::move(descriptor.params);
    record->state = JobState::Pending;
    record->submitTime = std::chrono::steady_clock::now();
    record->backgroundWork = std::move(descriptor.backgroundWork);
    record->mainThreadFinalize = std::move(descriptor.mainThreadFinalize);

    // Store record
    {
        std::unique_lock lock(m_jobsMutex);
        m_jobs[id] = record;
    }

    // Enqueue for worker
    {
        std::lock_guard lock(m_queueMutex);
        m_pendingQueue.push(id);
    }
    m_queueCV.notify_one();

    LOG_INFO("JobSystem", "Job {} submitted: {}", id, record->type);
    return id;
}

// ============================================================================
// Query
// ============================================================================

std::optional<JobStatus> JobSystem::getJobStatus(JobId id) const {
    std::shared_lock lock(m_jobsMutex);
    auto it = m_jobs.find(id);
    if (it == m_jobs.end()) return std::nullopt;

    auto& rec = it->second;
    JobStatus status;
    status.id = rec->id;
    status.type = rec->type;
    status.state = rec->state;
    status.progress = rec->progress.load();
    status.params = rec->params;
    {
        std::lock_guard mlock(rec->messageMutex);
        status.message = rec->message;
    }
    if (rec->state == JobState::Complete || rec->state == JobState::Failed) {
        status.result = rec->result;
    }
    auto now = std::chrono::steady_clock::now();
    status.elapsedSeconds = std::chrono::duration<double>(now - rec->submitTime).count();
    return status;
}

json JobSystem::getJobResult(JobId id) const {
    std::shared_lock lock(m_jobsMutex);
    auto it = m_jobs.find(id);
    if (it == m_jobs.end()) return json{};
    if (it->second->state == JobState::Complete || it->second->state == JobState::Failed) {
        return it->second->result;
    }
    return json{};
}

bool JobSystem::cancelJob(JobId id) {
    std::shared_lock lock(m_jobsMutex);
    auto it = m_jobs.find(id);
    if (it == m_jobs.end()) return false;

    auto& rec = it->second;
    if (rec->state == JobState::Complete || rec->state == JobState::Failed ||
        rec->state == JobState::Cancelled) {
        return false;  // Can't cancel a finished job
    }

    rec->cancelled.store(true);

    // If still pending, mark cancelled immediately
    if (rec->state == JobState::Pending) {
        rec->state = JobState::Cancelled;
        rec->result = json{{"cancelled", true}, {"message", "Cancelled before execution"}};
        LOG_INFO("JobSystem", "Job {} cancelled (was pending)", id);
    } else {
        LOG_INFO("JobSystem", "Job {} cancellation requested (running)", id);
    }
    return true;
}

std::vector<JobStatus> JobSystem::listJobs() const {
    std::shared_lock lock(m_jobsMutex);
    std::vector<JobStatus> result;
    result.reserve(m_jobs.size());

    for (auto& [id, rec] : m_jobs) {
        JobStatus status;
        status.id = rec->id;
        status.type = rec->type;
        status.state = rec->state;
        status.progress = rec->progress.load();
        status.params = rec->params;
        {
            std::lock_guard mlock(rec->messageMutex);
            status.message = rec->message;
        }
        if (rec->state == JobState::Complete || rec->state == JobState::Failed) {
            status.result = rec->result;
        }
        auto now = std::chrono::steady_clock::now();
        status.elapsedSeconds = std::chrono::duration<double>(now - rec->submitTime).count();
        result.push_back(std::move(status));
    }
    return result;
}

bool JobSystem::hasActiveJobs() const {
    std::shared_lock lock(m_jobsMutex);
    for (auto& [id, rec] : m_jobs) {
        if (rec->state == JobState::Pending || rec->state == JobState::Running ||
            rec->state == JobState::Completing) {
            return true;
        }
    }
    return false;
}

size_t JobSystem::activeJobCount() const {
    std::shared_lock lock(m_jobsMutex);
    size_t count = 0;
    for (auto& [id, rec] : m_jobs) {
        if (rec->state == JobState::Pending || rec->state == JobState::Running ||
            rec->state == JobState::Completing) {
            ++count;
        }
    }
    return count;
}

// ============================================================================
// Main-thread finalization
// ============================================================================

size_t JobSystem::processCompletedJobs() {
    size_t finalized = 0;

    // Drain finalize queue
    std::queue<JobId> toFinalize;
    {
        std::lock_guard lock(m_finalizeMutex);
        std::swap(toFinalize, m_finalizeQueue);
    }

    while (!toFinalize.empty()) {
        JobId id = toFinalize.front();
        toFinalize.pop();

        std::shared_ptr<JobRecord> rec;
        {
            std::shared_lock lock(m_jobsMutex);
            auto it = m_jobs.find(id);
            if (it == m_jobs.end()) continue;
            rec = it->second;
        }

        if (rec->state != JobState::Completing) continue;

        // Run main-thread finalization
        if (rec->mainThreadFinalize) {
            try {
                rec->mainThreadFinalize(rec->result);
            } catch (const std::exception& e) {
                LOG_ERROR("JobSystem", "Job {} finalization failed: {}", id, e.what());
                rec->result["finalize_error"] = e.what();
            }
        }

        rec->state = JobState::Complete;
        LOG_INFO("JobSystem", "Job {} complete: {}", id, rec->type);
        ++finalized;
    }

    // Periodic cleanup of old completed jobs
    cleanupOldJobs();

    return finalized;
}

// ============================================================================
// Worker thread
// ============================================================================

void JobSystem::workerLoop() {
    while (!m_shutdown.load()) {
        JobId jobId = 0;

        // Wait for a job
        {
            std::unique_lock lock(m_queueMutex);
            m_queueCV.wait(lock, [this] {
                return !m_pendingQueue.empty() || m_shutdown.load();
            });

            if (m_shutdown.load() && m_pendingQueue.empty()) break;
            if (m_pendingQueue.empty()) continue;

            jobId = m_pendingQueue.front();
            m_pendingQueue.pop();
        }

        // Get the job record
        std::shared_ptr<JobRecord> rec;
        {
            std::shared_lock lock(m_jobsMutex);
            auto it = m_jobs.find(jobId);
            if (it == m_jobs.end()) continue;
            rec = it->second;
        }

        // Skip if already cancelled
        if (rec->cancelled.load() || rec->state == JobState::Cancelled) {
            rec->state = JobState::Cancelled;
            continue;
        }

        // Execute background work
        rec->state = JobState::Running;
        LOG_INFO("JobSystem", "Job {} started: {}", jobId, rec->type);

        JobContext ctx{rec->cancelled, rec->progress, rec->messageMutex, rec->message};

        try {
            json result = rec->backgroundWork(ctx);

            if (rec->cancelled.load()) {
                rec->state = JobState::Cancelled;
                rec->result = json{{"cancelled", true}, {"message", "Cancelled during execution"}};
                LOG_INFO("JobSystem", "Job {} cancelled during execution", jobId);
            } else if (rec->mainThreadFinalize) {
                // Needs main-thread finalization
                rec->result = std::move(result);
                rec->state = JobState::Completing;
                {
                    std::lock_guard lock(m_finalizeMutex);
                    m_finalizeQueue.push(jobId);
                }
                LOG_INFO("JobSystem", "Job {} background complete, awaiting finalization: {}", jobId, rec->type);
            } else {
                // No finalization needed — done
                rec->result = std::move(result);
                rec->state = JobState::Complete;
                LOG_INFO("JobSystem", "Job {} complete: {}", jobId, rec->type);
            }
        } catch (const std::exception& e) {
            rec->state = JobState::Failed;
            rec->result = json{{"error", e.what()}, {"type", rec->type}};
            LOG_ERROR("JobSystem", "Job {} failed: {} — {}", jobId, rec->type, e.what());
        }
    }
}

// ============================================================================
// Cleanup
// ============================================================================

void JobSystem::cleanupOldJobs() {
    std::unique_lock lock(m_jobsMutex);
    if (m_jobs.size() <= MAX_JOB_HISTORY) return;

    // Collect completed/failed/cancelled jobs sorted by submit time
    std::vector<std::pair<JobId, std::chrono::steady_clock::time_point>> finished;
    for (auto& [id, rec] : m_jobs) {
        if (rec->state == JobState::Complete || rec->state == JobState::Failed ||
            rec->state == JobState::Cancelled) {
            finished.emplace_back(id, rec->submitTime);
        }
    }

    if (finished.size() <= MAX_JOB_HISTORY / 2) return;

    // Sort oldest first
    std::sort(finished.begin(), finished.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; });

    // Remove oldest until we're under limit
    size_t toRemove = m_jobs.size() - MAX_JOB_HISTORY;
    for (size_t i = 0; i < std::min(toRemove, finished.size()); ++i) {
        m_jobs.erase(finished[i].first);
    }
}

} // namespace Core
} // namespace Phyxel
