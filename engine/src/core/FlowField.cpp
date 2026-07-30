#include "core/FlowField.h"

#include "core/PriorityFlood.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace Phyxel {

FlowField::FlowField(const HeightFunc& heightAt, float originX, float originZ,
                     int cellsX, int cellsZ, float cellSize, float seaLevel, int riverThreshold)
    : m_originX(originX), m_originZ(originZ), m_cellSize(cellSize > 0.0f ? cellSize : 1.0f),
      m_cellsX(cellsX > 0 ? cellsX : 0), m_cellsZ(cellsZ > 0 ? cellsZ : 0) {
    if (m_cellsX <= 0 || m_cellsZ <= 0 || !heightAt) return;
    const size_t n = static_cast<size_t>(m_cellsX) * m_cellsZ;

    std::vector<float> base(n);
    for (int j = 0; j < m_cellsZ; ++j)
        for (int i = 0; i < m_cellsX; ++i)
            base[static_cast<size_t>(j) * m_cellsX + i] =
                heightAt(m_originX + i * m_cellSize, m_originZ + j * m_cellSize);

    // Depression-fill with flood flow directions (reached-from), giving `filled` (no pits) and a
    // fallback direction that is valid even across flats.
    PriorityFlood::FlowResult fr = PriorityFlood::fillWithFlow(base, m_cellsX, m_cellsZ, seaLevel);

    // Real drainage direction = STEEPEST DESCENT on the filled grid (this is what makes flow CONVERGE
    // into valleys → river-like accumulation, unlike the flood's drain-to-nearest-outlet reached-from).
    // Outlets (reachedFrom == self) are sinks. On a flat (no strictly-lower neighbor — a lake surface)
    // fall back to the flood's reached-from, which points toward the spill/outlet.
    const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    std::vector<int> downstream(n);
    for (int j = 0; j < m_cellsZ; ++j)
        for (int i = 0; i < m_cellsX; ++i) {
            int c = j * m_cellsX + i;
            if (fr.downstream[c] == c) { downstream[c] = c; continue; }  // outlet → sink
            int best = c;
            float bestH = fr.filled[c];
            for (int d = 0; d < 4; ++d) {
                int nx = i + dx[d], ny = j + dy[d];
                if (nx < 0 || ny < 0 || nx >= m_cellsX || ny >= m_cellsZ) continue;
                int ni = ny * m_cellsX + nx;
                if (fr.filled[ni] < bestH) { bestH = fr.filled[ni]; best = ni; }
            }
            downstream[c] = (best != c) ? best : fr.downstream[c];  // steepest, or flat→reached-from
        }

    // Accumulate upstream counts by a topological (Kahn) pass over the drainage forest: start at
    // sources (indegree 0), push each cell's running total into its downstream cell, releasing it
    // once all its upstream contributors are done. Exact for any acyclic downstream (incl. flats),
    // deterministic (ascending-index seeding, FIFO). accum[c] = 1 + all cells draining through c.
    m_accum.assign(n, 1);
    std::vector<int> indeg(n, 0);
    for (size_t c = 0; c < n; ++c)
        if (downstream[c] != static_cast<int>(c)) ++indeg[downstream[c]];
    std::vector<int> q;
    q.reserve(n);
    for (size_t c = 0; c < n; ++c)
        if (indeg[c] == 0) q.push_back(static_cast<int>(c));
    for (size_t head = 0; head < q.size(); ++head) {
        int c = q[head];
        int d = downstream[c];
        if (d != c) {
            m_accum[d] += m_accum[c];
            if (--indeg[d] == 0) q.push_back(d);
        }
    }
    m_released = q.size();  // every cell released ⟺ the drainage graph is acyclic (no under-count)
    m_maxAccum = m_accum.empty() ? 0 : *std::max_element(m_accum.begin(), m_accum.end());

    // Strahler order over the river cells (accum > threshold).
    m_order = computeStrahler(downstream, m_accum, riverThreshold);
    m_maxOrder = m_order.empty() ? 0 : *std::max_element(m_order.begin(), m_order.end());
    m_downstream = std::move(downstream);  // kept for channelAt's segment geometry
}

namespace {
// Distance from point p to segment a→b in the XZ plane.
float pointSegDist(float px, float pz, float ax, float az, float bx, float bz) {
    const float abx = bx - ax, abz = bz - az;
    const float ab2 = abx * abx + abz * abz;
    float t = (ab2 > 0.0f) ? ((px - ax) * abx + (pz - az) * abz) / ab2 : 0.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    const float cx = ax + t * abx, cz = az + t * abz;
    const float ddx = px - cx, ddz = pz - cz;
    return std::sqrt(ddx * ddx + ddz * ddz);
}
}  // namespace

