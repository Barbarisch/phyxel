#include "core/MainThreadJobs.h"

#include <algorithm>

#include "utils/Logger.h"

namespace Phyxel {
namespace Core {

MainThreadJobs::Job* MainThreadJobs::find(Id id) {
    for (auto& j : m_jobs)
        if (j.id == id) return &j;
    return nullptr;
}

MainThreadJobs::Id MainThreadJobs::start(const std::string& type, const std::string& label) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // trim completed history so the vector stays small
    size_t completed = 0;
    for (const auto& j : m_jobs) completed += j.complete ? 1 : 0;
    if (completed > kMaxHistory)
        m_jobs.erase(std::remove_if(m_jobs.begin(), m_jobs.end(),
                                    [](const Job& j) { return j.complete; }),
                     m_jobs.end());
    Job j;
    j.id = m_nextId++;
    j.type = type;
    j.label = label;
    m_jobs.push_back(std::move(j));
    LOG_INFO_FMT("MainThreadJobs", "job " << m_jobs.back().id << " started: " << label);
    return m_jobs.back().id;
}

void MainThreadJobs::addUnit(Id id, const std::string& phase, std::function<void()> fn) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (Job* j = find(id); j && !j->sealed && !j->cancelled) {
        if (j->units.empty() && j->done == j->total) j->phase = phase;  // next visible phase
        j->units.push_back({phase, std::move(fn)});
        ++j->total;
    }
}

void MainThreadJobs::seal(Id id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (Job* j = find(id)) {
        j->sealed = true;
        if (j->units.empty()) j->complete = true;   // nothing queued: done immediately
    }
}

void MainThreadJobs::mergeResult(Id id, const nlohmann::json& fields) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (Job* j = find(id))
        for (auto it = fields.begin(); it != fields.end(); ++it) j->result[it.key()] = it.value();
}

bool MainThreadJobs::cancel(Id id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (Job* j = find(id); j && !j->complete) {
        j->cancelled = true;
        j->units.clear();
        j->complete = true;
        j->phase = "cancelled";
        LOG_INFO_FMT("MainThreadJobs", "job " << id << " cancelled (" << j->done << "/"
                     << j->total << " units done)");
        return true;
    }
    return false;
}

void MainThreadJobs::tick(int maxUnitsPerTick) {
    for (int n = 0; n < maxUnitsPerTick; ++n) {
        Unit unit;
        Id runningId = 0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            Job* next = nullptr;
            for (auto& j : m_jobs)
                if (!j.complete && !j.units.empty()) { next = &j; break; }   // FIFO across jobs
            if (!next) return;
            unit = std::move(next->units.front());
            next->units.pop_front();
            next->phase = unit.phase;
            runningId = next->id;
        }
        // Run WITHOUT the lock: the unit may take seconds and the HTTP thread must be able
        // to read status meanwhile (that's the whole point of the progress surface).
        if (unit.run) unit.run();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (Job* j = find(runningId)) {
                ++j->done;
                if (j->sealed && j->units.empty()) {
                    j->complete = true;
                    j->phase = "complete";
                    LOG_INFO_FMT("MainThreadJobs", "job " << j->id << " complete: " << j->label
                                 << " (" << j->done << " units)");
                }
            }
        }
    }
}

bool MainThreadJobs::anyActive() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& j : m_jobs)
        if (!j.complete) return true;
    return false;
}

nlohmann::json MainThreadJobs::jobJson(const Job& j) const {
    // JobStatus-shaped (id/state/type/progress/message/elapsed_seconds) so the merged
    // /api/jobs listing and the MCP tools read both kinds uniformly.
    const double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - j.started).count() / 1000.0;
    const double progress = j.total == 0 ? 0.0 : static_cast<double>(j.done) / j.total;
    nlohmann::json out = {
        {"id", j.id},
        {"state", j.cancelled ? "cancelled" : j.complete ? "complete" : "running"},
        {"type", j.type},
        {"progress", progress},
        {"message", j.phase.empty() ? j.label : j.phase},
        {"elapsed_seconds", elapsed},
        {"label", j.label},
        {"units_done", j.done},
        {"units_total", j.total},
        {"main_thread", true},
    };
    if (j.complete) out["result"] = j.result;
    return out;
}

nlohmann::json MainThreadJobs::listJson() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& j : m_jobs) arr.push_back(jobJson(j));
    return arr;
}

nlohmann::json MainThreadJobs::statusJson(Id id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& j : m_jobs)
        if (j.id == id) return jobJson(j);
    return nlohmann::json();
}

} // namespace Core
} // namespace Phyxel
