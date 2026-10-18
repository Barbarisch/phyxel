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
#include "core/water/WaterBodyTable.h"   // Phase D Tier A records
#include "core/water/WaterSurfaceMesh.h"   // Phase F surface field
#include "core/water/WaterDroplets.h"      // 21: the droplet crown
#include "core/water/RippleLayer.h"        // 22: ripples
#include "core/water/RippleGpu.h"          // 25.34: the ripple step on the GPU
#include "core/WaterManager.h"   // WaterSurfaceCell (the debug feed's struct)
#include <unordered_map>
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
/// 26.19: an inclusive box of WORLD voxels (cubes) whose solids may have changed.
struct SolidsChangeBox { glm::ivec3 lo, hi; };
/// 26.19: the boxes changed after revision `since` (appended to `out`), or false = "assume everything changed" (the occupancy
/// pool's VoxelLightOccupancyGpu::changesSince).
using SolidsChangesQuery = std::function<bool(uint64_t since, std::vector<SolidsChangeBox>& out)>;

struct AvRecord {
    int id = 0;
    glm::ivec3 minVoxel{0}, maxVoxel{0};   ///< world voxel box, inclusive
    float cellSize = 1.0f;
    std::string transport;                 ///< the solver's CURRENT transport (a FLIP volume reads "eulerian" after its rest conversion)
    std::string backend = "cpu";          ///< Phase C: "cpu" (the reference) or "gpu" (WaterCoreGpu, parity-gated)
    float rbgsResidual = 0.0f;             ///< GPU backend: max |A p - b| after the last substep's sweeps (0 on the CPU)
    int   gpuSweeps = 0;
    double gpuMs = 0.0;                    ///< wall time of the last GPU step call (all its ticks)
    double gpuDeviceMs = 0.0;              ///< 26.15: of it, the GPU's own time (timestamps round the main submission)
    bool partialWake = false;              ///< 26.31: quiet brick columns freeze (GPU, sparse)
    int awakeColumns = 0, frozenColumns = 0, movingColumns = 0;   ///< 26.31: the last call's (WaterCoreGpu GpuStepStats)
    double gpuHostMs = 0.0;                ///< 26.15: of it, the host's residency work before recording
    double gpuWaitMs = 0.0;                ///< 26.15: of it, submission + fence wait (device time + queue contention + overhead)
    int gpuTicks = 0;                      ///< 26.15: ticks the last call stepped
    bool gpuAsync = false;                 ///< 26.16: the call is collected next frame (realtime) - the gpu_* numbers are the previous call's
    int gpuBricks = 0;                     ///< 26.12: bricks the GPU volume steps (stored: water + its halo); 0 = not on the GPU
    int gpuBrickCapacity = 0;              ///< 26.12: bricks it could store (the whole box)
    int gpuBrickHeight = 0;                ///< 26.17: cells per brick along y (bricks are 8 x h x 8); 0 = not on the GPU
    int gpuGroundBricks = 0;               ///< 26.18: halo bricks read as ground (all Solid at the last re-read), not stepped
    double gpuUploadMs = 0.0;              ///< 26.18: the last re-read after a world change (download + occupancy + upload), ms
    int gpuUploads = 0;                    ///< 26.18: re-reads since the volume was made
    double occRefreshMs = 0.0;             ///< 26.18: the last re-sample of the box's occupancy from the world, ms
    long occFullRefreshes = 0;             ///< 26.19: whole-box re-reads since the volume was made (creation is one)
    long occLocalRefreshes = 0;            ///< 26.19: re-reads of only the changed cubes' cells
    long occCellsResampled = 0;            ///< 26.19: cells the last re-read sampled
    long occCellsChanged = 0;              ///< 26.19: of them, cells whose occupancy changed
    double gpuPatchMs = 0.0;               ///< 26.19: the last in-place GPU occupancy patch, ms (0 = none needed)
    int gpuPatches = 0;                    ///< 26.19: in-place patches since the volume was made
    double surfaceAgeMs = -1.0;            ///< Phase F: age of the surface field the renderer last read (ms since the step that produced it; -1 = never)
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
    double sourceIn = 0.0;                 ///< 26.13: m^3 the pumps put in (placed)
    double outfallOut = 0.0;               ///< 26.13: m^3 the outfalls took (positive)
    int    outfalls = 0;                   ///< 26.13
    float  manningN = 0.0f;                ///< 26.13: the bed roughness (0 = pond friction)
    bool   autoSleep = true;               ///< Phase D: in realtime the volume writes back and frees itself when it sleeps
    double seededMass = 0.0;               ///< Phase D: m^3 the volume was seeded with from spans (0 = placed by hand)
};

