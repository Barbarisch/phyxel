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
#include "core/water/ColumnWater.h"        // 25.18 H1: the column tier
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

struct AvRecord {
    int id = 0;
    glm::ivec3 minVoxel{0}, maxVoxel{0};   ///< world voxel box, inclusive
    float cellSize = 1.0f;
    std::string transport;                 ///< the solver's CURRENT transport (a FLIP volume reads "eulerian" after its rest conversion)
    std::string backend = "cpu";          ///< Phase C: "cpu" (the reference) or "gpu" (WaterCoreGpu, parity-gated)
    float rbgsResidual = 0.0f;             ///< GPU backend: max |A p - b| after the last substep's sweeps (0 on the CPU)
    int   gpuSweeps = 0;
    double gpuMs = 0.0;                    ///< wall time of the last GPU step call (all its ticks)
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

    /// Create a volume over an inclusive world voxel box. `cellSize` must be a power-of-three
    /// fraction of a voxel (1, 1/3, 1/9, 1/27, 1/81); `transport` is "eulerian" (Phase B).
    /// Refuses (returns 0, fills `err`) when the cell count would exceed `maxCells`.
    int create(const glm::ivec3& minVoxel, const glm::ivec3& maxVoxel, float cellSize,
               const std::string& transport, std::string* err, const std::string& backend = "auto" /* auto: gpu for fills when ready, else cpu */, int gpuSweeps = 0 /* <= 0: auto, 1.5 x the longest dimension */);
    /// Phase C: give the manager a device; GPU volumes are refused until this succeeds (loudly).
    bool initGpu(VkDevice device, VkPhysicalDevice physical, VkQueue queue, uint32_t queueFamily, const std::string& shaderDir, std::string* err);
    bool gpuReady() const { return m_gpu && m_gpu->ready(); }
    static constexpr int kGpuSweeps = 40;   ///< default red-black SOR sweeps per projection (docs/WaterCore.md 15.12: omega 1.85, measured)
    static constexpr int kGpuSweepsMin = 8, kGpuSweepsMax = 160;   ///< fewer never converges a 26 m basin; more is the whole dispatch budget (15.11)
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
    /// Realtime auto-sleep records since the last drain (the application logs them).
    std::vector<WriteBackRecord> drainAutoSleepRecords() { std::vector<WriteBackRecord> r; r.swap(m_autoSlept); return r; }
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
    const std::vector<WaterSurfaceField>& surfaceFields();
    // ---- 25.18 H1: column-water regions ----
    struct ColumnRegion { std::unique_ptr<ColumnWater> cw; uint64_t occRevision = ~0ull; WaterSurfaceField field;
                          std::unique_ptr<RippleLayer> ripple; uint32_t rippleFrame = 0;     // 25.26 RC: the region's ripple window
                          std::vector<float> load; };                                         // 25.30 SC: the bodies' load per column (m)
    /// 25.30 SC: the column regions' boxes (voxels, inclusive) - the debris integrator reports the bodies inside them as wet.
    std::vector<std::pair<glm::ivec3, glm::ivec3>> columnBoxes() const;
    /// 25.30 SC: a body leaving (or coming to rest - it stops displacing, M2's gate) releases its load over this time, so a
    /// stone settling on the bed does not throw the water it held back as a wave.
    static constexpr float kLoadReleaseSeconds = 0.3f;
    /// 25.30 SC cosmetic splash (owner, 0a: "a few splash particles for affect", not taken from the water): a body crossing a
    /// creek's surface downward faster than kSplashMinSpeed throws `creekSplash` x its displaced volume rate as droplets from
    /// its waterline (up at 0.8 of its entry speed, out at 0.4 - look parameters). Their landings ripple the creek; they carry
    /// no water. `water_droplets {creek_splash}` (0 = off).
    static constexpr float kSplashMinSpeed = 0.5f;   // m/s
    void setCreekSplash(float f) { m_creekSplash = f < 0.0f ? 0.0f : f; }
    float creekSplash() const { return m_creekSplash; }
    /// A region over voxels [minVoxel, maxVoxel]; beds and walls read from the world's solids. Returns its id (0 = refused, `err`).
    int createColumns(const glm::ivec3& minVoxel, const glm::ivec3& maxVoxel, float manningN, std::string* err);
    bool destroyColumns(int id);
    ColumnWater* columns(int id);
    const std::vector<ColumnRegion>& columnRegions() const { return m_columns; }
    void stepColumns(float dt);
    /// 25.22: the generator's continuous bed height and its DRAWN top for a voxel column (world x, z); false = unknown
    /// (no generator - an authored or flat world). With it, new regions default to the `generator` bed (option (a)).
    using GeneratorBedQuery = std::function<bool(int wx, int wz, float& bedHeight, float& drawnTop)>;
    void setGeneratorBedQuery(GeneratorBedQuery q) { m_genBed = std::move(q); }
    /// 25.26 RC: where the camera is - a column region's ripple window (kColumnRippleWindow lattice cells square) follows it.
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
        double cosmeticM3 = 0.0; long cosmeticBirths = 0, cosmeticLandings = 0;   // 25.30 SC: creek splash droplets (no water, no ledger)
    };
    void setDroplets(bool on) { m_dstats.on = on; }
    /// Droplet edge as a fraction of the volume's cell (21.11 default 1/9); clamped [1/27, 1] where it is used (DropletPool::spawn).
    void setDropletSize(float k) { m_dstats.size = k; }
    /// Test hook (D-T4): lower the pool cap; a refused birth goes straight back into the grid.
    void setDropletCap(size_t c) { m_pool.setCap(c); }
    const DropletStats& dropletStats() { m_dstats.alive = static_cast<int>(m_pool.droplets().size()); return m_dstats; }
    double dropletVolume() const { double v = 0.0; for (const Droplet& d : m_pool.droplets()) if (d.volumeId > 0) v += d.volume; return v; }   ///< water in flight (cosmetic creek droplets excluded)
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
                         double brokenM3 = 0.0; };                      ///< height the limit removed, summed over the run (m^3)
    /// Measurement only (22.11): pin every layer to a uniform tilt - slope (rise per run) toward azimuth (degrees,
    /// 0 = +x, 90 = +z) - instead of stepping it, so one frame shows exactly one facet tilt. slope < 0 = off.
    struct RipplePattern { float slope = -1.0f, azimuthDeg = 0.0f; };
    void setRipplePattern(const RipplePattern& p) { m_rpattern = p; }
    const RipplePattern& ripplePattern() const { return m_rpattern; }
    void setRipples(bool on) { m_rstats.on = on; }
    void setRippleSmooth(bool on) { m_rstats.smooth = on; }
    void setRippleBreakSlope(float s) { m_rstats.breakSlope = s; }
    /// 24.7 pond friction (1/s) for every volume, now and created later.
    void setPondFriction(float perSecond) { m_pondFriction = std::max(0.0f, perSecond); for (auto& av : m_avs) av->solver->setPondFriction(m_pondFriction); }
    float pondFriction() const { return m_pondFriction; }
    const RippleStats& rippleStats() const { return m_rstats; }
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
        std::unique_ptr<RippleLayer> ripple;  // 22: the sub-cell wave layer (null = none / refused)
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
    void stepRipples(float dt);
    void collectBirths(Av& av, float dt);          // after a step: births -> the pool
    void stepDroplets(float dt);                   // flight + landing for every volume
    void depositDroplet(Av& av, const glm::vec3& at, double volume);
    Av* volumeById(int id) { for (auto& a : m_avs) if (a->id == id) return a.get(); return nullptr; }
    ColumnRegion* regionById(int id) { for (auto& r : m_columns) if (r.cw->id() == id) return &r; return nullptr; }
    void stepColumnSplashes(float dt);   // 25.30 SC: the cosmetic entry crown
    float m_creekSplash = 0.05f;         // 25.30 SC
    bool isAsleep(const Av& av) const { return av.backend == "gpu" ? av.gpuLast.asleep : av.solver->asleep(); }
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
    void markWritten(Av& av);   // Phase E1: a CPU-side write to a volume's grid: upload it, wake it
    std::vector<std::unique_ptr<Av>> m_avs;
    // 25.18 H1: column-water regions (the T2 tier, step 1 - placed by hand over a creek)
    std::vector<ColumnRegion> m_columns;
    int m_nextColumnId = 1;
    GeneratorBedQuery m_genBed;   // 25.22
    glm::vec3 m_view{0.0f};       // 25.26 RC
    float m_flowForcing = 200.0f; // 25.26 RC: C_f - calibrated live 2026-10-12 (25.29): facet slope p95 0.1-0.2 on the 100 L/s bench creek
    void refreshColumnSolids(ColumnRegion& r, bool force = false);
    SpanWriter m_spanWriter; SpanReader m_spanReader; BakeBodyQuery m_bakeBodyAt;
    WaterBodyTable m_bodies;
    std::vector<WriteBackRecord> m_autoSlept;
    int m_nextId = 1;
    uint64_t m_avRevision = 0;
    std::vector<std::pair<glm::ivec3, glm::ivec3>> m_boxes;   // mirrors m_avs (16.4)
    void refreshBoxes();
    bool m_realtime = false;
    std::vector<WaterSurfaceCell> m_surface;
    std::vector<WaterSurfaceField> m_fields;   // Phase F, parallel to m_avs
    std::unordered_map<uint64_t, glm::vec3> m_solidHold;   // 20: body id -> the centre it is rasterized at
    bool m_solidWakeRule = true;
    MotionStats m_motionStats;
    std::vector<std::vector<float>> m_motionPrevTops;   // E2: per field, the held reference top per column (NaN = dry)
    std::vector<glm::vec4> m_particleDraw;
};

} // namespace Water
} // namespace Core
} // namespace Phyxel
