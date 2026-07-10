#include "core/FlowField.h"

#include "core/PriorityFlood.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace Phyxel {

FlowField::FlowField(const HeightFunc& heightAt, float originX, float originZ,
                     int cellsX, int cellsZ, float cellSize, float seaLevel)
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
}

int FlowField::accumAt(float worldX, float worldZ) const {
    if (m_cellsX <= 0 || m_cellsZ <= 0) return 0;
    int i = static_cast<int>(std::floor((worldX - m_originX) / m_cellSize));
    int j = static_cast<int>(std::floor((worldZ - m_originZ) / m_cellSize));
    if (i < 0 || j < 0 || i >= m_cellsX || j >= m_cellsZ) return 0;
    return m_accum[static_cast<size_t>(j) * m_cellsX + i];
}

}  // namespace Phyxel