/// Phase D (docs/WaterCore.md 16.2): the record of a write-back.
struct WriteBackRecord {
    bool ok = false; std::string error;
    int  id = 0;
    long columns = 0, runs = 0, heldColumns = 0, unwrittenColumns = 0, chunksTouched = 0;
    double massWritten = 0.0, massSeeded = 0.0, heldMass = 0.0;
    double thinDropped = 0.0;   ///< m^3 of sub-millimetre-only columns not stored (residue, counted)
    double massStored = 0.0;   ///< sum of the written runs' depths as the chunks STORE them (float32 tops) - the body record's unit, so A.mass = sum of B exactly; differs from massWritten by float rounding (~2e-6 per column at Y 17)
    float surfaceVsMassMm = 0.0f, spreadMm = 0.0f;
    bool forced = false;
    int bodyId = 0;
};

struct ProbeResult {
    bool inVolume = false;
    int avId = -1;
    float fill = 0.0f;
    glm::vec3 velocity{0.0f};
    float surfaceY = 0.0f;                 ///< NaN when the column is dry
    double pressure = 0.0;
    int occupancy = 0;                     ///< Occ of the cell (0 air, 1 solid, 2 unknown)
    float solidFraction = 0.0f;            ///< 20: the moving-solid fraction s of the cell (0 = no body)
};

struct ColumnSample {
    int x = 0, z = 0;
    bool inVolume = false;
    float surfaceY = 0.0f;                 ///< NaN when dry
    double mass = 0.0;                     ///< m^3 in this 1x1 world column
};

/// Phase D: the application's chunk I/O. The writer receives world-column runs (held columns
/// carry `held = true` and must be skipped), replaces the spans intersecting [yLo, yHi) in
/// RESIDENT chunks, marks them dirty, and reports how many non-held columns it could not write
/// (a non-resident chunk) and how many chunks it touched. The reader returns the stored runs of
/// the box's columns clipped to [yLo, yHi) from resident chunks.
using SpanWriter = std::function<void(const std::vector<ColumnRuns>& runs, float yLo, float yHi, long* unwritten, long* chunksTouched)>;
using SpanReader = std::function<void(const glm::ivec3& minVoxel, const glm::ivec3& maxVoxel, std::vector<ColumnRuns>& out)>;
using BakeBodyQuery = std::function<int(int x, int z)>;   ///< bake body id of a world column, -1 = none

class WaterCoreManager {
public:
    explicit WaterCoreManager(MicroStateQuery state, SolidsRevisionQuery revision = nullptr);
    /// 26.19: where solids changed between revisions. Without it (or when it answers false) every change re-reads the whole box.
    void setSolidsChangesQuery(SolidsChangesQuery q) { m_changes = std::move(q); }
    /// 26.19 TEST ONLY: ignore the change list (every change re-reads the whole box), so the two can be compared.
    static inline bool s_forceFullRefreshForTest = false;