float FlowField::channelHalfWidth(int order) {
    static const float hw[6] = {1.0f, 1.5f, 2.5f, 4.0f, 7.0f, 11.0f};  // = width {2,3,5,8,14,22}/2
    if (order < 1) return 0.0f;
    return hw[order > 6 ? 5 : order - 1];
}

float FlowField::channelDepth(int order) {
    static const float dp[6] = {0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 2.0f};  // orders 1-2 sub-voxel → no carve
    if (order < 1) return 0.0f;
    return dp[order > 6 ? 5 : order - 1];
}

FlowField::ChannelHit FlowField::segmentChannel(float px, float pz, float ax, float az,
                                                float bx, float bz, int order) {
    ChannelHit h;
    if (order < 3) return h;                       // orders 1-2 are sub-voxel → no bed carve
    const float halfW = channelHalfWidth(order);
    const float dist = pointSegDist(px, pz, ax, az, bx, bz);
    if (dist >= halfW) return h;
    const float t = dist / halfW;                  // parabolic bed: full depth at centre, 0 at edge
    h.hit = true;
    h.order = order;
    h.depth = channelDepth(order) * (1.0f - t * t);
    return h;
}

FlowField::ChannelHit FlowField::channelAt(float worldX, float worldZ) const {
    ChannelHit best;
    if (m_cellsX <= 0 || m_cellsZ <= 0) return best;
    const int ci = static_cast<int>(std::floor((worldX - m_originX) / m_cellSize));
    const int cj = static_cast<int>(std::floor((worldZ - m_originZ) / m_cellSize));
    // Scan radius sized so no channel within half-width of the query is missed: a river cell whose
    // segment comes within halfWidth of the point has its centre within halfWidth + cellSize of it.
    // DEPENDS on drainage being 4-connected (segment length = one orthogonal cellSize hop — see
    // PriorityFlood::fillWithFlow + the steepest-descent pass); if that ever becomes 8-connected the
    // segment length grows to cellSize·√2 and this radius must grow too. For the real 32 m cell it is
    // 5×5 (r=2, since ceil(11/32)=1); it stays correct for small cellSize / large orders.
    const int r = 1 + static_cast<int>(std::ceil(channelHalfWidth(m_maxOrder) / m_cellSize));
    for (int j = cj - r; j <= cj + r; ++j)
        for (int i = ci - r; i <= ci + r; ++i) {
            if (i < 0 || j < 0 || i >= m_cellsX || j >= m_cellsZ) continue;
            const int rc = j * m_cellsX + i;
            const int ord = m_order[rc];
            if (ord <= best.order) continue;  // keep the deepest (max-order) hit
            const float ax = m_originX + (i + 0.5f) * m_cellSize;
            const float az = m_originZ + (j + 0.5f) * m_cellSize;
            float bx = ax, bz = az;  // sink → degenerate segment (a point at the cell centre)
            const int d = m_downstream[rc];
            if (d != rc && d >= 0 && static_cast<size_t>(d) < m_downstream.size()) {
                bx = m_originX + (d % m_cellsX + 0.5f) * m_cellSize;
                bz = m_originZ + (d / m_cellsX + 0.5f) * m_cellSize;
            }
            ChannelHit h = segmentChannel(worldX, worldZ, ax, az, bx, bz, ord);
            if (h.hit) best = h;
        }
    return best;
}

FlowField::NearestChannel FlowField::nearestChannel(float worldX, float worldZ, float searchRadius,
                                                   int minOrder) const {
    NearestChannel best;
    if (m_cellsX <= 0 || m_cellsZ <= 0) return best;
    const int ci = static_cast<int>(std::floor((worldX - m_originX) / m_cellSize));
    const int cj = static_cast<int>(std::floor((worldZ - m_originZ) / m_cellSize));
    // A channel segment whose centreline comes within `searchRadius` of the point has its cell centre
    // within searchRadius + cellSize of it (segment length = one orthogonal cellSize hop; see channelAt).
    const int r = 1 + static_cast<int>(std::ceil(searchRadius / m_cellSize));
    for (int j = cj - r; j <= cj + r; ++j)
        for (int i = ci - r; i <= ci + r; ++i) {
            if (i < 0 || j < 0 || i >= m_cellsX || j >= m_cellsZ) continue;
            const int rc = j * m_cellsX + i;
            const int ord = m_order[rc];
            if (ord < minOrder) continue;   // default 3: orders 1-2 cut no valley (see header)
            const float ax = m_originX + (i + 0.5f) * m_cellSize;
            const float az = m_originZ + (j + 0.5f) * m_cellSize;
            float bx = ax, bz = az;  // sink → degenerate segment (a point at the cell centre)
            const int d = m_downstream[rc];
            if (d != rc && d >= 0 && static_cast<size_t>(d) < m_downstream.size()) {
                bx = m_originX + (d % m_cellsX + 0.5f) * m_cellSize;
                bz = m_originZ + (d / m_cellsX + 0.5f) * m_cellSize;
            }
            const float dist = pointSegDist(worldX, worldZ, ax, az, bx, bz);
            if (dist < best.dist) { best.dist = dist; best.order = ord; }
        }
    return best;
}

