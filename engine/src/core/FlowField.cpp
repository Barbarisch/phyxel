#include "core/FlowField.h"

#include "core/PriorityFlood.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>

namespace Phyxel {

namespace {   // the polyline helpers, defined with channelAt below; the M2d-4 bake reads them first
float segmentParam(float px, float pz, float ax, float az, float bx, float bz);
float pointSegDist(float px, float pz, float ax, float az, float bx, float bz);
}  // namespace

FlowField::FlowField(const HeightFunc& heightAt, float originX, float originZ,
                     int cellsX, int cellsZ, float cellSize, float seaLevel, int riverThreshold,
                     const std::vector<Inflow>& inflows, bool areaLaw, const WorldFns& fns)
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

        m_bedY.assign(n, 0.0f);
        m_carryY.assign(n, 0.0f);
        m_basin.assign(n, 0);
        std::vector<float> groundK(n, 0.0f);   // per river cell: the ground the bed is cut from (lowered by the valley rounds below)
        auto centreOfCell = [&](int c) {
            return glm::vec2(m_originX + (c % m_cellsX + 0.5f) * m_cellSize, m_originZ + (c / m_cellsX + 0.5f) * m_cellSize);
        };
        for (size_t c = 0; c < n; ++c) {
            if (m_order[c] < 1) continue;
            // A coarse basin is a flooded cell OR a sub-sea cell: the sea is the basin every river ends in
            // (HydrologyMap clamps its filled surface to sea level; the same meaning here).
            m_basin[c] = (fr.filled[c] > base[c] + 1e-3f || base[c] < seaLevel - 1e-3f) ? 1 : 0;
            float ground = base[c];
            // M2d-4 (D37): a CARVED channel's base is the LOWEST natural over its warped footprint —
            // the straight segment to its downstream centre at 9 stations, each at the centreline and
            // ± the half-width (DESIGN: 27 samples), every one at the world column the generator's
            // warp lands it on — not the cell-centre point sample, which on mountain relief stood
            // 100–250 u above the land the channel actually runs through (the river on a wall;
            // m2d4_red_green.txt). The residual is bounded by the sampling and asserted ≤ 1 u by
            // RiverGeometryTest.ChannelSectionsMeetTheLandAtABoundedSlope. Creeks never fill, so
            // they keep the point sample; basins keep their floor.
            if (fns.toWorld && fns.naturalAt && m_carved[c] && !m_basin[c]) {
                const int d = m_downstream[static_cast<int>(c)];
                const glm::vec2 a = centreOfCell(static_cast<int>(c));
                const glm::vec2 b = (d != static_cast<int>(c) && d >= 0 && static_cast<size_t>(d) < n) ? centreOfCell(d) : a;
                const glm::vec2 dir = b - a;
                const float len = glm::length(dir);
                const glm::vec2 nrm = len > 0.0f ? glm::vec2(-dir.y, dir.x) / len : glm::vec2(0.0f);
                ground = 1e30f;
                for (int side = -1; side <= 1; ++side) {
                    glm::vec2 hint = a + nrm * (static_cast<float>(side) * m_halfW[c]);   // the inversion continues along each line
                    for (int s = 0; s <= 8; ++s) {   // 9 stations (every 4 u on a 32 u cell): GREEN 4 left 1.8 u between 8 u stations
                        const glm::vec2 p = a + dir * (0.125f * s) + nrm * (static_cast<float>(side) * m_halfW[c]);
                        const glm::vec2 w = fns.toWorld(p.x, p.y, *this, hint);
                        hint = w + dir * 0.125f;
                        ground = std::min(ground, fns.naturalAt(w.x, w.y));
                    }
                }
            }
            groundK[c] = ground;
        }
        std::vector<int> idx;
        idx.reserve(n);
        for (size_t c = 0; c < n; ++c) if (m_order[c] >= 1) idx.push_back(static_cast<int>(c));
        std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return m_accum[a] < m_accum[b]; });
        auto propagateProfile = [&]() {
            for (size_t c = 0; c < n; ++c) {
                if (m_order[c] < 1) continue;
                m_bedY[c] = groundK[c] - m_fullD[c];
                m_carryY[c] = m_basin[c] ? fr.filled[c] : m_bedY[c];
            }
            for (int c : idx) {
                const int d = m_downstream[c];
                if (d == c || d < 0 || static_cast<size_t>(d) >= n || m_order[d] < 1 || m_basin[d]) continue;
                if (m_carryY[c] < m_bedY[d]) { m_bedY[d] = m_carryY[c]; m_carryY[d] = m_bedY[d]; }
            }
        };
        propagateProfile();

        // M2d-2: junction fillets. Inflows per cell, then at every confluence the tributaries
        // (every inflow but the largest-accum one) get a Bézier last segment.
        m_filletOf.assign(n, -1);
        std::vector<std::vector<int>> inflows(n);
        const bool kSpringsEnabled = true;
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

        // M2e: spring sites from the same inflow structure (heads, basin feeds, basin outlets).
        if (kSpringsEnabled) {
            m_springs.clear();
            for (size_t c = 0; c < n; ++c) {
                if (m_order[c] < 1) continue;
                const int d = m_downstream[c];
                const bool hasDown = d != static_cast<int>(c) && d >= 0 && static_cast<size_t>(d) < n;
                Spring s;
                s.cell = static_cast<int>(c);
                s.x = m_originX + (static_cast<int>(c) % m_cellsX + 0.5f) * m_cellSize;
                s.z = m_originZ + (static_cast<int>(c) / m_cellsX + 0.5f) * m_cellSize;
                s.catchmentCells = m_accum[c];
                if (!m_basin[c] && inflows[c].empty()) { s.kind = 0; m_springs.push_back(s); }
                if (!m_basin[c] && hasDown && m_basin[d]) { s.kind = 1; m_springs.push_back(s); }
                if (m_basin[c] && hasDown && !m_basin[d]) { s.kind = 2; m_springs.push_back(s); }
            }
        }

        // M2d-4 (D36): the valley-envelope SOURCE lists, in WORLD space. The land beside a carved
        // channel may not stand above rim + kValleySideSlope x (max(0, perp − halfWidth x
        // kValleyWidthMul) + overshoot) — the flat floor across the perpendicular distance, then the
        // side at the hillslope bound; `overshoot` is the distance a column lies BEYOND an end of a
        // source's polyline along it (no floor allowance there: the next reach's floor takes over —
        // without it a downstream reach 20 u lower undercut the upstream reach's banks through the
        // floor allowance, GREEN 4/5). Every source's channel polyline is inverse-warped ONCE here
        // into world space with the rim at each station, so the per-column query is plain world
        // geometry and the side is <= kValleySideSlope per world u by construction. The lowest such
        // envelope over the channels that reach the column wins. A side reaches hundreds of u on
        // mountain relief, far beyond any per-column scan, so each source FLOODS outward over the
        // cell grid here and every cell keeps the list of sources that can win somewhere inside it:
        // per cell a source has a lower bound (its rim's low end, the distance less the half-diagonal)
        // and an upper bound (rim high end, twice the distance plus the half-diagonal — the overshoot
        // term at most doubles it); a source whose lower bound exceeds another's upper bound never
        // wins inside that cell and is dropped; a cell where the lower bound stands kEnvNaturalMargin
        // above the natural at its centre is neither entered nor expanded from (the crest). The query
        // is then EXACT: the minimum over the cell's few sources.
        if (fns.toWorld && fns.naturalAt) {
            auto pointAt = [](const std::vector<glm::vec2>& pl, float frac) {   // by arclength fraction
                float total = 0.0f;
                for (size_t q = 0; q + 1 < pl.size(); ++q) total += glm::length(pl[q + 1] - pl[q]);
                float want = frac * total, acc = 0.0f;
                for (size_t q = 0; q + 1 < pl.size(); ++q) {
                    const float L = glm::length(pl[q + 1] - pl[q]);
                    if (acc + L >= want || q + 2 == pl.size()) {
                        const float u = L > 0.0f ? std::clamp((want - acc) / L, 0.0f, 1.0f) : 0.0f;
                        return pl[q] + (pl[q + 1] - pl[q]) * u;
                    }
                    acc += L;
                }
                return pl.back();
            };
            // the carved band's measured half-width in WORLD columns (the flood below measures it: where the
            // warp expands, the band is wider than the half-width and a slope-shrunk floor ended INSIDE it,
            // the side rising at the bank - GREEN 21's lips at 1.3 u); the floor is never narrower than it
            std::vector<float> bandHalfWorld(n, 0.0f);
            auto buildEnvelope = [&]() {
            m_envPolyIdx.assign(n, -1);
            m_envPolys.clear();
            m_envRims.clear();
            m_envFloorHalf.clear();
            m_envFolded.clear();
            std::vector<glm::vec2> plScratch;
            for (int k = 0; k < static_cast<int>(n); ++k) {
                if (m_order[k] < 1 || !m_carved[k] || m_basin[k]) continue;
                const std::vector<glm::vec2>& pl = cellPolyline(k, plScratch);
                if (pl.size() < 2) continue;
                const int d = m_downstream[k];
                const float endY = (d != k && d >= 0 && static_cast<size_t>(d) < n) ? std::min(m_carryY[d], m_bedY[k]) : m_bedY[k];
                std::vector<glm::vec2> wp;
                std::vector<float> wr;
                glm::vec2 hint = pl.front(), prevQ = pl.front();
                for (int st = 0; st <= 8; ++st) {
                    const float t = static_cast<float>(st) / 8.0f;
                    const glm::vec2 q = pointAt(pl, t);
                    const glm::vec2 w = fns.toWorld(q.x, q.y, *this, hint + (q - prevQ));   // continue along one branch
                    hint = w; prevQ = q;
                    wp.push_back(w);
                    wr.push_back(m_bedY[k] + (endY - m_bedY[k]) * t + m_fullD[k]);
                }
                // the bed slope along the reach (bake units, as channelAt's bedSlope) sets the floor
                float bakeLen = 0.0f;
                for (size_t q = 0; q + 1 < pl.size(); ++q) bakeLen += glm::length(pl[q + 1] - pl[q]);
                const float bedSlope = bakeLen > 0.0f ? std::max(0.0f, (m_bedY[k] - endY) / bakeLen) : 0.0f;
                // a folded polyline: a station step reversing against the previous one, or the world
                // length under a third of the bake length (the warp's gradient beyond 1 folds it)
                bool folded = false;
                float worldLen = 0.0f;
                for (size_t q = 0; q + 1 < wp.size(); ++q) {
                    const glm::vec2 a = wp[q + 1] - wp[q];
                    worldLen += glm::length(a);
                    if (q > 0 && glm::dot(a, wp[q] - wp[q - 1]) < 0.0f) folded = true;
                }
                if (bakeLen > 0.0f && worldLen < bakeLen / 3.0f) folded = true;
                m_envPolyIdx[static_cast<size_t>(k)] = static_cast<int>(m_envPolys.size());
                m_envPolys.push_back(std::move(wp));
                m_envRims.push_back(std::move(wr));
                m_envFloorHalf.push_back(std::max(floorHalfFor(m_halfW[k], bedSlope), bandHalfWorld[static_cast<size_t>(k)] + 1.0f));
                m_envFolded.push_back(folded ? 1 : 0);
            }
            struct Cand { int cell, src; float lower, upper; };
            std::vector<Cand> cands;
            const float halfDiag = m_cellSize * 0.70710678f;
            std::vector<int> stamp(n, -1);
            std::vector<int> queue;
            std::vector<float> natC(n, -1e30f);
            auto naturalAtCell = [&](int c) {
                if (natC[static_cast<size_t>(c)] <= -1e29f)
                    natC[static_cast<size_t>(c)] = fns.naturalAt(m_originX + (c % m_cellsX + 0.5f) * m_cellSize, m_originZ + (c / m_cellsX + 0.5f) * m_cellSize);
                return natC[static_cast<size_t>(c)];
            };
            for (int k = 0; k < static_cast<int>(n); ++k) {
                if (m_envPolyIdx[static_cast<size_t>(k)] < 0) continue;
                const std::vector<glm::vec2>& wp = m_envPolys[static_cast<size_t>(m_envPolyIdx[static_cast<size_t>(k)])];
                const std::vector<float>& wr = m_envRims[static_cast<size_t>(m_envPolyIdx[static_cast<size_t>(k)])];
                const float rimLow = *std::min_element(wr.begin(), wr.end()), rimHigh = *std::max_element(wr.begin(), wr.end());
                const float valleyHalf = m_envFloorHalf[static_cast<size_t>(m_envPolyIdx[static_cast<size_t>(k)])];
                queue.clear(); queue.push_back(k); stamp[k] = k;
                for (size_t head = 0; head < queue.size(); ++head) {
                    const int c = queue[head];
                    const float cx = m_originX + (c % m_cellsX + 0.5f) * m_cellSize, cz = m_originZ + (c / m_cellsX + 0.5f) * m_cellSize;
                    float dist = 1e30f;
                    for (size_t q = 0; q + 1 < wp.size(); ++q) dist = std::min(dist, pointSegDist(cx, cz, wp[q].x, wp[q].y, wp[q + 1].x, wp[q + 1].y));
                    const float lower = rimLow + kValleySideSlope * std::max(0.0f, dist - halfDiag - valleyHalf);
                    if (lower >= naturalAtCell(c) + kEnvNaturalMargin) continue;   // above the natural anywhere in c: the crest
                    const float upper = rimHigh + kValleySideSlope * 2.0f * (dist + halfDiag);
                    cands.push_back({c, k, lower, upper});
                    const int ci = c % m_cellsX, cj = c / m_cellsX;
                    for (int dj = -1; dj <= 1; ++dj)
                        for (int di = -1; di <= 1; ++di) {
                            const int ni = ci + di, nj = cj + dj;
                            if ((di == 0 && dj == 0) || ni < 0 || nj < 0 || ni >= m_cellsX || nj >= m_cellsZ) continue;
                            const int nc = nj * m_cellsX + ni;
                            if (stamp[nc] == k) continue;
                            stamp[nc] = k; queue.push_back(nc);
                        }
                }
            }
            std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.cell != b.cell ? a.cell < b.cell : a.lower < b.lower; });
            m_envHead.assign(n, -1);
            m_envPool.clear();
            for (size_t i = 0; i < cands.size();) {
                size_t j = i;
                float minUpper = 1e30f;
                while (j < cands.size() && cands[j].cell == cands[i].cell) { minUpper = std::min(minUpper, cands[j].upper); ++j; }
                int prev = -1;
                for (size_t q = i; q < j; ++q) {
                    if (cands[q].lower > minUpper) break;   // sorted by lower: none past this can win
                    m_envPool.push_back({cands[q].src, -1});
                    const int idx = static_cast<int>(m_envPool.size()) - 1;
                    if (prev < 0) m_envHead[static_cast<size_t>(cands[i].cell)] = idx; else m_envPool[static_cast<size_t>(prev)].next = idx;
                    prev = idx;
                }
                i = j;
            }
            };
            // A channel is never PERCHED above its own valley side: where another reach's envelope
            // (its floor or its side) lies below this reach's rim at the reach's own stations, the
            // reach's ground drops to that envelope and the profile is propagated again - the bed
            // then descends along the valley side instead of standing 30 u above it (GREEN 12: a
            // reach 73 u above the reach 65 world u downstream, the lower side's cone cutting its
            // banks). Three rounds: each round pulls one more cell upstream along a chain steeper
            // than the side; the fillets keep the first round's slopes (a stated simplification).
            // The rule is checked at the reach's BANK stations (the world polyline offset by the
            // half-width along its normal), where a side from a lower reach is lowest - the centreline
            // alone left 13-16 u of perch at the banks (GREEN 14). Up to 8 rounds, until nothing moves.
            constexpr int kPerchRounds = 8;
            const std::vector<float> groundK0 = groundK;   // the D37 footprint minimum the rounds start from
            m_perch = PerchStats{};
            for (size_t c = 0; c < n; ++c) if (m_order[c] >= 1 && m_carved[c] && !m_basin[c]) ++m_perch.reaches;
            // M2d-4 residual (the rule as it stands after the audit): the reach's band is FLOODED in world
            // columns exactly as the sampler carves it (channelAt at the column's warped point: the hit's
            // cell is this reach, carved, with a profile), and at every BANK column beside it (a 4-neighbour
            // that is neither this band, another carved band nor a creek) the envelope the land will be cut
            // to - the minimum over the other sources listed for that column's cell - is compared with the
            // rim the sampler reads at the band column it touches (the profile at that column's projection +
            // the depth). The earlier rule compared each sample with the reach's UPSTREAM base, so at the
            // shared downstream vertex the next reach's rim - lower by this reach's own drop - "undercut" it
            // every round: the rounds translated the profile one cell upstream per round and never converged
            // (the instrumented run in m2d4_red_green.txt). A deficit is closed by an exact lever on the
            // reach's base: the rim at parameter t moves by (1 - t) of the base while the reach stays above
            // its downstream vertex, and by the whole base beyond that (the carry takes the vertex with it).
            constexpr float kPerchTol = 0.25f;   // a quarter voxel: below the 1 u levee count, above float noise
            constexpr int kBandCap = 20000;      // columns per reach: a folded band cannot run away
            std::vector<uint8_t> moved(n, 1), movedPrev(n, 1);
            std::vector<int> lastSrc(n, -1), roundsMoved(n, 0);
            std::vector<float> lastDeficit(n, 0.0f);
            std::vector<uint8_t> visited;
            struct BandCol { int x, z; float rimB, tB; bool fromBand; };
            std::vector<BandCol> queue;
            for (int round = 0; round < kPerchRounds; ++round) {
                buildEnvelope();
                if (round == kPerchRounds - 1) break;
                bool changed = false;
                int changedCount = 0;
                float worstLeft = 0.0f;
                std::swap(moved, movedPrev);
                std::fill(moved.begin(), moved.end(), 0);
                // DOWNSTREAM FIRST (descending accumulation): the envelope's rim is read live from the profile
                // (envelopeOf), so a reach sees the rims its downstream neighbours settled to in this same
                // sweep and a steep chain settles in one round, not one cell per round (patch 20: 7 rounds
                // still moving 595 reaches with 26 u of deficit left)
                for (size_t ii = idx.size(); ii-- > 0;) {
                    const int k = idx[ii];
                    const int pi = m_envPolyIdx[static_cast<size_t>(k)];
                    if (pi < 0) continue;
                    const std::vector<glm::vec2>& wp = m_envPolys[static_cast<size_t>(pi)];
                    // the band's world box: the inverse-warped polyline dilated by three half-widths (the
                    // band is up to that wide where the warp expands) and a margin for the bank columns
                    const float dil = 3.0f * m_halfW[static_cast<size_t>(k)] + 4.0f;
                    float bx0 = 1e30f, bx1 = -1e30f, bz0 = 1e30f, bz1 = -1e30f;
                    for (const glm::vec2& w : wp) { bx0 = std::min(bx0, w.x); bx1 = std::max(bx1, w.x); bz0 = std::min(bz0, w.y); bz1 = std::max(bz1, w.y); }
                    const int x0 = static_cast<int>(std::floor(bx0 - dil)), x1 = static_cast<int>(std::floor(bx1 + dil));
                    const int z0 = static_cast<int>(std::floor(bz0 - dil)), z1 = static_cast<int>(std::floor(bz1 + dil));
                    const int W = x1 - x0 + 1, H = z1 - z0 + 1;
                    if (W <= 0 || H <= 0 || static_cast<long long>(W) * H > 4 * kBandCap) { ++m_perch.overflow; continue; }
                    // only reaches whose sources moved last round (or that moved themselves) are re-read
                    if (round > 0) {
                        bool dirty = movedPrev[static_cast<size_t>(k)] != 0;
                        const int ci0 = std::max(0, static_cast<int>(std::floor((bx0 - dil - m_originX) / m_cellSize)));
                        const int ci1 = std::min(m_cellsX - 1, static_cast<int>(std::floor((bx1 + dil - m_originX) / m_cellSize)));
                        const int cj0 = std::max(0, static_cast<int>(std::floor((bz0 - dil - m_originZ) / m_cellSize)));
                        const int cj1 = std::min(m_cellsZ - 1, static_cast<int>(std::floor((bz1 + dil - m_originZ) / m_cellSize)));
                        for (int cj = cj0; cj <= cj1 && !dirty; ++cj)
                            for (int ci = ci0; ci <= ci1 && !dirty; ++ci)
                                for (int e = m_envHead[static_cast<size_t>(cj) * m_cellsX + ci]; e >= 0; e = m_envPool[static_cast<size_t>(e)].next)
                                    if (movedPrev[static_cast<size_t>(m_envPool[static_cast<size_t>(e)].src)]) { dirty = true; break; }
                        if (!dirty) continue;
                    }
                    visited.assign(static_cast<size_t>(W) * H, 0);
                    queue.clear();
                    for (const glm::vec2& w : wp) {
                        const int sx = static_cast<int>(std::floor(w.x)), sz = static_cast<int>(std::floor(w.y));
                        if (sx < x0 || sx > x1 || sz < z0 || sz > z1) continue;
                        queue.push_back({sx, sz, 0.0f, 0.0f, false});
                    }
                    const int dk = m_downstream[k];
                    const float endYk = (dk != k && dk >= 0 && static_cast<size_t>(dk) < n) ? std::min(m_carryY[dk], m_bedY[k]) : m_bedY[k];
                    const float dropK = std::max(0.0f, m_bedY[k] - endYk);
                    float lowerBy = 0.0f;   // the largest lever this reach's banks demand this round
                    int lowerSrc = -1; float lowerDeficit = 0.0f;
                    int popped = 0;
                    for (size_t head = 0; head < queue.size(); ++head) {
                        const BandCol bc = queue[head];
                        const size_t vi = static_cast<size_t>(bc.z - z0) * W + (bc.x - x0);
                        if (visited[vi]) continue;
                        visited[vi] = 1;
                        if (++popped > kBandCap) { ++m_perch.overflow; break; }
                        ++m_perch.columns;
                        const glm::vec2 w(static_cast<float>(bc.x) + 0.5f, static_cast<float>(bc.z) + 0.5f);
                        const glm::vec2 mq = fns.warp ? fns.warp(w.x, w.y, *this) : w;
                        const ChannelHit h = channelAt(mq.x, mq.y);
                        const bool inBand = h.hit && h.carved && h.hasProfile && h.cell == k;
                        if (inBand) {
                            const float rimB = h.bedProfile + h.fullDepth;
                            float perpW = 1e30f;   // the column's clamped distance to the world polyline
                            for (size_t q = 0; q + 1 < wp.size(); ++q) perpW = std::min(perpW, pointSegDist(w.x, w.y, wp[q].x, wp[q].y, wp[q + 1].x, wp[q + 1].y));
                            bandHalfWorld[static_cast<size_t>(k)] = std::max(bandHalfWorld[static_cast<size_t>(k)], perpW);
                            for (const glm::ivec2 dxy : {glm::ivec2(1, 0), glm::ivec2(-1, 0), glm::ivec2(0, 1), glm::ivec2(0, -1)}) {
                                const int nx = bc.x + dxy.x, nz = bc.z + dxy.y;
                                if (nx < x0 || nx > x1 || nz < z0 || nz > z1) continue;
                                if (visited[static_cast<size_t>(nz - z0) * W + (nx - x0)]) continue;
                                queue.push_back({nx, nz, rimB, h.t, true});
                            }
                            continue;
                        }
                        if (!bc.fromBand) continue;              // a seed off the band (an inversion that missed): nothing to read
                        if (h.hit) continue;                     // another carved band, or a creek: not this reach's bank
                        ++m_perch.banks;
                        const int ci = static_cast<int>(std::floor((w.x - m_originX) / m_cellSize));
                        const int cj = static_cast<int>(std::floor((w.y - m_originZ) / m_cellSize));
                        if (ci < 0 || cj < 0 || ci >= m_cellsX || cj >= m_cellsZ) continue;
                        float envMin = 1e30f; int envSrc = -1;
                        for (int e = m_envHead[static_cast<size_t>(cj) * m_cellsX + ci]; e >= 0; e = m_envPool[static_cast<size_t>(e)].next) {
                            const int src = m_envPool[static_cast<size_t>(e)].src;
                            if (src == k) continue;              // its own envelope moves with its rim: no lever closes that
                            const float ev = envelopeOf(src, w.x, w.y, mq.x, mq.y).env;
                            if (ev < envMin) { envMin = ev; envSrc = src; }
                        }
                        const float deficit = bc.rimB - envMin;
                        if (deficit <= kPerchTol) continue;
                        worstLeft = std::max(worstLeft, deficit);
                        const float oneMinusT = std::max(0.0f, 1.0f - bc.tB);
                        const float lever = (deficit < oneMinusT * dropK) ? deficit / std::max(oneMinusT, 1e-3f) : deficit + bc.tB * dropK;
                        if (lever > lowerBy) { lowerBy = lever; lowerSrc = envSrc; lowerDeficit = deficit; }
                    }
                    if (lowerBy > 0.0f) {
                        // the base the rim actually stands on (an upstream carry may already hold the bed
                        // below groundK - fullD), lowered by the lever
                        groundK[static_cast<size_t>(k)] = std::min(groundK[static_cast<size_t>(k)], m_bedY[k] + m_fullD[k]) - lowerBy;
                        moved[static_cast<size_t>(k)] = 1;
                        changed = true;
                        ++changedCount;
                        lastSrc[static_cast<size_t>(k)] = lowerSrc; lastDeficit[static_cast<size_t>(k)] = lowerDeficit; ++roundsMoved[static_cast<size_t>(k)];
                        // the profile follows at once (propagateProfile's rule, locally): this reach's bed,
                        // then the carry down the chain while it undercuts the next bed
                        m_bedY[k] = groundK[static_cast<size_t>(k)] - m_fullD[k];
                        m_carryY[k] = m_basin[k] ? fr.filled[k] : m_bedY[k];
                        for (int c = k;;) {
                            const int d = m_downstream[c];
                            if (d == c || d < 0 || static_cast<size_t>(d) >= n || m_order[d] < 1 || m_basin[d]) break;
                            if (m_carryY[c] < m_bedY[d]) { m_bedY[d] = m_carryY[c]; m_carryY[d] = m_bedY[d]; c = d; continue; }
                            break;
                        }
                    }
                }
                ++m_perch.rounds;
                m_perch.changed.push_back(changedCount);
                m_perch.worstDeficitLeft = worstLeft;
                if (!changed) break;
                propagateProfile();
            }
            for (size_t c = 0; c < n; ++c) {
                const float low = groundK0[c] - groundK[c];
                if (low <= 0.0f) continue;
                m_perch.sumLowering += low;
                m_perch.maxLowering = std::max(m_perch.maxLowering, low);
                if (low > 8.0f) ++m_perch.loweredOver8;
                PerchStats::Deep dp; dp.cell = static_cast<int>(c); dp.lowering = low; dp.lastSrc = lastSrc[c]; dp.lastDeficit = lastDeficit[c]; dp.rounds = roundsMoved[c];
                m_perch.deepest.push_back(dp);
                std::sort(m_perch.deepest.begin(), m_perch.deepest.end(), [](const PerchStats::Deep& a, const PerchStats::Deep& b) { return a.lowering > b.lowering; });
                if (m_perch.deepest.size() > 5) m_perch.deepest.pop_back();
            }
        }
    }
}

