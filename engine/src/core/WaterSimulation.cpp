#include "core/WaterSimulation.h"
#include <algorithm>

namespace Phyxel {
namespace Core {

WaterSimulation::WaterSimulation(int sizeX, int sizeY, int sizeZ)
    : m_sx(sizeX), m_sy(sizeY), m_sz(sizeZ),
      m_mass(static_cast<size_t>(sizeX) * sizeY * sizeZ, 0.0f),
      m_solid(static_cast<size_t>(sizeX) * sizeY * sizeZ, 0),
      m_next(static_cast<size_t>(sizeX) * sizeY * sizeZ, 0.0f) {}

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

void WaterSimulation::step(float flowSide) {
    // Start from the current state; all flow reads m_mass (this frame's snapshot) and
    // accumulates into m_next, so transfers are order-independent and mass-conserving.
    m_next = m_mass;

    static const int HX[4] = {1, -1, 0, 0};
    static const int HZ[4] = {0, 0, 1, -1};

    for (int z = 0; z < m_sz; ++z)
    for (int y = 0; y < m_sy; ++y)
    for (int x = 0; x < m_sx; ++x) {
        const size_t c = idx(x, y, z);
        if (m_solid[c]) continue;
        float avail = m_mass[c]; // mass this cell may still move out this tick
        if (avail <= 0.0f) continue;

        // 1) Gravity: push straight down into the free capacity below.
        if (y - 1 >= 0 && !m_solid[idx(x, y - 1, z)]) {
            const size_t b = idx(x, y - 1, z);
            float space = MAX_MASS - m_mass[b];
            if (space > 0.0f) {
                float f = std::min(avail, space);
                m_next[c] -= f;
                m_next[b] += f;
                avail     -= f;
            }
        }

        // 2) Horizontal: donate a damped fraction of the excess to lower neighbors,
        //    leveling the surface. Only the higher cell of a pair flows, so a pair is
        //    never double-counted within a tick.
        for (int k = 0; k < 4 && avail > 0.0f; ++k) {
            int nx = x + HX[k], nz = z + HZ[k];
            if (!inBounds(nx, y, nz) || m_solid[idx(nx, y, nz)]) continue;
            const size_t n = idx(nx, y, nz);
            float diff = m_mass[c] - m_mass[n];
            if (diff <= 0.0f) continue;
            float f = std::min(avail, diff * 0.25f * flowSide);
            if (f <= 0.0f) continue;
            m_next[c] -= f;
            m_next[n] += f;
            avail     -= f;
        }
    }

    m_mass.swap(m_next);
}

} // namespace Core
} // namespace Phyxel
