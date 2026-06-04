#include "core/WaterManager.h"
#include "core/ChunkManager.h"
#include <algorithm>
#include <cmath>

namespace Phyxel {
namespace Core {

WaterManager::WaterManager(ChunkManager* chunkManager, const glm::ivec3& origin, const glm::ivec3& dims)
    : m_cm(chunkManager), m_origin(origin), m_dims(dims),
      m_sim(dims.x, dims.y, dims.z) {
    syncSolidsFromChunks();
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
    m_accum += std::min(dt, 0.25f);
    int steps = 0;
    while (m_accum >= STEP_DT && steps < MAX_STEPS_PER_UPDATE) {
        m_sim.step();
        m_accum -= STEP_DT;
        ++steps;
    }
    if (steps == MAX_STEPS_PER_UPDATE) m_accum = 0.0f; // drop backlog after a stall
}

bool WaterManager::worldToLocal(const glm::vec3& w, int& lx, int& ly, int& lz) const {
    lx = static_cast<int>(std::floor(w.x)) - m_origin.x;
    ly = static_cast<int>(std::floor(w.y)) - m_origin.y;
    lz = static_cast<int>(std::floor(w.z)) - m_origin.z;
    return m_sim.inBounds(lx, ly, lz);
}

void WaterManager::placeWater(const glm::vec3& worldPos, float amount) {
    int lx, ly, lz;
    if (worldToLocal(worldPos, lx, ly, lz)) m_sim.addWater(lx, ly, lz, amount);
}

float WaterManager::massAtWorld(const glm::vec3& worldPos) const {
    int lx, ly, lz;
    if (!worldToLocal(worldPos, lx, ly, lz)) return 0.0f;
    return m_sim.massAt(lx, ly, lz);
}

} // namespace Core
} // namespace Phyxel