FlowField::EnvSample FlowField::envelopeOf(int rc, float worldX, float worldZ, float warpedX, float warpedZ) const {
    // The envelope one source imposes at a world point: the floor across the perpendicular distance
    // to its WORLD polyline, the side beyond it — and beyond either END of the polyline the along-
    // distance counts in full; the RIM as the carve reads it: the profile at the bake projection of
    // the warped point (channelAt's rule), so the floor meets the bed's own rim at the band edge.
    EnvSample out;
    const int pi = (rc >= 0 && static_cast<size_t>(rc) < m_envPolyIdx.size()) ? m_envPolyIdx[static_cast<size_t>(rc)] : -1;
    if (pi < 0) return out;
    const std::vector<glm::vec2>& wp = m_envPolys[static_cast<size_t>(pi)];
    const glm::vec2 p(worldX, worldZ);
    float bestDist = 1e30f; size_t bestSeg = 0; float bestU = 0.0f;
    for (size_t q = 0; q + 1 < wp.size(); ++q) {
        const glm::vec2 ab = wp[q + 1] - wp[q];
        const float L2 = glm::dot(ab, ab);
        const float u = L2 > 0.0f ? std::clamp(glm::dot(p - wp[q], ab) / L2, 0.0f, 1.0f) : 0.0f;
        const float dist = glm::length(p - (wp[q] + ab * u));
        if (dist < bestDist) { bestDist = dist; bestSeg = q; bestU = u; }
    }
    // the clamped distance to the polyline (continuous); an along/perp split beyond the end vertices was
    // tried (patch 6) and withdrawn: it jumps where the nearest segment changes at a vertex (GREEN 11)
    const float perp = bestDist, over = 0.0f;
    {   // the rim: the bake-space projection of the warped point onto this cell's bake polyline
        thread_local std::vector<glm::vec2> plScratch;
        const std::vector<glm::vec2>& pl = cellPolyline(rc, plScratch);
        float bDist = 1e30f, bT = 0.0f, lenSum = 0.0f, total = 0.0f;
        for (size_t q = 0; q + 1 < pl.size(); ++q) total += glm::length(pl[q + 1] - pl[q]);
        for (size_t q = 0; q + 1 < pl.size(); ++q) {
            const float dist = pointSegDist(warpedX, warpedZ, pl[q].x, pl[q].y, pl[q + 1].x, pl[q + 1].y);
            const float segLen = glm::length(pl[q + 1] - pl[q]);
            if (dist < bDist) {
                bDist = dist;
                bT = total > 0.0f ? (lenSum + segmentParam(warpedX, warpedZ, pl[q].x, pl[q].y, pl[q + 1].x, pl[q + 1].y) * segLen) / total : 0.0f;
            }
            lenSum += segLen;
        }
        const int d = m_downstream[rc];
        const float endY = (d != rc && d >= 0 && static_cast<size_t>(d) < m_carryY.size()) ? std::min(m_carryY[d], m_bedY[rc]) : m_bedY[rc];
        out.rim = m_bedY[rc] + (endY - m_bedY[rc]) * bT + m_fullD[rc];
    }
    (void)bestU; (void)over;
    out.perp = perp;
    out.valleyHalf = m_envFloorHalf[static_cast<size_t>(pi)];
    out.env = out.rim + kValleySideSlope * (std::max(0.0f, perp - out.valleyHalf) + over);
    return out;
}

