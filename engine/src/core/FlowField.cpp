#include "core/FlowField.h"

#include "core/PriorityFlood.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>

namespace Phyxel {

FlowField::FlowField(const HeightFunc& heightAt, float originX, float originZ,
                     int cellsX, int cellsZ, float cellSize, float seaLevel, int riverThreshold,
                     const std::vector<Inflow>& inflows, bool areaLaw)
    : m_originX(originX), m_originZ(originZ), m_cellSize(cellSize > 0.0f ? cellSize : 1.0f),
      m_cellsX(cellsX > 0 ? cellsX : 0), m_cellsZ(cellsZ > 0 ? cellsZ : 0), m_inflows(inflows),
      m_areaLaw(areaLaw) {
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
    // M2b: macro inflows (D15) join the count BEFORE the pass, so they ride downstream.
    for (const Inflow& in : m_inflows)
        if (in.cell >= 0 && static_cast<size_t>(in.cell) < n && in.extraCells > 0) m_accum[in.cell] += in.extraCells;
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

    // M2b-2: per-cell channel geometry + class, fixed at bake time so every query reads one
    // number. Law (areaLaw) or the per-order table; class by centreline depth (law) or order (table).
    m_halfW.assign(n, 0.0f);
    m_fullD.assign(n, 0.0f);
    m_carved.assign(n, 0);
    const bool useLaw = m_areaLaw;   // v2: the grounded law; v1: the per-order table (D10)
    const float cellArea = m_cellSize * m_cellSize;
    for (size_t c = 0; c < n; ++c) {
        const int ord = m_order[c];
        if (ord <= 0) continue;
        float hw, fd;
        if (useLaw) {
            const float aKm2 = static_cast<float>(m_accum[c]) * cellArea * 1e-6f;
            hw = lawHalfWidth(aKm2);
            fd = lawDepth(aKm2);
        } else {
            hw = channelHalfWidth(ord);
            fd = channelDepth(ord);
        }
        const bool carved = useLaw ? (fd >= kRiverCarveDepth) : (ord >= 3);
        m_halfW[c] = hw;
        m_fullD[c] = fd;
        m_carved[c] = carved ? 1 : 0;
        m_maxHalfW = std::max(m_maxHalfW, hw);
        if (!carved) m_maxCreekHalfW = std::max(m_maxCreekHalfW, hw);
    }

    // M2d-1: the longitudinal bed profile (areaLaw only). Upstream-first by ascending accum (a
    // cell's downstream always has strictly greater accum), the bed at a cell is the minimum of its
    // own (sampled height − depth) and whatever its river inflows carry; a coarse basin cell is not
    // carved or filled and carries its spill (the filled level) downstream, so a river leaves a
    // basin at the basin's rim, never below its floor.
    if (m_areaLaw) {
        m_bedY.assign(n, 0.0f);
        m_carryY.assign(n, 0.0f);
        m_basin.assign(n, 0);
        for (size_t c = 0; c < n; ++c) {
            if (m_order[c] < 1) continue;
            m_basin[c] = (fr.filled[c] > base[c] + 1e-3f) ? 1 : 0;
            m_bedY[c] = base[c] - m_fullD[c];
            m_carryY[c] = m_basin[c] ? fr.filled[c] : m_bedY[c];
        }
        std::vector<int> idx;
        idx.reserve(n);
        for (size_t c = 0; c < n; ++c) if (m_order[c] >= 1) idx.push_back(static_cast<int>(c));
        std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return m_accum[a] < m_accum[b]; });
        for (int c : idx) {
            const int d = m_downstream[c];
            if (d == c || d < 0 || static_cast<size_t>(d) >= n || m_order[d] < 1 || m_basin[d]) continue;
            if (m_carryY[c] < m_bedY[d]) { m_bedY[d] = m_carryY[c]; m_carryY[d] = m_bedY[d]; }
        }

