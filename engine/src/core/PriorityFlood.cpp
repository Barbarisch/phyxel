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
}  // namespace

std::vector<float> PriorityFlood::fill(const std::vector<float>& elevation, int w, int h) {
    std::vector<float> filled = elevation;
    if (w <= 0 || h <= 0 || static_cast<int>(elevation.size()) < w * h) return filled;
    if (w <= 2 || h <= 2) return filled;  // every cell is a border/outlet → already drained

    std::vector<uint8_t> closed(static_cast<size_t>(w) * h, 0);
    std::priority_queue<Node, std::vector<Node>, Greater> pq;
    uint64_t seq = 0;

    auto idx = [w](int x, int y) { return static_cast<size_t>(y) * w + x; };
    auto push = [&](int x, int y) {
        size_t i = idx(x, y);
        if (closed[i]) return;
        closed[i] = 1;
        pq.push({filled[i], seq++, static_cast<int>(i)});
    };

    // Seed the min-heap with every border cell at its own elevation (the outlets).
    for (int x = 0; x < w; ++x) { push(x, 0); push(x, h - 1); }
    for (int y = 1; y < h - 1; ++y) { push(0, y); push(w - 1, y); }

    static const int dx[4] = {1, -1, 0, 0};
    static const int dy[4] = {0, 0, 1, -1};
    while (!pq.empty()) {
        Node c = pq.top();
        pq.pop();
        int cx = c.index % w, cy = c.index / w;
        for (int d = 0; d < 4; ++d) {
            int nx = cx + dx[d], ny = cy + dy[d];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            size_t ni = idx(nx, ny);
            if (closed[ni]) continue;
            // Raise the neighbor to at least the current fill front: it can drain no lower than the
            // level we reached it at. If it was already higher, it keeps its own (real) elevation.
            if (filled[ni] < c.level) filled[ni] = c.level;
            closed[ni] = 1;
            pq.push({filled[ni], seq++, static_cast<int>(ni)});
        }
    }
    return filled;
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
