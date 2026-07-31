#include "core/WaterSimulation.h"
#include <algorithm>
#include <climits>
#include <cmath>

namespace Phyxel {
namespace Core {

WaterSimulation::WaterSimulation(int sizeX, int sizeY, int sizeZ)
    : m_sx(sizeX), m_sy(sizeY), m_sz(sizeZ),
      m_mass(static_cast<size_t>(sizeX) * sizeY * sizeZ, 0.0f),
      m_solid(static_cast<size_t>(sizeX) * sizeY * sizeZ, 0),
      m_next(static_cast<size_t>(sizeX) * sizeY * sizeZ, 0.0f),
      m_source(static_cast<size_t>(sizeX) * sizeY * sizeZ, -1.0f),
      m_channel(static_cast<size_t>(sizeX) * sizeY * sizeZ, 0),
      m_colDirty(static_cast<size_t>(sizeX) * sizeZ, 1),   // all dirty: first sweep is full
      m_colProcess(static_cast<size_t>(sizeX) * sizeZ, 0),
      m_colWrite(static_cast<size_t>(sizeX) * sizeZ, 0),
      m_floor(static_cast<size_t>(sizeX) * sizeY * sizeZ, 0.0f),
      m_flow(static_cast<size_t>(sizeX) * sizeY * sizeZ, glm::vec2(0.0f)),
      m_flowAccum(static_cast<size_t>(sizeX) * sizeY * sizeZ, glm::vec2(0.0f)),
      m_edgeOutflowAccum(static_cast<size_t>(sizeX) * sizeZ, 0.0f) {}

glm::vec2 WaterSimulation::flowAt(int x, int y, int z) const {
    if (!inBounds(x, y, z)) return glm::vec2(0.0f);
    return m_flow[idx(x, y, z)];
}

void WaterSimulation::setFloor(int x, int y, int z, float fraction) {
    if (!inBounds(x, y, z)) return;
    m_floor[idx(x, y, z)] = std::min(std::max(fraction, 0.0f), 1.0f);
}

float WaterSimulation::floorAt(int x, int y, int z) const {
    if (!inBounds(x, y, z)) return 0.0f;
    return m_floor[idx(x, y, z)];
}

void WaterSimulation::setMinHold(float depth) {
    m_minHold = std::max(0.0f, depth);
    // A changed hold can release water that was resting (or hold water that was flowing) —
    // the settled flag and per-column clean state no longer reflect reality.
    wake();
}

void WaterSimulation::setMomentum(float strength) {
    strength = std::max(0.0f, strength);
    if (m_momentum == strength) return;
    m_momentum = strength;
    markAllCols();   // the flow rule changed; a settled field may now redistribute differently
}

void WaterSimulation::markAllCols() {
    std::fill(m_colDirty.begin(), m_colDirty.end(), 1);
    m_settled = false;
}

void WaterSimulation::wake() {
    markAllCols();
}

void WaterSimulation::setEvaporation(bool enabled) {
    if (m_evaporate == enabled) return;
    m_evaporate = enabled;
    // A settled field can hold thin (< EVAP_THRESHOLD) cells that only became sink-eligible by this
    // toggle; without a full wake they would never dry (the settle-skip would keep bypassing them).
    markAllCols();
}

void WaterSimulation::setSolid(int x, int y, int z, bool solid) {
    if (!inBounds(x, y, z)) return;
    m_solid[idx(x, y, z)] = solid ? 1 : 0;
    if (solid) m_mass[idx(x, y, z)] = 0.0f; // solid cells hold no water
    markCol(x, z);                           // terrain change may re-open flow
}

bool WaterSimulation::isSolid(int x, int y, int z) const {
    if (!inBounds(x, y, z)) return true; // out-of-bounds acts as a solid wall
    return m_solid[idx(x, y, z)] != 0;
}

void WaterSimulation::setChannel(int x, int y, int z, bool channel) {
    if (!inBounds(x, y, z)) return;
    m_channel[idx(x, y, z)] = channel ? 1 : 0;
    markCol(x, z);                           // changes evaporation eligibility for this cell
}

bool WaterSimulation::isChannel(int x, int y, int z) const {
    if (!inBounds(x, y, z)) return false;
    return m_channel[idx(x, y, z)] != 0;
}

void WaterSimulation::addWater(int x, int y, int z, float amount) {
    if (!inBounds(x, y, z) || m_solid[idx(x, y, z)]) return;
    m_mass[idx(x, y, z)] = std::max(0.0f, m_mass[idx(x, y, z)] + amount);
    markCol(x, z);                           // injected/removed mass → flow again
}

void WaterSimulation::setSource(int x, int y, int z, float mass) {
    if (!inBounds(x, y, z)) return;
    m_source[idx(x, y, z)] = mass;
    m_hasSources = true;
    markCol(x, z);                           // a new reservoir will inject
}

void WaterSimulation::clearSource(int x, int y, int z) {
    if (!inBounds(x, y, z)) return;
    m_source[idx(x, y, z)] = -1.0f;
    // (m_hasSources stays true; harmless — the per-cell check still gates pinning.)
    markCol(x, z);
}

float WaterSimulation::sourceAt(int x, int y, int z) const {
    if (!inBounds(x, y, z)) return -1.0f;
    return m_source[idx(x, y, z)];
}

int WaterSimulation::fillOcean(const std::vector<glm::ivec3>& localSeeds, int seaLevelY) {
    markAllCols();                           // the ocean re-pin/clear can touch any column
    // Ocean owns the source system: clear all pins, then re-flood.
    std::fill(m_source.begin(), m_source.end(), -1.0f);
    m_hasSources = false;

    auto canOcean = [&](int x, int y, int z) {
        return inBounds(x, y, z) && !m_solid[idx(x, y, z)] && y <= seaLevelY;
    };

    std::vector<uint8_t> visited(m_mass.size(), 0);
    std::vector<glm::ivec3> stack;
    for (const glm::ivec3& s : localSeeds) {
        if (canOcean(s.x, s.y, s.z) && !visited[idx(s.x, s.y, s.z)]) {
            visited[idx(s.x, s.y, s.z)] = 1;
            stack.push_back(s);
        }
    }

    static const int NB[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    int count = 0;
    while (!stack.empty()) {
        glm::ivec3 v = stack.back(); stack.pop_back();
        m_source[idx(v.x, v.y, v.z)] = MAX_MASS; // pin full (infinite reservoir)
        ++count;
        for (const auto& n : NB) {
            int nx = v.x + n[0], ny = v.y + n[1], nz = v.z + n[2];
            if (canOcean(nx, ny, nz) && !visited[idx(nx, ny, nz)]) {
                visited[idx(nx, ny, nz)] = 1;
                stack.push_back({nx, ny, nz});
            }
        }
    }
    m_hasSources = (count > 0);
    return count;
}

int WaterSimulation::fillWaterTable(const std::function<int(int lx, int lz)>& levelLocalY) {
    markAllCols();   // the re-pin/clear can touch any column
    std::fill(m_source.begin(), m_source.end(), -1.0f);
    m_hasSources = false;

    // Cache the per-column level once (the callback may be an engine-side hydrology lookup).
    std::vector<int> lvl(static_cast<size_t>(m_sx) * m_sz);
    for (int z = 0; z < m_sz; ++z)
        for (int x = 0; x < m_sx; ++x) lvl[colIdx(x, z)] = levelLocalY(x, z);

    auto canWater = [&](int x, int y, int z) {
        return inBounds(x, y, z) && !m_solid[idx(x, y, z)] && y <= lvl[colIdx(x, z)];
    };

    std::vector<uint8_t> visited(m_mass.size(), 0);
    std::vector<glm::ivec3> stack;
    auto seed = [&](int x, int y, int z) {
        if (canWater(x, y, z) && !visited[idx(x, y, z)]) {
            visited[idx(x, y, z)] = 1;
            stack.push_back({x, y, z});
        }
    };
    for (int z = 0; z < m_sz; ++z)
        for (int x = 0; x < m_sx; ++x) {
            const int L = lvl[colIdx(x, z)];
            if (L == INT_MIN) continue;
            // The column's water-surface cell (clamped into the region if the surface is above it).
            seed(x, std::min(L, m_sy - 1), z);
            // Region side edges: the water continues beyond the window, so every open edge cell
            // at/below its level acts as a seed (the moving-region boundary condition, per column).
            if (x == 0 || x == m_sx - 1 || z == 0 || z == m_sz - 1)
                for (int y = 0; y <= std::min(L, m_sy - 1); ++y) seed(x, y, z);
        }

    static const int NB[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    int count = 0;
    while (!stack.empty()) {
        glm::ivec3 v = stack.back(); stack.pop_back();
        m_source[idx(v.x, v.y, v.z)] = MAX_MASS; // pin full (infinite reservoir)
        ++count;
        for (const auto& n : NB) {
            int nx = v.x + n[0], ny = v.y + n[1], nz = v.z + n[2];
            if (canWater(nx, ny, nz) && !visited[idx(nx, ny, nz)]) {
                visited[idx(nx, ny, nz)] = 1;
                stack.push_back({nx, ny, nz});
            }
        }
    }
    m_hasSources = (count > 0);
    return count;
}

float WaterSimulation::massAt(int x, int y, int z) const {
    if (!inBounds(x, y, z)) return 0.0f;
    return m_mass[idx(x, y, z)];
}

float WaterSimulation::totalMass() const {
    float sum = 0.0f;
    for (float m : m_mass) sum += m;
    return sum;
}

float WaterSimulation::minMass() const {
    float lo = 0.0f;
    for (float m : m_mass) lo = std::min(lo, m);
    return lo;
}

namespace {
// Allow a cell under load to hold slightly more than full ("compression"). This is
// what lets pressure propagate so connected water rises to a common level — the
// classic finite-water-cells rule. Given the combined mass of a vertical pair,
// returns how much belongs in the BOTTOM cell.
constexpr float MAX_COMPRESS = 0.02f;
inline float stableBottom(float total) {
    const float M = WaterSimulation::MAX_MASS;
    if (total <= M) return total;
    if (total < 2.0f * M + MAX_COMPRESS)
        return (M * M + total * MAX_COMPRESS) / (M + MAX_COMPRESS);
    return (total + MAX_COMPRESS) * 0.5f;
}
} // namespace

void WaterSimulation::setEdgeOutflow(bool on) {
    if (m_edgeOutflow == on) return;
    m_edgeOutflow = on;
    if (on) wake();   // a settled field may hold edge water that is now eligible to bleed
}

void WaterSimulation::runEdgeOutflow() {
    auto bleedColumn = [&](int x, int z) {
        for (int y = m_sy - 1; y >= 0; --y) {
            const size_t i = idx(x, y, z);
            if (m_solid[i] || m_mass[i] <= 0.0f) continue;
            // Pinned/channel water is an infinite reservoir / held ribbon — bleeding it would
            // mint mass into the bank forever. It stays a wall, as before.
            if (m_source[i] >= 0.0f || m_channel[i]) return;
            // Bleed the top wet cell: its excess above the hold, or — when it is a thin layer
            // riding on deeper water — the whole layer, so a stack drains from the top down to
            // one resting ≤hold layer on actual ground. One cell per column per step.
            const bool stacked = y > 0 && !m_solid[idx(x, y - 1, z)] && m_mass[idx(x, y - 1, z)] > 0.5f;
            float excess = 0.0f;
            if (m_mass[i] > m_minHold)  excess = m_mass[i] - m_minHold;
            else if (stacked)           excess = m_mass[i];
            else return;
            const float take = std::min(excess, OUTFLOW_RATE);
            m_mass[i] -= take;
            m_edgeOutflowAccum[colIdx(x, z)] += take;
            markCol(x, z);
            return;
        }
    };
    for (int x = 0; x < m_sx; ++x) {
        bleedColumn(x, 0);
        if (m_sz > 1) bleedColumn(x, m_sz - 1);
    }
    for (int z = 1; z < m_sz - 1; ++z) {
        bleedColumn(0, z);
        if (m_sx > 1) bleedColumn(m_sx - 1, z);
    }
}

float WaterSimulation::drainEdgeOutflow(const std::function<void(int, int, float)>& sink) {
    float total = 0.0f;
    for (int z = 0; z < m_sz; ++z)
        for (int x = 0; x < m_sx; ++x) {
            const size_t c = colIdx(x, z);
            if (m_edgeOutflowAccum[c] <= 0.0f) continue;
            total += m_edgeOutflowAccum[c];
            if (sink) sink(x, z, m_edgeOutflowAccum[c]);
            m_edgeOutflowAccum[c] = 0.0f;
        }
    return total;
}

void WaterSimulation::step(float flowSide) {
    // P4: the frontier bleed runs even on a settled field (edge water is eligible the moment the
    // flag turns on or the window shifts); a bleed marks its column dirty, un-settling the field.
    if (m_edgeOutflow) runEdgeOutflow();
    if (m_settled) return;   // provably at rest since the last executed step → nothing to do

    // ACTIVE SET: build the sweep set P = dirty ∪ N4(dirty) and the snapshot/write-back set
    // W = P ∪ N4(P). Soundness: all flow is a pure gather from m_mass (writes go to m_next), so a
    // cell's transfers are a deterministic function of its column and its N4-neighbor columns. A
    // column outside P has unchanged mass AND unchanged neighbors since it was last swept, so
    // re-sweeping it would reproduce the same below-MIN_FLOW transfers — skipping it cannot change
    // the result. W covers every possible write: destinations are N4/vertical neighbors of P cells.
    int processed = 0;
    for (int cz = 0; cz < m_sz; ++cz)
        for (int cx = 0; cx < m_sx; ++cx) {
            const size_t c = colIdx(cx, cz);
            const bool p = m_colDirty[c] != 0 ||
                           (cx > 0        && m_colDirty[c - 1]) ||
                           (cx + 1 < m_sx && m_colDirty[c + 1]) ||
                           (cz > 0        && m_colDirty[c - m_sx]) ||
                           (cz + 1 < m_sz && m_colDirty[c + m_sx]);
            m_colProcess[c] = p ? 1 : 0;
            if (p) ++processed;
        }
    if (processed == 0) {    // nothing changed since the last sweep anywhere → at rest
        m_colsProcessed = 0;
        m_settled = true;
        return;
    }
    for (int cz = 0; cz < m_sz; ++cz)
        for (int cx = 0; cx < m_sx; ++cx) {
            const size_t c = colIdx(cx, cz);
            m_colWrite[c] = (m_colProcess[c] ||
                             (cx > 0        && m_colProcess[c - 1]) ||
                             (cx + 1 < m_sx && m_colProcess[c + 1]) ||
                             (cz > 0        && m_colProcess[c - m_sx]) ||
                             (cz + 1 < m_sz && m_colProcess[c + m_sx])) ? 1 : 0;
        }
    m_colsProcessed = processed;
    ++m_sweepsRun;
    // From here on m_colDirty describes the NEXT sweep: cleared now, re-marked by every transfer.
    std::fill(m_colDirty.begin(), m_colDirty.end(), 0);
    bool changed = false;    // did this step move any mass? (if not, the field is settled)

    const float MIN_FLOW = 1e-4f;
    // Re-pin source cells to their held mass (infinite reservoirs) before flowing. A re-pin that
    // actually restores drained mass counts as movement (the field is still doing work). Only P
    // columns can hold a drifted source: draining a source's mass is a transfer that marked it dirty.
    if (m_hasSources) {
        for (int cz = 0; cz < m_sz; ++cz)
        for (int cx = 0; cx < m_sx; ++cx) {
            if (!m_colProcess[colIdx(cx, cz)]) continue;
            for (int y = 0; y < m_sy; ++y) {
                const size_t i = idx(cx, y, cz);
                if (m_source[i] >= 0.0f && !m_solid[i]) {
                    if (std::abs(m_mass[i] - m_source[i]) > MIN_FLOW) {
                        changed = true;
                        markCol(cx, cz);
                    }
                    m_mass[i] = m_source[i];
                }
            }
        }
    }

    // Snapshot the write set: all flow reads m_mass (this frame's snapshot) and accumulates into
    // m_next, so transfers are order-independent and mass-conserving. Only W columns can be written,
    // so only they need the snapshot (and the write-back below) — not the whole box.
    // The flow accumulator is cleared over the same set (it is filled by the same transfers).
    for (int cz = 0; cz < m_sz; ++cz)
    for (int cx = 0; cx < m_sx; ++cx) {
        if (!m_colWrite[colIdx(cx, cz)]) continue;
        for (int y = 0; y < m_sy; ++y) {
            const size_t i = idx(cx, y, cz);
            const float m = m_mass[i];
            m_next[i] = m;
            // PERF: only WET cells touch the flow arrays. The write set spans each column's FULL
            // height, and the box is overwhelmingly air, so unconditionally streaming two extra
            // 8-byte-per-cell arrays through cache dominated the cost (Release: 160 -> 269 us/step
            // before this gate). The `accum != 0` term still clears a cell that received flow last
            // step but has since drained, so no stale accumulation can survive into a later sweep.
#if PHYXEL_WATER_FLOW_ENABLED
            if (m > 0.0f) {
                m_flowAccum[i] = glm::vec2(0.0f);
            } else if (m_flowAccum[i].x != 0.0f || m_flowAccum[i].y != 0.0f) {
                m_flowAccum[i] = glm::vec2(0.0f);
            }
#endif
        }
    }

    static const int HX[4] = {1, -1, 0, 0};
    static const int HZ[4] = {0, 0, 1, -1};
    const float MAX_SPEED = 1.0f; // cap on mass moved out of one cell per direction

    // Transfers between TWO source-pinned cells are skipped: both ends are re-clamped to their
    // pinned mass at the next re-pin, so the exchange is pure churn — and it is what kept a
    // fully-pinned ocean from EVER settling (the compression rule wants deep cells slightly over
    // full, the pin says exactly full, so pinned column stacks re-donated ~0.01 down every step,
    // forever — ~6ms/step live at 20 Hz over open sea, doing nothing). Pinned→unpinned still
    // flows: that is how a breach floods and how a spring feeds its pool.
    auto pinned = [&](size_t i) { return m_source[i] >= 0.0f; };
    for (int z = 0; z < m_sz; ++z)
    for (int x = 0; x < m_sx; ++x) {
        if (!m_colProcess[colIdx(x, z)]) continue;
        for (int y = 0; y < m_sy; ++y) {
        const size_t c = idx(x, y, z);
        if (m_solid[c]) continue;
        float remaining = m_mass[c]; // working mass still available to move out
        if (remaining <= 0.0f) continue;
        const bool cPinned = pinned(c);

        // 1) Gravity (compression-aware): the cell below holds up to its stable share
        //    of the combined column; the excess stays here to be pushed up later.
        //
        // SUB-VOXEL FLOOR (Phase 4B): a cell with a floor has solid sub-voxel ground beneath its
        // water, so it cannot drain downward — even though the cell itself is passable. Without
        // this, making floored cells passable would let water fall straight THROUGH a subcube
        // platform into the air below (found live: a puddle poured onto a 1/3 platform vanished).
        // This only ever removes a transfer, so mass conservation and settling are unaffected.
        if (y - 1 >= 0 && !m_solid[idx(x, y - 1, z)] && m_floor[c] <= 0.0f) {
            const size_t b = idx(x, y - 1, z);
            if (!(cPinned && pinned(b))) {
                float flow = stableBottom(remaining + m_mass[b]) - m_mass[b];
                flow = std::min(flow, std::min(MAX_SPEED, remaining));
                if (flow > MIN_FLOW) {
                    m_next[c] -= flow; m_next[b] += flow; remaining -= flow; changed = true;
                    markCol(x, z);
                }
            }
        }

        // 2) Horizontal: move toward the average with each same-level neighbor,
        //    leveling the surface. Only the higher cell of a pair flows.
        //
        // MOMENTUM (Phase 4): the share sent to each neighbour is biased by how well that direction
        // aligns with the flow this cell already carries, so moving water continues rather than
        // fanning out isotropically. The factor stays < 0.5 (see setMomentum) so this only changes
        // WHERE the outflow goes, never how far past the average it can push.
        float mvx = 0.0f, mvz = 0.0f, mspeed = 0.0f;
        if (m_momentum > 0.0f) {
            const glm::vec2 fv = m_flow[c];
            mspeed = std::sqrt(fv.x * fv.x + fv.y * fv.y);
            if (mspeed > 1e-5f) { mvx = fv.x / mspeed; mvz = fv.y / mspeed; }
        }
        const float mramp = std::min(mspeed / FLOW_FULL, 1.0f) * m_momentum;

        // MIN_HOLD donor gate (small-scale plan Phase 1): a donor at or below the hold makes no
        // horizontal transfers, so films/puddles/fractional pins REST instead of creeping
        // sideways forever (the sheeting that flooded a hillside in the reverted creek fix and
        // deleted every placed puddle via spread-then-evaporate). Donor-local and a pure function
        // of the snapshot working mass: it only REMOVES transfers, so mass conservation, active-set
        // equivalence (full sweep computes the identical gate) and settling are unaffected. Deep
        // bodies still level exactly — leveling raises every cell above the hold, and equalization
        // then proceeds as before; only a body whose mass over its reachable area is at or below
        // the hold stalls (which is the point). Gravity above and pressure below are NOT gated.
        // m_minHold = 0 restores the old behavior exactly.
        for (int k = 0; k < 4 && remaining > m_minHold; ++k) {
            int nx = x + HX[k], nz = z + HZ[k];
            if (!inBounds(nx, y, nz) || m_solid[idx(nx, y, nz)]) continue;
            const size_t n = idx(nx, y, nz);
            if (cPinned && pinned(n)) continue;
            float rate = 0.25f;
            if (mramp > 0.0f) {
                const float align = mvx * static_cast<float>(HX[k]) + mvz * static_cast<float>(HZ[k]);
                rate = std::min(0.45f, std::max(0.05f, 0.25f * (1.0f + MOMENTUM_GAIN * align * mramp)));
            }
            float flow = (remaining - m_mass[n]) * rate * flowSide;
            flow = std::min(flow, remaining);
            if (flow > MIN_FLOW) {
                m_next[c] -= flow; m_next[n] += flow; remaining -= flow; changed = true;
                markCol(x, z); markCol(nx, nz);
                // FLOW PROXY (Phase 3): this transfer moved `flow` mass in direction k. Credit BOTH
                // endpoints — the donor is losing water that way and the receiver is gaining it from
                // that way, so both read as "water is heading in direction k" for shading purposes.
#if PHYXEL_WATER_FLOW_ENABLED
                const glm::vec2 d(static_cast<float>(HX[k]), static_cast<float>(HZ[k]));
                m_flowAccum[c] += d * flow;
                m_flowAccum[n] += d * flow;
#endif
            }
        }

        // 3) Upward (pressure): if still compressed (> capacity), push the excess into
        //    the cell above. This is what makes connected water rise to a common level.
        if (remaining > MAX_MASS && y + 1 < m_sy && !m_solid[idx(x, y + 1, z)]) {
            const size_t a = idx(x, y + 1, z);
            if (!(cPinned && pinned(a))) {
                float flow = remaining - stableBottom(remaining + m_mass[a]);
                flow = std::min(flow, remaining);
                if (flow > MIN_FLOW) {
                    m_next[c] -= flow; m_next[a] += flow; remaining -= flow; changed = true;
                    markCol(x, z);
                }
            }
        }
        }
    }

    // Write back the W columns (the only ones m_next was synced for — a full buffer swap would
    // publish stale data everywhere else). The flow proxy is EMA-folded over the same set, so a
    // cell that stopped moving decays toward zero instead of latching its last direction.
    for (int cz = 0; cz < m_sz; ++cz)
    for (int cx = 0; cx < m_sx; ++cx) {
        if (!m_colWrite[colIdx(cx, cz)]) continue;
        for (int y = 0; y < m_sy; ++y) {
            const size_t i = idx(cx, y, cz);
            const float m = m_next[i];
            m_mass[i] = m;
            // PERF: same gate as the clear — dry cells never touch the flow arrays. A cell that
            // drained to nothing keeps whatever flow it last had, which is invisible: rendering
            // only reads cells above RENDER_MIN, and if water returns the EMA re-converges in a
            // few steps.
#if !PHYXEL_WATER_FLOW_ENABLED
            continue;
#endif
            if (m <= 0.0f) continue;
            const glm::vec2 a = m_flowAccum[i];
            glm::vec2 f = m_flow[i];
            if (a.x == 0.0f && a.y == 0.0f) {
                // Nothing moved here this step. Decay HARDER than the EMA would: with momentum
                // reading this field (Phase 4), a column must reach exactly zero quickly or it
                // stays awake (see the markCol below) and delays settling for dozens of steps.
                f *= 0.5f;
            } else {
                f += (a - f) * FLOW_EMA;
            }
            // Snap the geometric tail to exactly zero so a cell that stopped flowing settles at a
            // clean zero instead of decaying through denormals forever.
            if (std::fabs(f.x) < 1e-3f && std::fabs(f.y) < 1e-3f) f = glm::vec2(0.0f);
            m_flow[i] = f;
            // ACTIVE-SET EQUIVALENCE (load-bearing). Momentum makes the mass evolution depend on
            // this flow field, so the field itself must evolve IDENTICALLY whether or not the
            // active set skipped a column — otherwise a column that goes quiet keeps a frozen flow
            // under the active set while a full sweep decays it to zero, and the two runs diverge
            // the moment that column wakes. Keeping any column with residual flow dirty makes both
            // paths take the same decay. (Bounded: the 0.5 decay + 1e-3 snap above reaches zero in
            // ~8 steps, so this cannot hold the field awake indefinitely.)
            if (f.x != 0.0f || f.y != 0.0f) markCol(cx, cz);
        }
    }

    // Evaporation sink: thin cells (the frontier of a spreading flow, films) lose a
    // little mass each step, so free flow has a finite reach and spills dry up. Deep
    // (>= EVAP_THRESHOLD) water is untouched, so ponds and channels persist. Only P columns can
    // hold an eligible cell: becoming thin was a mass change that marked the column (and
    // setEvaporation() marks all columns, covering cells that were already thin at the toggle).
    if (m_evaporate) {
        for (int cz = 0; cz < m_sz; ++cz)
        for (int cx = 0; cx < m_sx; ++cx) {
            if (!m_colProcess[colIdx(cx, cz)]) continue;
            for (int y = 0; y < m_sy; ++y) {
                const size_t i = idx(cx, y, cz);
                float m = m_mass[i];
                if (m > 0.0f && m < EVAP_THRESHOLD && !m_solid[i] && !m_channel[i]) {
                    m_mass[i] = std::max(0.0f, m - EVAP_RATE); // channel cells never evaporate
                    changed = true;
                    markCol(cx, cz);
                }
            }
        }
    }

    // If nothing moved this step, the field is at rest — skip future steps until a disturbance wakes it.
    if (!changed) m_settled = true;
}

void WaterSimulation::shift(const glm::ivec3& delta) {
    if (delta == glm::ivec3(0)) return;
    // Gather into fresh buffers to avoid in-place aliasing; new[p] = old[p+delta], frontier = default.
    std::vector<float>   nm(m_mass.size(), 0.0f);
    std::vector<uint8_t> ns(m_solid.size(), 0);
    std::vector<float>   nsrc(m_source.size(), -1.0f);   // -1 = not a source
    std::vector<uint8_t> nch(m_channel.size(), 0);
    std::vector<float>     nflr(m_floor.size(), 0.0f);
    std::vector<glm::vec2> nfl(m_flow.size(), glm::vec2(0.0f));
    for (int z = 0; z < m_sz; ++z)
    for (int y = 0; y < m_sy; ++y)
    for (int x = 0; x < m_sx; ++x) {
        const int sx = x + delta.x, sy = y + delta.y, sz = z + delta.z;
        if (!inBounds(sx, sy, sz)) continue;   // exposed frontier keeps the defaults above
        const size_t d = idx(x, y, z), s = idx(sx, sy, sz);
        nm[d] = m_mass[s]; ns[d] = m_solid[s]; nsrc[d] = m_source[s]; nch[d] = m_channel[s];
        nflr[d] = m_floor[s]; // sub-voxel floors travel with the window, like solidity
        nfl[d] = m_flow[s];   // the flow proxy travels with its water across a recenter
    }
    m_mass.swap(nm); m_solid.swap(ns); m_source.swap(nsrc); m_channel.swap(nch);
    m_floor.swap(nflr);
    m_flow.swap(nfl);
    m_hasSources = false;
    for (float v : m_source) if (v >= 0.0f) { m_hasSources = true; break; }
    markAllCols();   // the field was relocated → re-settle from the new configuration
}

} // namespace Core
} // namespace Phyxel