        // M2d-2: junction fillets. Inflows per cell, then at every confluence the tributaries
        // (every inflow but the largest-accum one) get a Bézier last segment.
        m_filletOf.assign(n, -1);
        std::vector<std::vector<int>> inflows(n);
        for (int c : idx) {
            const int d = m_downstream[c];
            if (d != c && d >= 0 && static_cast<size_t>(d) < n && m_order[d] >= 1) inflows[d].push_back(c);
        }
        auto centreOf = [&](int c) {
            return glm::vec2(m_originX + (c % m_cellsX + 0.5f) * m_cellSize, m_originZ + (c / m_cellsX + 0.5f) * m_cellSize);
        };
        // bed slope over k cells along the largest-accum inflow chain ending at c (rise / run, >= 0)
        auto slopeUp = [&](int c, int k) {
            int cur = c, steps = 0;
            while (steps < k && !inflows[cur].empty()) {
                int best = inflows[cur][0];
                for (int u : inflows[cur]) if (m_accum[u] > m_accum[best]) best = u;
                cur = best; ++steps;
            }
            if (steps == 0) return 0.0f;
            return std::max(0.0f, (m_bedY[cur] - m_bedY[c]) / (steps * m_cellSize));
        };
        auto slopeDown = [&](int c, int k) {
            int cur = c, steps = 0;
            while (steps < k) {
                const int d = m_downstream[cur];
                if (d == cur || d < 0 || static_cast<size_t>(d) >= n || m_order[d] < 1) break;
                cur = d; ++steps;
            }
            if (steps == 0) return 0.0f;
            return std::max(0.0f, (m_bedY[c] - m_bedY[cur]) / (steps * m_cellSize));
        };
        const float kMin = glm::radians(kJunctionMinDeg), kMax = glm::radians(kJunctionMaxDeg);
        for (size_t j = 0; j < n; ++j) {
            // Junctions inside coarse basins get fillets too: the centreline is data there (no
            // carve), and leaving them on the grid kept 24 % of all junctions at 90° (measured).
            if (m_order[j] < 1 || inflows[j].size() < 2) continue;
            const int dj = m_downstream[j];
            if (dj == static_cast<int>(j) || dj < 0 || static_cast<size_t>(dj) >= n) continue;   // no downstream direction
            int mainIn = inflows[j][0];
            for (int u : inflows[j]) if (m_accum[u] > m_accum[mainIn]) mainIn = u;
            const glm::vec2 P2 = centreOf(static_cast<int>(j));
            const glm::vec2 D = glm::normalize(centreOf(dj) - P2);
            const float sMain = slopeDown(static_cast<int>(j), kFilletSlopeCells);
            for (int t : inflows[j]) {
                if (t == mainIn) continue;
                const float sTrib = slopeUp(t, kFilletSlopeCells) ;
                // Howard: cos θ = S_main / S_trib; a tributary no steeper than the stem joins at the
                // band's minimum, one on a flat stem at its maximum.
                float theta = (sTrib > 1e-6f) ? std::acos(std::clamp(sMain / sTrib, 0.0f, 1.0f)) : kMin;
                theta = std::clamp(theta, kMin, kMax);
                const glm::vec2 P0 = centreOf(t);
                const float side = (D.x * (P0.y - P2.y) - D.y * (P0.x - P2.x)) >= 0.0f ? 1.0f : -1.0f;   // which side the tributary comes from
                // the arrival velocity: D rotated AWAY from the tributary's side by θ
                const float ang = -side * theta;
                const glm::vec2 v(D.x * std::cos(ang) - D.y * std::sin(ang), D.x * std::sin(ang) + D.y * std::cos(ang));
                const float L = 0.5f * glm::length(P2 - P0);
                const glm::vec2 P1 = P2 - v * L;
                std::vector<glm::vec2> pts;
                pts.reserve(kFilletSegments + 1);
                for (int s = 0; s <= kFilletSegments; ++s) {
                    const float u = static_cast<float>(s) / kFilletSegments, w = 1.0f - u;
                    pts.push_back(P0 * (w * w) + P1 * (2.0f * w * u) + P2 * (u * u));
                }
                m_filletOf[t] = static_cast<int>(m_fillets.size());
                m_fillets.push_back(std::move(pts));
            }
        }