float FlowField::valleyEnvelopeAt(float worldX, float worldZ, float warpedX, float warpedZ) const {
    if (m_envHead.empty() || m_cellsX <= 0 || m_cellsZ <= 0) return 1e30f;
    const int ci = static_cast<int>(std::floor((worldX - m_originX) / m_cellSize));
    const int cj = static_cast<int>(std::floor((worldZ - m_originZ) / m_cellSize));
    if (ci < 0 || cj < 0 || ci >= m_cellsX || cj >= m_cellsZ) return 1e30f;
    float env = 1e30f;
    for (int e = m_envHead[static_cast<size_t>(cj) * m_cellsX + ci]; e >= 0; e = m_envPool[static_cast<size_t>(e)].next)
        env = std::min(env, envelopeOf(m_envPool[static_cast<size_t>(e)].src, worldX, worldZ, warpedX, warpedZ).env);
    return env;
}

FlowField::EnvSample FlowField::envelopeSample(int sourceCell, float worldX, float worldZ, float warpedX, float warpedZ) const {
    return envelopeOf(sourceCell, worldX, worldZ, warpedX, warpedZ);
}

bool FlowField::isFoldedSource(int sourceCell) const {
    const int pi = (sourceCell >= 0 && static_cast<size_t>(sourceCell) < m_envPolyIdx.size()) ? m_envPolyIdx[static_cast<size_t>(sourceCell)] : -1;
    return pi >= 0 && m_envFolded[static_cast<size_t>(pi)] != 0;
}

