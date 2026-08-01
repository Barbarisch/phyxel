#include "core/WaterBodyIndex.h"

#include <cmath>
#include <vector>

namespace Phyxel {

WaterBodyIndex::WaterBodyIndex(const HydrologyMap& hydro, const HydrologyMap::HeightFunc& heightAt)
    : m_originX(hydro.originX()), m_originZ(hydro.originZ()), m_cellSize(hydro.cellSize()),
      m_cellsX(hydro.cellsX()), m_cellsZ(hydro.cellsZ()) {
    const size_t n = static_cast<size_t>(m_cellsX) * m_cellsZ;
    m_cellBody.assign(n, -1);
    if (n == 0) return;

    const std::vector<float>& lvl = hydro.levels();
    auto wet = [&](int cx, int cz) {
        return lvl[static_cast<size_t>(cz) * m_cellsX + cx] > HydrologyMap::NO_WATER * 0.5f;
    };
    auto levelOf = [&](int cx, int cz) { return lvl[static_cast<size_t>(cz) * m_cellsX + cx]; };

    // BFS connected components over wet cells; same-body iff neighbor levels match (basins are
    // flat by Priority-Flood construction, so level equality IS the basin membership test — two
    // touching basins at different spills split exactly at their divide).
    std::vector<int> stack;
    static const int NX[4] = {1, -1, 0, 0}, NZ[4] = {0, 0, 1, -1};
    for (int sz = 0; sz < m_cellsZ; ++sz)
        for (int sx = 0; sx < m_cellsX; ++sx) {
            const size_t si = static_cast<size_t>(sz) * m_cellsX + sx;
            if (m_cellBody[si] != -1 || !wet(sx, sz)) continue;

            const int32_t id = static_cast<int32_t>(m_bodies.size());
            Body b;
            b.id = id;
            b.level = levelOf(sx, sz);
            b.bboxMin = b.bboxMax = glm::ivec2(sx, sz);
            bool touchesBoundary = false;

            m_cellBody[si] = id;
            stack.assign(1, static_cast<int>(si));
            while (!stack.empty()) {
                const int ci = stack.back();
                stack.pop_back();
                const int cx = ci % m_cellsX, cz = ci / m_cellsX;
                ++b.areaCells;
                b.bboxMin = glm::min(b.bboxMin, glm::ivec2(cx, cz));
                b.bboxMax = glm::max(b.bboxMax, glm::ivec2(cx, cz));
                if (cx == 0 || cz == 0 || cx == m_cellsX - 1 || cz == m_cellsZ - 1)
                    touchesBoundary = true;
                // Volume while the height function is in scope (the bake doesn't retain it).
                const float terrain = heightAt(m_originX + (cx + 0.5f) * m_cellSize,
                                               m_originZ + (cz + 0.5f) * m_cellSize);
                b.volumeEst += std::max(0.0f, b.level - terrain) * m_cellSize * m_cellSize;

                for (int k = 0; k < 4; ++k) {
                    const int nx = cx + NX[k], nz = cz + NZ[k];
                    if (nx < 0 || nz < 0 || nx >= m_cellsX || nz >= m_cellsZ) continue;
                    const size_t ni = static_cast<size_t>(nz) * m_cellsX + nx;
                    if (m_cellBody[ni] != -1 || !wet(nx, nz)) continue;
                    if (std::fabs(levelOf(nx, nz) - b.level) > kLevelEps) continue;
                    m_cellBody[ni] = id;
                    stack.push_back(static_cast<int>(ni));
                }
            }

            // Classification. OCEAN needs both the boundary touch AND the sea level — an inland
            // lake that happens to reach the bake edge at altitude stays a lake, and a no-outlet
            // world (minTerrain above sea level) simply has no ocean.
            if (touchesBoundary && std::fabs(b.level - hydro.seaLevel()) <= kOceanLevelEps)
                b.cls = Class::Ocean;
            else if (b.areaCells >= kInfiniteMinCells)
                b.cls = Class::Lake;
            else
                b.cls = Class::Pond;

            m_bodies.push_back(b);
        }
}

int32_t WaterBodyIndex::bodyIdAt(float worldX, float worldZ) const {
    if (m_cellsX <= 0 || m_cellsZ <= 0) return -1;
    const int cx = static_cast<int>(std::floor((worldX - m_originX) / m_cellSize));
    const int cz = static_cast<int>(std::floor((worldZ - m_originZ) / m_cellSize));
    if (cx < 0 || cz < 0 || cx >= m_cellsX || cz >= m_cellsZ) return -1;
    return m_cellBody[static_cast<size_t>(cz) * m_cellsX + cx];
}

const WaterBodyIndex::Body* WaterBodyIndex::bodyAt(float worldX, float worldZ) const {
    return body(bodyIdAt(worldX, worldZ));
}

}  // namespace Phyxel
