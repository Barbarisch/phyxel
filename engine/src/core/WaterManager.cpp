#include "core/WaterManager.h"
#include "core/ChunkManager.h"
#include <algorithm>
#include <cmath>

namespace Phyxel {
namespace Core {

WaterManager::WaterManager(ChunkManager* chunkManager, const glm::ivec3& origin, const glm::ivec3& dims)
    : m_cm(chunkManager), m_origin(origin), m_dims(dims),
      m_sim(dims.x, dims.y, dims.z) {
    m_sim.setEvaporation(true); // bound free flow / dry thin spills in-game
    syncSolidsFromChunks();
    rebuildSurface();
}

void WaterManager::syncSolidsFromChunks() {
    if (!m_cm) return;
    for (int z = 0; z < m_dims.z; ++z)
    for (int y = 0; y < m_dims.y; ++y)
    for (int x = 0; x < m_dims.x; ++x) {
        glm::ivec3 world(m_origin.x + x, m_origin.y + y, m_origin.z + z);
        m_sim.setSolid(x, y, z, m_cm->hasVoxelAt(world));
    }
}

void WaterManager::update(float dt) {
    if (m_oceanDirty) rebuildOcean(); // re-flood once before stepping
    m_accum += std::min(dt, 0.25f);
    int steps = 0;
    while (m_accum >= STEP_DT && steps < MAX_STEPS_PER_UPDATE) {
        m_sim.step();
        m_accum -= STEP_DT;
        ++steps;
    }
    if (steps == MAX_STEPS_PER_UPDATE) m_accum = 0.0f; // drop backlog after a stall
    if (steps > 0) rebuildSurface();
}

void WaterManager::rebuildSurface() {
    m_surface.clear();
    for (int z = 0; z < m_dims.z; ++z)
    for (int x = 0; x < m_dims.x; ++x)
    for (int y = 0; y < m_dims.y; ++y) {
        float m = m_sim.massAt(x, y, z);
        if (m <= RENDER_MIN) continue;
        // Surface cell: the one above is empty (or solid / out of bounds).
        if (m_sim.massAt(x, y + 1, z) > RENDER_MIN && !m_sim.isSolid(x, y + 1, z)) continue;
        float fill = std::min(m, 1.0f);
        m_surface.emplace_back(
            static_cast<float>(m_origin.x + x) + 0.5f,
            static_cast<float>(m_origin.y + y) + fill,
            static_cast<float>(m_origin.z + z) + 0.5f,
            fill);
    }
}

bool WaterManager::worldToLocal(const glm::vec3& w, int& lx, int& ly, int& lz) const {
    lx = static_cast<int>(std::floor(w.x)) - m_origin.x;
    ly = static_cast<int>(std::floor(w.y)) - m_origin.y;
    lz = static_cast<int>(std::floor(w.z)) - m_origin.z;
    return m_sim.inBounds(lx, ly, lz);
}

void WaterManager::placeWater(const glm::vec3& worldPos, float amount) {
    int lx, ly, lz;
    if (worldToLocal(worldPos, lx, ly, lz)) {
        m_sim.addWater(lx, ly, lz, amount);
        rebuildSurface();
    }
}

void WaterManager::setSolidWorld(int worldX, int worldY, int worldZ, bool solid) {
    int lx = worldX - m_origin.x, ly = worldY - m_origin.y, lz = worldZ - m_origin.z;
    if (m_sim.inBounds(lx, ly, lz)) {
        m_sim.setSolid(lx, ly, lz, solid);
        // Terrain changed: re-flood the ocean so breaches fill / dug seabed refills.
        if (!m_oceanSeeds.empty()) m_oceanDirty = true;
    }
}

void WaterManager::setSeaLevel(float worldY) {
    m_seaLevel = worldY;
    if (!m_oceanSeeds.empty()) m_oceanDirty = true;
}

void WaterManager::addOceanSeed(const glm::vec3& worldPos) {
    m_oceanSeeds.emplace_back(static_cast<int>(std::floor(worldPos.x)),
                              static_cast<int>(std::floor(worldPos.y)),
                              static_cast<int>(std::floor(worldPos.z)));
    m_oceanDirty = true;
}

void WaterManager::clearOcean() {
    m_oceanSeeds.clear();
    m_sim.fillOcean({}, 0); // clears all source pins
    applySprings();         // ...but keep authored springs
    m_oceanDirty = false;
    rebuildSurface();
}

void WaterManager::rebuildOcean() {
    m_oceanDirty = false;
    const int seaLevelLocalY = static_cast<int>(std::floor(m_seaLevel)) - m_origin.y;
    std::vector<glm::ivec3> localSeeds;
    localSeeds.reserve(m_oceanSeeds.size());
    for (const glm::ivec3& s : m_oceanSeeds)
        localSeeds.emplace_back(s.x - m_origin.x, s.y - m_origin.y, s.z - m_origin.z);
    m_sim.fillOcean(localSeeds, seaLevelLocalY); // clears all sources, then pins the ocean
    applySprings();                               // re-pin authored springs over the top
    rebuildSurface();
}

void WaterManager::applySprings() {
    for (const Spring& s : m_springs) {
        int lx = s.cell.x - m_origin.x, ly = s.cell.y - m_origin.y, lz = s.cell.z - m_origin.z;
        if (m_sim.inBounds(lx, ly, lz)) m_sim.setSource(lx, ly, lz, s.mass);
    }
}

void WaterManager::addSpring(const glm::vec3& worldPos, float mass) {
    glm::ivec3 cell(static_cast<int>(std::floor(worldPos.x)),
                    static_cast<int>(std::floor(worldPos.y)),
                    static_cast<int>(std::floor(worldPos.z)));
    m_springs.push_back({cell, mass});
    int lx = cell.x - m_origin.x, ly = cell.y - m_origin.y, lz = cell.z - m_origin.z;
    if (m_sim.inBounds(lx, ly, lz)) m_sim.setSource(lx, ly, lz, mass);
    rebuildSurface();
}

void WaterManager::setChannelWorld(int worldX, int worldY, int worldZ, bool channel) {
    int lx = worldX - m_origin.x, ly = worldY - m_origin.y, lz = worldZ - m_origin.z;
    if (!m_sim.inBounds(lx, ly, lz)) return;
    m_sim.setChannel(lx, ly, lz, channel);
    if (channel) m_channelCells.emplace_back(worldX, worldY, worldZ);
}

void WaterManager::setChannelRegion(const glm::ivec3& a, const glm::ivec3& b) {
    glm::ivec3 lo(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z));
    glm::ivec3 hi(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z));
    for (int z = lo.z; z <= hi.z; ++z)
        for (int y = lo.y; y <= hi.y; ++y)
            for (int x = lo.x; x <= hi.x; ++x)
                setChannelWorld(x, y, z, true);
}

void WaterManager::clearSprings() {
    for (const Spring& s : m_springs) {
        int lx = s.cell.x - m_origin.x, ly = s.cell.y - m_origin.y, lz = s.cell.z - m_origin.z;
        if (m_sim.inBounds(lx, ly, lz)) m_sim.clearSource(lx, ly, lz);
    }
    m_springs.clear();
    rebuildSurface();
}

float WaterManager::massAtWorld(const glm::vec3& worldPos) const {
    int lx, ly, lz;
    if (!worldToLocal(worldPos, lx, ly, lz)) return 0.0f;
    return m_sim.massAt(lx, ly, lz);
}

} // namespace Core
} // namespace Phyxel
