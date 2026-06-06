#pragma once

#include "core/WaterSimulation.h"
#include "vulkan/ComputePipeline.h"
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

namespace Phyxel {

class ChunkManager;
namespace Vulkan { class VulkanDevice; }

namespace Core {

// One renderable water surface quad. Carries a *sloped* top (per-corner world Y,
// averaged from neighbouring columns to remove stair-stepping) and the water column's
// depth so the shader can darken/opacify deep water and fade thin shorelines.
struct WaterSurfaceCell {
    glm::vec4 centerDepth; // xyz = cell-center surface point (y = cellY+fill), w = column depth (cells)
    glm::vec4 corners;     // per-quad-corner world Y: (-x,-z), (+x,-z), (+x,+z), (-x,+z)
    glm::vec4 skirt;       // per-edge side-face bottom world Y: (+x), (-x), (+z), (-z)
};

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

    // --- Channels (authored riverbeds, exempt from evaporation) ---
    void  setChannelWorld(int worldX, int worldY, int worldZ, bool channel);
    void  setChannelRegion(const glm::ivec3& a, const glm::ivec3& b); // inclusive box

    // --- Persistence accessors (authoring inputs; the field reconstructs on load) ---
    const std::vector<glm::ivec3>& oceanSeeds()   const { return m_oceanSeeds; }
    const std::vector<glm::ivec3>& channelCells() const { return m_channelCells; }

    // --- GPU backend ---
    // Run the per-tick flow step on a compute shader instead of the CPU. Behaviour is
    // close (gather-formulated, see docs/WaterSystem.md), not bit-identical. Masks
    // (solid/source/channel) and the field round-trip CPU<->GPU each step, so all CPU
    // authoring (ocean flood, springs, place/edit) and rendering keep working unchanged.
    void enableGpu(Vulkan::VulkanDevice* device); // create GPU resources (once)
    void setUseGpu(bool on) { m_useGpu = on && m_gpuReady; }
    bool useGpu() const { return m_useGpu; }
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

    // Renderable water surface: one WaterSurfaceCell per surface cell (a water cell
    // whose cell above is ~empty) with a smoothed sloped top + column depth. Rebuilt
    // whenever the field changes (step/sync/place).
    const std::vector<WaterSurfaceCell>& surfaceCells() const { return m_surface; }

    // Detected waterfall lips: xyz = world lip point, w = drop height. A side-face edge
    // whose skirt falls >= WATERFALL_MIN_DROP into open air or much-lower water. Consumed
    // by the host to spawn mist/spray. Rebuilt alongside the surface.
    const std::vector<glm::vec4>& waterfalls() const { return m_waterfalls; }

    // Minimum cell mass that renders / is treated as a surface (ignores thin film).
    static constexpr float RENDER_MIN = 0.05f;
    // A skirt edge dropping at least this far (cells) is treated as a waterfall lip.
    static constexpr float WATERFALL_MIN_DROP = 1.5f;
    static constexpr size_t MAX_WATERFALLS = 48; // cap mist emitter points

private:
    bool worldToLocal(const glm::vec3& w, int& lx, int& ly, int& lz) const;
    void rebuildSurface();
    void rebuildOcean(); // re-run the ocean flood-fill from the seeds
    void applySprings(); // (re-)pin authored springs after the ocean clears sources

    float                   m_seaLevel = 0.0f;
    bool                    m_oceanDirty = false;
    std::vector<glm::ivec3> m_oceanSeeds; // world-space flood seeds

    struct Spring { glm::ivec3 cell; float mass; };
    std::vector<Spring>     m_springs;      // world-space authored sources
    std::vector<glm::ivec3> m_channelCells; // world-space channel cells (for persistence)

    // GPU backend state.
    void stepGpu();        // upload field+masks, dispatch flow, read back
    void uploadMasks();    // solid/source/channel CPU -> GPU
    Vulkan::VulkanDevice*  m_vk = nullptr;
    bool                   m_useGpu = false;
    bool                   m_gpuReady = false;
    bool                   m_gpuMasksDirty = true;
    Vulkan::ComputePipeline m_flowPipe;
    VkBuffer       m_bufMassIn = VK_NULL_HANDLE,  m_bufMassOut = VK_NULL_HANDLE;
    VkBuffer       m_bufSolid  = VK_NULL_HANDLE,  m_bufSource = VK_NULL_HANDLE, m_bufChannel = VK_NULL_HANDLE;
    VkDeviceMemory m_memMassIn = VK_NULL_HANDLE,  m_memMassOut = VK_NULL_HANDLE;
    VkDeviceMemory m_memSolid  = VK_NULL_HANDLE,  m_memSource = VK_NULL_HANDLE, m_memChannel = VK_NULL_HANDLE;
    void*          m_mapMassIn = nullptr;  void* m_mapMassOut = nullptr;
    void*          m_mapSolid  = nullptr;  void* m_mapSource  = nullptr; void* m_mapChannel = nullptr;

    ChunkManager*   m_cm;
    glm::ivec3      m_origin;
    glm::ivec3      m_dims;
    WaterSimulation m_sim;
    std::vector<WaterSurfaceCell> m_surface;   // cached renderable surface cells
    std::vector<glm::vec4>        m_waterfalls; // mist emitter points (lip xyz, drop w)
    float           m_accum = 0.0f;

    static constexpr float STEP_HZ = 20.0f;
    static constexpr float STEP_DT = 1.0f / STEP_HZ;
    static constexpr int   MAX_STEPS_PER_UPDATE = 4; // cap to avoid spiral-of-death
};

} // namespace Core
} // namespace Phyxel