        // M2d-3: the local width field — max half-width within the radius, then a separable box
        // blur over the same radius (two passes), so the warp's wavelength changes smoothly.
        {
            const int R = kLocalWidthRadius;
            std::vector<float> a(n, 0.0f), b(n, 0.0f);
            for (int j = 0; j < m_cellsZ; ++j)
                for (int i = 0; i < m_cellsX; ++i) {
                    float m = 0.0f;
                    for (int dj = -R; dj <= R; ++dj)
                        for (int di = -R; di <= R; ++di) {
                            const int ii = i + di, jj = j + dj;
                            if (ii < 0 || jj < 0 || ii >= m_cellsX || jj >= m_cellsZ) continue;
                            m = std::max(m, m_halfW[static_cast<size_t>(jj) * m_cellsX + ii]);
                        }
                    a[static_cast<size_t>(j) * m_cellsX + i] = m;
                }
            const float inv = 1.0f / (2 * R + 1);
            for (int j = 0; j < m_cellsZ; ++j)
                for (int i = 0; i < m_cellsX; ++i) {
                    float sum = 0.0f;
                    for (int di = -R; di <= R; ++di) sum += a[static_cast<size_t>(j) * m_cellsX + std::clamp(i + di, 0, m_cellsX - 1)];
                    b[static_cast<size_t>(j) * m_cellsX + i] = sum * inv;
                }
            m_localHalfW.assign(n, 0.0f);
            for (int j = 0; j < m_cellsZ; ++j)
                for (int i = 0; i < m_cellsX; ++i) {
                    float sum = 0.0f;
                    for (int dj = -R; dj <= R; ++dj) sum += b[static_cast<size_t>(std::clamp(j + dj, 0, m_cellsZ - 1)) * m_cellsX + i];
                    m_localHalfW[static_cast<size_t>(j) * m_cellsX + i] = sum * inv;
                }
        }
    }
}

float FlowField::localHalfWidthAt(float worldX, float worldZ) const {
    if (m_localHalfW.empty() || m_cellsX <= 1 || m_cellsZ <= 1) return 0.0f;
    const float u = (worldX - m_originX) / m_cellSize - 0.5f, v = (worldZ - m_originZ) / m_cellSize - 0.5f;
    const float fu = std::floor(u), fv = std::floor(v);
    const float tu = u - fu, tv = v - fv;
    const int i0 = std::clamp(static_cast<int>(fu), 0, m_cellsX - 1), i1 = std::clamp(static_cast<int>(fu) + 1, 0, m_cellsX - 1);
    const int j0 = std::clamp(static_cast<int>(fv), 0, m_cellsZ - 1), j1 = std::clamp(static_cast<int>(fv) + 1, 0, m_cellsZ - 1);
    auto at = [&](int i, int j) { return m_localHalfW[static_cast<size_t>(j) * m_cellsX + i]; };
    return (at(i0, j0) * (1 - tu) + at(i1, j0) * tu) * (1 - tv) + (at(i0, j1) * (1 - tu) + at(i1, j1) * tu) * tv;
}

const std::vector<glm::vec2>& FlowField::cellPolyline(int rc, std::vector<glm::vec2>& scratch) const {
    scratch.clear();
    if (rc < 0 || static_cast<size_t>(rc) >= m_order.size()) return scratch;
    // A fillet is returned by reference (no copy per scanned cell: the per-column cost gate).
    if (!m_filletOf.empty() && m_filletOf[rc] >= 0) return m_fillets[static_cast<size_t>(m_filletOf[rc])];
    const glm::vec2 a(m_originX + (rc % m_cellsX + 0.5f) * m_cellSize, m_originZ + (rc / m_cellsX + 0.5f) * m_cellSize);
    scratch.push_back(a);
    const int d = m_downstream[rc];
    if (d != rc && d >= 0 && static_cast<size_t>(d) < m_downstream.size())
        scratch.push_back(glm::vec2(m_originX + (d % m_cellsX + 0.5f) * m_cellSize, m_originZ + (d / m_cellsX + 0.5f) * m_cellSize));
    else
        scratch.push_back(a);   // sink: a degenerate segment
    return scratch;
}

std::vector<glm::vec2> FlowField::centrelineAt(float worldX, float worldZ) const {
    std::vector<glm::vec2> out;
    if (m_cellsX <= 0 || m_cellsZ <= 0) return out;
    const int i = static_cast<int>(std::floor((worldX - m_originX) / m_cellSize));
    const int j = static_cast<int>(std::floor((worldZ - m_originZ) / m_cellSize));
    if (i < 0 || j < 0 || i >= m_cellsX || j >= m_cellsZ) return out;
    const int rc = j * m_cellsX + i;
    if (m_order[rc] < 1) return out;
    std::vector<glm::vec2> scratch;
    out = cellPolyline(rc, scratch);
    return out;
}

float FlowField::lawHalfWidth(float areaKm2) {
    return areaKm2 > 0.0f ? 0.5f * kLawWidthCoeffU * std::pow(areaKm2, kLawWidthExp) : 0.0f;
}

