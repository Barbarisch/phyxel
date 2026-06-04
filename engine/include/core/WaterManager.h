#pragma once

#include "core/WaterSimulation.h"
#include <glm/glm.hpp>

namespace Phyxel {

class ChunkManager;

namespace Core {

// Runs the CPU water cellular automaton (WaterSimulation) over a fixed axis-aligned
// region of the live world. Solidity is read from the chunk terrain; the sim is
// stepped at a fixed rate independent of frame rate. This is the CPU integration that
// precedes the GPU compute port + per-cell rendering (see docs/WaterSystem.md).
class WaterManager {
public:
    WaterManager(ChunkManager* chunkManager, const glm::ivec3& origin, const glm::ivec3& dims);

    // Re-read terrain solidity for the whole region from the chunk manager. Call after
    // the world loads and whenever terrain changes (e.g. after destruction).
    void syncSolidsFromChunks();

    // Fixed-timestep stepping; accumulates real time and steps at STEP_HZ.
    void update(float dt);

    // World-space helpers. Amount may be negative to remove water.
    void  placeWater(const glm::vec3& worldPos, float amount);
    float massAtWorld(const glm::vec3& worldPos) const;

    float totalMass() const { return m_sim.totalMass(); }
    const glm::ivec3& origin() const { return m_origin; }
    const glm::ivec3& dims() const   { return m_dims; }
    const WaterSimulation& sim() const { return m_sim; }

    // Renderable water surface: one entry per surface cell (a water cell whose cell
    // above is ~empty), as vec4(worldCenterX, worldSurfaceY, worldCenterZ, fill), where
    // surfaceY = cellY + fill. Rebuilt whenever the field changes (step/sync/place).
    const std::vector<glm::vec4>& surfaceCells() const { return m_surface; }

    // Minimum cell mass that renders / is treated as a surface (ignores thin film).
    static constexpr float RENDER_MIN = 0.05f;

private:
    bool worldToLocal(const glm::vec3& w, int& lx, int& ly, int& lz) const;
    void rebuildSurface();

    ChunkManager*   m_cm;
    glm::ivec3      m_origin;
    glm::ivec3      m_dims;
    WaterSimulation m_sim;
    std::vector<glm::vec4> m_surface; // cached renderable surface cells
    float           m_accum = 0.0f;

    static constexpr float STEP_HZ = 20.0f;
    static constexpr float STEP_DT = 1.0f / STEP_HZ;
    static constexpr int   MAX_STEPS_PER_UPDATE = 4; // cap to avoid spiral-of-death
};

} // namespace Core
} // namespace Phyxel