    /// Create a volume over an inclusive world voxel box. `cellSize` must be a power-of-three
    /// fraction of a voxel (1, 1/3, 1/9, 1/27, 1/81); `transport` is "eulerian" (Phase B).
    /// Refuses (returns 0, fills `err`) when the cell count would exceed `maxCells`.
    int create(const glm::ivec3& minVoxel, const glm::ivec3& maxVoxel, float cellSize,
               const std::string& transport, std::string* err, const std::string& backend = "auto" /* auto: gpu for fills when ready, else cpu */, int gpuSweeps = 0 /* <= 0: auto, 1.5 x the longest dimension */,
               int brickHeight = 0 /* 26.17: GPU brick height 2 / 4 / 8; 0 = WaterCoreGpu::s_brickHeight */,
               int groundBricks = -1 /* 26.18: 1 / 0; -1 = WaterCoreGpu::s_groundBricks */);
    /// Phase C: give the manager a device; GPU volumes are refused until this succeeds (loudly).
    bool initGpu(VkDevice device, VkPhysicalDevice physical, VkQueue queue, uint32_t queueFamily, const std::string& shaderDir, std::string* err);
    bool gpuReady() const { return m_gpu && m_gpu->ready(); }
    static constexpr int kGpuSweeps = 40;   ///< default red-black SOR sweeps per projection (docs/WaterCore.md 15.12: omega 1.85, measured)
    static constexpr int kGpuSweepsMin = 8, kGpuSweepsMax = 160;   ///< fewer never converges a 26 m basin; more is the whole dispatch budget (15.11)
    /// 26.23: the AUTOMATIC count's ceiling (an explicit `sweeps` may still ask for up to kGpuSweepsMax). Measured one-step velocity
    /// error vs a 640-sweep solve: free-surface water is within 0.4 % at 40 whatever the box's length (12-540 cells) - the surface
    /// pins the pressure along the top - and water pressed under a solid lid does not converge at 160 either (14-80 % off).
    /// 1.5 x longest up to 160 cost the dam & pond bench 3.17 ms where 40 gave the same answer at 1.73 ms.
    static constexpr int kGpuSweepsAutoMax = 40;
    bool destroy(int id);
    // ── Phase D (16.2): rest, write-back, wake ────────────────────────────────────────────
    void setSpanIo(SpanWriter writer, SpanReader reader) { m_spanWriter = std::move(writer); m_spanReader = std::move(reader); }
    void setBakeBodyQuery(BakeBodyQuery q) { m_bakeBodyAt = std::move(q); }
    /// G3: the look of the body owning a world column (generation body, else av pond; unset = derived).
    WaterLook lookAt(int x, int z, float topY = std::numeric_limits<float>::quiet_NaN()) const { return m_bodies.lookAt(x, z, m_bakeBodyAt, topY); }
    int bodyAt(int x, int z, float topY = std::numeric_limits<float>::quiet_NaN()) const { return m_bodies.bodyAt(x, z, m_bakeBodyAt, topY); }
    WaterBodyTable& bodies() { return m_bodies; }
    const WaterBodyTable& bodies() const { return m_bodies; }
    /// Phase D2: rule 3 - a solid displaced `m3` of span water in column (x, z); debit its body.
    void displaceAt(int x, int z, double m3) { m_bodies.displace(x, z, m3, m_bakeBodyAt); }
    /// Phase D2: does a live volume own column (x, z) at height y (then IT applies the edit)?
    bool volumeOwns(int x, int y, int z) const { for (const auto& b : m_boxes) if (x >= b.first.x && x <= b.second.x && y >= b.first.y && y <= b.second.y && z >= b.first.z && z <= b.second.z) return true; return false; }
    /// Write the volume back to spans + body records and destroy it. Refuses an awake volume
    /// unless `force` (then the record says so), and refuses - keeping the volume - when any
    /// column could not be written (Unknown occupancy or a non-resident chunk) unless `force`.
    WriteBackRecord sleep(int id, bool force);
    /// Fill the volume from the chunk spans of its box (replace); returns m^3 seeded, < 0 without a reader.
    double seedFromSpans(int id);
    bool setAutoSleep(int id, bool on);
    /// 26.31 the partial wake for one volume (GPU, sparse; false = no such volume or not one) and the default for new volumes.
    bool setPartialWake(int id, bool on);
    static inline bool s_partialWakeDefault = false;
    /// 26.31 measurement: the GPU volume's last unit map (WaterCoreGpu::s_unitCapture), per 8 x 2 x 8 cells: x = KE sum, y = max
    /// |df| (last tick), z = mass (cells). dims = units (x, y, z); cellOffset = unit (0,0,0)'s first cell in grid cells. false =
    /// no GPU volume, not bricked, or nothing captured yet.
    bool unitMap(int id, std::vector<glm::vec4>& units, glm::ivec3& dims, glm::ivec3& cellOffset, float& h) const;
    static void setUnitCapture(bool on);
    /// 26.31 measurement: the GPU's per-stage timestamps (WaterCoreGpu::s_kernelTiming; single-tick, single-substep calls) -
    /// {label, ms summed} and the calls summed; reset clears them.
    static void setKernelTiming(bool on, bool reset);
    static std::vector<std::pair<std::string, double>> kernelTiming(long& calls);
    static bool unitCapture();
    /// Realtime auto-sleep records since the last drain (the application logs them).
    std::vector<WriteBackRecord> drainAutoSleepRecords() { std::vector<WriteBackRecord> r; r.swap(m_autoSlept); return r; }
    std::vector<AvRecord> list() const;
    /// TEST ONLY (26.19): a volume's grid as the solver reads it (its occupancy is the refreshed cache), or null.
    const WaterGrid* gridForTest(int id) const { for (const auto& av : m_avs) if (av->id == id) return av->grid.get(); return nullptr; }
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
    bool addSource(int id, const glm::vec3& world, float rate, std::string* err, bool outfall = false);
    bool setBedManning(int id, float n);   ///< 26.13: Manning n at the bed (0 = pond friction); CPU and GPU   ///< 26.13: outfall = take what arrives (rate unused)
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
    /// Phase E1: a blast's radial kick into every volume its reach touches (both backends).
    struct KickReport { int volumes = 0; long faces = 0; long clamped = 0; };
    KickReport addRadialImpulse(const glm::vec3& centre, float reach, float speedAtCentre, float upBias);
    /// Phase E2: a frame's momentum exchange from wet debris. Each record gives the water `momentum`
    /// (m^3 * m/s) near `pos`; records outside every volume are counted, not applied. One GPU sync and
    /// one upload per touched volume.
    struct MomentumRecord { glm::vec3 pos{0.0f}; float radius = 0.0f; glm::vec3 momentum{0.0f}; };
    struct MomentumReport { long records = 0, applied = 0, outside = 0, dry = 0, clamped = 0; int volumes = 0; glm::vec3 total{0.0f}; };
    MomentumReport applyMomentum(const std::vector<MomentumRecord>& records);
    /// docs/WaterCore.md 20 (M2): the bodies taking up room this frame (world space; T = the frame). Each volume
    /// gets the bodies inside its box; a CPU volume runs WaterSolver::setMovingSolids, a GPU volume the host half
    /// (updateSolidFields) + WaterCoreGpu::setSolids (the device computes the rates from its own fill). A volume
    /// keeps being fed while its fields or wake are non-zero, so a body leaving clears them. FLIP volumes refuse
    /// (their particles need their own displacement rule - counted, 20.5).
    struct SolidsFeed { int volumes = 0; long bodies = 0, rasterCells = 0, freshCells = 0, rateCells = 0, clamped = 0, wakeCells = 0, flipRefused = 0; double bodyVolume = 0.0, rate = 0.0; };
    SolidsFeed setMovingSolids(const std::vector<MovingSolid>& bodies, float frameSeconds);
    /// A body (with an id) is rasterized at a HELD centre that moves only when the body has moved more than
    /// this from it. Measured 2026-10-10 (M2): with every pose rasterized, a single floating wood piece never
    /// rested - it bobbed +-2.5 cm at up to 0.2 m/s forever (without solids it sleeps): its displacement moved
    /// the surface it reads two frames later (a delayed one-way loop is an oscillator). A tenth of a 1/3 m
    /// cell is below what the grid resolves; entries and real motion still displace in full. The cure for the
    /// loop itself is the two-way pressure force (20.6 v2, M4).
    static constexpr float kSolidHoldDistance = 0.03f;   // m
    /// The wake rule (air a body drags rises back out, 20.5) on GPU volumes: off uploads a zero wake (A/B).
    void setSolidWakeRule(bool on) { m_solidWakeRule = on; }
    bool solidWakeRule() const { return m_solidWakeRule; }