std::vector<int> FlowField::computeStrahler(const std::vector<int>& downstream,
                                            const std::vector<int>& accum, int threshold) {
    const size_t n = std::min(downstream.size(), accum.size());
    std::vector<int> order(n, 0);
    if (n == 0) return order;

    // Process river cells upstream-first (ascending accum: a cell's downstream always has strictly
    // greater accum, so this is a valid topological order). At each cell, the max incoming river
    // order + how many tributaries carry it decide the Strahler rule.
    std::vector<int> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return accum[a] < accum[b]; });

    std::vector<int> maxIn(n, 0), cntAtMax(n, 0);
    for (int c : idx) {
        if (accum[c] <= threshold) continue;                 // not a river → order stays 0
        order[c] = (maxIn[c] == 0) ? 1                       // headwater
                                   : (cntAtMax[c] >= 2 ? maxIn[c] + 1 : maxIn[c]);  // Strahler rule
        int d = downstream[c];
        if (d != c && d >= 0 && static_cast<size_t>(d) < n && accum[d] > threshold) {
            if (order[c] > maxIn[d]) { maxIn[d] = order[c]; cntAtMax[d] = 1; }
            else if (order[c] == maxIn[d]) { ++cntAtMax[d]; }
        }
    }
    return order;
}

int FlowField::orderAt(float worldX, float worldZ) const {
    if (m_cellsX <= 0 || m_cellsZ <= 0) return 0;
    int i = static_cast<int>(std::floor((worldX - m_originX) / m_cellSize));
    int j = static_cast<int>(std::floor((worldZ - m_originZ) / m_cellSize));
    if (i < 0 || j < 0 || i >= m_cellsX || j >= m_cellsZ) return 0;
    return m_order[static_cast<size_t>(j) * m_cellsX + i];
}

int FlowField::accumAt(float worldX, float worldZ) const {
    if (m_cellsX <= 0 || m_cellsZ <= 0) return 0;
    int i = static_cast<int>(std::floor((worldX - m_originX) / m_cellSize));
    int j = static_cast<int>(std::floor((worldZ - m_originZ) / m_cellSize));
    if (i < 0 || j < 0 || i >= m_cellsX || j >= m_cellsZ) return 0;
    return m_accum[static_cast<size_t>(j) * m_cellsX + i];
}

glm::vec2 FlowField::flowDirAt(float worldX, float worldZ) const {
    if (m_cellsX <= 0 || m_cellsZ <= 0) return glm::vec2(0.0f);
    int i = static_cast<int>(std::floor((worldX - m_originX) / m_cellSize));
    int j = static_cast<int>(std::floor((worldZ - m_originZ) / m_cellSize));
    if (i < 0 || j < 0 || i >= m_cellsX || j >= m_cellsZ) return glm::vec2(0.0f);
    const size_t c = static_cast<size_t>(j) * m_cellsX + i;
    if (c >= m_downstream.size()) return glm::vec2(0.0f);
    const int d = m_downstream[c];
    if (d < 0 || d == static_cast<int>(c)) return glm::vec2(0.0f);   // sink: water stops here
    // Cell index -> cell coords -> the offset toward the drainage target. Neighbours are adjacent
    // (steepest descent over the 8-neighbourhood), so this is a unit or diagonal step.
    const int di = (d % m_cellsX) - i;
    const int dj = (d / m_cellsX) - j;
    if (di == 0 && dj == 0) return glm::vec2(0.0f);
    const glm::vec2 v(static_cast<float>(di), static_cast<float>(dj));
    return v / std::sqrt(v.x * v.x + v.y * v.y);
}

}  // namespace Phyxel
