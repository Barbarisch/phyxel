#include "utils/GpuTimingHistory.h"

#include <algorithm>
#include <cmath>

namespace Phyxel {

GpuTimingHistory::GpuTimingHistory(size_t capacity) : capacity_(capacity == 0 ? 1 : capacity) {}

bool GpuTimingHistory::addFrame(uint64_t frameSerial, const std::vector<GpuTimingSample>& samples) {
    if (any_ && frameSerial <= lastSerial_) {
        ++stale_;
        return false;
    }
    any_ = true;
    lastSerial_ = frameSerial;
    ++accepted_;

    serials_.push_back(frameSerial);
    if (serials_.size() > capacity_) serials_.pop_front();
    const uint64_t oldestHeld = serials_.front();

    for (const auto& s : samples) {
        auto it = entries_.find(s.key);
        if (it == entries_.end()) {
            it = entries_.emplace(s.key, Entry{}).first;
            order_.push_back(s.key);
        }
        Entry& e = it->second;
        e.name = s.name;
        e.depth = s.depth;
        e.samples.emplace_back(frameSerial, s.ms);
    }

    // Evict every sample older than the oldest frame still held.
    for (auto& [key, e] : entries_) {
        while (!e.samples.empty() && e.samples.front().first < oldestHeld) e.samples.pop_front();
    }
    return true;
}

namespace {

// Nearest-rank percentile on an ascending-sorted, non-empty vector.
double nearestRank(const std::vector<double>& sorted, double p) {
    const size_t n = sorted.size();
    size_t rank = static_cast<size_t>(std::ceil(p / 100.0 * static_cast<double>(n)));
    if (rank < 1) rank = 1;
    if (rank > n) rank = n;
    return sorted[rank - 1];
}

double median(const std::vector<double>& sorted) {
    const size_t n = sorted.size();
    return (n % 2 == 1) ? sorted[n / 2] : 0.5 * (sorted[n / 2 - 1] + sorted[n / 2]);
}

}  // namespace

std::vector<GpuTimingStats> GpuTimingHistory::stats(size_t frames) const {
    std::vector<GpuTimingStats> out;
    if (serials_.empty() || frames == 0) return out;
    const size_t window = std::min(frames, serials_.size());
    const uint64_t firstInWindow = serials_[serials_.size() - window];

    for (const auto& key : order_) {
        const Entry& e = entries_.at(key);
        std::vector<double> v;
        double lastMs = 0.0;
        for (const auto& [serial, ms] : e.samples) {
            if (serial < firstInWindow) continue;
            v.push_back(ms);
            lastMs = ms;
        }
        if (v.empty()) continue;

        GpuTimingStats s;
        s.key = key;
        s.name = e.name;
        s.depth = e.depth;
        s.n = v.size();
        s.last = lastMs;
        double sum = 0.0;
        for (double x : v) sum += x;
        s.mean = sum / static_cast<double>(v.size());
        std::sort(v.begin(), v.end());
        s.median = median(v);
        s.p90 = nearestRank(v, 90.0);
        s.p99 = nearestRank(v, 99.0);
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<std::string> GpuTimingHistory::occurrenceKeys(const std::vector<std::string>& paths) {
    std::unordered_map<std::string, int> seen;
    std::vector<std::string> keys;
    keys.reserve(paths.size());
    for (const auto& p : paths) {
        const int n = seen[p]++;
        keys.push_back(n == 0 ? p : p + "#" + std::to_string(n));
    }
    return keys;
}

}  // namespace Phyxel
