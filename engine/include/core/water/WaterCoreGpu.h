// WaterCoreGpu — the WaterCore fill solver as Vulkan compute (docs/WaterCore.md §15.11, Phase C).
//
// The CPU reference (WaterSolver, WaterCore.h) stays the oracle; this class runs the SAME rules as
// kernels (shaders/wc_*.comp) on a grid uploaded from a WaterGrid and is accepted on parity rows,
// never on looks. Slice 3: device-local buffers behind one host-visible staging buffer, ONE fenced
// submission per step() call (all ticks, all substeps), reductions (rest, residual, residue) folded
// on the device, and the grid downloaded only when the caller asks (download()). Raw Vulkan handles
// so the same class runs under the integration test fixture and under the engine's VulkanDevice.
#pragma once

#include "core/water/WaterCore.h"
#include "core/water/WaterSurfaceMesh.h"   // 29 S1: SurfaceColumn / WaterSurfaceField (the host writes into the device field)
#include "vulkan/ComputePipeline.h"
#include <vulkan/vulkan.h>
#include <chrono>
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Phyxel { namespace Core { namespace Water {

/// Push block shared by every kernel (shaders/water_core.glsl WcPush).
struct WcPush {
    int32_t nx = 0, ny = 0, nz = 0, mode = 0;
    float dt = 0.0f, h = 1.0f, thr = 0.5f, gravity = 9.81f;
    float filmHold = 0.01f, param0 = 0.0f, param1 = 0.0f;
    int32_t iparam = 0;
    float thetaMin = 0.01f;   ///< SolverParams::thetaMin (WaterCore.md 24.1)
    // 26.6 P2a: brick storage (shaders/water_core.glsl)
    int32_t bricked = 0, slotsUsed = 0, ox = 0, oy = 0, oz = 0, bnx = 0, bny = 0, bnz = 0;
    int32_t slotTableOff = 0, colTableOff = 0, counterOff = 0, colCount = 0;
    /// 27.22: three small switches packed into one int so the block stays within the guaranteed 128 bytes - bit 0 the hydrostatic
    /// ghost at a clamped surface (26.9, WaterSolver::surfaceGhost); bits 1-2 the partial wake's frozen columns (26.31: 0 off, 1 a
    /// wall, 2 the test's open interface); bit 3 count the over-full carry (26.32, WaterCoreGpu::s_carryMeasure)
    int32_t flags = 1;
    float quietRise = 0.0f;     ///< 26.9: m/s under which a partial cell's surface rises by filling (EulerianTransport::s_quietRiseFraction)
    int32_t bly = 3;            ///< 26.17: log2 of the brick height (bricks are 8 x (1 << bly) x 8)
    float vcBand = 0.02f;       ///< 26.21: SolverParams::volumeCorrectionBand (wc_classify mode 0)
    int32_t tallY = -1;         ///< 27.22: the tall layer's grid y (the cell under the band's floor); -1 = no tall cells
    int32_t tallOff = 0;        ///< 27.22: Bricks[] offset of the per-column tall heights (dense column x + nx z)
    void setSurfaceGhost(bool on) { flags = (flags & ~1) | (on ? 1 : 0); }
    void setFrozenOn(int v) { flags = (flags & ~6) | ((v & 3) << 1); }
    void setCarryMeasure(bool on) { flags = (flags & ~8) | (on ? 8 : 0); }
    int frozenOn() const { return (flags >> 1) & 3; }
};
static_assert(sizeof(WcPush) == 124, "WcPush must match the GLSL push block");

/// A pump or sink on the device (shaders/wc_sources.comp GpuSource, 32 bytes).
struct GpuSource {
    int32_t cell = 0;          ///< grid-local cell index
    float rate = 0.0f;         ///< m^3/s, negative = sink
    float pending = 0.0f;      ///< m^3 owed
    float placedTotal = 0.0f;  ///< m^3 placed (negative = removed)
    float unplaced = 0.0f;
    float kind = 0.0f;         ///< 26.13: 0 = pump / sink, 1 = outfall (takes what arrives: SourceSpec::outfall)
    float placedLo = 0.0f;     ///< 26.15: placedTotal's rounding remainder (two-sum) - the books stay exact past 100 m^3 through
    float pad2 = 0.0f;
    double total() const { return static_cast<double>(placedTotal) + static_cast<double>(placedLo); }
    void setTotal(double t) { placedTotal = static_cast<float>(t); placedLo = static_cast<float>(t - static_cast<double>(placedTotal)); }
};
static_assert(sizeof(GpuSource) == 32, "GpuSource must match the GLSL struct");

struct GpuStepStats {
    int    ticks = 0;
    int    substepsLast = 0;         ///< the substep count used for every tick of the call (CFL from the last known max speed, see step())
    int    sweeps = 0;
    int    momentumRecords = 0;     ///< 26.26: debris momentum records applied on the device this call
    float  rbgsResidualMax = 0.0f;   ///< max |A p - b| over liquid cells after the last substep's sweeps
    double kineticEnergy = 0.0;
    double maxDeltaF = 0.0;          ///< over the LAST tick of the call
    double totalMass = 0.0;          ///< m^3 (from the fill reduction)
    double maxSpeed = 0.0;
    double residueDropped = 0.0;     ///< m^3 this call (sweep; downward merge only on the GPU, see wc_column_ops)
    int    quietTicks = 0;
    bool   asleep = false;
    double gpuMs = 0.0;              ///< wall time of the fenced submission (GPU time + submit/fence overhead)
    double hostResidencyMs = 0.0;    ///< 26.6: of gpuMs, the host's residency work before recording (sparse volumes)
    double hostAfterMs = 0.0;        ///< 26.6: after the fence (the halo check)
    double submitWaitMs = 0.0;       ///< 26.6: of gpuMs, the submission and the fence wait (the GPU's own time + overhead)
    double deviceMs = 0.0;           ///< 26.15: the GPU's own time for the call's main submission (timestamps; 0 = unsupported)
    // 26.6 P2a: brick storage
    int    bricks = 0;               ///< bricks stored after this call (a dense volume: all of its brick box)
    int    brickHeight = 8;          ///< 26.17: cells per brick along y (a brick is 8 x brickHeight x 8)
    int    groundBricks = 0;         ///< 26.18: bricks of the halo that are all ground, read from the GROUND sentinel (not stepped)
    int    capacity = 0;             ///< slots (bricks) the volume can hold
    int    allocated = 0, freed = 0; ///< bricks allocated / freed by this call's residency update
    int    haloRings = 0;            ///< neighbour rings the residency took (1 unless the water is fast)
    int    compacted = 0;            ///< bricks moved to lower slots this call (fragmentation repair)
    long   fluxIntoAbsent = 0;       ///< faces where water would have moved into a brick that is not stored (must be 0)
    double carryBodyM3 = 0.0;        ///< 26.32: the over-full carry this call - excess made in a body's cells (its fill over 1 - s) WITHOUT a displacement rate, m^3
    double carryRateM3 = 0.0;        ///< 26.32: the same in a body's cells WITH a rate (q > 0: the projection was to shed it), m^3
    double carryFlowM3 = 0.0;        ///< 26.32: the same in cells without a body (the transport over-filled them), m^3
    double carryParkedM3 = 0.0;      ///< 26.32: carry that reached a ceiling or the box top and parked over full there, m^3
    long   heldByCapacity = 0;       ///< bricks the residency wanted but the capacity refused (their faces stay closed)
    long   haloShort = 0;            ///< after the call: bricks within 4 cells of water that are NOT stored, from one distance per brick face - an UPPER bound (two films in different cells can read as one in a corner; WaterBrickTest checks by cell, 26.28)
    double unstoredWater = 0.0;      ///< m^3 the last upload could not store (capacity smaller than the water itself; must be 0)
    bool   sentinelDirty = false;    ///< something wrote into the absent brick (must be false)
    int    awakeColumns = 0;         ///< 26.31: brick columns stepped by this call (the partial wake; all stored columns without it)
    int    frozenColumns = 0;        ///< 26.31: brick columns frozen this call (not stepped; a wall to the awake ones)
    int    movingColumns = 0;        ///< 26.31: of the awake, those failing the sleep test themselves (the rest is the margin)
    int    stepSlots = 0;            ///< 26.31 step 2: slots the step dispatched (the awake bricks + the two sentinels; 0 = all)
    int    dispatches = 0;           ///< 28: compute dispatches this call recorded (all substeps), and the barriers between them
    int    barriers = 0;
    int    partitionMoves = 0;       ///< 26.31 step 2: bricks moved to keep the awake ones first
    int    reduceSlots = 0;          ///< 27.23 W1e: slots the rest reductions ran over (the awake ones when the unit map is cached)
    bool   reduceCached = false;     ///< 27.23 W1e: the frozen columns' units came from the kept unit map
};

/// 26.6 P2a: how a volume stores its cells. Dense = every brick of its box kept (the engine default); sparse = only the
/// bricks holding water or within reach of it (residency follows the water each call).
struct BrickOptions {
    bool   sparse = false;
    size_t capacity = 0;             ///< slots for bricks (0 = every brick of the box: never full)
    bool   freezeForTest = false;    ///< TEST ONLY (the equality test's red control): residency never follows the water
    int    height = 0;               ///< 26.17: brick height in cells (2, 4 or 8; 0 = WaterCoreGpu::s_brickHeight); x and z are 8
    int    ground = -1;              ///< 26.18: halo bricks that are all ground read the GROUND sentinel (1 on, 0 off, -1 = s_groundBricks); sparse only
    bool   groundAsAirForTest = false;   ///< TEST ONLY (the red control): the ground sentinel's occupancy left as air
};

/// 27.28 THE WATER'S CPU, BY PHASE. Every lap adds the time since the last lap (or the caller's start) to its key; WaterCoreManager::
/// update() counts the frames. Read and reset with water_av_host_phases. Always on: ~30 map adds a frame.
struct WaterHostProfile {
    using Clock = std::chrono::steady_clock;
    static inline std::map<std::string, double> ms;
    static inline std::map<std::string, long> calls;
    static inline long frames = 0;
    static void lap(const char* key, Clock::time_point& t) {
        const auto now = Clock::now();
        ms[key] += std::chrono::duration<double, std::milli>(now - t).count(); ++calls[key];
        t = now;
    }
    static void reset() { ms.clear(); calls.clear(); frames = 0; }
};

/// 31 (M4): one pressure window a volume's call writes (wc_body_pressure.comp's Req, std430): a dense box of cells round a wet body.
struct PressureWindowReq {
    glm::ivec3 originVol{0}; int32_t dataOffset = 0;    ///< origin in the VOLUME's cells; the window's first float in the data buffer
    glm::ivec3 dims{0};      int32_t index = 0;         ///< cells a side (<= PRESSURE_WINDOW_MAX_DIM); the window's index (< MAX_PRESSURE_WINDOWS)
    glm::ivec3 originWorld{0}; uint32_t slot = 0;       ///< origin in WORLD cells (floor(x / h)); the body's debris solver slot
    float h = 0.0f; uint32_t tag = 0; float pad0 = 0.0f, pad1 = 0.0f;   ///< the volume's cell size; the frame tag; pad0 = the body's
                                                                        ///< footprint radius, pad1 = its top (31.7: the level map's ring and run)
};
static_assert(sizeof(PressureWindowReq) == 64, "wc_body_pressure.comp Req");

class WaterCoreGpu {
public:
    struct Buffer {
        VkBuffer       buf = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;
        VkDeviceSize   bytes = 0;
        void*          mapped = nullptr;   // staging only
    };
    struct Volume {
        GridSpec spec;
        /// STORAGE counts (what the buffers hold and the kernels address): dense volumes - the grid's own sizes; bricked
        /// volumes - capacity x bc for cells and every lattice, column capacity x 64 for columns (26.6).
        size_t cells = 0, nu = 0, nv = 0, nw = 0, latticeMax = 0, columns = 0, groups = 0;
        size_t dCells = 0, dNu = 0, dNv = 0, dNw = 0, dColumns = 0;   ///< the grid's own (dense) counts - the host API's layout
        // 26.6 P2a bricks
        bool bricked = false, sparse = false;
        glm::ivec3 o{0}, bn{0};          ///< origin offset in its first brick (cells); the brick box (bricks)
        int by = 8, bly = 3;             ///< 26.17: brick height (cells) and its log2; bricks are 8 x by x 8
        size_t bc = 512, nuy = 0;        ///< 26.17: cells per brick; the unit map's y count (units are 8 x 2 x 8, the reductions')
        // 26.18: slot 1 is the GROUND sentinel (every cell Solid, nothing else, never written); a map entry of 1 = "this brick is
        // all ground". Which bricks are is read from the CURRENT occupancy at every upload (the world is destructible).
        bool groundOn = false, groundAsAir = false;
        std::vector<char> allSolid;      ///< per map entry: every cell Solid at the last upload (out-of-box cells count as Solid)
        int groundCount = 0;             ///< map entries that are ground
        glm::ivec3 bs() const { return glm::ivec3(8, by, 8); }
        size_t mapCount = 0, capacity = 0, colCap = 0;   ///< capacity counts slot 0 (the absent brick)
        int slotsUsed = 1, colCount = 0;
        std::vector<int> map, slotBrick, colEntries;   ///< host mirrors: map entry -> slot (0 absent); slot -> packed brick (-1 free)
        size_t slotTableOff = 0, colTableOff = 0, counterOff = 0, infoOff = 0, brickInts = 0;
        Buffer bricks;                   ///< [map | slot table | column table | counters 2 | water record per slot]
        VkDeviceSize offBricks = 0;      ///< staging region of the bricks buffer
        bool recordValid = false;        ///< staging holds the water record of the last call (sparse residency reads it)
        bool freezeForTest = false;
        std::vector<int32_t> hostInfo;   ///< the last call's water records (2 per slot), copied once from staging after the fence
        bool tablesDirty = true;         ///< the map / slot / column tables changed since they were last uploaded
        int storedCount = 0;             ///< bricks stored (kept by applyResidency)
        std::vector<uint32_t> hostOcc;   ///< dense occupancy from the last upload (new bricks are filled from it)
        std::vector<float> hostSolid;    ///< dense (s, q, wake, ledger) from the last setSolids
        std::vector<float> hostDeposit;  ///< dense deposits pending for the next step
        std::vector<int> sourceCells;    ///< dense indices of the pumps (their bricks stay stored)
        int lastAllocated = 0, lastFreed = 0, lastRings = 0, lastCompacted = 0; long lastHeld = 0;
        std::vector<int> liquidAtStartForTest;
        int lastTravelForTest = 0;   ///< 26.23 diagnostic: the travel the residency allowed this call (cells)   ///< 26.23 diagnostic: packed bricks holding LIQUID at the call's start
        double unstoredM3 = 0.0;          ///< water the last upload could not store (counted, never silent)
        Buffer f, fOrig, fPrev, u, v, w, uOld, vOld, wOld, occ, src, liq, diag, rhs, p, res;
        Buffer fLo;                      ///< 26.10: per cell, the part of the transport's fill change the float store could not hold
        Buffer valA, valB, knownA, knownB, first, keep, dropped, part, part2, out;
        // Phase B2 on the GPU: the particle transport (FlipTransport mirrored on the device)
        bool particles = false;          ///< this volume's mass is carried by particles
        size_t particleCount = 0, particleCapacity = 0;
        bool haveOldGrid = false;        ///< a p2g base exists (else the first g2p is PIC)
        Buffer posA, velA, posB, velB, pcount, pstart, pcursor;
        VkDeviceSize offParticles = 0;   ///< staging region for pos+vel (capacity x 32 B)
        Buffer sources;                  ///< GpuSource array (device-local; kMaxSources)
        Buffer momentum;                 ///< 26.26: HOST-VISIBLE, 2 vec4 per record (pos rel. to the origin, radius | momentum, max speed)
        std::vector<float> pendingMomentum;   ///< 26.26: records queued since the last call (8 floats each), applied at its start
        long momentumApplied = 0, momentumDropped = 0;   ///< 26.26: records dispatched / refused past kMaxMomentum, since create
        Buffer surf;                     ///< Phase F: the surface field, 26.11 PACKED - only the columns holding water: [one-run records, 32 B x
                                         ///< columns | multi-run records, 64 B x columns]; a dry column is rebuilt on the host (ground at hostSolidTop)
        Buffer birthsPacked, lists;      ///< 26.11: the columns with droplet births (8 floats, the dense column index in slot 7); [one-run, births, multi-run] counts
        std::vector<float> hostSolidTop; ///< 26.11: per dense column, the top face of its highest solid cell (-1e30 none) - from the upload's occupancy
        std::array<uint32_t, 3> lastCount{}, copied{};   ///< 26.11: per list (one-run, births, multi-run): last call's length (the next copy is
                                                         ///< predicted from it) and how many entries the staging holds
        bool listsSized = false;         ///< 26.11: false until a first call measured the lists (the first copy takes every column)
        Buffer solid;                    ///< 20 (moving solids): vec4 per cell = (s, q m^3/s, wake seconds, ledger 0/1); all zero = no body (every rule as before)
        VkDeviceSize offSolid = 0;       ///< staging region of the solid buffer (cells x 16 B)
        bool  solidRatesPending = false; ///< setSolids uploaded new fields: the next step computes q and the ledger marks first (wc_classify mode 1)
        float solidFrame = 0.0f;         ///< the frame (s) the uploaded bodies cover - q's T
        bool  hasSolids = false;         ///< the buffer holds anything nonzero
        int   solidLowY = 1 << 30;       ///< 27.22e: the lowest grid y a body (or its wake) holds (1 << 30 = none) - the tall floor stays under it
        Buffer stageCap;                 ///< 26.10 test hook: f after each stage of the last tick (host-visible, s_stageCapture)
        Buffer unitCap;                  ///< 26.31 measurement: the unit map (per 8 x 2 x 8 unit: KE, max |df|, mass) of the last call (s_unitCapture)
        bool unitPending = false;        ///<   the call in flight copies it
        std::vector<glm::vec4> hostUnits; ///<   the collected call's map (bn.x x nuy x bn.z, x fastest), empty = none
        // 27.23 W1e: the unit map kept on the device between calls (bricked volumes) - a frozen column's units are read from it
        // instead of recomputed (wc_reduce mode 6); unitsFullNext = something wrote frozen cells, the next call computes every unit
        Buffer unitMap;
        bool unitMapValid = false, unitsFullNext = true;
        bool lastSpeedLane = false;      ///<   the recorded call's max speed is mode 0's w lane (mode 1 not run)
        int lastReduceSlots = 0; bool lastReduceCached = false;
        // 26.31 THE PARTIAL WAKE (docs/WaterCore.md 26.31): per brick column of the brick box (bn.x x bn.z)
        bool partialWake = false;        ///< on: quiet columns away from moving ones freeze (sparse volumes only)
        bool unitsFresh = false;         ///< hostUnits holds a call not yet counted
        int unitsTicks = 0;              ///<   its ticks
        std::vector<int> colQuiet;       ///< QUIET UNITS (1/600 s, 27.26) each column has passed the freeze test in a row
        float unitsDt = 1.0f / 60.0f;    ///< 27.26: the collected call's tick (s)
        double quietRef = 0.0;           ///< 27.26: the volume's quiet time in 1/60 s ticks (quietTicks = its integer part)
        std::vector<char> colFrozen, colWake;   ///< frozen now; a wake asked for since the last call
        bool wakeAllPending = false, anyFrozen = false;
        // §27 W1b: RECORDED brick columns - the record (chunk spans + bodies) holds their water. Every brick of such a column reads the
        // GROUND sentinel (Solid: a wall to the awake water, as a frozen column is) and residency never stores it until it is woken;
        // a wake that reaches one (the partial wake's margin, a wake box) is queued for the manager, which seeds its tile.
        std::vector<char> colRecorded;  ///< per brick column (bn.x x bn.z)
        bool anyRecorded = false;
        /// 27.15: a seed changed frozen columns' cells - the next call's surface covers EVERY column (the partial surface keeps a
        /// frozen column's last surface, which for a tile just woken from the record is empty: the tile drew nothing, 0.4 s live)
        bool surfaceFullNext = false;
        std::vector<int> recordedWakes; ///< brick columns (index bx + bn.x bz) a wake reached since the manager last took them
        double seedUnplacedM3 = 0.0;    ///< seeded water whose brick the capacity refused (m^3, cumulative) - reported, never silent
        long wakeAllCount = 0;          ///< whole-volume wakes since creation (diagnostic)
        int awakeCols = 0, frozenCols = 0, movingCols = 0;
        // 26.31 step 2: the awake bricks held in slots [kFirstSlot, stepSlots) and the awake column entries first (stepCols) - the
        // step's dispatches and copies cover only those (frozen bricks get no threads); 0 = every stored brick (no partial wake)
        int stepSlots = 0, stepCols = 0;
        int lastPartitionMoves = 0;      ///< bricks moved by the last call's partition
        Buffer scratch;                  ///< the partition's swap space (grown on demand)
        double stageLast = -1.0;         ///< 26.10: the last captured call's end sum (cells), -1 = none
        VkQueryPool tsPool = VK_NULL_HANDLE;   ///< 26.11 test hook: GPU timestamps per stage (s_kernelTiming)
        // 27.22 TALL CELLS: with tallActive, the cell at grid y = tallY of a tall column holds that column's water from its bed up
        // (tallH cells high; its f is a VOLUME in cells), the bricks under the tall layer read the ground sentinel
        size_t tallOff = 0;            ///< Bricks[] offset of the heights (dense columns)
        bool tallActive = false;
        int tallY = -1;
        std::vector<int> tallH;        ///< host copy, dense columns (1 = an ordinary cell)
        int tallPending = 0;           ///< 1 = collapse at the next call (tallY / tallH set), 2 = expand at the next call
        long tallRegrids = 0;          ///< collapses + expands done
        int lastDispatches = 0, lastBarriers = 0;   ///< 28: the last submitted call's launches
        // 27.23 W1d.1: the slot pool sized by the WATER - a growable (sparse, no fixed capacity) volume starts small and doubles when the
        // residency needs more (growPool); an explicit BrickOptions capacity stays fixed (the capacity test's backstop)
        bool growable = false;
        size_t growWant = 0;            ///< slots a call could not store - the next call grows to hold them (held one call, counted)
        long poolGrows = 0;
        int tallColumns = 0;           ///< columns with H > 1 while active
        VkQueryPool callTs = VK_NULL_HANDLE;   ///< 26.15: two timestamps round every step call - the device's own time, live
        int tsCount = 0;                 ///< stamps written this call
        std::array<int, 64> tsLabel{};   ///< the stage each stamp closes
        Buffer births, deposit;          ///< 21 (droplets): per column 8 floats (wc_column_ops mode 4/5); per cell fractions to add
        VkDeviceSize offBirths = 0, offDeposit = 0;
        bool dropletsOn = false;         ///< run the birth pass after every tick
        bool depositPending = false;     ///< staging holds deposits for the next step
        bool birthsFresh = false;        ///< staging holds birth records no one has read (a sleeping volume does not step: its old records must not be read twice)
        VkDeviceSize offSurf = 0;        ///< staging region of the surface field
        int sourceCount = 0;
        Buffer staging;                  ///< host-visible: [f | u | v | w | occ | src | out | p | sources]
        VkDeviceSize offU = 0, offV = 0, offW = 0, offOcc = 0, offSrc = 0, offOut = 0, offP = 0, offSources = 0;
        std::array<std::unique_ptr<Vulkan::ComputePipeline>, 32> pipes;   // >= KernelCount (static_assert below): 16 overflowed into quietTicks/asleep when the FLIP kernels arrived, 2026-10-08
        int quietTicks = 0;
        bool asleep = false;
        bool quietBefore = false;
        double lastMaxSpeed = 0.0;       ///< from the last call's reduction (or the upload), drives the CFL count
        long lastHaloShort = 0;          ///< 29 S3: the last measured haloShort (live calls measure it once a second)
        std::chrono::steady_clock::time_point lastHaloCheck{};
        bool surfaceValid = false;       ///< Phase F: staging holds a surface field from the last step
        // 26.16: one call in flight (ASYNC) - what the host reads of a call is copied out of staging when it is collected
        bool async = false, inFlight = false;
        VkFence callFence = VK_NULL_HANDLE;
        VkCommandBuffer callCmd = VK_NULL_HANDLE;
        struct Pending {
            int n = 1, ticks = 0; bool capture = false;
            SolverParams params;
            std::chrono::steady_clock::time_point t0, tResidency, tSubmit;
            GpuStepStats st;
            std::vector<int> surfCols;   ///< 26.33: the column entries this call's surface covered (empty = every column)
            uint64_t callId = 0;         ///< 27.15: this call's number (callsSubmitted when it was submitted)
            float dt = 1.0f / 60.0f;     ///< 27.26: the call's tick
            bool speedLane = false;      ///< 27.23 W1e: read the max speed from out[0].w
        } pend;
        uint64_t callsSubmitted = 0;                     ///< 27.15: calls submitted so far
        uint64_t surfCallId = 0;                         ///< 27.15: the call whose surface hostSurf holds (0 = none yet)
        GpuStepStats lastStats; bool haveLast = false;   ///< the last collected call's stats (ASYNC returns them)
        std::vector<char> hostSurf;                      ///< the collected call's packed surface lists (one-run, multi-run records)
        std::vector<char> hostBirths;                    ///< the collected call's packed birth list
        std::array<uint32_t, 3> hostCount{};             ///< their lengths (one-run, births, multi-run)
        std::vector<int> hostSurfCols;                   ///< 26.33: the column entries the collected surface covers (empty = every column)
        uint64_t surfCollected = 0;                      ///< 26.33: calls collected; readSurface keeps the rest of a field only if it read the call before
        mutable uint64_t surfReadAt = 0;                 ///< 26.33: the collected call the host last read the surface of
        std::vector<GpuSource> hostSources;              ///< the sources' totals as the collected call left them (or as set)
        // 29 S1 THE SURFACE ON THE DEVICE (docs/WaterCore.md 29): the dense surface field (a SurfaceColumn per dense column, written by
        // wc_surface for every column it covers), the host's solid tops it reads, the mesh wc_surface_mesh makes from it (corners
        // 16 floats + a meta word per column, the extras list, the indirect draw's args), the host's writes (surfUp: [records | solid tops])
        Buffer surfDense, solidTopD, meshCorners, meshMeta, meshList, meshArgs, meshArgsHost, surfUp;
        uint32_t meshListCap = 0;
        uint64_t serial = 0;             ///< unique per volume (WaterCoreGpu lifetime) - the renderer's descriptor sets are keyed by it
        std::vector<std::pair<uint32_t, Core::Water::SurfaceColumn>> surfPending;   ///< host writes for the next call (later ones win)
        bool solidTopDirty = true;       ///< hostSolidTop changed - re-sent to solidTopD
        bool meshFullNext = true;        ///< the next mesh pass covers every column (host writes, a template)
        bool meshValid = false;          ///< the device field and mesh are whole (false: the first call, the switch was off - the next call builds them whole)
        uint32_t lastMeshEntries = 0, lastMeshDropped = 0;   ///< the collected call's extras list (wanted, dropped past the capacity)
        // 29 S2: the field's numbers the tall floor reads, made on the device (per brick column, folded): the lowest wet top and the water
        Buffer colAgg;
        float fieldMinTop = 0.0f; double fieldWaterSum = 0.0; bool fieldStatsValid = false;
        // 29 S2: motion on the device (takeMotion): the per-column reference tops, the call's result
        Buffer motionRef, motionOut, motionOutHost;
        // 31 (M4): this call's pressure windows (the host's requests, written into winReq at submit; wc_body_pressure.comp)
        Buffer winReq;
        std::vector<PressureWindowReq> windows;
        uint32_t lastWindows = 0;   // the windows the last recorded call wrote
        struct Motion { bool fresh = false; int riseCols = 0, flowCols = 0; float maxRise = 0.0f, maxFlow = 0.0f; int x0 = 0, x1 = -1, z0 = 0, z1 = -1; float y0 = 0.0f, y1 = 0.0f; } motion;
    };
    enum Kernel : int { FillAdvect = 0, VelAdvectU, VelAdvectV, VelAdvectW, FaceOpsU, FaceOpsV, FaceOpsW,
                        ColumnOps, Classify, Rbgs, ExtrapU, ExtrapV, ExtrapW, Reduce, Sources, Surface, Bricks, FlipSort, FlipP2g, FlipG2p, Momentum, Tall, RbgsLine, SurfaceMesh, BodyPressure, KernelCount };   // 31: + BodyPressure   // 28: + RbgsLine (zebra line sweeps)   // 27.22: + Tall (collapse / expand)   // 29 S1: + SurfaceMesh
    static constexpr size_t kMaxMapEntries = 4u << 20;   ///< 26.6: brick-map cap (16 MB) - a larger box is refused (world scale is P5)
    static constexpr int kMaxSources = 64;
    static constexpr int kMaxMomentum = 256;   ///< 26.26: momentum records a call can take (more are counted as dropped)
    static_assert(KernelCount <= 32, "Volume::pipes must hold every kernel");

    WaterCoreGpu() = default;
    ~WaterCoreGpu();
    WaterCoreGpu(const WaterCoreGpu&) = delete;
    WaterCoreGpu& operator=(const WaterCoreGpu&) = delete;

    bool init(VkDevice device, VkPhysicalDevice physical, VkQueue queue, uint32_t queueFamily,
              const std::string& shaderDir, std::string* err);
    void shutdown();
    bool ready() const { return m_device != VK_NULL_HANDLE && m_pool != VK_NULL_HANDLE; }
    /// 26.9: the projection starts from the still-water pressure of the water as it is now (classify mode 2), not 0.
    /// 26.32 measurement: count the over-full carry by kind (water_av_list carry_*_m3). Off by default - the step's atomics on
    /// four shared words cost it when a splash over-fills thousands of cells. water_transport {carry_measure}.
    static inline bool s_carryMeasure = false;
    static inline bool s_stillWaterGuess = true;    // ON (26.10): the resting-body loss was float rounding in the fill store, fixed (fLo)
    /// 26.17: the brick height (cells) a volume gets when its BrickOptions name none - 2, 4 or 8 (bricks are 8 x h x 8). 2 since
    /// 26.17's bench run: the creek at q 0.2 stored 691,200 -> 457,984 cells, GPU 1.83 -> 1.57 ms a call, the water identical
    static inline int s_brickHeight = 2;
    /// 26.18: sparse volumes read halo bricks that are all ground from the GROUND sentinel instead of stepping them. ON since the
    /// bench run: the creek at q 0.2 stored 456,192 -> 282,240 cells, GPU 1.56 -> 1.35 ms a call, the water identical
    static inline bool s_groundBricks = true;
    /// 26.19 TEST ONLY (the red control): updateOccupancy leaves the bricks' all-ground flags as they were (a dug ground brick
    /// stays ground)
    static inline bool s_patchStaleGroundForTest = false;
    /// 26.23 diagnostic (WaterBrickTest): each halo-short brick by the record that needed it - a LIQUID record in a brick that
    /// held liquid at the call's start (water moved past the travel allowance), one that held only film then (film became liquid
    /// in place), or a FILM record
    static inline long s_haloShortLiquidOldForTest = 0, s_haloShortLiquidNewForTest = 0, s_haloShortFilmForTest = 0;
    static inline std::string s_haloShortDetailForTest;   ///< 26.23: per short brick - the film brick's offset, its face distances, the call's travel
    /// 29 S3: the halo check (haloShortAfterCall - a DIAGNOSTIC, nothing steps on it) redoes the whole residency over every stored brick:
    /// ~2.5 ms a call on the 60 x 60 m lake. A synchronous call (the tests, the harness) measures it every call; a live (async) call
    /// once a second, reporting the last value between. true = every call (the old behaviour).
    static inline bool s_haloCheckEveryCall = false;
    /// 29 S3: the residency recomputed only round the AWAKE water - the awake brick columns dilated by the rings a call's reach can
    /// cross (R = ceil(reach / 8)); the marks read from the slots within R of those. Every other entry keeps its state: a frozen
    /// column's water does not move, so its need is what it was. Off = every stored brick (the old way, ~3 ms on the lake).
    static inline bool s_residencyAwakeOnly = true;
    /// TEST ONLY: compute the full residency too and count the entries whose outcome (absent / ground / stored) differs; and the
    /// red control - nothing recomputed, every entry kept (the check must see the water's own residency changes). Measured: the
    /// dilation itself was not needed on the test pond (the partial wake's 2-column margin already keeps quiet awake columns
    /// between the moving water and the frozen ones) - it stays as a margin of safety (cheap: a few thousand flags)
    static inline bool s_residencyCheckForTest = false, s_residencyCopyAllForTest = false;
    static inline long s_residencyMismatchForTest = 0, s_residencyCallsCheckedForTest = 0, s_residencyRestrictedCallsForTest = 0;
    /// 26.10 test hook: copy f after every stage of each single-tick, single-substep call and sum it on the host in double -
    /// exact per-stage mass changes (the device's own sums are float). s_stageDm[k] accumulates stage k's change in cell
    /// fractions: 0 between calls, 1 sources, 2 transport gather, 3 transport carry, 4 ledger, 5 compaction, 6 rest of the
    /// substep, 7 residue sweep, 8 droplet births. s_stageCalls counts the calls captured (multi-substep calls are skipped).
    static constexpr int kStages = 9;
    static inline bool s_stageCapture = false;
    static inline double s_stageDm[kStages] = {};
    static inline long s_stageCalls = 0;
    /// 26.11 test hook: GPU time per stage of single-tick, single-substep calls (timestamps; ms accumulated in s_tsMs[label]).
    enum TsLabel : int { TsStart = 0, TsResidency, TsSources, TsTransport, TsVelAdvect, TsCompaction, TsForces, TsClassify, TsSweeps,
                         TsVelUpdate, TsExtrapolate, TsSettle, TsSweepBirths, TsBrickRecord, TsReduce, TsSurfaceKernel, TsSurfaceCopy, TsReduceSurface, TsLabelCount };
    static inline bool s_kernelTiming = false;
    /// 26.31 measurement: copy each call's unit map (the rest reductions' per-unit partials: x = sum f |v|^2 / 2, y = max |f - fPrev|
    /// over the call's last tick, z = sum f; per 8 x 2 x 8 cells, world-aligned) to the host - what a partial wake would read.
    static inline bool s_unitCapture = false;
    /// The last collected unit map; dims = (bn.x, nuy, bn.z), cellOffset = the grid cell of unit (0, 0, 0)'s first cell (-o).
    bool readUnits(const Volume& vol, std::vector<glm::vec4>& out, glm::ivec3& dims, glm::ivec3& cellOffset) const;
    /// 26.31 the partial wake: on/off per volume (sparse only; off = every stored brick steps, as before). The margin (brick
    /// columns grown round the moving ones) is kPartialWakeMargin.
    void setPartialWake(Volume& vol, bool on);
    static constexpr int kPartialWakeMargin = 2;    /// TEST ONLY (WaterPartialWakeTest's red control): frozen columns are skipped but NOT a wall - their awake neighbours read them as water.
    static inline bool s_partialWakeOpenInterfaceForTest = false;
    /// 26.33: with the partial wake, the surface kernel and its copy to the host cover the AWAKE columns only (a frozen column's
    /// surface cannot change); false = every column every call (the A/B, water_av_partial_wake {surface}).
    static inline bool s_partialSurface = true;
    /// 29 S1: the surface mesh made and drawn on the device (water_render_core {gpu_surface}). Off = the host's tiler draws every
    /// volume (the A/B control). TEST ONLY: the mesh pass without its one-column ring (the parity test's red control).
    static inline bool s_gpuSurface = true;    // ON since 29 S1 passed (mesh parity, red control, live pixels)
    static inline bool s_meshNoRingForTest = false;
    static inline bool s_tallAggNoFullForTest = false;
    static inline bool s_motionHalfThresholdForTest = false;   ///< 29 S2 TEST ONLY: the device's motion at half moveM (the red control)
    static inline float s_motionMoveM = 0.015f;   ///< 29 S2: the surface move (m) a column must exceed to count as rising (takeMotion's moveM)   ///< 29 S2 TEST ONLY: the brick-column numbers never redone whole (the red control)
    /// 27.23 W1e: the rest reductions follow the awake water - frozen columns' units kept in the unit map, mode 1's three face
    /// passes folded into mode 0's speed lane. Off = every stored brick reduced every call (today's path, the parity test's A).
    static inline bool s_unitCache = true;
    /// 27.26 THE FREEZE AND SLEEP TESTS IN TIME, NOT TICKS. A live tick is a frame: at 30 fps the same wave moves the level twice
    /// as far a tick and failed |df| < 3e-5 a tick (the 60 x 60 m lake stayed awake for ever, its own cost slowing the frame); at
    /// 600 fps 30 quiet ticks were 50 ms. Now |df| is compared as a RATE (the threshold x dt / (1/60 s)) and the quiet run counts
    /// seconds (restTicks x 1/60 s) - at 60 fps exactly as before. TEST ONLY (the red control): the old per-tick tests.
    static inline bool s_freezePerTickForTest = false;
    static constexpr int kQuietUnitsPerTick = 10;   ///< colQuiet counts 1/600 s
    /// TEST ONLY (the parity test's red control): the cache ignores unitsFullNext - a write to frozen cells is not seen
    static inline bool s_unitCacheStaleForTest = false;
    /// 26.34: the per-column FREEZE test's thresholds as a multiple of the volume's sleep test (keWake, maxDeltaFQuiet). 1 = the
    /// same test (26.31). Measured: at 1 a waking column lets sub-0.1 mm waves in, they count as moving, and the awake patch creeps
    /// over the pond (66-79 % awake 11-20 s after a push whose waves were 0.5 mm at 8 m); at 10 nothing moves after ~11 s. A look
    /// decision (a frozen column is a wall: the waves too small to wake it reflect). water_av_partial_wake {quiet_scale}.
    /// Default 3: the owner's call on pw_look5 (2026-10-19, "multiplier 3 looks fine").
    static constexpr float kColumnQuietScaleDefault = 10.0f;   // owner 2026-10-23: x10 (was x3, 26.34) - a stone's lake records ~10 s after its waves (27.24)
    static inline float s_columnQuietScale = kColumnQuietScaleDefault;
    /// 27.14 measurement: ticks a column must stay quiet before it freezes (0 = the volume's restTicks, 30 = 0.5 s). A pond-wide
    /// slosh (46 m, 1 m deep: ~30 s period) is still at its turn for longer than 0.5 s. water_av_partial_wake {rest_ticks}.
    static inline int s_columnRestTicks = 0;
    /// 26.33 measurement: surface reads that kept the frozen columns (WaterPartialWakeTest asserts they happened).
    static inline long s_partialSurfaceReads = 0;
    /// TEST ONLY (the red control): a partial read resets every column it did not cover, as the whole read does - frozen ones too.
    static inline bool s_partialSurfaceResetAllForTest = false;
    /// 26.31: wake the brick columns touching grid cells [lo, hi] (the margin grows round them next call) / every column.
    void wakeBox(Volume& vol, const glm::ivec3& lo, const glm::ivec3& hi);
    void wakeAll(Volume& vol);
    /// §27 W1b: the cells of the brick columns [bLo, bHi] (brick-box x, z; inclusive) read back - only those bricks cross - into `out`
    /// (spec in world cells: origin = the columns' first cell, every y of the volume; occupancy from the host copy, Solid where
    /// unknown). Settles the call in flight. False when the volume is not bricked.
    bool readColumns(Volume& vol, const glm::ivec2& bLo, const glm::ivec2& bHi, WaterGrid& out);
    /// 27.22 TALL CELLS: from the next call, the cell at grid y `tallY` of every column with heights[x + nx z] > 1 holds that column's
    /// water from tallY - H + 1 up (gathered exactly), and the bricks under the tall layer's brick read the ground sentinel. `tallY`
    /// must be the bottom cell of a brick ((tallY + o.y) % brick height == 0) and every column of height H open (no solid) from
    /// tallY - H + 1 to tallY - the caller's rule (WaterCoreManager). Refused (false + err) on a dense or particle
    /// volume, without ground bricks, or while tall cells are active (clear first). 27.22 TC4: recorded tiles combine - readColumns
    /// spreads a tall cell back into the record's columns, seedColumns collapses a woken tile's columns into their tall cells.
    bool setTall(Volume& vol, int tallY, const std::vector<int>& heights, std::string* err);
    /// 27.22: every tall cell back to ordinary cells at the next call (exact; the bricks under the floor stored again).
    void clearTall(Volume& vol);
    /// §27 W1b: the brick columns [bLo, bHi] are RECORDED from the next call (their bricks freed, read as ground - a wall).
    void setColumnsRecorded(Volume& vol, const glm::ivec2& bLo, const glm::ivec2& bHi);
    /// §27 W1b measurement: the ground sentinel's largest |f|, |u|, |v|, |w| (must stay 0 - every ground and recorded brick reads it).
    glm::vec4 sentinelMaxAbs(Volume& vol);
    /// §27 W1b: wake recorded columns [bLo, bHi] with the cells in `cells` (spec in world cells, the volume's h): their bricks (and a
    /// ring of halo) are stored and these fills uploaded NOW (one submission), the columns at rest (frozen unless motion reaches them).
    void seedColumns(Volume& vol, WaterGrid cells, const glm::ivec2& bLo, const glm::ivec2& bHi);
    /// §27 W1b: the recorded brick columns a wake reached since the last take (index bx + bn.x * bz).
    std::vector<int> takeRecordedWakes(Volume& vol) { std::vector<int> out; out.swap(vol.recordedWakes); return out; }
    /// §27 W1b TEST ONLY: recorded columns NOT walled - the halo stores their bricks as air and they are not frozen, so awake water
    /// beside them pours in (the water then sits in cells AND in the record): the red control.
    static inline bool s_recordedOpenForTest = false;
    /// §27 W1b TEST ONLY: the seed leaves fPrev at 0 (the live cascade's cause) - the red control of the wake-speed bound.
    static inline bool s_seedSkipsFPrevForTest = false;
    /// 27.19 measurement (ms, last call): a tile wake's seed upload / a record's cell read, split into the wait for the call in
    /// flight (settle), the host work, and the blocking submission
    struct TileGpuMs { double seedSettle = 0, seedHost = 0, seedSubmit = 0, readSettle = 0, readSubmit = 0, readHost = 0; double seedPart[5]{}; };   // seedPart: flags+need, residency, fresh uploads, fills, tables
    static inline TileGpuMs s_tileGpuMs;
    /// 28 (the launch count): dispatches / barriers recorded since the last reset (step() resets at its start, reports at its end)
    mutable long m_dispatchCount = 0, m_barrierCount = 0;
    /// 27.22: red-black sweep pairs of the tall layer alone after the band's sweeps (one cell a column - cheap)
    static inline int s_tallSweeps = 20;
    /// 28 (the launch count): ZEBRA LINE sweeps of the pressure (wc_rbgs_line.comp - every column solved exactly along y, the columns
    /// in a checkerboard) in place of the point sweeps: 0 = off (the point sweeps, the default until the owner looks), N = N line
    /// sweeps (2N dispatches); omega their over-relaxation. Volumes taller than 64 cells keep the point sweeps.
    /// 27.22d TEST ONLY (the red control): upload clears the tall state only at its end - its residency then reads the bricks under the
    /// floor as ground and the grid's water there is lost
    static inline bool s_uploadKeepsTallForTest = false;
    /// 27.22 TC4 TEST ONLY (the red control): a woken tile's seed is NOT collapsed into its tall cells - its water under the floor lands
    /// in bricks that read the ground, and is lost
    static inline bool s_seedSkipsTallCollapseForTest = false;
    static inline int s_lineSweeps = 0;
    /// 28 TEST ONLY: the GPU time (ms) of `n` dispatches of the brick-record kernel over `groups` workgroups each, with a barrier after
    /// each (`barriers`) or none - what a launch and a barrier cost by themselves
    double diagLaunchCost(Volume& vol, int n, int groups, bool barriers);
    static inline float s_lineOmega = 1.0f;
    static constexpr int kLineMaxNy = 64;
    /// 27.23 W1d.1: the slots a growable volume starts with (~8 MB at 2-tall bricks)
    static constexpr size_t kInitialSlots = 510;
    static inline double s_tsMs[TsLabelCount] = {};
    static inline long s_tsCalls = 0;
    void setOmega(float w) { m_omega = w; }
    float omega() const { return m_omega; }

    Volume* createVolume(const WaterGrid& g, std::string* err, const BrickOptions& bricks = BrickOptions{});
    /// Allocation only (no upload): refuses with the byte count when the device cannot hold it. A particle volume is
    /// dense-laid (the FLIP kernels index the grid directly); every other volume is bricked (26.6).
    Volume* createVolume(const GridSpec& spec, std::string* err, bool particles = false, size_t particleCapacity = 0,
                         const BrickOptions& bricks = BrickOptions{});
    void destroyVolume(Volume* vol);
    void upload(Volume& vol, const WaterGrid& g);                              ///< f, u, v, w, occ -> GPU (fenced)
    /// docs/WaterCore.md 20 (M1): the fields of the bodies in this volume for the next `frameSeconds` - s and
    /// the wake from WaterSolver::updateSolidFields, `fresh` its fresh-body marks. The rates (q and the
    /// walled-in / body-filled ledger marks) are computed on the device at the next step, from the GPU's own
    /// fill (wc_classify mode 1, the same rule as WaterSolver::computeSolidRates). All-zero inputs after a
    /// zero buffer upload nothing. Wakes the volume when anything is or was there.
    void setSolids(Volume& vol, const std::vector<float>& s, const std::vector<float>& wake, const std::vector<uint8_t>& fresh, float frameSeconds);
    /// 26.12: setSolids where only the inclusive grid-cell box [lo, hi] may have changed (WaterSolver::solidActiveBox): the host
    /// copy is updated there and only the stored bricks it touches are uploaded. The device's solid buffer ends identical to
    /// setSolids' (WaterCoreGpuParityTest.GpuBoxedSolidsEqualTheWhole).
    void setSolidsBox(Volume& vol, const std::vector<float>& s, const std::vector<float>& wake, const std::vector<uint8_t>& fresh, float frameSeconds,
                      const glm::ivec3& lo, const glm::ivec3& hi);
    /// 26.19: the world changed under the water at `cells` (dense grid indices; `g` holds their new occupancy). Patched IN
    /// PLACE: the host copy, the all-ground flags of their bricks, the solid top of their columns, and the occupancy of the
    /// stored bricks among them on the device. Nothing else is touched - the water, faces, pressure and remainders keep their
    /// values; the next call's residency stores a dug ground brick (dry) or lets a filled one become ground. Wakes the volume.
    /// A volume never uploaded (no host copy yet) is left for its first upload.
    void updateOccupancy(Volume& vol, const WaterGrid& g, const std::vector<int>& cells);
    /// Replace the volume's pumps/sinks (fenced); the projection's per-cell rate buffer is reset.
    bool setSources(Volume& vol, const std::vector<GpuSource>& sources, std::string* err);
    /// 26.26: give the water `momentum` (m^3 x m/s - density 1) inside a sphere at `pos` (world metres) - WaterSolver::addMomentum's
    /// rule, applied ON THE DEVICE at the start of the volume's next call (no read-back, no re-upload). Wakes the volume.
    void queueMomentum(Volume& vol, const glm::vec3& pos, float radius, const glm::vec3& momentum, float maxSpeed = 20.0f);
    /// 27.23 W1d.2: WaterSolver::addImpulse / addRadialImpulse ON THE DEVICE, applied at the start of the next call (world metres) - in
    /// place of the manager's whole-volume round trip (download, the CPU rule, upload: ~200 ms on a 46 x 28 m pond)
    void queueImpulse(Volume& vol, const glm::vec3& worldPos, float radius, const glm::vec3& deltaV);
    void queueRadialImpulse(Volume& vol, const glm::vec3& worldCentre, float reach, float speedAtCentre, float upBias, float maxSpeed);
    /// TEST ONLY (26.26 parity): apply the queued records now - only them, no substep - and wait.
    int applyQueuedMomentumForTest(Volume& vol);
    void readSources(Volume& vol, std::vector<GpuSource>& out) const;         ///< placedTotal/pending as the last collected call left them (26.16: no submission)
    /// Phase F: the surface field as the last step() left it in the staging ring (no submission: a memcpy).
    /// `out` must hold columns * 16 floats (SurfaceColumn). Returns false when no step has run since the upload.
    /// `keep`: `out` already holds this volume's whole field (the caller's last read). With the partial wake (26.33) a call's surface
    /// covers only its awake columns; they are written over `out` and the rest kept - refused (false: the caller extracts the whole
    /// field) without `keep` or if a collected call's surface was never read (its columns would be lost).
    bool readSurface(const Volume& vol, float* out, bool keep = false) const;
    bool surfaceReady(const Volume& vol) const { return vol.surfaceValid; }
    /// 29 S4: the readable surface covers only the awake columns (a partial read keeps every other column - a recorded tile's marks too)
    bool surfacePartial(const Volume& vol) const { return !vol.hostSurfCols.empty(); }
    /// 31 (M4, docs/WaterCore.md 31.2): where the windows go - the debris solver's buffers (GpuParticlePhysics::pressureWindowTargets).
    /// Unset (null) = no window is ever written (every wet body keeps the table law).
    void setPressureWindowTargets(VkBuffer wins, VkDeviceSize winsBytes, VkBuffer data, VkDeviceSize dataBytes, VkBuffer of, VkDeviceSize ofBytes);
    bool pressureWindowTargetsSet() const { return m_pwWins.buf != VK_NULL_HANDLE; }
    /// The windows this volume's NEXT call writes (after its last substep): replaces the last frame's. Indices and data offsets are
    /// global across the volumes (the host assigns them inside the budgets of solver_shared.h).
    void setPressureWindows(Volume& vol, std::vector<PressureWindowReq> w) { vol.windows = std::move(w); }
    /// TEST (31): window targets owned here, host-visible (no debris solver in the test), and a window's header + map read back
    bool makePressureTargetsForTest(std::string* err);
    bool readPressureWindowForTest(uint32_t k, int32_t origin[4], int32_t dims[4], float meta[4], std::vector<float>& map) const;
    /// TEST (31): the RED control - every window's volume origin one column off in x (the map then is not the column it says)
    static inline bool s_pwShiftForTest = false;
    /// 29 S2: the collected call's motion (the rising columns' box in the volume's cells, their tops' range, the counts) - `take`
    /// consumes it (once per call); false when the device surface is off for this volume
    bool motionResult(Volume& vol, Volume::Motion& out, bool take) const {
        if (!s_gpuSurface || !vol.bricked || !vol.meshValid) return false;
        out = vol.motion; if (take) vol.motion.fresh = false; return true;
    }
    /// 29 S2: the collected call's lowest wet top (m; +inf = no water) and its field's water (m^3: the runs' heights x h^2) - what
    /// updateTall scanned the host's field for. False until the device surface has run.
    bool surfaceStats(const Volume& vol, float& minTop, double& waterM3) const {
        if (!vol.fieldStatsValid) return false;
        minTop = vol.fieldMinTop; waterM3 = vol.fieldWaterSum * static_cast<double>(vol.spec.h) * vol.spec.h; return true;
    }
    /// 29 S1: host writes into the device's surface field - the columns [x0, x1) x [z0, z1) of `field` (the volume's own lattice), sent
    /// in the next call's command buffer before its surface pass (a recorded tile's marks, a woken tile's seed)
    void queueSurfaceColumns(Volume& vol, const Core::Water::WaterSurfaceField& field, int x0, int z0, int x1, int z1);
    /// 29 S1: the next call starts the device field from the dry template (an upload, the first call, the switch turned on) - the
    /// host's own writes (the recorded tiles' marks) must be queued again for it
    bool surfaceTemplatePending(const Volume& vol) const { return s_gpuSurface && vol.bricked && !vol.meshValid; }
    /// 29 S1: what the renderer draws a volume from (valid: the switch on and a mesh pass has run)
    struct SurfaceView { VkBuffer surf = VK_NULL_HANDLE, corners = VK_NULL_HANDLE, meta = VK_NULL_HANDLE, list = VK_NULL_HANDLE, args = VK_NULL_HANDLE;
                         VkDeviceSize surfBytes = 0, cornersBytes = 0, metaBytes = 0, listBytes = 0; glm::ivec3 origin{0}; int nx = 0, nz = 0; float h = 0.0f; uint64_t serial = 0; bool ok = false; };
    SurfaceView surfaceView(const Volume& vol) const;
    /// 29 S1 TEST ONLY: every quad of the device mesh expanded through wspQuad (what the vertex shader draws) - 4 vertices of 12 floats
    /// each (WaterSurfaceVertex) per quad; settles the volume first. False if the device surface is off for it.
    bool expandSurfaceForTest(Volume& vol, std::vector<float>& vertices);
    /// 27.15: the call the readable surface came from, and how many calls were submitted - a write applied now (a seed) shows in
    /// the surface of call callsSubmitted() + 1 and later
    uint64_t surfaceCallId(const Volume& vol) const { return vol.surfCallId; }
    uint64_t callsSubmitted(const Volume& vol) const { return vol.callsSubmitted; }
    /// 21 (droplets): births on/off for the next steps.
    void setDroplets(Volume& vol, bool on) { vol.dropletsOn = on && !vol.particles; }
    /// 21: per-cell water (cell fractions) to add at the start of the next step (wc_column_ops mode 5); wakes the volume.
    void setDeposits(Volume& vol, const std::vector<float>& cellFractions);
    /// 21: the last step's birth records (columns x 8 floats, see wc_column_ops mode 4): a memcpy of the staging copy.
    bool readBirths(Volume& vol, std::vector<float>& out) const;
    /// 26.30 step 3: the same births as the packed list (8 floats a column WITH births, its dense column index in slot 7 as
    /// uint bits) - readBirths spreads them over every column (2.3 MB zeroed and scanned a step on a 32 x 28 m pond, 0.44 ms).
    bool readBirthRecords(Volume& vol, std::vector<float>& out) const;
    /// Phase B2: replace the volume's particles (fenced) and rebuild the grid's f and faces from them.
    bool setParticles(Volume& vol, const std::vector<FlipParticle>& ps, std::string* err);
    void readParticles(Volume& vol, std::vector<FlipParticle>& out) const;     ///< the sorted list (fenced)
    void setFlipBlend(float b) { m_flipBlend = b; }
    void download(Volume& vol, WaterGrid& g) const;                            ///< f, u, v, w -> grid (fenced)
    void readPressure(Volume& vol, std::vector<float>& p) const;               ///< the last projection's pressure (fenced; tests)
    /// Diagnostics: a per-cell buffer by name ("f", "p", "liq", "rhs", "diag", "res", "occ") as floats (fenced).
    void readCells(Volume& vol, const char* which, std::vector<float>& out) const;

    /// Runs `ticks` ticks of `dt` in ONE submission. The CFL substep count is taken from the last
    /// known max speed plus the gravity this call can add (ticks x g x dt), so it is conservative
    /// rather than per-tick exact; rest bookkeeping advances by `ticks` when the final state is
    /// quiet (exact for realtime's one tick per call).
    GpuStepStats step(Volume& vol, const SolverParams& params, float dt, int ticks, int sweeps);
    /// 26.16: ASYNC - step() submits its call and returns the stats of the call before it (collected first); nothing waits on
    /// the GPU in the frame. The water is bit-identical to SYNC (the default); what the host reads lags one call
    /// (WaterCoreGpuParityTest.GpuAsyncEqualsSync). Turning it off finishes the call in flight.
    void setAsync(Volume& vol, bool on) { if (!on) settle(vol); vol.async = on; }
    /// 26.16: finish the call in flight, if any (wait on its fence, collect its results) - every host entry that touches the
    /// staging buffer or the device state does this first.
    void settle(Volume& vol) { if (vol.inFlight) finishCall(vol); }

private:
    bool createBuffer(Buffer& b, VkDeviceSize bytes, bool hostVisible, std::string* err);
    void destroyBuffer(Buffer& b);
    bool createPipelines(Volume& vol, std::string* err);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) const;
    void barrier(VkCommandBuffer cmd) const;
    void dispatchNoBarrier(VkCommandBuffer cmd, Vulkan::ComputePipeline& pipe, const WcPush& push, size_t threads, uint32_t local = 64) const;
    void dispatch(VkCommandBuffer cmd, Vulkan::ComputePipeline& pipe, const WcPush& push, size_t threads, uint32_t local = 64) const;
    void copy(VkCommandBuffer cmd, const Buffer& from, const Buffer& to, VkDeviceSize bytes, VkDeviceSize srcOff = 0, VkDeviceSize dstOff = 0) const;
    void recordSubstep(VkCommandBuffer cmd, Volume& vol, WcPush push, const SolverParams& params, int sweeps, bool quietBefore);
    void recordExtrapolate(VkCommandBuffer cmd, Volume& vol, WcPush push, bool particles);
    void stamp(VkCommandBuffer cmd, Volume& vol, int label) const;   ///< 26.11 test hook (no-op unless the volume's pool is armed)
    void recordReductions(VkCommandBuffer cmd, Volume& vol, WcPush push);
    VkCommandBuffer beginCommands() const;
    void submitAndWait(VkCommandBuffer cmd) const;
    void finishCall(Volume& vol);   ///< 26.16: wait on the call in flight and collect it (everything step did after its fence)
    // 26.6 P2a bricks (host side)
    long storageIndex(const Volume& vol, int x, int y, int z) const;   ///< -1 = absent / outside the brick box
    void fillPushBricks(const Volume& vol, WcPush& push) const;
    /// 27.23 W1d.1: the descriptor bindings of every kernel (createPipelines; again after a pool grows)
    void bindPipelines(Volume& vol);
    /// 27.23 W1d.1: grow the slot pool to hold at least `slots` stored bricks (doubling): new device buffers, every stored slot copied
    /// across (slot numbers unchanged), the tables rewritten, the kernels rebound. Between calls only (settles first). False = no change.
    bool growPool(Volume& vol, size_t slots);
    /// 27.23 W1d.1: grow before a residency that would need more slots than the pool has (upload, seed)
    void ensureCapacity(Volume& vol, const std::vector<char>& need);
    void updatePartialWake(Volume& vol, const SolverParams& params);   // 26.31: the columns' state from the last unit map + wakes
    int partitionSlots(VkCommandBuffer cmd, Volume& vol);              // 26.31 step 2: awake bricks into the low slots (returns moves)
    size_t liveCells(const Volume& vol) const { return vol.bricked ? static_cast<size_t>(vol.slotsUsed) * vol.bc : vol.cells; }
    size_t liveLattice(const Volume& vol, int L) const { return vol.bricked ? liveCells(vol) : (L == 0 ? vol.nu : (L == 1 ? vol.nv : vol.nw)); }
    size_t liveColumns(const Volume& vol) const { return vol.bricked ? static_cast<size_t>(vol.colCount) * 64 : vol.columns; }
    long columnStorage(const Volume& vol, int x, int z) const;          ///< storage index of a column's thread, -1 = none
    /// Residency: `need` (per map entry: 2 = holds water, 1 = within reach of it, 0 = not needed) -> slots. Bricks holding
    /// water are stored FIRST, then the halo; returns the new slots (to be zeroed and filled); counts refusals.
    std::vector<int> applyResidency(Volume& vol, const std::vector<char>& need);
    void markNeed(const Volume& vol, std::vector<char>& need, const glm::ivec3& brick, const int dist[6], int reach) const;
    void needFromDenseFill(const Volume& vol, const std::vector<float>& f, std::vector<char>& need, int reach) const;
    long haloShortAfterCall(const Volume& vol) const;   ///< from the staging water record: bricks within 4 cells of water not stored
    void writeTablesToStaging(Volume& vol) const;
    void scatterSlots(Volume& vol, const std::vector<int>& slots, const WaterGrid* g) const;   ///< staging <- grid / host copies
    void recordSlotUploads(VkCommandBuffer cmd, Volume& vol, const std::vector<int>& slots, bool zero) const;
    /// Move the highest stored bricks into the lowest free slots when the addressed range has grown past the stored count
    /// (the dispatches walk every slot up to the highest used). Records the device copies; returns the bricks moved.
    int compactSlots(VkCommandBuffer cmd, Volume& vol);

    VkDevice         m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical = VK_NULL_HANDLE;
    VkQueue          m_queue = VK_NULL_HANDLE;
    uint32_t         m_queueFamily = 0;
    VkCommandPool    m_pool = VK_NULL_HANDLE;
    VkFence          m_fence = VK_NULL_HANDLE;
    std::string      m_shaderDir;
    float            m_flipBlend = 0.95f;
    void recordParticleTransport(VkCommandBuffer cmd, Volume& vol, WcPush push);
    float            m_omega = 1.85f;   // measured: residual 0.14 at 40 sweeps on the 10 m column (1.6 at 1.7, 8.2 at 1.0); see GpuRbgsConvergenceScan
    std::vector<Volume*> m_volumes;
    Buffer m_pwWins, m_pwData, m_pwOf;   // 31: not owned (the debris solver's); buf + bytes only
    Buffer m_pwTest[3];                   // 31 TEST: owned host-visible targets (makePressureTargetsForTest)
};

}}} // namespace Phyxel::Core::Water