float FlowField::lawDepth(float areaKm2) {
    return areaKm2 > 0.0f ? kLawDepthCoeffU * std::pow(areaKm2, kLawDepthExp) : 0.0f;
}

namespace {
// Distance from point p to segment a→b in the XZ plane.
// Projection parameter of p on segment a→b, clamped to [0,1] (M2d-1: where along the segment
// the longitudinal profile is read).
float segmentParam(float px, float pz, float ax, float az, float bx, float bz) {
    const float abx = bx - ax, abz = bz - az;
    const float ab2 = abx * abx + abz * abz;
    float t = (ab2 > 0.0f) ? ((px - ax) * abx + (pz - az) * abz) / ab2 : 0.0f;
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

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
    // Orders 1-2 are CREEKS (small-scale plan Phase 2a): sub-voxel WATER depths — thirds of a
    // voxel, matching the subcube step — that the water runtime pins as a fractional ribbon
    // (clamped to the sim's MIN_HOLD so it can never sheet). They still cut no TERRAIN: the
    // generator clamps the surface carve to order ≥ 3 (a 0.66 centreline would lround to a full
    // voxel trench), and nearestChannel still skips them for valley shaping.
    static const float dp[6] = {0.33f, 0.66f, 1.0f, 1.0f, 1.0f, 2.0f};
    if (order < 1) return 0.0f;
    return dp[order > 6 ? 5 : order - 1];
}

FlowField::ChannelHit FlowField::segmentChannel(float px, float pz, float ax, float az,
                                                float bx, float bz, int order) {
    if (order < 1) return {};                      // not a river cell
    return segmentChannel(px, pz, ax, az, bx, bz, order, channelHalfWidth(order), channelDepth(order), order >= 3);
}

FlowField::ChannelHit FlowField::segmentChannel(float px, float pz, float ax, float az,
                                                float bx, float bz, int order,
                                                float halfWidth, float fullDepth, bool carved) {
    ChannelHit h;
    if (order < 1 || halfWidth <= 0.0f) return h;  // not a river cell
    const float dist = pointSegDist(px, pz, ax, az, bx, bz);
    if (dist >= halfWidth) return h;
    const float t = dist / halfWidth;              // parabolic bed: full depth at centre, 0 at edge
    h.hit = true;
    h.order = order;
    h.depth = fullDepth * (1.0f - t * t);
    h.halfWidth = halfWidth;
    h.fullDepth = fullDepth;
    h.carved = carved;
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
    // segment length grows to cellSize·√2 and this radius must grow too. At the real 128-unit bake
    // cell (WorldGenerator kHydroCell) it is 5×5 (r=2, since ceil(11/128)=1); it stays correct for
    // small cellSize / large orders.
    const int r = 1 + static_cast<int>(std::ceil(m_maxHalfW / m_cellSize));
    thread_local std::vector<glm::vec2> plScratch;   // reused across cells and queries: no allocation per column
    for (int j = cj - r; j <= cj + r; ++j)
        for (int i = ci - r; i <= ci + r; ++i) {
            if (i < 0 || j < 0 || i >= m_cellsX || j >= m_cellsZ) continue;
            const int rc = j * m_cellsX + i;
            const int ord = m_order[rc];
            // Keep the DEEPEST hit (centreline depth, then order). Under the per-order table this
            // is exactly the old max-order rule (depth is non-decreasing in order; ties by order).
            const float fd = m_fullD[rc];
            if (fd < best.fullDepth || (fd == best.fullDepth && ord <= best.order)) continue;
            const int d = m_downstream[rc];
            // M2d-2: the cell's channel is a polyline (a straight segment, or a Bézier fillet into a
            // junction); the hit is the closest sub-segment, `t` the fraction of the polyline length.
            const std::vector<glm::vec2>& pl = cellPolyline(rc, plScratch);
            float bestDist = 1e30f, bestT = 0.0f, lenSum = 0.0f;
            float total = 0.0f;
            for (size_t s = 0; s + 1 < pl.size(); ++s) total += glm::length(pl[s + 1] - pl[s]);
            for (size_t s = 0; s + 1 < pl.size(); ++s) {
                const float dist = pointSegDist(worldX, worldZ, pl[s].x, pl[s].y, pl[s + 1].x, pl[s + 1].y);
                const float segLen = glm::length(pl[s + 1] - pl[s]);
                if (dist < bestDist) {
                    bestDist = dist;
                    bestT = total > 0.0f ? (lenSum + segmentParam(worldX, worldZ, pl[s].x, pl[s].y, pl[s + 1].x, pl[s + 1].y) * segLen) / total : 0.0f;
                }
                lenSum += segLen;
            }
            if (pl.size() < 2) continue;
            ChannelHit h;
            if (bestDist < m_halfW[rc] && m_halfW[rc] > 0.0f) {
                const float tt = bestDist / m_halfW[rc];
                h.hit = true; h.order = ord; h.depth = fd * (1.0f - tt * tt);
                h.halfWidth = m_halfW[rc]; h.fullDepth = fd; h.carved = m_carved[rc] != 0;
                h.cell = rc;
            }
            if (h.hit && m_areaLaw && !m_bedY.empty()) {
                // M2d-1: the profile at the hit — from this cell's bed to what it hands downstream
                // (its own downstream cell's bed, or a basin's spill), linear along the polyline.
                const float t = bestT;
                // A basin's spill can sit above the incoming bed (the river's own depth): never let a
                // segment rise — the river enters the basin at its bed, not at the rim.
                const float endY = (d != rc && d >= 0 && static_cast<size_t>(d) < m_carryY.size())
                                       ? std::min(m_carryY[d], m_bedY[rc]) : m_bedY[rc];
                h.bedProfile = m_bedY[rc] + (endY - m_bedY[rc]) * t;
                h.hasProfile = true;
                h.inBasin = m_basin[rc] != 0;
                h.bedSlope = total > 0.0f ? std::max(0.0f, (m_bedY[rc] - endY) / total) : 0.0f;
            }
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
    thread_local std::vector<glm::vec2> plScratch;   // reused: no allocation per column
    for (int j = cj - r; j <= cj + r; ++j)
        for (int i = ci - r; i <= ci + r; ++i) {
            if (i < 0 || j < 0 || i >= m_cellsX || j >= m_cellsZ) continue;
            const int rc = j * m_cellsX + i;
            const int ord = m_order[rc];
            // default 3 = CARVED rivers only (the valley pass); 1 = every channel (the creek swale).
            // Under the table carved == order >= 3, so this is the old minOrder rule exactly.
            if (minOrder >= 3 ? m_carved[rc] == 0 : ord < minOrder) continue;
            const std::vector<glm::vec2>& pl = cellPolyline(rc, plScratch);   // M2d-2: straight segment or junction fillet
            float dist = 1e30f;
            for (size_t s = 0; s + 1 < pl.size(); ++s)
                dist = std::min(dist, pointSegDist(worldX, worldZ, pl[s].x, pl[s].y, pl[s + 1].x, pl[s + 1].y));
            if (dist < best.dist) { best.dist = dist; best.order = ord; best.halfWidth = m_halfW[rc]; best.carved = m_carved[rc] != 0; }
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

float FlowField::halfWidthAt(float worldX, float worldZ) const {
    if (m_cellsX <= 0 || m_cellsZ <= 0) return 0.0f;
    const int i = static_cast<int>(std::floor((worldX - m_originX) / m_cellSize));
    const int j = static_cast<int>(std::floor((worldZ - m_originZ) / m_cellSize));
    if (i < 0 || j < 0 || i >= m_cellsX || j >= m_cellsZ) return 0.0f;
    return m_halfW[static_cast<size_t>(j) * m_cellsX + i];
}

float FlowField::fullDepthAt(float worldX, float worldZ) const {
    if (m_cellsX <= 0 || m_cellsZ <= 0) return 0.0f;
    const int i = static_cast<int>(std::floor((worldX - m_originX) / m_cellSize));
    const int j = static_cast<int>(std::floor((worldZ - m_originZ) / m_cellSize));
    if (i < 0 || j < 0 || i >= m_cellsX || j >= m_cellsZ) return 0.0f;
    return m_fullD[static_cast<size_t>(j) * m_cellsX + i];
}

bool FlowField::carvedRiverAt(float worldX, float worldZ) const {
    if (m_cellsX <= 0 || m_cellsZ <= 0) return false;
    const int i = static_cast<int>(std::floor((worldX - m_originX) / m_cellSize));
    const int j = static_cast<int>(std::floor((worldZ - m_originZ) / m_cellSize));
    if (i < 0 || j < 0 || i >= m_cellsX || j >= m_cellsZ) return false;
    return m_carved[static_cast<size_t>(j) * m_cellsX + i] != 0;
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
