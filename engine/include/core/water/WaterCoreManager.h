#pragma once
// WaterCoreManager — the engine's owner of WaterCore active volumes (docs/WaterCore.md §5, §15.1).
//
// Phase B scope: volumes are created over a world box by debug routes, stepped explicitly by the
// harness (or every frame when realtime is on), read back by world position, and fed to the
// existing cell renderer at 1-voxel column resolution so Phase B has eyes without new rendering.
// Solids come from a three-state micro-occupancy query the engine binds (Empty / Solid / Unknown
// per world micro cell, 9 per voxel per axis); Unknown is a hold wall (§5.1).
#include "core/water/WaterCore.h"
#include "core/water/WaterCoreGpu.h"   // Phase C backend (optional; CPU reference without it)
#include "core/WaterManager.h"   // WaterSurfaceCell (the debug feed's struct)
#include <glm/glm.hpp>
#include <array>
#include <climits>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Phyxel {
namespace Core {
namespace Water {

/// 0 = Empty (known air), 1 = Solid, 2 = Unknown — mirrors Graphics::OccupancyState.
using MicroStateQuery = std::function<int(const glm::ivec3& worldMicro)>;
/// The occupancy pool's pack revision (any monotonic counter that changes when solids change).
using SolidsRevisionQuery = std::function<uint64_t()>;

struct AvRecord {
    int id = 0;
    glm::ivec3 minVoxel{0}, maxVoxel{0};   ///< world voxel box, inclusive
    float cellSize = 1.0f;
    std::string transport;                 ///< the solver's CURRENT transport (a FLIP volume reads "eulerian" after its rest conversion)
    std::string backend = "cpu";          ///< Phase C: "cpu" (the reference) or "gpu" (WaterCoreGpu, parity-gated)
    float rbgsResidual = 0.0f;             ///< GPU backend: max |A p - b| after the last substep's sweeps (0 on the CPU)
    int   gpuSweeps = 0;
    double gpuMs = 0.0;                    ///< wall time of the last GPU step call (all its ticks)
    size_t particles = 0;                  ///< Phase B2: FLIP particles alive (0 for fills)
    size_t cells = 0;
    bool asleep = false;
    double mass = 0.0;                     ///< m^3
    double kineticEnergy = 0.0;
    int lastSubsteps = 0;
    int lastPcgIterations = 0;
    double lastPcgResidual = 0.0;
    int quietTicks = 0;
    double sourceUnplaced = 0.0;
    double residueDropped = 0.0;           ///< m^3 dropped by the residue sweep over the volume's life
    double sourcePlaced = 0.0;             ///< m^3 all sources actually added (negative = removed) over the volume's life
    int    sourceCount = 0;
};

struct ProbeResult {
    bool inVolume = false;
    int avId = -1;
    float fill = 0.0f;
    glm::vec3 velocity{0.0f};
    float surfaceY = 0.0f;                 ///< NaN when the column is dry
    double pressure = 0.0;
    int occupancy = 0;                     ///< Occ of the cell (0 air, 1 solid, 2 unknown)
};

struct ColumnSample {
    int x = 0, z = 0;
    bool inVolume = false;
    float surfaceY = 0.0f;                 ///< NaN when dry
    double mass = 0.0;                     ///< m^3 in this 1x1 world column
};

class WaterCoreManager {
public:
    explicit WaterCoreManager(MicroStateQuery state, SolidsRevisionQuery revision = nullptr);

    /// Create a volume over an inclusive world voxel box. `cellSize` must be a power-of-three
    /// fraction of a voxel (1, 1/3, 1/9, 1/27, 1/81); `transport` is "eulerian" (Phase B).
    /// Refuses (returns 0, fills `err`) when the cell count would exceed `maxCells`.
    int create(const glm::ivec3& minVoxel, const glm::ivec3& maxVoxel, float cellSize,
               const std::string& transport, std::string* err, const std::string& backend = "cpu", int gpuSweeps = 0 /* <= 0: auto, 1.5 x the longest dimension */);
    /// Phase C: give the manager a device; GPU volumes are refused until this succeeds (loudly).
    bool initGpu(VkDevice device, VkPhysicalDevice physical, VkQueue queue, uint32_t queueFamily, const std::string& shaderDir, std::string* err);
    bool gpuReady() const { return m_gpu && m_gpu->ready(); }
    static constexpr int kGpuSweeps = 40;   ///< default red-black SOR sweeps per projection (docs/WaterCore.md 15.12: omega 1.85, measured)
    static constexpr int kGpuSweepsMin = 8, kGpuSweepsMax = 160;   ///< fewer never converges a 26 m basin; more is the whole dispatch budget (15.11)
    bool destroy(int id);
    std::vector<AvRecord> list() const;
    const AvRecord* find(int id) const;

