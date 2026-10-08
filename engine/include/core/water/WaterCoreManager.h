#pragma once
// WaterCoreManager — the engine's owner of WaterCore active volumes (docs/WaterCore.md §5, §15.1).
//
// Phase B scope: volumes are created over a world box by debug routes, stepped explicitly by the
// harness (or every frame when realtime is on), read back by world position, and fed to the
// existing cell renderer at 1-voxel column resolution so Phase B has eyes without new rendering.
// Solids come from a three-state micro-occupancy query the engine binds (Empty / Solid / Unknown
// per world micro cell, 9 per voxel per axis); Unknown is a hold wall (§5.1).
#include "core/water/WaterCore.h"
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
               const std::string& transport, std::string* err);
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
    };
    void refreshOccupancy(Av& av);
    Occ sampleOccupancy(const Av& av, const glm::ivec3& cellLocal) const;
    AvRecord record(const Av& av) const;
    const Av* volumeAtVoxel(int x, int y, int z) const;

    MicroStateQuery m_state;
    SolidsRevisionQuery m_revision;
    std::vector<std::unique_ptr<Av>> m_avs;
    int m_nextId = 1;
    bool m_realtime = false;
    std::vector<WaterSurfaceCell> m_surface;
    std::vector<glm::vec4> m_particleDraw;
};

} // namespace Water
} // namespace Core
} // namespace Phyxel