    ProbeResult probe(const glm::vec3& world);   // refreshes the solids cache first
    /// Per world column (x, z): the highest surface and the mass over cells whose world y lies in
    /// [yMin, yMax] (voxel units; the default spans everything). The y range lets a probe tell a
    /// cavity under a floor from the pool above it (S5).
    std::vector<ColumnSample> probeColumns(int x1, int z1, int x2, int z2, int yMin = INT_MIN, int yMax = INT_MAX) const;
    double totalMass() const;
    size_t totalCells() const;

    /// Debug feed: one WaterSurfaceCell per wet world column (1 x 1), rebuilt on demand.
    const std::vector<WaterSurfaceCell>& surfaceCells();
    /// Phase F (17.1): one surface field per volume, from the last step (GPU: the field the step
    /// wrote into its staging ring, a memcpy; CPU: extracted from the grid). Rebuilt on every call
    /// that follows a step; a volume that did not step since the last call keeps its field.
    /// 26.30 step 3: the volumes' own fields (pointers, parallel to the volumes; valid until a volume is destroyed - destroy()
    /// drops its pointer at once). Was a copy of every field, made on every call (4.6 MB a 32 x 28 m pond, ~1 ms a frame awake).
    const std::vector<const WaterSurfaceField*>& surfaceFields();
    /// 25.26 RC: where the camera is - a volume's ripple window (kColumnRippleWindow lattice cells square) follows it.
    void setViewPoint(const glm::vec3& p) { m_view = p; }
    static constexpr int kColumnRippleWindow = 384;   ///< 42.7 m at 1/9 m: the renderer's atlas holds <= 512 per axis; 1/9 m facets
                                                      ///< are sub-pixel well before 40 m, so a window - not the whole creek - is drawn
    /// 25.26 RC flow forcing: the turbulence moving water feeds into its own ripple layer - an impulse per unit area per step
    /// C_f rho u*^2 dt xi (xi a deterministic hash in [-1, 1] of the world lattice cell and the step; u* the bed shear velocity
    /// u n sqrt(g) / d^(1/6), Manning's friction the column solver applies). Still water: u* = 0, nothing. `water_ripples
    /// {flow_forcing}` tunes C_f live (0 = off, the A/B control).
    void setRippleFlowForcing(float c) { m_flowForcing = c < 0.0f ? 0.0f : c; }
    float rippleFlowForcing() const { return m_flowForcing; }
    /// E2: one world voxel column's water from the last surface fields - the highest top over the
    /// column's sub-columns and the mean surface velocity of the wet ones (what GPU debris reads).
    bool columnWater(int wx, int wz, float& surfaceY, glm::vec2& flow) const;
    /// 26.20 measurement: the same column's top run over its WET sub-columns - the mean, the lowest and the highest top, and how
    /// many sub-columns are wet. A depth gauge reads the mean; the highest (columnWater's) reads a sloped or rippled surface high.
    struct ColumnSurfaceStats { float mean = 0.0f, lowest = 0.0f, highest = 0.0f; int wet = 0, sub = 0; };
    bool columnSurfaceStats(int wx, int wz, ColumnSurfaceStats& out) const;
    /// 26.20 measurement: the water IN world voxel column (wx, wz) as a depth (m^3 over its 1 m^2) - the quantity a mass
    /// gauge reads, not a surface. Reads a GPU volume back (rate-limited to once a second in realtime): a debug cost.
    bool columnMassDepth(int wx, int wz, double& depthM);
    /// Measurement (20, M2): the drawn surface of the volume sub-column under world (x, z) from the last
    /// surfaceFields(): `level` = the top of the run that starts lowest (the pond itself), `top` = the highest
    /// run's top (a splash thrown above it). False when no volume owns the column or it is dry.
    bool surfaceAtWorld(float x, float z, float& level, float& top) const;
    /// Measurement (21.7 S1): every DETACHED run of water in every fill volume now (WaterDroplets.h) - world
    /// position of its bottom cell's centre, its water (m^3), length and whether it is isolated. A GPU volume
    /// is read back (two submit-and-waits): a debug cost, called only while `water_jet_scan` records.
    struct JetRun { glm::vec3 bottom{0.0f}; float volume = 0.0f, sumF = 0.0f; int cells = 0; bool isolated = false; };
    std::vector<JetRun> scanDetachedRuns();