const std::vector<glm::vec2>* FlowField::sourceWorldPolyline(int sourceCell) const {
    const int pi = (sourceCell >= 0 && static_cast<size_t>(sourceCell) < m_envPolyIdx.size()) ? m_envPolyIdx[static_cast<size_t>(sourceCell)] : -1;
    return pi >= 0 ? &m_envPolys[static_cast<size_t>(pi)] : nullptr;
}

float FlowField::floorHalfOf(int sourceCell) const {
    const int pi = (sourceCell >= 0 && static_cast<size_t>(sourceCell) < m_envPolyIdx.size()) ? m_envPolyIdx[static_cast<size_t>(sourceCell)] : -1;
    return pi >= 0 ? m_envFloorHalf[static_cast<size_t>(pi)] : 0.0f;
}

bool FlowField::hasValleySourcesAt(float worldX, float worldZ) const {
    if (m_envHead.empty() || m_cellsX <= 0 || m_cellsZ <= 0) return false;
    const int ci = static_cast<int>(std::floor((worldX - m_originX) / m_cellSize));
    const int cj = static_cast<int>(std::floor((worldZ - m_originZ) / m_cellSize));
    if (ci < 0 || cj < 0 || ci >= m_cellsX || cj >= m_cellsZ) return false;
    return m_envHead[static_cast<size_t>(cj) * m_cellsX + ci] >= 0;
}

std::vector<int> FlowField::valleySourcesAt(float worldX, float worldZ) const {
    std::vector<int> out;
    if (!hasValleySourcesAt(worldX, worldZ)) return out;
    const int ci = static_cast<int>(std::floor((worldX - m_originX) / m_cellSize));
    const int cj = static_cast<int>(std::floor((worldZ - m_originZ) / m_cellSize));
    for (int e = m_envHead[static_cast<size_t>(cj) * m_cellsX + ci]; e >= 0; e = m_envPool[static_cast<size_t>(e)].next)
        out.push_back(m_envPool[static_cast<size_t>(e)].src);
    return out;
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
                h.t = t;
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
