#include "core/HydrologyMap.h"

#include "core/PriorityFlood.h"
#include "core/FlowField.h"

#include <algorithm>
#include <cmath>

namespace Phyxel {

HydrologyMap::HydrologyMap(const HeightFunc& heightAt, float originX, float originZ,
                           int cellsX, int cellsZ, float cellSize, float seaLevel)
    : m_originX(originX), m_originZ(originZ), m_cellSize(cellSize > 0.0f ? cellSize : 1.0f),
      m_seaLevel(seaLevel), m_cellsX(cellsX > 0 ? cellsX : 0), m_cellsZ(cellsZ > 0 ? cellsZ : 0) {
    if (m_cellsX <= 0 || m_cellsZ <= 0 || !heightAt) return;

    // Sample the coarse base height into a grid (cell (i,j) at its world corner).
    std::vector<float> base(static_cast<size_t>(m_cellsX) * m_cellsZ);
    for (int j = 0; j < m_cellsZ; ++j)
        for (int i = 0; i < m_cellsX; ++i) {
            const float h = heightAt(m_originX + i * m_cellSize, m_originZ + j * m_cellSize);
            base[static_cast<size_t>(j) * m_cellsX + i] = h;
            if (h < m_minTerrain) m_minTerrain = h;
        }

    // Depression-fill with the ocean as an outlet: inland basins fill only to their spill.
    std::vector<float> filled = PriorityFlood::fill(base, m_cellsX, m_cellsZ, seaLevel);

    m_filled.resize(filled.size());
    for (size_t i = 0; i < filled.size(); ++i) m_filled[i] = std::max(filled[i], seaLevel);
    // Resolve the water surface per cell: ocean at sea level; inland lake at the filled level; else dry.
    m_waterLevel.assign(base.size(), NO_WATER);
    for (size_t i = 0; i < base.size(); ++i) {
        if (base[i] <= m_seaLevel)
            m_waterLevel[i] = m_seaLevel;                         // ocean (or sub-sea basin)
        else if (filled[i] > base[i] + 1e-4f)
            m_waterLevel[i] = filled[i];                          // inland lake at its flat spill level
        // else: dry land (NO_WATER)
    }
}

void HydrologyMap::drainCarvedBasins(const FlowField& flow, const HeightFunc& heightAt,
                                     const CellSurfaceFunc& exitSurface) {
    m_basinsDrained = m_basinsUnresolved = 0;
    if (m_cellsX <= 0 || m_cellsZ <= 0 || !heightAt || !exitSurface) return;
    if (flow.cellsX() != m_cellsX || flow.cellsZ() != m_cellsZ) return;   // not the same grid: nothing to say
    const int n = m_cellsX * m_cellsZ;
    constexpr float kEps = 1e-3f;
    auto inland = [&](int c) { return m_waterLevel[c] > NO_WATER * 0.5f && m_waterLevel[c] > m_seaLevel + kEps; };
    std::vector<int> body(n, -1), cells, stack, beside;
    std::vector<int> besideMark(n, -1);   // body id that last listed this cell as a river cell beside it
    int nextId = 0;
    for (int s = 0; s < n; ++s) {
        if (!inland(s) || body[s] >= 0) continue;
        // Label one body: 4-connected, the same level (Priority-Flood basins are flat).
        const int id = nextId++;
        const float level = m_waterLevel[s];
        cells.clear(); beside.clear(); stack.assign(1, s); body[s] = id;
        int riverCell = -1;
        while (!stack.empty()) {
            const int c = stack.back(); stack.pop_back();
            cells.push_back(c);
            if (flow.orderOfCell(c) >= 3 && (riverCell < 0 || c < riverCell)) riverCell = c;
            const int x = c % m_cellsX, z = c / m_cellsX;
            // A carved channel BESIDE the body (any of the 8 neighbours, not the body's own): its valley (relief driven to 0
            // over a few channel widths, plus the meander) cuts the coarse rim one cell away - RiverCarve's 107.8 pits beside
            // the order-5 trunk flooded its 80 m-deep valley through a rim the fine terrain does not have.
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int qx = x + dx, qz = z + dz;
                    if ((dx == 0 && dz == 0) || qx < 0 || qz < 0 || qx >= m_cellsX || qz >= m_cellsZ) continue;
                    const int k = qz * m_cellsX + qx;
                    if (flow.orderOfCell(k) >= 3 && besideMark[k] != id && !(inland(k) && std::fabs(m_waterLevel[k] - level) <= kEps)) {
                        besideMark[k] = id; beside.push_back(k);
                    }
                }
            const int nb[4][2] = {{x + 1, z}, {x - 1, z}, {x, z + 1}, {x, z - 1}};
            for (const auto& q : nb) {
                if (q[0] < 0 || q[1] < 0 || q[0] >= m_cellsX || q[1] >= m_cellsZ) continue;
                const int k = q[1] * m_cellsX + q[0];
                if (body[k] < 0 && inland(k) && std::fabs(m_waterLevel[k] - level) <= kEps) { body[k] = id; stack.push_back(k); }
            }
        }
        float lowered = NO_WATER;
        if (riverCell >= 0) {
            // CROSSED: the exit cell, down the D8 chain until the next step leaves the body (or a sink / a cycle: bounded by n).
            int exitCell = riverCell;
            for (int guard = 0; guard < n; ++guard) {
                const int dn = flow.downstreamOfCell(exitCell);
                if (dn == exitCell || dn < 0 || dn >= n || body[dn] != id) break;
                exitCell = dn;
            }
            lowered = exitSurface(exitCell);
        } else if (!beside.empty()) {
            // BESIDE: the lowest carved surface among the channel cells round it (where the valley cuts deepest into the rim).
            std::sort(beside.begin(), beside.end());   // evaluation order fixed (the result is a min - order-free anyway)
            for (const int k : beside) {
                const float sk = exitSurface(k);
                if (sk > NO_WATER * 0.5f && (lowered <= NO_WATER * 0.5f || sk < lowered)) lowered = sk;
            }
        } else {
            continue;   // no carved channel crosses or passes it: the coarse rim is the rim
        }
        if (lowered <= NO_WATER * 0.5f) { ++m_basinsUnresolved; continue; }
        lowered = std::max(lowered, m_seaLevel);
        if (lowered >= level - kEps) continue;   // the channel's surface is not below the lake: it holds
        ++m_basinsDrained;
        for (const int c : cells) {
            // The same corner sample the constructor flooded.
            const float h = heightAt(m_originX + (c % m_cellsX) * m_cellSize, m_originZ + (c / m_cellsX) * m_cellSize);
            if (h < lowered) { m_waterLevel[c] = lowered; m_filled[c] = lowered; }
            else             { m_waterLevel[c] = NO_WATER; m_filled[c] = std::max(h, m_seaLevel); }
        }
    }
}

