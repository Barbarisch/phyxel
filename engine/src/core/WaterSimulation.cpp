#include "core/WaterSimulation.h"
#include <algorithm>

namespace Phyxel {
namespace Core {

WaterSimulation::WaterSimulation(int sizeX, int sizeY, int sizeZ)
    : m_sx(sizeX), m_sy(sizeY), m_sz(sizeZ),
      m_mass(static_cast<size_t>(sizeX) * sizeY * sizeZ, 0.0f),
      m_solid(static_cast<size_t>(sizeX) * sizeY * sizeZ, 0),
      m_next(static_cast<size_t>(sizeX) * sizeY * sizeZ, 0.0f),
      m_source(static_cast<size_t>(sizeX) * sizeY * sizeZ, -1.0f) {}

void WaterSimulation::setSolid(int x, int y, int z, bool solid) {
    if (!inBounds(x, y, z)) return;
    m_solid[idx(x, y, z)] = solid ? 1 : 0;
    if (solid) m_mass[idx(x, y, z)] = 0.0f; // solid cells hold no water
}

bool WaterSimulation::isSolid(int x, int y, int z) const {
    if (!inBounds(x, y, z)) return true; // out-of-bounds acts as a solid wall
    return m_solid[idx(x, y, z)] != 0;
}

void WaterSimulation::addWater(int x, int y, int z, float amount) {
    if (!inBounds(x, y, z) || m_solid[idx(x, y, z)]) return;
    m_mass[idx(x, y, z)] = std::max(0.0f, m_mass[idx(x, y, z)] + amount);
}

void WaterSimulation::setSource(int x, int y, int z, float mass) {
    if (!inBounds(x, y, z)) return;
    m_source[idx(x, y, z)] = mass;
    m_hasSources = true;
}

void WaterSimulation::clearSource(int x, int y, int z) {
    if (!inBounds(x, y, z)) return;
    m_source[idx(x, y, z)] = -1.0f;
    // (m_hasSources stays true; harmless — the per-cell check still gates pinning.)
}

int WaterSimulation::fillOcean(const std::vector<glm::ivec3>& localSeeds, int seaLevelY) {
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

void WaterSimulation::step(float flowSide) {
    // Re-pin source cells to their held mass (infinite reservoirs) before flowing.
    if (m_hasSources) {
        const size_t n = m_mass.size();
        for (size_t i = 0; i < n; ++i)
            if (m_source[i] >= 0.0f && !m_solid[i]) m_mass[i] = m_source[i];
    }

    // Start from the current state; all flow reads m_mass (this frame's snapshot) and
    // accumulates into m_next, so transfers are order-independent and mass-conserving.
    m_next = m_mass;

    static const int HX[4] = {1, -1, 0, 0};
    static const int HZ[4] = {0, 0, 1, -1};
    const float MIN_FLOW = 1e-4f;
    const float MAX_SPEED = 1.0f; // cap on mass moved out of one cell per direction

    for (int z = 0; z < m_sz; ++z)
    for (int y = 0; y < m_sy; ++y)
    for (int x = 0; x < m_sx; ++x) {
        const size_t c = idx(x, y, z);
        if (m_solid[c]) continue;
        float remaining = m_mass[c]; // working mass still available to move out
        if (remaining <= 0.0f) continue;

        // 1) Gravity (compression-aware): the cell below holds up to its stable share
        //    of the combined column; the excess stays here to be pushed up later.
        if (y - 1 >= 0 && !m_solid[idx(x, y - 1, z)]) {
            const size_t b = idx(x, y - 1, z);
            float flow = stableBottom(remaining + m_mass[b]) - m_mass[b];
            flow = std::min(flow, std::min(MAX_SPEED, remaining));
            if (flow > MIN_FLOW) {
                m_next[c] -= flow; m_next[b] += flow; remaining -= flow;
            }
        }

        // 2) Horizontal: move toward the average with each same-level neighbor,
        //    leveling the surface. Only the higher cell of a pair flows.
        for (int k = 0; k < 4 && remaining > 0.0f; ++k) {
            int nx = x + HX[k], nz = z + HZ[k];
            if (!inBounds(nx, y, nz) || m_solid[idx(nx, y, nz)]) continue;
            const size_t n = idx(nx, y, nz);
            float flow = (remaining - m_mass[n]) * 0.25f * flowSide;
            flow = std::min(flow, remaining);
            if (flow > MIN_FLOW) {
                m_next[c] -= flow; m_next[n] += flow; remaining -= flow;
            }
        }

        // 3) Upward (pressure): if still compressed (> capacity), push the excess into
        //    the cell above. This is what makes connected water rise to a common level.
        if (remaining > MAX_MASS && y + 1 < m_sy && !m_solid[idx(x, y + 1, z)]) {
            const size_t a = idx(x, y + 1, z);
            float flow = remaining - stableBottom(remaining + m_mass[a]);
            flow = std::min(flow, remaining);
            if (flow > MIN_FLOW) {
                m_next[c] -= flow; m_next[a] += flow; remaining -= flow;
            }
        }
    }

    m_mass.swap(m_next);

    // Evaporation sink: thin cells (the frontier of a spreading flow, films) lose a
    // little mass each step, so free flow has a finite reach and spills dry up. Deep
    // (>= EVAP_THRESHOLD) water is untouched, so ponds and channels persist.
    if (m_evaporate) {
        const size_t n = m_mass.size();
        for (size_t i = 0; i < n; ++i) {
            float m = m_mass[i];
            if (m > 0.0f && m < EVAP_THRESHOLD && !m_solid[i])
                m_mass[i] = std::max(0.0f, m - EVAP_RATE);
        }
    }
}

} // namespace Core
} // namespace Phyxel
