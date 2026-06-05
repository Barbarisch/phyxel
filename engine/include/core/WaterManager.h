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

    // Update one cell's solid state (world coords) — wired to voxel break/place so
    // water flows into newly-removed cells on the next step. Cheap; ignores cells
    // outside the region.
    void  setSolidWorld(int worldX, int worldY, int worldZ, bool solid);

    // --- Ocean seam (infinite reservoir at sea level) ---
    // Open cells at/below `seaLevel` that are connected to an ocean seed become an
    // infinite reservoir: they hold sea level, refill when dug, and flood through
    // breaches; sealed sub-sea cavities stay dry. (See docs/WaterSystem.md.)
    void  setSeaLevel(float worldY);
    float seaLevel() const { return m_seaLevel; }
    void  addOceanSeed(const glm::vec3& worldPos); // a point the ocean floods out from
    void  clearOcean();                             // remove the ocean (seeds + pins)

    // --- Authored sources (springs / river heads) ---
    // A spring is a persistent source pinned to `mass` each step — a continuous supply
    // (a fountain, a river head). Survives ocean re-floods (kept separate from the
    // ocean's pinned cells).
    void  addSpring(const glm::vec3& worldPos, float mass);
    void  clearSprings();

    // --- Persistence accessors (authoring inputs; the field reconstructs on load) ---
    const std::vector<glm::ivec3>& oceanSeeds() const { return m_oceanSeeds; }
    std::vector<glm::vec4> springsData() const {       // (x, y, z, mass) per spring
        std::vector<glm::vec4> out;
        out.reserve(m_springs.size());
        for (const Spring& s : m_springs)
            out.emplace_back(float(s.cell.x), float(s.cell.y), float(s.cell.z), s.mass);
        return out;
    }

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
    void rebuildOcean(); // re-run the ocean flood-fill from the seeds
    void applySprings(); // (re-)pin authored springs after the ocean clears sources

    float                   m_seaLevel = 0.0f;
    bool                    m_oceanDirty = false;
    std::vector<glm::ivec3> m_oceanSeeds; // world-space flood seeds

    struct Spring { glm::ivec3 cell; float mass; };
    std::vector<Spring>     m_springs;    // world-space authored sources

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