    /// Step one volume `ticks` times (explicit, simulation time).
    bool step(int id, int ticks, float dt, AvRecord* out);
    /// Realtime: step every awake volume once per call (Application::update) when enabled.
    void update(float dt);
    void setRealtime(bool on) { m_realtime = on; }
    bool realtime() const { return m_realtime; }
    /// The world changed: every volume re-samples its solids before its next tick.
    void markSolidsDirty();

    /// Set f for every cell inside the world voxel box (replace, not add); wakes the volume.
    /// Returns the number of cells set. Cells outside every volume are ignored (reported).
    long placeBox(const glm::ivec3& minVoxel, const glm::ivec3& maxVoxel, float fill, long* outsideCells);
    bool addSource(int id, const glm::vec3& world, float rate, std::string* err);
    bool clearSources(int id);   ///< stop every source on the volume (the pump is switched off)
    /// Phase B2: a deterministic sample of up to `max` particles as (x, y, z, vx, vy, vz) in world
    /// metres and m/s (every k-th of the sorted list); empty for a fill volume.
    std::vector<std::array<float, 6>> particleSample(int id, int max) const;
    /// Phase B2: force the rest conversion (particles -> fills); returns false for a fill volume.
    bool settle(int id);
    /// Phase B2 debug draw: every particle of every particle volume as (x, y, z, cell size) in world
    /// metres, in the deterministic list order (subsampled by the renderer when over its cap).
    const std::vector<glm::vec4>& particleDrawList();
    bool addImpulse(const glm::vec3& world, float radius, float deltaSpeed, const glm::vec3& dir);

    ProbeResult probe(const glm::vec3& world);   // refreshes the solids cache first
    /// Per world column (x, z): the highest surface and the mass over cells whose world y lies in
    /// [yMin, yMax] (voxel units; the default spans everything). The y range lets a probe tell a
    /// cavity under a floor from the pool above it (S5).
    std::vector<ColumnSample> probeColumns(int x1, int z1, int x2, int z2, int yMin = INT_MIN, int yMax = INT_MAX) const;
    double totalMass() const;
    size_t totalCells() const;

    /// Debug feed: one WaterSurfaceCell per wet world column (1 x 1), rebuilt on demand.
    const std::vector<WaterSurfaceCell>& surfaceCells();

    static constexpr size_t kMaxCellsPerVolume = 2'000'000;   ///< CPU reference ceiling (§15.4)
    static bool snapCellSize(float requested, float* snapped);  ///< power-of-three fractions only

private:
    struct Av {
        int id = 0;
        float h = 1.0f;
        int per = 1;                       ///< cells per voxel per axis
        glm::ivec3 minVoxel{0}, maxVoxel{0};
        std::string transport;
        std::unique_ptr<WaterGrid> grid;
        std::unique_ptr<WaterSolver> solver;
        std::vector<Occ> occCache;         ///< per grid cell, refreshed when dirty
        bool occDirty = true;
        uint64_t occRevision = ~0ull;   // pack revision the cache was sampled at
        StepReport last;
        double residueDropped = 0.0;   // cumulative
        std::string backend = "cpu";
        WaterCoreGpu::Volume* gpuVol = nullptr;
        bool gpuDirty = true;          // the grid (fills, velocities or occupancy) is newer than the GPU copy
        GpuStepStats gpuLast;
        int gpuSweeps = kGpuSweeps;
        bool gpuStale = false;         // the GPU copy is newer than the grid (a step ran, no download yet)
        double gpuLastDownloadSec = -1.0;
    };
    void refreshOccupancy(Av& av);
    Occ sampleOccupancy(const Av& av, const glm::ivec3& cellLocal) const;
    AvRecord record(const Av& av) const;
    const Av* volumeAtVoxel(int x, int y, int z) const;

    MicroStateQuery m_state;
    SolidsRevisionQuery m_revision;
    std::unique_ptr<WaterCoreGpu> m_gpu;
    void stepGpu(Av& av, int ticks, float dt);
    bool pushSourcesToGpu(Av& av, std::string* err);
    void syncFromGpu(Av& av, bool rateLimited = false);   // download when stale (realtime: at most once a second unless forced)
    std::vector<std::unique_ptr<Av>> m_avs;
    int m_nextId = 1;
    bool m_realtime = false;
    std::vector<WaterSurfaceCell> m_surface;
    std::vector<glm::vec4> m_particleDraw;
};

} // namespace Water
} // namespace Core
} // namespace Phyxel