float HydrologyMap::waterLevelAt(float worldX, float worldZ) const {
    if (m_cellsX <= 0 || m_cellsZ <= 0) return NO_WATER;
    // Containing cell (piecewise-constant, so a lake's surface stays exactly flat within a basin;
    // the fine shoreline comes from the per-column terrain-vs-level test, not this coarse grid).
    int i = static_cast<int>(std::floor((worldX - m_originX) / m_cellSize));
    int j = static_cast<int>(std::floor((worldZ - m_originZ) / m_cellSize));
    if (i < 0 || j < 0 || i >= m_cellsX || j >= m_cellsZ) return NO_WATER;  // outside the baked region
    return m_waterLevel[static_cast<size_t>(j) * m_cellsX + i];
}

float HydrologyMap::filledAt(float worldX, float worldZ) const {
    if (m_cellsX <= 0 || m_cellsZ <= 0 || m_filled.empty()) return NO_WATER;
    const int i = static_cast<int>(std::floor((worldX - m_originX) / m_cellSize));
    const int j = static_cast<int>(std::floor((worldZ - m_originZ) / m_cellSize));
    if (i < 0 || j < 0 || i >= m_cellsX || j >= m_cellsZ) return NO_WATER;
    return m_filled[static_cast<size_t>(j) * m_cellsX + i];
}

}  // namespace Phyxel
