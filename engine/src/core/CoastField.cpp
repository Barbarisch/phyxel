#include "core/CoastField.h"

#include "core/HydrologyMap.h"
#include "core/WaterBodyIndex.h"

#include <algorithm>
#include <cmath>

namespace Phyxel {

namespace {
// Two-pass chamfer distance transform (Borgefors): seeds at 0, 8-neighbour weights 1 / sqrt(2),
// in cell units. Exact enough for a shoreline band (error < 8 % of the distance).
void chamfer(std::vector<float>& d, int nx, int nz) {
    const float s2 = std::sqrt(2.0f);
    auto at = [&](int i, int j) -> float& { return d[static_cast<size_t>(j) * nx + i]; };
    for (int j = 0; j < nz; ++j)
        for (int i = 0; i < nx; ++i) {
            float v = at(i, j);
            if (i > 0)           v = std::min(v, at(i - 1, j) + 1.0f);
            if (j > 0)           v = std::min(v, at(i, j - 1) + 1.0f);
            if (i > 0 && j > 0)  v = std::min(v, at(i - 1, j - 1) + s2);
            if (i + 1 < nx && j > 0) v = std::min(v, at(i + 1, j - 1) + s2);
            at(i, j) = v;
        }
    for (int j = nz - 1; j >= 0; --j)
        for (int i = nx - 1; i >= 0; --i) {
            float v = at(i, j);
            if (i + 1 < nx)              v = std::min(v, at(i + 1, j) + 1.0f);
            if (j + 1 < nz)              v = std::min(v, at(i, j + 1) + 1.0f);
            if (i + 1 < nx && j + 1 < nz) v = std::min(v, at(i + 1, j + 1) + s2);
            if (i > 0 && j + 1 < nz)     v = std::min(v, at(i - 1, j + 1) + s2);
            at(i, j) = v;
        }
}
}  // namespace

CoastField::CoastField(const HydrologyMap& hydro, const WaterBodyIndex& bodies)
    : m_originX(hydro.originX()), m_originZ(hydro.originZ()), m_cellSize(hydro.cellSize()),
      m_cellsX(hydro.cellsX()), m_cellsZ(hydro.cellsZ()) {
    const size_t n = static_cast<size_t>(m_cellsX) * m_cellsZ;
    if (n == 0) return;
    std::vector<uint8_t> sea(n, 0);
    for (int j = 0; j < m_cellsZ; ++j)
        for (int i = 0; i < m_cellsX; ++i) {
            const int32_t id = bodies.bodyIdAtCell(i, j);
            const WaterBodyIndex::Body* b = bodies.body(id);
            if (b && b->cls == WaterBodyIndex::Class::Ocean) { sea[static_cast<size_t>(j) * m_cellsX + i] = 1; ++m_seaCells; }
        }
    m_dist.assign(n, kFar);
    if (m_seaCells == 0 || static_cast<size_t>(m_seaCells) == n) return;
    const float big = static_cast<float>(m_cellsX + m_cellsZ) * 2.0f;
    std::vector<float> toSea(n, big), toLand(n, big);
    for (size_t c = 0; c < n; ++c) { if (sea[c]) toSea[c] = 0.0f; else toLand[c] = 0.0f; }
    chamfer(toSea, m_cellsX, m_cellsZ);
    chamfer(toLand, m_cellsX, m_cellsZ);
    for (size_t c = 0; c < n; ++c)
        m_dist[c] = (sea[c] ? -toLand[c] : toSea[c]) * m_cellSize;
}

float CoastField::signedDistAt(float worldX, float worldZ) const {
    if (m_dist.empty() || m_seaCells == 0 || m_cellsX <= 1 || m_cellsZ <= 1) return kFar;
    const float u = (worldX - m_originX) / m_cellSize - 0.5f, v = (worldZ - m_originZ) / m_cellSize - 0.5f;
    if (u < -1.0f || v < -1.0f || u > static_cast<float>(m_cellsX) || v > static_cast<float>(m_cellsZ)) return kFar;
    const float fu = std::floor(u), fv = std::floor(v);
    const float tu = u - fu, tv = v - fv;
    const int i0 = std::clamp(static_cast<int>(fu), 0, m_cellsX - 1), i1 = std::clamp(static_cast<int>(fu) + 1, 0, m_cellsX - 1);
    const int j0 = std::clamp(static_cast<int>(fv), 0, m_cellsZ - 1), j1 = std::clamp(static_cast<int>(fv) + 1, 0, m_cellsZ - 1);
    auto at = [&](int i, int j) { return m_dist[static_cast<size_t>(j) * m_cellsX + i]; };
    return (at(i0, j0) * (1 - tu) + at(i1, j0) * tu) * (1 - tv) + (at(i0, j1) * (1 - tu) + at(i1, j1) * tu) * tv;
}

}  // namespace Phyxel