    // ── 21: the droplet crown (WaterDroplets.h) ─────────────────────────────────────────────────
    /// Water the grid cannot hold (scraps, spray off a fast-rising surface) leaves as droplets every tick, flies
    /// (gravity, air drag) and lands back - into the water or onto the ground of its own volume. Grid + pool is
    /// conserved: a refused birth (pool full) or a deposit that does not fit goes straight back. A volume with
    /// droplets in flight does not auto-sleep; sleep() lands them first; destroy() discards them with its water.
    struct DropletStats {
        bool on = true; float size = 1.0f / 9.0f; int alive = 0;
        double bornM3 = 0.0, landedM3 = 0.0, refusedM3 = 0.0, relocatedM3 = 0.0, flushedM3 = 0.0, destroyedM3 = 0.0;
        long births = 0, landings = 0; int scraps = 0, sprays = 0;   // scraps / sprays: CPU volumes (the GPU counts per column record)
    };
    void setDroplets(bool on) { m_dstats.on = on; }
    /// Droplet edge as a fraction of the volume's cell (21.11 default 1/9); clamped [1/27, 1] where it is used (DropletPool::spawn).
    void setDropletSize(float k) { m_dstats.size = k; }
    /// Test hook (D-T4): lower the pool cap; a refused birth goes straight back into the grid.
    void setDropletCap(size_t c) { m_pool.setCap(c); }
    const DropletStats& dropletStats() { m_dstats.alive = static_cast<int>(m_pool.droplets().size()); return m_dstats; }
    double dropletVolume() const { double v = 0.0; for (const Droplet& d : m_pool.droplets()) v += d.volume; return v; }   ///< water in flight
    /// Droplet cubes for the renderer (xyz centre, w edge), rebuilt every update.
    const std::vector<glm::vec4>& dropletDrawList() const { return m_dropletDraw; }

