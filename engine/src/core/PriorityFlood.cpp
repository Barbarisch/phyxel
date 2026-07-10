#include "core/PriorityFlood.h"

#include <queue>
#include <vector>

namespace Phyxel {

namespace {
struct Node {
    float level;      // the elevation to pop at (the fill front)
    uint64_t seq;     // tie-breaker for deterministic ordering at equal levels
    int index;
};
// Min-heap by level, then by insertion sequence (deterministic).
struct Greater {
    bool operator()(const Node& a, const Node& b) const {
        if (a.level != b.level) return a.level > b.level;
        return a.seq > b.seq;
    }
};
// Core Priority-Flood loop: `closed`/`pq` are pre-seeded with the outlet cells; this raises every
// remaining cell to its drainage-limited level. Shared by all public fill*() entry points. When
// `downstream` is non-null it records, for each cell, the neighbor it was reached from (toward the
// outlet) — a valid D8 flow direction even across flats.
void floodCore(std::vector<float>& filled, std::vector<uint8_t>& closed,
               std::priority_queue<Node, std::vector<Node>, Greater>& pq, int w, int h, uint64_t& seq,
               std::vector<int>* downstream) {
    static const int dx[4] = {1, -1, 0, 0};
    static const int dy[4] = {0, 0, 1, -1};
    while (!pq.empty()) {
        Node c = pq.top();
        pq.pop();
        int cx = c.index % w, cy = c.index / w;
        for (int d = 0; d < 4; ++d) {
            int nx = cx + dx[d], ny = cy + dy[d];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            size_t ni = static_cast<size_t>(ny) * w + nx;
            if (closed[ni]) continue;
            // Raise the neighbor to at least the current fill front: it can drain no lower than the
            // level we reached it at. If it was already higher, it keeps its own (real) elevation.
            if (filled[ni] < c.level) filled[ni] = c.level;
            closed[ni] = 1;
            if (downstream) (*downstream)[ni] = c.index;  // ni drains toward c (the outlet side)
            pq.push({filled[ni], seq++, static_cast<int>(ni)});
        }
    }
}

}  // namespace

std::vector<float> PriorityFlood::fill(const std::vector<float>& elevation, int w, int h) {
    std::vector<float> filled = elevation;
    if (w <= 0 || h <= 0 || static_cast<int>(elevation.size()) < w * h) return filled;
    if (w <= 2 || h <= 2) return filled;  // every cell is a border/outlet → already drained

    std::vector<uint8_t> closed(static_cast<size_t>(w) * h, 0);
    std::priority_queue<Node, std::vector<Node>, Greater> pq;
    uint64_t seq = 0;
    auto push = [&](int x, int y) {
        size_t i = static_cast<size_t>(y) * w + x;
        if (closed[i]) return;
        closed[i] = 1;
        pq.push({filled[i], seq++, static_cast<int>(i)});
    };
    // Seed every border cell at its own elevation (the outlets).
    for (int x = 0; x < w; ++x) { push(x, 0); push(x, h - 1); }
    for (int y = 1; y < h - 1; ++y) { push(0, y); push(w - 1, y); }

    floodCore(filled, closed, pq, w, h, seq, nullptr);
    return filled;
}

std::vector<float> PriorityFlood::fill(const std::vector<float>& elevation, int w, int h, float seaLevel) {
    std::vector<float> filled = elevation;
    if (w <= 0 || h <= 0 || static_cast<int>(elevation.size()) < w * h) return filled;

    std::vector<uint8_t> closed(static_cast<size_t>(w) * h, 0);
    std::priority_queue<Node, std::vector<Node>, Greater> pq;
    uint64_t seq = 0;
    auto push = [&](int x, int y) {
        size_t i = static_cast<size_t>(y) * w + x;
        if (closed[i]) return;
        closed[i] = 1;
        pq.push({filled[i], seq++, static_cast<int>(i)});
    };
    // Outlets = the grid border AND every sub-sea cell (the ocean drains to infinity).
    for (int x = 0; x < w; ++x) { push(x, 0); push(x, h - 1); }
    for (int y = 1; y < h - 1; ++y) { push(0, y); push(w - 1, y); }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (filled[static_cast<size_t>(y) * w + x] <= seaLevel) push(x, y);

    floodCore(filled, closed, pq, w, h, seq, nullptr);
    return filled;
}

PriorityFlood::FlowResult PriorityFlood::fillWithFlow(const std::vector<float>& elevation, int w, int h, float seaLevel) {
    FlowResult r;
    r.filled = elevation;
    const size_t n = (w > 0 && h > 0) ? static_cast<size_t>(w) * h : 0;
    r.downstream.resize(elevation.size());
    for (size_t i = 0; i < r.downstream.size(); ++i) r.downstream[i] = static_cast<int>(i);  // default: self
    if (n == 0 || elevation.size() < n) return r;

    std::vector<uint8_t> closed(n, 0);
    std::priority_queue<Node, std::vector<Node>, Greater> pq;
    uint64_t seq = 0;
    auto push = [&](int x, int y) {
        size_t i = static_cast<size_t>(y) * w + x;
        if (closed[i]) return;
        closed[i] = 1;                       // seed cells keep downstream == self (they are outlets)
        pq.push({r.filled[i], seq++, static_cast<int>(i)});
    };
    // Outlets = the grid border AND every sub-sea cell (same as the sea-outlet fill).
    for (int x = 0; x < w; ++x) { push(x, 0); push(x, h - 1); }
    for (int y = 1; y < h - 1; ++y) { push(0, y); push(w - 1, y); }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (r.filled[static_cast<size_t>(y) * w + x] <= seaLevel) push(x, y);

    floodCore(r.filled, closed, pq, w, h, seq, &r.downstream);
    return r;
}

std::vector<float> PriorityFlood::waterDepth(const std::vector<float>& elevation, int w, int h) {
    std::vector<float> filled = fill(elevation, w, h);
    std::vector<float> depth(filled.size(), 0.0f);
    for (size_t i = 0; i < filled.size() && i < elevation.size(); ++i) {
        float d = filled[i] - elevation[i];
        depth[i] = d > 0.0f ? d : 0.0f;
    }
    return depth;
}

}  // namespace Phyxel