    // ── 22: ripples (RippleLayer) ─────────────────────────────────────────────────────────────────
    /// Every wet body this frame (moving or resting) - the bodies crossing the surface drive the ripple layer with
    /// the sharp-edge remainder of their footprint (22.3.2), floaters bobbing included (they do not displace: M2's gate).
    void setRippleBodies(std::vector<MovingSolid> bodies) { m_rippleBodies = std::move(bodies); }
    /// slopeHist: water cells by facet slope |grad r| (the shader's central difference, 22.4), bin edges kSlopeEdges -
    /// bin i counts slopes in [edge[i-1], edge[i]), the last bin everything from the top edge up (22.11).
    static constexpr int kSlopeBins = 12;
    static constexpr float kSlopeEdges[kSlopeBins - 1] = {0.05f, 0.1f, 0.2f, 0.3f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 5.0f};
    struct RippleStats { bool on = true, smooth = false; int layers = 0, awake = 0, refused = 0; long cells = 0, kinematicCells = 0, impulses = 0; double ms = 0.0; float maxAbs = 0.0f;
                         float maxSlope = 0.0f; long slopeHist[kSlopeBins] = {};
                         float breakSlope = RippleLayer::kBreakSlope;   ///< 22.12 B1 (0 = off, the A/B control)
                         double brokenM3 = 0.0;                         ///< height the limit removed, summed over the run (m^3)
                         bool gpu = false;                              ///< 25.34: the layers stepped on the GPU this frame
                         double gpuMs = 0.0, gpuWaitMs = 0.0, gpuRecordMs = 0.0, gpuCollectMs = 0.0; int gpuDispatches = 0, gpuLayers = 0; };
    /// Measurement only (22.11): pin every layer to a uniform tilt - slope (rise per run) toward azimuth (degrees,
    /// 0 = +x, 90 = +z) - instead of stepping it, so one frame shows exactly one facet tilt. slope < 0 = off.
    struct RipplePattern { float slope = -1.0f, azimuthDeg = 0.0f; };
    void setRipplePattern(const RipplePattern& p) { m_rpattern = p; }
    const RipplePattern& ripplePattern() const { return m_rpattern; }
    void setRipples(bool on) { m_rstats.on = on; }
    void setRippleSmooth(bool on) { m_rstats.smooth = on; }
    void setRippleBreakSlope(float s) { m_rstats.breakSlope = s; }
    /// 25.34: step the ripple layers on the GPU (asynchronous, one frame late) or the CPU. On by default when initGpu made
    /// the device; turning it off pulls every layer's levels back first, so the CPU continues exactly where the GPU was.
    void setRippleGpu(bool on);
    bool rippleGpuOn() const { return m_rgpuOn && m_rgpu && m_rgpu->ready(); }
    /// 25.34: a layer steps on the GPU only from this many lattice cells - below it the GPU's fixed cost per frame (record,
    /// uploads, readback) outweighs the arithmetic. Measured 2026-10-12: the pond bench's layer 0.137 ms CPU vs 0.238 ms
    /// GPU; the creek's 384 x 54 window (20,736 cells) 0.57 ms CPU vs 0.35 ms GPU.
    static constexpr int kRippleGpuMinCells = 8192;
    bool rippleGpuAvailable() const { return m_rgpu && m_rgpu->ready(); }
    const std::string& rippleGpuError() const { return m_rgpuErr; }
    /// 24.7 pond friction (1/s) for every volume, now and created later.
    void setPondFriction(float perSecond) { m_pondFriction = std::max(0.0f, perSecond); for (auto& av : m_avs) av->solver->setPondFriction(m_pondFriction); }
    float pondFriction() const { return m_pondFriction; }
    /// 26.21: the session's volume correction (applied to every volume now and to new ones)
    void setVolumeCorrection(float seconds, float band) { m_vcTime = std::max(0.0f, seconds); m_vcBand = std::clamp(band, 0.0f, 1.0f); for (auto& av : m_avs) av->solver->setVolumeCorrection(m_vcTime, m_vcBand); }
    float volumeCorrectionTime() const { return m_vcTime; }
    float volumeCorrectionBand() const { return m_vcBand; }
    const RippleStats& rippleStats() const { return m_rstats; }
    /// 25.34: the SHAPE stats (max |r|, max slope, the slope histogram) are measurement only - computed when asked (the
    /// water_ripples route), not every frame (0.15 ms per frame on the creek window, measured).
    void refreshRippleShapeStats();
    static constexpr int kRippleMaxCells = 512;   ///< per axis (a 57 m pond); a larger volume gets no layer - refused, counted (22.5)
    static constexpr int kRippleMaxLayers = 4;    ///< the renderer's atlas holds four 512 x 512 layers
    /// The layers, parallel to the fields surfaceFields() returns (null = none).
    std::vector<const RippleLayer*> rippleLayers() const;
    /// E2 (docs/WaterCore.md 19.6): where the water is still MOVING since the last call - per volume, a
    /// sphere (world centre, radius) around the columns whose surface has moved more than `moveM` from
    /// where it last counted as moved (a held reference: sub-mm jitter never adds up, a real slosh does,
    /// and calm water stops reporting). Sleeping debris inside it must wake (a floater frozen while its
    /// water moves hangs in air or sinks into the water). Reads the last surfaceFields(). Surface FLOW is
    /// reported in the stats but does not count: the top-cell velocity of a still pond reads 0.08-0.10
    /// m/s on GPU (19.6, logged) while its surface moves < 1 mm.
    /// moveM 1.5 cm: a piece frozen while its water moves less than that is off by ~5 % of a subcube - not
    /// visible; 5 mm tripped forever on the floaters' own stirring (live: 0-2 columns per sample, 20 s).
    std::vector<std::pair<glm::vec3, float>> takeMotion(float moveM = 0.015f);
    /// The last takeMotion(): largest per-column rise/fall (m) and surface flow (m/s), columns over each bar.
    struct MotionStats { float maxRise = 0.0f, maxFlow = 0.0f; int riseCols = 0, flowCols = 0, spheres = 0; float lastRadius = 0.0f; };
    const MotionStats& motionStats() const { return m_motionStats; }
    /// 26.30 step 3: the last update()'s CPU by part (ms): occupancy refresh, the volumes' steps, droplets, ripples
    struct UpdateCpu { double occupancy = 0, step = 0, droplets = 0, ripples = 0, sleep = 0;
                       double stepCall = 0, births = 0;                                  // of step: the GPU call, the births read
                       double rCollect = 0, rMask = 0, rLayer = 0, rSubmit = 0; };       // of ripples: collect, the mask, the layer's step, submit
    const UpdateCpu& updateCpu() const { return m_updateCpu; }

    static constexpr size_t kMaxCellsPerVolume = 2'000'000;   ///< CPU reference ceiling (§15.4)
    static bool snapCellSize(float requested, float* snapped);  ///< power-of-three fractions only
    /// Phase D (16.4): the world voxel boxes of every live volume (awake or not - a volume draws its
    /// own surface until it is destroyed), and a revision that changes on create/destroy, so the
    /// span render grid can leave those columns to the volume and rebuild when the set changes.
    const std::vector<std::pair<glm::ivec3, glm::ivec3>>& volumeBoxes() const { return m_boxes; }
    uint64_t avRevision() const { return m_avRevision; }

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
        double gpuUploadMs = 0.0; int gpuUploads = 0;   ///< 26.18: the last re-read's time, and how many
        double occRefreshMs = 0.0;                      ///< 26.18: the last occupancy re-sample of the box, ms
        long occFullRefreshes = 0, occLocalRefreshes = 0, occCellsResampled = 0, occCellsChanged = 0;   ///< 26.19
        double gpuPatchMs = 0.0; int gpuPatches = 0;    ///< 26.19
        int gpuSweeps = kGpuSweeps;
        bool gpuStale = false;         // the GPU copy is newer than the grid (a step ran, no download yet)
        double gpuLastDownloadSec = -1.0;
        bool autoSleep = true;         // Phase D
        double seededMass = 0.0;
        std::vector<WaterBodyTable::ColumnMass> seededColumns;   // per-column seed for the body credit
        WaterSurfaceField field;        // Phase F: the last surface field
        uint64_t fieldStep = ~0ull;     // the step counter the field was built from
        uint64_t stepCount = 0;         // steps taken (CPU ticks or GPU calls)
        double fieldStepSec = -1.0;     // wall time of that step
        bool hadSolids = false;         // 20: fed bodies (or a wake) last frame - keep feeding until it clears
        std::vector<float> pendingDeposit;   // 21: GPU volume - landed droplets (cell fractions) for the next step
        std::unique_ptr<RippleLayer> ripple;  // 22: the sub-cell wave layer (null = none / refused); 26.13: a window following the camera
        uint64_t rippleMaskVersion = 0;       // 26.30 step 3: the field version the ripple mask was built from
        glm::ivec4 rippleMaskKey{0};          //   and the window (origin x/z, nx, nz)
        std::vector<uint8_t> rippleWet;       //   and which of the field's columns held water
        uint32_t rippleFrame = 0;             // 26.13: the forcing's hash frame (as a region's)
        bool rippleRefused = false;
        bool hasPendingDeposit = false;
    };
    // 21: droplets
    DropletPool m_pool;
    DropletStats m_dstats;
    uint32_t m_dropletTick = 0;
    std::vector<glm::vec4> m_dropletDraw;
    // 22: ripples
    std::vector<MovingSolid> m_rippleBodies;
    RippleStats m_rstats;
    RipplePattern m_rpattern;
    float m_pondFriction = SolverParams{}.pondFriction;   // 24.7
    float m_vcTime = SolverParams{}.volumeCorrectionTime, m_vcBand = SolverParams{}.volumeCorrectionBand;   // 26.21
    void stepRipples(float dt);
    void collectBirths(Av& av, float dt);          // after a step: births -> the pool
    void stepDroplets(float dt);                   // flight + landing for every volume
    void depositDroplet(Av& av, const glm::vec3& at, double volume);
    Av* volumeById(int id) { for (auto& a : m_avs) if (a->id == id) return a.get(); return nullptr; }
    bool isAsleep(const Av& av) const { return av.backend == "gpu" ? av.gpuLast.asleep : av.solver->asleep(); }
    void refreshOccupancy(Av& av);
    Occ sampleOccupancy(const Av& av, const glm::ivec3& cellLocal) const;
    AvRecord record(const Av& av) const;
    void pullSourceTotals(Av& av);   ///< 26.16: the GPU's collected source totals into the solver's list
    const Av* volumeAtVoxel(int x, int y, int z) const;

    MicroStateQuery m_state;
    SolidsRevisionQuery m_revision;
    SolidsChangesQuery m_changes;   // 26.19
    /// 26.19: re-sample only the cells of the changed boxes; patch what differs (grid, cache, GPU in place)
    void refreshOccupancyLocal(Av& av, const std::vector<SolidsChangeBox>& boxes);
    std::unique_ptr<WaterCoreGpu> m_gpu;
    std::unique_ptr<RippleGpu> m_rgpu;   // 25.34 (declared after m_gpu: torn down first)
    bool m_rgpuOn = true;
    std::string m_rgpuErr;
    std::vector<RippleLayer*> liveRippleLayers();
    void stepLayer(RippleLayer& L, float dt, const std::vector<glm::vec2>* vel);
    /// 26.13: THE ripple window of every volume (25.26 RC): min(kColumnRippleWindow, the box) lattice
    /// cells per axis, centred on the camera and clamped to the box, moved in 4 m steps; created on first use; its border absorbs
    /// (RippleLayer's default, as every layer always had).
    RippleLayer& rippleWindow(std::unique_ptr<RippleLayer>& layer, const glm::ivec3& minVoxel, const glm::ivec3& maxVoxel, bool gpu);
    void stepGpu(Av& av, int ticks, float dt);
    bool pushSourcesToGpu(Av& av, std::string* err);
    void syncFromGpu(Av& av, bool rateLimited = false);   // download when stale (realtime: at most once a second unless forced)
    void markWritten(Av& av, const glm::vec3* wakeCentre = nullptr, float wakeRadius = 0.0f);   // Phase E1: a CPU-side write to a volume's grid: upload it, wake it (26.31: only the columns round wakeCentre when given - the partial wake)
    std::vector<std::unique_ptr<Av>> m_avs;
    glm::vec3 m_view{0.0f};       // 25.26 RC
    float m_flowForcing = 200.0f; // 25.26 RC: C_f - calibrated live 2026-10-12 (25.29): facet slope p95 0.1-0.2 on the 100 L/s bench creek
    SpanWriter m_spanWriter; SpanReader m_spanReader; BakeBodyQuery m_bakeBodyAt;
    WaterBodyTable m_bodies;
    std::vector<WriteBackRecord> m_autoSlept;
    int m_nextId = 1;
    uint64_t m_avRevision = 0;
    std::vector<std::pair<glm::ivec3, glm::ivec3>> m_boxes;   // mirrors m_avs (16.4)
    void refreshBoxes();
    bool m_realtime = false;
    std::vector<WaterSurfaceCell> m_surface;
    std::vector<const WaterSurfaceField*> m_fields;   // Phase F, parallel to m_avs (26.30 step 3: each volume's own field)
    std::unordered_map<uint64_t, glm::vec3> m_solidHold;   // 20: body id -> the centre it is rasterized at
    bool m_solidWakeRule = true;
    MotionStats m_motionStats;
    struct MotionSeen { int source = 0; uint64_t version = 0; float maxFlow = 0.0f; int flowCols = 0; };
    std::vector<MotionSeen> m_motionSeen;   // 26.30 step 3: the field each takeMotion() slot last scanned
    UpdateCpu m_updateCpu;
    std::vector<std::vector<float>> m_motionPrevTops;   // E2: per field, the held reference top per column (NaN = dry)
    std::vector<glm::vec4> m_particleDraw;
};

} // namespace Water
} // namespace Core
} // namespace Phyxel
