// WaterCoreManager — docs/WaterCore.md §5, §15.1 (Phase B engine integration).
#include "core/water/WaterCoreManager.h"
#include "core/water/WaterDroplets.h"
#include "solver_shared.h"   // 31: the pressure windows' budgets
#include <nlohmann/json.hpp>   // 35: the slept boxes' save format
#include <array>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <cmath>
#include <limits>
#include <set>
#include <thread>

namespace Phyxel {
namespace Core {
namespace Water {

namespace {
constexpr int kMicroPerVoxel = 9;

int floorDiv(int a, int b) { return (a >= 0) ? (a / b) : -(((-a) + b - 1) / b); }
}

WaterCoreManager::WaterCoreManager(MicroStateQuery state, SolidsRevisionQuery revision)
    : m_state(std::move(state)), m_revision(std::move(revision)) {}

void WaterCoreManager::setStateQueryThreadSafe(bool on, int threads) {
    if (!on) { m_occThreads = 1; return; }
    if (threads <= 0) threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) - 2, 1, 8);
    m_occThreads = std::clamp(threads, 1, 16);
}

bool WaterCoreManager::snapCellSize(float requested, float* snapped) {
    static const float kAllowed[] = {1.0f, 1.0f / 3.0f, 1.0f / 9.0f, 1.0f / 27.0f, 1.0f / 81.0f};
    for (float a : kAllowed) {
        if (std::abs(requested - a) <= 1e-3f * a) { *snapped = a; return true; }
    }
    return false;
}

int WaterCoreManager::create(const glm::ivec3& minVoxel, const glm::ivec3& maxVoxel, float cellSize,
                             const std::string& transport, std::string* err, const std::string& backend, int gpuSweeps, int brickHeight, int groundBricks) {
    float h = 1.0f;
    if (!snapCellSize(cellSize, &h)) { if (err) *err = "cellSize must be 1, 1/3, 1/9, 1/27 or 1/81 (a cell that does not divide a microcube would sit half inside a wall)"; return 0; }
    const int per = static_cast<int>(std::lround(1.0f / h));
    const glm::ivec3 lo = glm::min(minVoxel, maxVoxel), hi = glm::max(minVoxel, maxVoxel);
    // 32: a new volume over a remembered (slept) box replaces it - two volumes would wake over the same record
    m_slept.erase(std::remove_if(m_slept.begin(), m_slept.end(), [&](const SleptVolume& s) {
        return glm::all(glm::lessThanEqual(s.lo, hi)) && glm::all(glm::greaterThanEqual(s.hi, lo)); }), m_slept.end());
    const glm::ivec3 dims = (hi - lo + glm::ivec3(1)) * per;
    const size_t cells = static_cast<size_t>(dims.x) * dims.y * dims.z;
    if (transport != "eulerian" && transport != "flip") { if (err) *err = "transport must be 'eulerian' or 'flip'"; return 0; }
    std::string be = backend;
    if (be.empty() || be == "auto") be = (gpuReady() && transport == "eulerian") ? "gpu" : "cpu";   // the default: fills on the device once their parity rows passed (15.13-15.15); particles stay on the CPU until the splash row closes
    if (be != "cpu" && be != "gpu") { if (err) *err = "backend must be 'cpu', 'gpu' or 'auto'"; return 0; }
    // 27.23 W1d: a GPU fill volume's device memory follows its water (W1d.1) and its writes go in place (W1d.2) - its cap is the CPU
    // mirror's (~50 B a cell of the box: ~800 MB at 16 M cells, until W1d.3 removes it); the CPU reference and FLIP keep theirs
    const bool gpuFill = be == "gpu" && transport == "eulerian";
    const size_t cap = gpuFill ? kMaxCellsPerGpuVolume : kMaxCellsPerVolume;
    if (cells > cap) { if (err) *err = "volume would be " + std::to_string(cells) + " cells; the " + std::string(gpuFill ? "GPU volume (its CPU mirror)" : "CPU reference") + " caps at " + std::to_string(cap); return 0; }
    if (be == "gpu" && !gpuReady()) { if (err) *err = "no GPU backend: WaterCoreGpu was not initialised (no device, or a kernel failed to load) - the CPU reference is the fallback, say so"; return 0; }

    if (transport == "flip" && cells * static_cast<size_t>(FlipTransport::kParticlesPerCell) > FlipTransport::kMaxParticlesPerVolume) {
        if (err) *err = "a FLIP volume of " + std::to_string(cells) + " cells could hold " + std::to_string(cells * FlipTransport::kParticlesPerCell) + " particles; the CPU reference caps at " + std::to_string(FlipTransport::kMaxParticlesPerVolume);
        return 0;
    }
    using PClock = std::chrono::steady_clock;   // A2f: the parts' times (m_createSplit; a wake copies them)
    const auto pc0 = PClock::now();
    auto av = std::make_unique<Av>();
    av->id = m_nextId++;
    av->h = h; av->per = per; av->minVoxel = lo; av->maxVoxel = hi; av->transport = transport;
    GridSpec spec; spec.origin = lo * per; spec.dims = dims; spec.h = h;
    av->grid = std::make_unique<WaterGrid>(spec);
    av->occCache.assign(cells, Occ::Air);
    Av* raw = av.get();
    SolidQuery q = [raw](const glm::ivec3& cellWorld) -> Occ {
        const glm::ivec3 c = cellWorld - raw->grid->spec().origin;
        if (!raw->grid->inBounds(c.x, c.y, c.z)) return Occ::Solid;
        return raw->occCache[raw->grid->idx(c.x, c.y, c.z)];
    };
    // 39 (A2f): no first refreshSolids - refreshOccupancy below writes the grid's occupancy, and a new grid has no water or velocity
    // for the solid cells and faces to clear (WaterWakeSame pins the volume cell for cell)
    av->solver = std::make_unique<WaterSolver>(*av->grid, q, SolverParams{}, false);
    av->solver->setPondFriction(m_pondFriction);   // 24.7: the session's setting, not just the default
    av->solver->setVolumeCorrection(m_vcTime, m_vcBand);   // 26.21: likewise
    if (transport == "flip") av->solver->setTransport(std::make_unique<FlipTransport>());   // empty until water is placed
    av->occDirty = true;
    av->backend = be;
    // SOR needs O(N) sweeps for a Poisson problem N cells across: 1.5 x the longest grid dimension
    // (40 on the 1 m Basin, 117 at 1/3 m where 40 left a residual of 0.48 and 100 gave 5e-4 for
    // +0.2 ms, 2026-10-08). An explicit `sweeps` overrides; both are clamped and echoed. 26.23: the automatic count stops at 40
    // (kGpuSweepsAutoMax - past it free-surface water gains nothing and lidded water too little).
    const int longest = std::max({dims.x, dims.y, dims.z});
    const int autoSweeps = static_cast<int>(std::lround(1.5 * longest));
    av->gpuSweeps = gpuSweeps > 0 ? std::clamp(gpuSweeps, kGpuSweepsMin, kGpuSweepsMax) : std::clamp(autoSweeps, kGpuSweepsMin, kGpuSweepsAutoMax);   // 26.23
    const auto pc1 = PClock::now();
    refreshOccupancy(*av);
    const auto pc2 = PClock::now();
    // 39 (A2f step 1a): the refreshSolids that stood here asked the query again for every cell (19 ms on an 8 m pit) for the
    // occupancy refreshOccupancy has just mirrored into the grid, then cleared water and velocities a new grid does not have yet -
    // nothing to do (WaterWakeSame pins the volume cell for cell)
    const auto pc3 = PClock::now();
    if (be == "gpu") {
        // the grid's occupancy must hold the hold boundary the GPU reads (it is the solver's query on the CPU)
        for (int z = 0; z < av->grid->nz(); ++z) for (int y = 0; y < av->grid->ny(); ++y) for (int x = 0; x < av->grid->nx(); ++x)
            av->grid->occ(x, y, z) = av->occCache[av->grid->idx(x, y, z)];
        // 26.12 P2b: a fill volume is SPARSE - it stores and steps only the bricks holding water and their halo (26.6), bit-identical
        // to keeping every brick (WaterBrickTest); the particle transport stays dense (26.6 point 7)
        const auto pg0 = PClock::now();
        av->gpuVol = m_gpu->createVolume(av->grid->spec(), err, transport == "flip", 0, BrickOptions{transport != "flip", 0, false, brickHeight, groundBricks});   // 26.17: the brick height (refused with the reason unless 2 / 4 / 8)
        if (!av->gpuVol) return 0;   // the refusal carries the byte count
        const auto pg1 = PClock::now();
        m_gpu->upload(*av->gpuVol, *av->grid);
        m_createGpuSplit = {std::chrono::duration<double, std::milli>(pg0 - pc3).count(), std::chrono::duration<double, std::milli>(pg1 - pg0).count(),
                            std::chrono::duration<double, std::milli>(PClock::now() - pg1).count()};   // A2f: occ copy / createVolume / upload
        av->gpuDirty = false;
    }
    {   auto ms = [](PClock::time_point x, PClock::time_point y) { return std::chrono::duration<double, std::milli>(y - x).count(); };
        m_createSplit = {ms(pc0, pc1), ms(pc1, pc2), ms(pc2, pc3), ms(pc3, PClock::now()), static_cast<long>(cells)}; }
    m_avs.push_back(std::move(av));
    refreshBoxes();
    if (s_partialWakeDefault && raw->gpuVol && m_gpu && raw->gpuVol->sparse) m_gpu->setPartialWake(*raw->gpuVol, true);   // 26.31
    raw->tallBand = s_tallBandDefault;   // 27.22
    raw->recordTiles = s_recordTilesDefault && raw->gpuVol && raw->gpuVol->sparse;   // §27 W1b
    return raw->id;
}

bool WaterCoreManager::destroy(int id) {
    auto it = std::find_if(m_avs.begin(), m_avs.end(), [id](const std::unique_ptr<Av>& a) { return a->id == id; });
    if (it == m_avs.end()) return false;
    for (const Droplet& d : m_pool.take(id)) m_dstats.destroyedM3 += d.volume;   // 21: discarded with the volume's water
    if ((*it)->gpuVol && m_gpu) m_gpu->destroyVolume((*it)->gpuVol);
    m_avs.erase(it);
    m_fields.clear(); for (const auto& a : m_avs) m_fields.push_back(&a->field);   // 26.30 step 3: no pointer to the destroyed field survives
    refreshBoxes();
    return true;
}

WaterCoreManager::RemoveResult WaterCoreManager::remove(int id, bool discard) {
    RemoveResult out;
    Av* av = nullptr;
    for (auto& a : m_avs) if (a->id == id) av = a.get();
    if (!av) { out.error = "no such volume"; return out; }
    const glm::ivec3 lo = av->minVoxel, hi = av->maxVoxel;
    if (!m_spanWriter) {   // no record in this world: the volume's water has nowhere to go
        syncFromGpu(*av, false);
        out.discardedM3 = av->grid->totalMass();
        out.ok = destroy(id);
        return out;
    }
    if (!discard) {
        const double recorded = av->recordedM3;   // the tiles already in the record stay there
        const WriteBackRecord rec = sleep(id, true);
        if (!rec.ok) { out.error = "write-back failed: " + rec.error + " (volume kept)"; return out; }
        out.ok = true; out.keptM3 = rec.massStored + recorded; out.heldM3 = rec.heldMass; out.unwrittenColumns = rec.unwrittenColumns;
        return out;
    }
    // discard: the cells, the droplets in flight and every run the record holds in the box
    syncFromGpu(*av, false);
    double cells = av->grid->totalMass();
    const double recorded = av->recordedM3;   // the books of a live volume: its cells + its recorded tiles (the spans under its
                                              // awake columns are the copy it was seeded from - cleared below, not counted twice)
    for (const Droplet& d : m_pool.take(id)) { cells += d.volume; m_dstats.destroyedM3 += d.volume; }
    std::vector<WaterBodyTable::ColumnMass> before, written;
    double spans = 0.0;
    if (m_spanReader) {
        std::vector<ColumnRuns> prior;
        m_spanReader(lo, hi, prior);
        for (const auto& c : prior) {
            WaterBodyTable::ColumnMass m; m.x = c.x; m.z = c.z;
            for (const auto& r : c.runs) m.mass += static_cast<double>(r.topY - r.bottomY);
            spans += m.mass; before.push_back(m);
            WaterBodyTable::ColumnMass e; e.x = c.x; e.z = c.z; written.push_back(e);   // written empty: the body is debited its column
        }
    }
    std::vector<ColumnRuns> empty;
    for (int z = lo.z; z <= hi.z; ++z) for (int x = lo.x; x <= hi.x; ++x) { ColumnRuns c; c.x = x; c.z = z; empty.push_back(std::move(c)); }
    long unwritten = 0, chunks = 0;
    writeSpans(empty, static_cast<float>(lo.y), static_cast<float>(hi.y + 1), &unwritten, &chunks);
    if (unwritten > 0) { out.error = std::to_string(unwritten) + " column(s) of the record could not be cleared (non-resident chunk); volume kept"; return out; }
    m_bodies.credit(written, before, m_bakeBodyAt);
    destroy(id);
    buildRecordTilesNow(lo, hi);
    (void)spans;
    out.ok = true; out.discardedM3 = cells + recorded;
    return out;
}

void WaterCoreManager::refreshBoxes() {
    m_boxes.clear();
    for (const auto& av : m_avs) m_boxes.emplace_back(av->minVoxel, av->maxVoxel);
    ++m_avRevision;
}

AvRecord WaterCoreManager::record(const Av& av) const {
    AvRecord r;
    r.id = av.id; r.minVoxel = av.minVoxel; r.maxVoxel = av.maxVoxel; r.cellSize = av.h; r.transport = av.solver->transport().name();
    r.particles = av.solver->transport().particleCount();
    r.backend = av.backend; r.rbgsResidual = av.gpuLast.rbgsResidualMax; r.gpuSweeps = av.backend == "gpu" ? av.gpuSweeps : 0; r.gpuMs = av.gpuLast.gpuMs; r.gpuDeviceMs = av.gpuLast.deviceMs; r.partialWake = av.gpuVol && av.gpuVol->partialWake; r.tallBandM = av.tallBand; r.tallActive = av.gpuVol && av.gpuVol->tallActive; r.tallColumns = av.gpuVol ? av.gpuVol->tallColumns : 0; r.tallRegrids = av.gpuVol ? av.gpuVol->tallRegrids : 0; r.tallReason = av.tallReason; r.tallOffs = av.tallOffs; r.writesInPlace = av.writesInPlace; r.writesRoundTrip = av.writesRoundTrip; r.tallLastOff = av.tallLastOff; r.tallRestY = std::isnan(av.tallRestY) ? 0.0f : av.tallRestY; r.tallFloorY = (av.gpuVol && av.gpuVol->tallY >= 0) ? (static_cast<float>(av.grid->spec().origin.y + av.gpuVol->tallY) + 1.0f) * av.h : 0.0f; r.recordTiles = av.recordTiles; r.recordedM3 = av.recordedM3; r.recordedTiles = static_cast<int>(av.recordedTileList.size()); r.tilesRecorded = av.tilesRecorded; r.tilesWoken = av.tilesWoken; r.tilesWokenByWrite = av.tilesWokenByWrite; r.gpuWakeAll = av.gpuVol ? av.gpuVol->wakeAllCount : 0; r.recordSpreadMmMax = av.recordSpreadMmMax; r.recordJumpMmMax = av.recordJumpMmMax; r.lastRecordMs = av.lastRecordMs; r.lastWakeMs = av.lastWakeMs; r.seedUnplacedM3 = av.gpuVol ? av.gpuVol->seedUnplacedM3 : 0.0; r.awakeColumns = av.gpuLast.awakeColumns; r.frozenColumns = av.gpuLast.frozenColumns; r.movingColumns = av.gpuLast.movingColumns; r.stepSlots = av.gpuLast.stepSlots; r.partitionMoves = av.gpuLast.partitionMoves; r.carryBodyM3 = av.carryBodyM3; r.carryRateM3 = av.carryRateM3; r.carryFlowM3 = av.carryFlowM3; r.carryParkedM3 = av.carryParkedM3; r.gpuHostMs = av.gpuLast.hostResidencyMs; r.gpuWaitMs = av.gpuLast.submitWaitMs; r.gpuTicks = av.gpuLast.ticks; r.gpuAsync = av.gpuVol && av.gpuVol->async; r.gpuBricks = av.gpuLast.bricks; r.gpuDispatches = av.gpuVol ? av.gpuVol->lastDispatches : 0; r.gpuPoolGrows = av.gpuVol ? av.gpuVol->poolGrows : 0; r.gpuReduceSlots = av.gpuLast.reduceSlots; r.gpuReduceCached = av.gpuLast.reduceCached; r.gpuBarriers = av.gpuVol ? av.gpuVol->lastBarriers : 0; r.gpuBrickCapacity = av.gpuLast.capacity; r.gpuBrickHeight = av.gpuVol ? av.gpuVol->by : 0; r.gpuGroundBricks = av.gpuLast.groundBricks; r.gpuUploadMs = av.gpuUploadMs; r.gpuUploads = av.gpuUploads; r.occRefreshMs = av.occRefreshMs;
    r.occFullRefreshes = av.occFullRefreshes; r.occLocalRefreshes = av.occLocalRefreshes; r.occCellsResampled = av.occCellsResampled;
    r.occCellsChanged = av.occCellsChanged; r.gpuPatchMs = av.gpuPatchMs; r.gpuPatches = av.gpuPatches;
    r.cells = av.grid->cellCount();
    r.autoSleep = av.autoSleep; r.seededMass = av.seededMass;
    r.surfaceAgeMs = av.fieldStepSec < 0.0 ? -1.0 : (std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count() - av.fieldStepSec) * 1000.0;
    // a GPU volume's sleep lives in WaterCoreGpu::Volume (the solver never stepped): this line used to
    // sit BEFORE the GPU override and overwrote it, so S1 on the GPU read asleep=false for ever with
    // quiet_ticks at 30 and no substeps (2026-10-08)
    r.asleep = av.backend == "gpu" ? av.gpuLast.asleep : av.solver->asleep();
    r.mass = av.solver->transport().ownsMass() ? av.solver->transport().ownedMass() : ((av.backend == "gpu" && av.gpuStale) ? av.gpuLast.totalMass : av.grid->totalMass());
    r.kineticEnergy = av.grid->kineticEnergy(); r.lastSubsteps = av.last.substeps; r.lastPcgIterations = av.last.pcgIterations;
    r.lastPcgResidual = av.last.pcgResidual; r.quietTicks = av.last.quietTicks; r.sourceUnplaced = av.last.sourceUnplaced; r.residueDropped = av.residueDropped;
    for (const auto& src : av.solver->sources()) {
        r.sourcePlaced += src.placedTotal; ++r.sourceCount;
        if (src.outfall) { r.outfallOut -= src.placedTotal; ++r.outfalls; } else if (src.placedTotal > 0.0) r.sourceIn += src.placedTotal;
    }
    r.manningN = av.solver->params().bedManning;
    return r;
}

std::vector<AvRecord> WaterCoreManager::list() const {
    std::vector<AvRecord> out;
    for (const auto& av : m_avs) out.push_back(record(*av));
    return out;
}

const AvRecord* WaterCoreManager::find(int id) const {
    static AvRecord s_rec;
    for (const auto& av : m_avs) if (av->id == id) { s_rec = record(*av); return &s_rec; }
    return nullptr;
}

Occ WaterCoreManager::sampleOccupancy(const Av& av, const glm::ivec3& cellLocal) const {
    if (!m_state) return Occ::Air;
    // World extent of the cell in micro units (9 per voxel). A cell coarser than a micro cell is
    // sampled on a 3x3x3 lattice inside it: Solid if any sample is solid (a wall a third of a
    // voxel thick must block a 1-voxel water cell), Unknown if any sample is unknown and none
    // solid, else Air. A cell finer than a micro cell samples the micro cell holding its centre.
    const glm::ivec3 cellWorld = av.grid->spec().origin + cellLocal;
    const double h = av.h;
    const double x0 = cellWorld.x * h, y0 = cellWorld.y * h, z0 = cellWorld.z * h;
    bool anyUnknown = false;
    if (h <= 1.0 / kMicroPerVoxel + 1e-6) {
        const glm::ivec3 micro(static_cast<int>(std::floor((x0 + 0.5 * h) * kMicroPerVoxel)),
                               static_cast<int>(std::floor((y0 + 0.5 * h) * kMicroPerVoxel)),
                               static_cast<int>(std::floor((z0 + 0.5 * h) * kMicroPerVoxel)));
        const int s = m_state(micro);
        return s == 1 ? Occ::Solid : (s == 2 ? Occ::Unknown : Occ::Air);
    }
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) for (int k = 0; k < 3; ++k) {
        const double fx = (i + 0.5) / 3.0, fy = (j + 0.5) / 3.0, fz = (k + 0.5) / 3.0;
        const glm::ivec3 micro(static_cast<int>(std::floor((x0 + fx * h) * kMicroPerVoxel)),
                               static_cast<int>(std::floor((y0 + fy * h) * kMicroPerVoxel)),
                               static_cast<int>(std::floor((z0 + fz * h) * kMicroPerVoxel)));
        const int s = m_state(micro);
        if (s == 1) return Occ::Solid;
        if (s == 2) anyUnknown = true;
    }
    return anyUnknown ? Occ::Unknown : Occ::Air;
}

void WaterCoreManager::refreshOccupancy(Av& av) {
    // The per-voxel edit callback does not fire for every path that changes solids (a bulk
    // /api/world/clear dug a hole the live volume never saw: S5, 2026-10-08), so the cache is also
    // keyed to the occupancy pool's pack revision.
    if (m_revision) {
        const uint64_t rev = m_revision();
        if (rev != av.occRevision) {
            const uint64_t since = av.occRevision;
            av.occRevision = rev;
            // 26.19: the cache is current up to `since` - re-read only where the pool says solids changed after it. Anything the
            // list cannot vouch for (no list, history gone, pool full) is the whole box, as before.
            std::vector<SolidsChangeBox> boxes;
            if (!av.occDirty && since != ~0ull && m_changes && !s_forceFullRefreshForTest && m_changes(since, boxes)) {
                refreshOccupancyLocal(av, boxes);
                return;
            }
            av.occDirty = true; av.solver->wake();
        }
    }
    if (!av.occDirty) return;
    const auto tOcc = std::chrono::steady_clock::now();   // 26.18: measured - it runs on every world change
    const WaterGrid& g = *av.grid;
    auto slab = [&](int z0, int z1) {
        for (int z = z0; z < z1; ++z)
            for (int y = 0; y < g.ny(); ++y)
                for (int x = 0; x < g.nx(); ++x)
                    av.occCache[g.idx(x, y, z)] = sampleOccupancy(av, glm::ivec3(x, y, z));
    };
    // 39 (A2f step 1b): split by z slabs over the workers (each cell its own slot - the same result); a small box stays on this thread
    const int workers = (m_occThreads > 1 && g.cellCount() >= 32768) ? std::min(m_occThreads, g.nz()) : 1;
    if (workers == 1) slab(0, g.nz());
    else {
        std::vector<std::thread> pool;
        for (int i = 1; i < workers; ++i) pool.emplace_back(slab, g.nz() * i / workers, g.nz() * (i + 1) / workers);
        slab(0, g.nz() / workers);
        for (auto& t : pool) t.join();
    }
    av.occDirty = false;
    // the GPU reads occupancy from the grid, and so does the Phase F surface extraction: mirror the cache
    for (int z = 0; z < av.grid->nz(); ++z) for (int y = 0; y < av.grid->ny(); ++y) for (int x = 0; x < av.grid->nx(); ++x)
        av.grid->occ(x, y, z) = av.occCache[av.grid->idx(x, y, z)];
    if (av.gpuVol) av.gpuDirty = true;
    av.occRefreshMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tOcc).count();
    ++av.occFullRefreshes; av.occCellsResampled = static_cast<long>(av.occCache.size()); av.occCellsChanged = -1;   // -1: not compared
}

void WaterCoreManager::refreshOccupancyLocal(Av& av, const std::vector<SolidsChangeBox>& boxes) {
    const auto t0 = std::chrono::steady_clock::now();
    WaterGrid& g = *av.grid;
    const glm::ivec3 o = g.spec().origin, n(g.nx(), g.ny(), g.nz());
    const int per = av.per;   // cells per voxel per axis (cells are 1 / per of a voxel: exact integer ranges)
    std::vector<int> changed;
    long resampled = 0;
    for (const SolidsChangeBox& b : boxes) {
        // the cells that overlap world voxels [lo, hi + 1), padded one cell each side (margin, not a dependency: a cell samples
        // only the voxels it overlaps)
        glm::ivec3 lo, hi;
        for (int a = 0; a < 3; ++a) {
            lo[a] = b.lo[a] * per - o[a] - 1;
            hi[a] = (b.hi[a] + 1) * per - 1 - o[a] + 1;
            lo[a] = std::max(lo[a], 0); hi[a] = std::min(hi[a], n[a] - 1);
        }
        if (lo.x > hi.x || lo.y > hi.y || lo.z > hi.z) continue;   // not in this volume
        for (int z = lo.z; z <= hi.z; ++z) for (int y = lo.y; y <= hi.y; ++y) for (int x = lo.x; x <= hi.x; ++x) {
            const size_t i = g.idx(x, y, z);
            const Occ s = sampleOccupancy(av, glm::ivec3(x, y, z));
            ++resampled;
            if (s == av.occCache[i]) continue;
            av.occCache[i] = s; g.occ(x, y, z) = s;
            changed.push_back(static_cast<int>(i));
        }
    }
    ++av.occLocalRefreshes; av.occCellsResampled = resampled; av.occCellsChanged = static_cast<long>(changed.size());
    av.occRefreshMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    av.gpuPatchMs = 0.0;
    if (changed.empty()) return;   // a remesh, a far edit, a chunk streamed beside the box: nothing for this water
    av.solver->wake();
    // a pending full upload carries the grid anyway; otherwise patch the device in place (no read-back, no re-upload)
    if (av.gpuVol && m_gpu && !av.gpuDirty) {
        const auto tp = std::chrono::steady_clock::now();
        m_gpu->updateOccupancy(*av.gpuVol, g, changed);
        av.gpuPatchMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tp).count();
        ++av.gpuPatches;
        av.gpuLast.asleep = false;
    }
}

void WaterCoreManager::markSolidsDirty() {
    for (auto& av : m_avs) { av->occDirty = true; av->solver->wake(); }
}

bool WaterCoreManager::step(int id, int ticks, float dt, AvRecord* out) {
    for (auto& av : m_avs) {
        if (av->id != id) continue;
        refreshOccupancy(*av);
        if (av->backend == "gpu") { if (av->gpuVol && m_gpu) m_gpu->setDroplets(*av->gpuVol, m_dstats.on); stepGpu(*av, ticks, dt); ++av->stepCount; collectBirths(*av, dt); }
        else for (int i = 0; i < ticks; ++i) { av->last = av->solver->step(dt); av->residueDropped += av->last.residueDropped; ++av->stepCount; collectBirths(*av, dt); }
        stepDroplets(dt * static_cast<float>(ticks));
        stepRipples(dt * static_cast<float>(ticks));
        if (out) *out = record(*av);
        return true;
    }
    return false;
}

void WaterCoreManager::update(float dt) {
    checkRecordResidency();   // 32.4: before the realtime return - a harness that steps by hand still follows residency
    if (!m_realtime) return;
    using UClock = std::chrono::steady_clock;
    auto ums = [](UClock::time_point a) { return std::chrono::duration<double, std::milli>(UClock::now() - a).count(); };
    m_updateCpu = UpdateCpu{};
    ++WaterHostProfile::frames;   // 27.28
    int ticks = 1; float tickDt = dt;   // 27.26: the fixed water tick
    if (s_fixedTickS > 0.0f) {
        m_tickAccum += dt;
        ticks = static_cast<int>(std::floor(m_tickAccum / s_fixedTickS));
        if (ticks > kMaxTicksPerFrame) { m_ticksDropped += ticks - kMaxTicksPerFrame; ticks = kMaxTicksPerFrame; m_tickAccum = 0.0; }
        else m_tickAccum -= ticks * static_cast<double>(s_fixedTickS);
        tickDt = s_fixedTickS;
    }
    for (auto& av : m_avs) {
        if (ticks == 0) break;   // 27.26: no water tick due this frame - nothing steps (droplets and ripples still run on the frame)
        { const auto t = UClock::now(); refreshOccupancy(*av); m_updateCpu.occupancy += ums(t); }
        const auto t = UClock::now();
        if (av->backend == "gpu") {
            if (av->gpuVol && m_gpu) m_gpu->setDroplets(*av->gpuVol, m_dstats.on);
            if (!av->gpuLast.asleep || (av->gpuVol && !av->gpuVol->asleep)) {
                { const auto tc = UClock::now(); stepGpu(*av, ticks, tickDt); m_updateCpu.stepCall += ums(tc); }
                ++av->stepCount;
                { const auto tb = UClock::now(); collectBirths(*av, ticks * tickDt); m_updateCpu.births += ums(tb); }
            } else {
                recordTilesStep(*av, dt);   // §27 W1b: an asleep body steps nothing, but its frozen tiles still count toward the record
                if (av->tallBand > 0.0f && std::isnan(av->tallRestY)) captureTallRest(*av);   // 27.22: the rest level, read while it sleeps
            }
        }
        else if (!av->solver->asleep()) {
            for (int t = 0; t < ticks && !av->solver->asleep(); ++t) { av->last = av->solver->step(tickDt); av->residueDropped += av->last.residueDropped; }
            ++av->stepCount; collectBirths(*av, ticks * tickDt);
        }
        m_updateCpu.step += ums(t);
    }
    { const auto t = UClock::now(); stepDroplets(dt); m_updateCpu.droplets = ums(t); }   // 21: flight + landing (deposits reach a GPU volume at its next step)
    { const auto t = UClock::now(); stepRipples(dt); m_updateCpu.ripples = ums(t); }     // 22: the sub-cell wave layer
    const auto tSleep = UClock::now();
    // Phase D (16.2): a volume that slept this tick writes back and frees itself (auto_sleep). Ids
    // first - sleep() destroys. A refused write-back (held or non-resident columns) leaves the
    // volume alive and is reported through the drained records, never silent.
    std::vector<int> slept;
    const double nowS = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    for (const auto& av : m_avs) if (av->autoSleep && isAsleep(*av) && !m_pool.anyFor(av->id) && !(av->ripple && !av->ripple->asleep()) && nowS >= av->noSleepBefore) slept.push_back(av->id);   // 32: not inside a woken volume's grace   // 21: not with droplets in flight; 22 (F5): nor with rings running
    for (int id : slept) {
        WriteBackRecord rec = sleep(id, false);
        if (!rec.ok) { for (auto& av : m_avs) if (av->id == id) av->autoSleep = false; }   // once: do not retry every tick
        m_autoSlept.push_back(std::move(rec));
    }
    m_updateCpu.sleep = ums(tSleep);
}

bool WaterCoreManager::unitMap(int id, std::vector<glm::vec4>& units, glm::ivec3& dims, glm::ivec3& cellOffset, float& h) const {
    for (const auto& av : m_avs) {
        if (av->id != id) continue;
        if (!av->gpuVol || !m_gpu) return false;
        h = av->h;
        return m_gpu->readUnits(*av->gpuVol, units, dims, cellOffset);
    }
    return false;
}
void WaterCoreManager::setUnitCapture(bool on) { WaterCoreGpu::s_unitCapture = on; }
void WaterCoreManager::setKernelTiming(bool on, bool reset) {
    WaterCoreGpu::s_kernelTiming = on;
    if (reset) { for (double& m : WaterCoreGpu::s_tsMs) m = 0.0; WaterCoreGpu::s_tsCalls = 0; }
}
std::vector<std::pair<std::string, double>> WaterCoreManager::kernelTiming(long& calls) {
    static const char* names[] = {"start", "residency", "sources", "transport", "vel_advect", "compaction", "forces", "classify", "sweeps",
                                  "vel_update", "extrapolate", "settle", "sweep_births", "brick_record", "reduce", "surface_kernel", "surface_copy", "reduce_surface"};
    static_assert(sizeof(names) / sizeof(names[0]) == WaterCoreGpu::TsLabelCount, "one name per label");
    calls = WaterCoreGpu::s_tsCalls;
    std::vector<std::pair<std::string, double>> out;
    for (int k = 1; k < WaterCoreGpu::TsLabelCount; ++k) out.push_back({names[k], WaterCoreGpu::s_tsMs[k]});
    return out;
}
bool WaterCoreManager::unitCapture() { return WaterCoreGpu::s_unitCapture; }

bool WaterCoreManager::setAutoSleep(int id, bool on) {
    for (auto& av : m_avs) if (av->id == id) { av->autoSleep = on; return true; }
    return false;
}

double WaterCoreManager::seedFromSpans(int id) {
    if (!m_spanReader) return -1.0;
    for (auto& av : m_avs) {
        if (av->id != id) continue;
        using SClock = std::chrono::steady_clock;   // A2f: the parts' times (m_seedSplit)
        auto ms = [](SClock::time_point x, SClock::time_point y) { return std::chrono::duration<double, std::milli>(y - x).count(); };
        const auto s0 = SClock::now();
        std::vector<ColumnRuns> runs;
        m_spanReader(av->minVoxel, av->maxVoxel, runs);
        const auto s1 = SClock::now();
        // §27.8: the seed fills AIR cells only - the grid's occupancy must be the world's now (the GPU path mirrors it at create,
        // a CPU volume's grid never did)
        refreshOccupancy(*av);
        for (int z = 0; z < av->grid->nz(); ++z) for (int y = 0; y < av->grid->ny(); ++y) for (int x = 0; x < av->grid->nx(); ++x)
            av->grid->occ(x, y, z) = av->occCache[av->grid->idx(x, y, z)];
        const auto s2 = SClock::now();
        const double placed = seedGridFromRuns(*av->grid, runs);
        m_seedSplit = {ms(s0, s1), ms(s1, s2), ms(s2, SClock::now())};
        av->seededMass = placed;
        av->seededColumns.clear();
        for (const auto& c : runs) { WaterBodyTable::ColumnMass m; m.x = c.x; m.z = c.z; for (const auto& r : c.runs) { m.mass += r.mass; m.top = std::max(m.top, r.topY); } av->seededColumns.push_back(m); }
        av->solver->wake();
        av->gpuDirty = true; av->gpuStale = false;
        if (av->gpuVol) av->gpuLast = GpuStepStats{};
        return placed;
    }
    return -1.0;
}

WriteBackRecord WaterCoreManager::sleep(int id, bool force) {
    WriteBackRecord rec; rec.id = id; rec.forced = force;
    Av* av = nullptr;
    for (auto& a : m_avs) if (a->id == id) av = a.get();
    if (!av) { rec.error = "no such volume"; return rec; }
    if (!m_spanWriter) { rec.error = "no span writer installed (the application owns the chunks)"; return rec; }
    refreshOccupancy(*av);
    syncFromGpu(*av, false);   // forced: never the rate-limited mirror, or the record is a second old
    {   // 21: in-flight droplets land now, into the grid the write-back reads (sleep must not lose them)
        const std::vector<Droplet> flying = m_pool.take(id);
        for (const Droplet& d : flying) {
            const double left = depositIntoGrid(*av->grid, d.pos, d.volume);
            m_dstats.flushedM3 += d.volume - left;
            if (left > 0.0) { DropletBirth b; b.pos = d.pos; b.volume = static_cast<float>(left); m_pool.spawn(b, id, av->h, m_dstats.size, m_dropletTick); }
        }
        if (!flying.empty()) { av->solver->wake(); if (av->gpuVol) av->gpuDirty = true; }
    }
    if (!isAsleep(*av) && !force) {
        const int q = av->backend == "gpu" ? av->gpuLast.quietTicks : av->last.quietTicks;
        rec.error = "volume is awake (quiet_ticks " + std::to_string(q) + "/" + std::to_string(av->solver->params().restTicks) + "); force:true writes a snapshot";
        return rec;
    }
    // held columns: any Unknown occupancy in the column's cells (the 5.1 hold boundary)
    std::vector<uint8_t> heldCol(static_cast<size_t>((av->maxVoxel.x - av->minVoxel.x + 1) * (av->maxVoxel.z - av->minVoxel.z + 1)), 0);
    const int cw = av->maxVoxel.x - av->minVoxel.x + 1;
    for (int z = 0; z < av->grid->nz(); ++z) for (int y = 0; y < av->grid->ny(); ++y) for (int x = 0; x < av->grid->nx(); ++x)
        if (av->occCache[av->grid->idx(x, y, z)] == Occ::Unknown) heldCol[static_cast<size_t>((z / av->per) * cw + (x / av->per))] = 1;
    auto held = [&](int vx, int vz) { return heldCol[static_cast<size_t>((vz - av->minVoxel.z) * cw + (vx - av->minVoxel.x))] != 0; };
    std::vector<ColumnRuns> runs;
    const WriteBackStats st = columnRunsFromGrid(*av->grid, held, runs);
    // §27 W1b: a recorded tile's columns are already in the record (the volume holds no cells there) - writing the grid's empty
    // columns over them would erase that water
    runs.erase(std::remove_if(runs.begin(), runs.end(), [&](const ColumnRuns& c) { return columnRecorded(*av, c.x, c.z); }), runs.end());
    rec.columns = st.columns; rec.runs = st.runs; rec.heldColumns = st.heldColumns; rec.heldMass = st.heldMass;
    rec.massWritten = st.mass; rec.thinDropped = st.thinDropped; rec.surfaceVsMassMm = st.surfaceVsMassMmMax; rec.spreadMm = st.spreadMmMax; rec.massSeeded = av->seededMass;
    long unwritten = 0, chunks = 0;
    const float yLo = static_cast<float>(av->minVoxel.y), yHi = static_cast<float>(av->maxVoxel.y + 1);
    if (st.heldColumns > 0 && !force) {
        rec.unwrittenColumns = st.heldColumns;
        rec.error = std::to_string(st.heldColumns) + " column(s) hold Unknown occupancy (chunk not resident / outside the occupancy window): nothing written, volume kept; force:true writes the rest";
        return rec;
    }
    // what the written columns held before (span unit), for the body delta
    std::vector<WaterBodyTable::ColumnMass> before;
    if (m_spanReader) {
        std::vector<ColumnRuns> prior;
        m_spanReader(av->minVoxel, av->maxVoxel, prior);
        for (const auto& c : prior) { WaterBodyTable::ColumnMass m; m.x = c.x; m.z = c.z; for (const auto& r : c.runs) m.mass += static_cast<double>(r.topY - r.bottomY); before.push_back(m); }
    }
    writeSpans(runs, yLo, yHi, &unwritten, &chunks);
    rec.unwrittenColumns = st.heldColumns + unwritten; rec.chunksTouched = chunks;
    if (unwritten > 0 && !force) {
        // the writer refuses whole columns atomically (a non-resident vertical chunk): nothing of
        // those columns was written; the rest was. Report, keep the volume so nothing is lost.
        rec.error = std::to_string(unwritten) + " column(s) could not be written (non-resident chunk); the others were; volume kept";
        return rec;
    }
    // Tier A credit, per column
    std::vector<WaterBodyTable::ColumnMass> written;
    // the record is credited in the SPANS' unit (float32 top - bottom, what the chunk stores), not the
    // cells' double mass: P3 says A.mass = sum of B over the body's columns, and the seed side (the
    // reader) is in that unit too, so a wake -> sleep round trip books exactly zero (S11's restart
    // leg read a -2.5e-6 drift from mixing the units, 2026-10-08)
    for (const auto& c : runs) { if (c.held || c.runs.empty()) continue; WaterBodyTable::ColumnMass m; m.x = c.x; m.z = c.z; for (const auto& r : c.runs) { m.mass += static_cast<double>(r.topY - r.bottomY); m.top = std::max(m.top, r.topY); } written.push_back(m); rec.massStored += m.mass; }
    rec.bodyId = m_bodies.credit(written, before, m_bakeBodyAt);
    rec.ok = true;
    const glm::ivec3 lo = av->minVoxel, hi = av->maxVoxel;
    if (const AvRecord* ar = find(id)) {   // 32: remembered - a disturbance inside the box wakes it from the record
        SleptVolume sv; sv.lo = lo; sv.hi = hi; sv.cellSize = ar->cellSize; sv.transport = av->transport; sv.backend = ar->backend;
        sv.sweeps = ar->gpuSweeps; sv.partialWake = ar->partialWake; sv.recordTiles = ar->recordTiles; sv.tallBandM = ar->tallBandM;
        m_slept.erase(std::remove_if(m_slept.begin(), m_slept.end(), [&](const SleptVolume& s) { return s.lo == lo && s.hi == hi; }), m_slept.end());
        m_slept.push_back(std::move(sv));
    }
    destroy(id);
    buildRecordTilesNow(lo, hi);   // §27 W1a: the record draws these columns in the frame the volume stops drawing them
    return rec;
}

const WaterCoreManager::Av* WaterCoreManager::volumeAtVoxel(int x, int y, int z) const {
    for (const auto& av : m_avs)
        if (x >= av->minVoxel.x && x <= av->maxVoxel.x && y >= av->minVoxel.y && y <= av->maxVoxel.y && z >= av->minVoxel.z && z <= av->maxVoxel.z)
            return av.get();
    return nullptr;
}

long WaterCoreManager::placeBox(const glm::ivec3& minVoxel, const glm::ivec3& maxVoxel, float fill, long* outsideCells) {
    const glm::ivec3 lo = glm::min(minVoxel, maxVoxel), hi = glm::max(minVoxel, maxVoxel);
    for (auto& avp : m_avs) {   // §27 W1b: placed over the current state - a recorded tile's water comes back into cells first
        const glm::ivec3 a = glm::max(lo, avp->minVoxel), b = glm::min(hi, avp->maxVoxel);
        if (a.x > b.x || a.y > b.y || a.z > b.z) continue;
        const glm::vec3 c = (glm::vec3(a) + glm::vec3(b) + glm::vec3(1.0f)) * 0.5f;
        wakeRecordedNear(*avp, c, 0.5f * glm::length(glm::vec3(b - a + glm::ivec3(1))));
    }
    long set = 0, outside = 0;
    for (int vz = lo.z; vz <= hi.z; ++vz) for (int vy = lo.y; vy <= hi.y; ++vy) for (int vx = lo.x; vx <= hi.x; ++vx) {
        const Av* cav = volumeAtVoxel(vx, vy, vz);
        if (!cav) { ++outside; continue; }
        Av* av = const_cast<Av*>(cav);
        refreshOccupancy(*av);
        syncFromGpu(*av);   // a placement writes fills over the CURRENT state
        const glm::ivec3 base = (glm::ivec3(vx, vy, vz) * av->per) - av->grid->spec().origin;
        for (int k = 0; k < av->per; ++k) for (int j = 0; j < av->per; ++j) for (int i = 0; i < av->per; ++i) {
            const int cx = base.x + i, cy = base.y + j, cz = base.z + k;
            if (!av->grid->inBounds(cx, cy, cz)) continue;
            if (av->occCache[av->grid->idx(cx, cy, cz)] != Occ::Air) continue;   // never put water in rock
            // the voxel's water lies on its FLOOR (WaterCore.md 24.11): sub-layer j from the bottom holds fill x per - j,
            // clamped - the same total. Writing `fill` into every sub-cell stacked half-full cells (0.5 / 0.5 / 0.5 for a
            // half voxel), a void under water the solver then had to collapse; once compaction stopped acting on moving
            // water (24.6) the demo pond carried it until it settled and dropped 11 cm in one jerk (owner, 2026-10-11).
            av->grid->f(cx, cy, cz) = std::clamp(std::clamp(fill, 0.0f, 1.0f) * static_cast<float>(av->per) - static_cast<float>(j), 0.0f, 1.0f);
            ++set;
        }
        // Phase B2: a particle volume is re-seeded from the fills it now shows (its own p2g field
        // plus the box just written) - mass-exact, positions re-jittered; placement is not motion
        if (av->solver->transport().ownsMass()) av->solver->transport().seed(*av->grid);
        av->solver->wake();
        av->gpuDirty = true; av->gpuLast.asleep = false;
        if (av->gpuVol) { av->gpuVol->asleep = false; av->gpuVol->quietTicks = 0; av->gpuVol->quietRef = 0.0; }
    }
    if (outsideCells) *outsideCells = outside;
    return set;
}

std::vector<std::array<float, 6>> WaterCoreManager::particleSample(int id, int max) const {
    std::vector<std::array<float, 6>> out;
    for (const auto& av : m_avs) {
        if (av->id != id || !av->solver->transport().ownsMass()) continue;
        const auto* flip = dynamic_cast<const FlipTransport*>(&av->solver->transport());
        if (!flip) break;
        const auto& ps = flip->particles();
        const size_t stride = std::max<size_t>(1, ps.size() / static_cast<size_t>(std::max(1, max)));
        const glm::vec3 origin = glm::vec3(av->grid->spec().origin);
        for (size_t i = 0; i < ps.size() && out.size() < static_cast<size_t>(max); i += stride) {
            const glm::vec3 w = (origin + ps[i].pos) * av->h;
            out.push_back({w.x, w.y, w.z, ps[i].vel.x, ps[i].vel.y, ps[i].vel.z});
        }
        break;
    }
    return out;
}

const std::vector<glm::vec4>& WaterCoreManager::particleDrawList() {
    m_particleDraw.clear();
    for (const auto& av : m_avs) {
        if (!av->solver->transport().ownsMass()) continue;
        const auto* flip = dynamic_cast<const FlipTransport*>(&av->solver->transport());
        if (!flip) continue;
        const glm::vec3 origin = glm::vec3(av->grid->spec().origin);
        for (const FlipParticle& p : flip->particles()) {
            const glm::vec3 w = (origin + p.pos) * av->h;
            m_particleDraw.emplace_back(w.x, w.y, w.z, av->h);
        }
    }
    return m_particleDraw;
}

bool WaterCoreManager::settle(int id) {
    for (auto& av : m_avs) {
        if (av->id != id) continue;
        if (!av->solver->transport().ownsMass()) return false;
        av->solver->transport().settle(*av->grid);
        av->solver->setTransport(std::make_unique<EulerianTransport>());
        av->solver->wake();
        return true;
    }
    return false;
}

bool WaterCoreManager::initGpu(VkDevice device, VkPhysicalDevice physical, VkQueue queue, uint32_t queueFamily, const std::string& shaderDir, std::string* err) {
    auto gpu = std::make_unique<WaterCoreGpu>();
    if (!gpu->init(device, physical, queue, queueFamily, shaderDir, err)) return false;
    m_gpu = std::move(gpu);
    auto rg = std::make_unique<RippleGpu>();   // 25.34: the ripple step; without it the layers step on the CPU (said in water_ripples)
    if (rg->init(device, physical, queue, queueFamily, shaderDir, &m_rgpuErr)) m_rgpu = std::move(rg);
    return true;
}

void WaterCoreManager::refreshRippleShapeStats() {
    m_rstats.maxAbs = 0.0f; m_rstats.maxSlope = 0.0f;
    for (long& b : m_rstats.slopeHist) b = 0;
    const float d = RippleLayer::kPitch;
    for (RippleLayer* Lp : liveRippleLayers()) {
        RippleLayer& L = *Lp;
        if (L.asleep()) continue;
        m_rstats.maxAbs = std::max(m_rstats.maxAbs, L.maxAbs());
        // 22.11: the facet slopes the shader draws (central difference, clamped at the layer edge), water cells only
        const auto& mask = L.mask();
        const auto hh = [&](int x, int z) { return L.heights()[L.idx(std::clamp(x, 0, L.nx() - 1), std::clamp(z, 0, L.nz() - 1))]; };
        for (int z = 0; z < L.nz(); ++z) for (int x = 0; x < L.nx(); ++x) {
            if (!mask[L.idx(x, z)]) continue;
            const float sx = (hh(x + 1, z) - hh(x - 1, z)) * 0.5f / d, sz = (hh(x, z + 1) - hh(x, z - 1)) * 0.5f / d;
            const float sl = std::sqrt(sx * sx + sz * sz);
            m_rstats.maxSlope = std::max(m_rstats.maxSlope, sl);
            int b = 0; while (b < kSlopeBins - 1 && sl >= kSlopeEdges[b]) ++b;
            ++m_rstats.slopeHist[b];
        }
    }
}

std::vector<RippleLayer*> WaterCoreManager::liveRippleLayers() {
    std::vector<RippleLayer*> out;
    for (auto& av : m_avs) if (av->ripple) out.push_back(av->ripple.get());
    return out;
}

void WaterCoreManager::setRippleGpu(bool on) {
    if (on == m_rgpuOn) return;
    if (m_rgpu && m_rgpu->ready()) {
        const auto live = liveRippleLayers();
        if (!on) {   // the CPU continues where the GPU was: everything in flight in, both levels
            m_rgpu->drain(live);
            for (RippleLayer* L : live) m_rgpu->syncPrev(*L, live);
        } else {
            for (RippleLayer* L : live) m_rgpu->markDirty(*L);   // the CPU stepped meanwhile: its levels go up first
        }
    }
    m_rgpuOn = on;
}

void WaterCoreManager::stepLayer(RippleLayer& L, float dt, const std::vector<glm::vec2>* vel) {
    if (rippleGpuOn() && L.nx() * L.nz() >= kRippleGpuMinCells) {
        std::string err;
        if (m_rgpu->record(L, dt, vel, &err) || err.empty()) return;
        m_rgpuErr = err; m_rgpuOn = false;   // refused (device memory): the CPU from here on - said in water_ripples, never silent
        const auto live = liveRippleLayers();
        for (RippleLayer* l : live) m_rgpu->syncPrev(*l, live);
        return;   // this layer's frame is spent (its accumulators ran); it steps on the CPU next frame
    }
    if (vel) L.advect(*vel, dt);
    L.step(dt);
}

// Phase C slice 2: the grid is the interface. Upload when it is newer than the GPU copy, run the
// ticks on the device (fenced), download so probes, the surface feed and the ledger keep reading
// the grid exactly as they do for the CPU reference. StepReport mirrors the GPU reductions.
void WaterCoreManager::syncFromGpu(Av& av, bool rateLimited) {
    if (!av.gpuVol || !m_gpu || !av.gpuStale) return;
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (rateLimited && av.gpuLastDownloadSec >= 0.0 && now - av.gpuLastDownloadSec < 1.0) return;   // §15.11: realtime readers see the grid once a second
    m_gpu->download(*av.gpuVol, *av.grid);
    if (av.gpuVol->sourceCount > 0) pullSourceTotals(av);   // 26.16: the grid is the newest call's (download settled it) - so are the totals
    if (av.solver->transport().ownsMass()) {
        auto* flip = dynamic_cast<FlipTransport*>(&av.solver->transport());
        if (flip) { std::vector<FlipParticle> ps; m_gpu->readParticles(*av.gpuVol, ps); flip->setParticles(std::move(ps)); }
    }
    av.gpuStale = false; av.gpuLastDownloadSec = now;
}

bool WaterCoreManager::setTallBand(int id, float metres) {
    for (auto& av : m_avs) {
        if (av->id != id) continue;
        av->tallBand = std::max(0.0f, metres);
        if (av->tallBand <= 0.0f && av->gpuVol && m_gpu) { m_gpu->clearTall(*av->gpuVol); av->tallReason = "off"; }
        av->tallRestY = std::numeric_limits<float>::quiet_NaN();
        return true;
    }
    return false;
}

// 27.22 TALL CELLS - the floor (docs/WaterCore.md 27.22 items 1-2, 7-8). The floor is the water's REST level minus the band B, rounded
// down to a brick: the rest level is taken from the surface while the volume sleeps (not from the volume's lowest surface - that
// would depend on the volume's extent and on which tiles are awake). It moves only DOWN while the water moves - here, v1: a trough
// within one brick of B above it, a source below it, a moving body, an edit under it or a column that is not open from its bed
// return every tall cell to ordinary cells, and the volume stays all fine until it sleeps again (a new rest level).
// 27.22d: the water the surface field holds (m^3): every run's height over its column - the rest rule's own measure (the same field it
// reads the level from, whatever the GPU's last stats say)
static double fieldWater(const WaterSurfaceField& f) {
    double s = 0.0;
    for (const SurfaceColumn& c : f.cols) { const int runs = std::min(static_cast<int>(c.runs + 0.5f), kSurfaceMaxRuns); for (int r = 0; r < runs; ++r) s += std::max(0.0f, c.top[r] - c.bottom[r]); }
    return s * static_cast<double>(f.h) * f.h;
}

bool WaterCoreManager::captureTallRest(Av& av) {
    const WaterSurfaceField& fld = av.field;
    if (fld.cols.size() != static_cast<size_t>(fld.nx) * fld.nz) return false;
    std::vector<float> tops;
    for (const SurfaceColumn& c : fld.cols) { const int runs = static_cast<int>(c.runs + 0.5f); if (runs >= 1) tops.push_back(c.top[std::min(runs, kSurfaceMaxRuns) - 1]); }
    if (tops.empty()) return false;
    // 27.22d: at rest = the surface FLAT (the wet tops within 5 mm) - a deep walled pond never falls asleep (27.20 #4) but is flat
    const auto mm = std::minmax_element(tops.begin(), tops.end());
    if (*mm.second - *mm.first > 0.005f) return false;
    std::nth_element(tops.begin(), tops.begin() + tops.size() / 2, tops.end());
    av.tallRestY = tops[tops.size() / 2];
    av.tallDrop = 0.0f;
    av.tallRestMass = fieldWater(fld) + av.recordedM3;   // 27.22 TC4: the record's share too (a recorded tile leaves the field)
    av.tallReason = "rest level read";
    return true;
}

void WaterCoreManager::updateTall(Av& av) {
    WaterCoreGpu::Volume& vol = *av.gpuVol;
    auto off = [&](const std::string& why) {
        if (vol.tallActive || vol.tallPending) { m_gpu->clearTall(vol); ++av.tallOffs; av.tallLastOff = why; }
        av.tallReason = why;
    };
    if (av.tallBand <= 0.0f) { off("off"); return; }
    if (!vol.bricked || !vol.sparse || vol.particles || av.solver->transport().ownsMass()) { off("not a sparse fill volume"); return; }
    if (!EulerianTransport::s_allFacesAtOnce) { off("needs the all-faces transport (26.5)"); return; }
    const WaterSurfaceField& fld = av.field;
    auto wetTops = [&](std::vector<float>& tops) {
        tops.clear();
        if (fld.cols.size() != static_cast<size_t>(fld.nx) * fld.nz) return;
        for (const SurfaceColumn& c : fld.cols) { const int runs = static_cast<int>(c.runs + 0.5f); if (runs >= 1) tops.push_back(c.top[std::min(runs, kSurfaceMaxRuns) - 1]); }
    };
    std::vector<float> tops;
    // 29 S2: the field's lowest wet top and its water from the device (no scan of the host's field); the host's field when it has none
    float devMinTop = 0.0f; double devWater = 0.0;
    const bool dev = !s_tallHostScanForTest && m_gpu && m_gpu->surfaceStats(vol, devMinTop, devWater);
    // 27.22d: water added or taken since the rest level was read (more than half a cell over the surface) - read it again (the bench
    // fills a pond in layers: the first layer is flat, and a 2 m pond got the floor of a 1 m one)
    if (!std::isnan(av.tallRestY) && fld.cols.size() == static_cast<size_t>(fld.nx) * fld.nz) {
        const double area = static_cast<double>(fld.nx) * fld.nz * av.h * av.h;
        if (std::abs((dev ? devWater : fieldWater(fld)) + av.recordedM3 - av.tallRestMass) > 0.5 * av.h * area) { off("the water changed - a new rest level"); av.tallRestY = std::numeric_limits<float>::quiet_NaN(); }
    }
    // the rest level is read while the volume SLEEPS (WaterCoreManager::update -> captureTallRest): a sleeping volume is never stepped,
    // and the call that decides it sleeps returns before the decision (26.16) - so rarely here
    if (std::isnan(av.tallRestY) && !captureTallRest(av)) { av.tallReason = "waiting for rest (a flat surface)"; return; }
    const glm::ivec3 n = vol.spec.dims;
    const float h = av.h;
    const int oy = av.grid->spec().origin.y;
    // the tall layer: the bottom cell of the brick whose bottom face lies at or below (rest - B) - the floor is that cell's top
    const int cyFloor = static_cast<int>(std::floor((av.tallRestY - av.tallBand - av.tallDrop) / h)) - oy;   // the cell whose BOTTOM is at or under rest - B (- the troughs' drop)
    const int T = ((cyFloor - 1 + vol.o.y) >= 0 ? (cyFloor - 1 + vol.o.y) / vol.by : -1) * vol.by - vol.o.y;   // a brick bottom at or under cyFloor - 1
    if (T < 1 || T >= n.y) { off("the band reaches the bed"); return; }
    const float floorTop = (static_cast<float>(oy + T) + 1.0f) * h;
    // the trough rule: the lowest wet surface within one brick of B above the floor - all fine until the next rest
    float minTop;
    if (dev) minTop = std::isfinite(devMinTop) ? devMinTop : av.tallRestY;   // 29 S2
    else { wetTops(tops); minTop = tops.empty() ? av.tallRestY : *std::min_element(tops.begin(), tops.end()); }
    // a TROUGH: the lowest wet surface within one brick of the floor - the band must keep the surface's motion (27.22 item 7). The first
    // build used B - one brick, which turned the tall cells off for a stone's crater 0.28 m deep in a 2 m pond (27.22d)
    if (vol.tallActive && minTop - floorTop < static_cast<float>(vol.by) * h) {
        // the floor steps DOWN two bricks (the tall cells expand this call and collapse lower at the next) - not off until rest (27.22d)
        char why[128]; std::snprintf(why, sizeof why, "trough (lowest surface %.3f m, floor %.3f m) - floor down %.2f m", minTop, floorTop, 2.0f * vol.by * h);
        off(why); av.tallDrop += 2.0f * static_cast<float>(vol.by) * h; return;
    }
    // sources under the floor
    for (int c : vol.sourceCells) if (c >= 0 && (c / n.x) % n.y <= T) { off("a source under the floor"); return; }
    // 27.22e: a moving body (debris, a stone, a float - 20's coupling needs cells round it) within a brick of the floor: the floor steps
    // down below it (a float at the surface never reaches it; the first build turned the tall cells off for any body anywhere)
    if (vol.hasSolids && vol.solidLowY < (1 << 30) && vol.solidLowY <= T + vol.by) {
        const int needCells = T + 1 + vol.by - vol.solidLowY;
        const float drop = static_cast<float>(((needCells + vol.by - 1) / vol.by) * vol.by) * h;
        char why[128]; std::snprintf(why, sizeof why, "a body at %.2f m, the floor at %.2f m - floor down %.2f m", (oy + vol.solidLowY) * h, floorTop, drop);
        off(why); av.tallDrop += drop; return;
    }
    if (vol.tallActive || vol.tallPending == 1) { av.tallReason = av.tallDrop > 0.0f ? "rest, lowered by troughs" : "rest"; return; }   // the floor stands
    if (vol.tallPending == 2) { av.tallReason = "expanding"; return; }   // 27.22d: an expand in flight - collapse after it
    // the heights: each column open (no solid) from its bed up to T; every non-solid cell under T must belong to one such run (a cave
    // under a solid would be read as ground - refused)
    std::vector<int> heights(vol.dColumns, 1);
    const WaterGrid& g = *av.grid;
    int tallCols = 0;
    for (int z = 0; z < n.z; ++z) for (int x = 0; x < n.x; ++x) {
        int y = T;
        while (y >= 0 && g.occ(x, y, z) != Occ::Solid) --y;   // the open run down from T
        const int Hc = T - y;                                   // cells y+1 .. T (0 when T itself is solid)
        for (int yy = y - 1; yy >= 0; --yy) if (g.occ(x, yy, z) != Occ::Solid) { off("a cave under the floor"); return; }
        if (Hc > 1) { heights[static_cast<size_t>(x) + static_cast<size_t>(n.x) * z] = Hc; ++tallCols; }
    }
    if (tallCols == 0) { av.tallReason = "no column reaches under the floor"; return; }
    // 27.22f: worth it only when the tall layer is deep enough - its own sweeps cost ~as much as a 3-cell tall layer saves (live, a
    // 24 x 24 m pond 1 m deep: 1.66 -> 1.77 ms with tall cells); from 6 cells (0.67 m) under the floor it pays (2 m: 2.37 -> 1.97)
    {
        std::vector<int> hs; hs.reserve(static_cast<size_t>(tallCols));
        for (int hgt : heights) if (hgt > 1) hs.push_back(hgt);
        std::nth_element(hs.begin(), hs.begin() + hs.size() / 2, hs.end());
        if (hs[hs.size() / 2] < kTallMinCells) { av.tallReason = "too shallow to pay (the tall layer under 6 cells)"; return; }
    }
    std::string err;
    if (!m_gpu->setTall(vol, T, heights, &err)) { av.tallReason = err; return; }
    av.tallReason = "rest";
}

void WaterCoreManager::stepGpu(Av& av, int ticks, float dt) {
    if (!av.gpuVol || !m_gpu) return;
    auto hp = WaterHostProfile::Clock::now();   // 27.28
    if (av.gpuDirty) {
        const auto tUp = std::chrono::steady_clock::now();   // 26.18: what a world change costs the water (measured, reported)
        syncFromGpu(av); m_gpu->upload(*av.gpuVol, *av.grid);
        av.gpuUploadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tUp).count(); ++av.gpuUploads;
        if (av.solver->transport().ownsMass()) {
            const auto* flip = dynamic_cast<const FlipTransport*>(&av.solver->transport());
            std::string perr;
            if (flip && !m_gpu->setParticles(*av.gpuVol, flip->particles(), &perr)) { /* over capacity: the volume keeps its last state; the record's particles tells */ }
        }
        av.gpuDirty = false;
    }
    // §27 W1c: the tile life cycle runs HERE - before the call, where the call in flight is collected anyway (the step's own first
    // act), after a pending upload (a record or wake marks the mirror stale: before the upload it would re-read over a push not yet
    // sent). Run after the call it collected the call just submitted; live, a pushed pond then never settled (KE 2-7e-2 at 24 s,
    // the pressure solve's residual climbing to 14; this order: 4e-8 J, the whole pond back in the record by 41 s - 27.13).
    WaterHostProfile::lap("mgr.1 upload (world changes)", hp);
    if (!s_recordAfterStepForTest) recordTilesStep(av, dt * static_cast<float>(ticks));   // seconds, not ticks (a realtime tick is a frame)
    WaterHostProfile::lap("mgr.2 record / wake tiles", hp);
    updateTall(av);   // 27.22: the floor and the heights for this call (after a pending upload: it resets the tall cells)
    WaterHostProfile::lap("mgr.3 tall cells", hp);
    if (m_gpu->surfaceTemplatePending(*av.gpuVol) && av.field.cols.size() == static_cast<size_t>(av.field.nx) * av.field.nz)   // 29 S1
        for (const auto& [tx, tz] : av.recordedTileList) { markRecordedColumns(av, tx, tz); queueTileSurface(av, tx, tz); }
    m_gpu->setAsync(*av.gpuVol, m_realtime);   // 26.16: realtime frames never wait on the water's call (collected next frame)
    const GpuStepStats st = m_gpu->step(*av.gpuVol, av.solver->params(), dt, ticks, av.gpuSweeps);
    WaterHostProfile::lap("mgr.4 the GPU step (= gpu.* + collect.*)", hp);
    av.gpuStale = true;
    av.gpuLast = st;
    av.carryBodyM3 += st.carryBodyM3; av.carryRateM3 += st.carryRateM3; av.carryFlowM3 += st.carryFlowM3; av.carryParkedM3 += st.carryParkedM3;
    if (st.asleep && av.solver->transport().ownsMass()) {   // §15.9 rest conversion on the GPU backend
        syncFromGpu(av);
        av.solver->transport().settle(*av.grid);
        av.solver->setTransport(std::make_unique<EulerianTransport>());
        av.gpuVol->particles = false; av.gpuVol->particleCount = 0;
        m_gpu->upload(*av.gpuVol, *av.grid);
    }
    if (av.gpuVol->sourceCount > 0) {   // the ledger: placedTotal/pending back into the solver's list
        pullSourceTotals(av);
        av.last.sourceUnplaced = 0.0; for (const auto& s : av.solver->sources()) av.last.sourceUnplaced += s.unplaced;
    }
    av.last.substeps = st.substepsLast; av.last.totalMass = st.totalMass; av.last.kineticEnergy = st.kineticEnergy;
    av.last.maxDeltaF = st.maxDeltaF; av.last.quietTicks = st.quietTicks; av.last.asleep = st.asleep;
    av.last.pcgIterations = st.sweeps; av.last.pcgResidual = st.rbgsResidualMax; av.last.residueDropped = st.residueDropped;
    av.residueDropped += st.residueDropped;
    if (s_recordAfterStepForTest) recordTilesStep(av, dt * static_cast<float>(ticks));   // the W1b order: the red control
    if (s_settleAfterStepForTest) m_gpu->settle(*av.gpuVol);   // 27.13 open item: a collect right after the submission
    WaterHostProfile::lap("mgr.5 after the step", hp);
}

bool WaterCoreManager::clearSources(int id) {
    for (auto& av : m_avs) if (av->id == id) { av->solver->clearSources(); if (av->backend == "gpu") pushSourcesToGpu(*av, nullptr); return true; }
    return false;
}

// The solver's source list is the authority (cells, rates); the GPU holds a mirror whose
// placedTotal/pending come back after every step so the ledger reads as for the CPU.
// 26.16: the sources' totals as the GPU's last collected call left them, into the solver's list (the GPU's list is the solver's
// list as last pushed: sources are only appended or cleared, so the GPU's entries are the solver's first ones).
void WaterCoreManager::pullSourceTotals(Av& av) {
    std::vector<GpuSource> gs; m_gpu->readSources(*av.gpuVol, gs);
    auto& srcs = av.solver->sourcesMutable();
    for (size_t i = 0; i < gs.size() && i < srcs.size(); ++i) { srcs[i].placedTotal = gs[i].total(); srcs[i].pending = gs[i].pending; srcs[i].unplaced = gs[i].unplaced; }
}

bool WaterCoreManager::pushSourcesToGpu(Av& av, std::string* err) {
    if (!av.gpuVol || !m_gpu) return false;
    // 26.16: in ASYNC mode the solver's totals are one call old - the call in flight has placed water since. Finish it and take
    // the GPU's totals first, or re-sending the list would erase what that call counted (the creek bench: 7.6e-3 m^3 lost from
    // the books over 18 springs added one by one)
    m_gpu->settle(*av.gpuVol);
    pullSourceTotals(av);
    std::vector<GpuSource> gs;
    for (const SourceSpec& s : av.solver->sources()) {
        GpuSource g; g.cell = static_cast<int32_t>(av.grid->idx(s.cell.x, s.cell.y, s.cell.z)); g.rate = s.rate;
        g.pending = static_cast<float>(s.pending); g.setTotal(s.placedTotal); g.unplaced = static_cast<float>(s.unplaced);
        g.kind = s.outfall ? 1.0f : 0.0f;
        gs.push_back(g);
    }
    return m_gpu->setSources(*av.gpuVol, gs, err);
}

bool WaterCoreManager::addSource(int id, const glm::vec3& world, float rate, std::string* err, bool outfall) {
    for (auto& av : m_avs) {
        if (av->id != id) continue;
        const glm::ivec3 c = av->grid->worldToCell(world);
        if (!av->grid->inBounds(c.x, c.y, c.z)) { if (err) *err = "source position is outside the volume"; return false; }
        if (outfall && av->solver->transport().ownsMass()) { if (err) *err = "an outfall on a particle volume is not built (26.13)"; return false; }
        if (outfall) av->solver->addOutfall(c); else av->solver->addSource(c, rate);
        if (av->backend == "gpu" && av->solver->transport().ownsMass()) { if (err) *err = "sources on a GPU particle volume are not built yet (particles emit on the CPU transport only)"; return false; }
        if (av->backend == "gpu") return pushSourcesToGpu(*av, err);
        return true;
    }
    if (err) *err = "no such volume";
    return false;
}

bool WaterCoreManager::setBedManning(int id, float n) {
    for (auto& av : m_avs) if (av->id == id) { av->solver->setBedManning(n); av->solver->wake(); if (av->gpuVol) av->gpuVol->asleep = false; return true; }
    return false;
}

WaterCoreManager::SolidsFeed WaterCoreManager::setMovingSolids(const std::vector<MovingSolid>& bodiesIn, float frameSeconds) {
    SolidsFeed out;
    // the held poses (kSolidHoldDistance): a body is rasterized where it was until it has moved further than that
    std::vector<MovingSolid> bodies = bodiesIn;
    std::unordered_map<uint64_t, glm::vec3> hold;
    for (MovingSolid& b : bodies) {
        if (b.id == 0) continue;
        auto it = m_solidHold.find(b.id);
        if (it != m_solidHold.end() && glm::length(b.centre - it->second) <= kSolidHoldDistance) b.centre = it->second;
        hold[b.id] = b.centre;
    }
    m_solidHold.swap(hold);   // bodies that left are forgotten
    for (auto& avp : m_avs) {
        Av& av = *avp;
        const glm::vec3 lo(av.minVoxel), hi = glm::vec3(av.maxVoxel) + glm::vec3(1.0f);
        std::vector<MovingSolid> mine;
        for (const MovingSolid& b : bodies)
            if (glm::all(glm::greaterThanEqual(b.centre + b.halfExtents, lo)) && glm::all(glm::lessThanEqual(b.centre - b.halfExtents, hi))) mine.push_back(b);
        if (mine.empty() && !av.hadSolids) continue;
        if (av.solver->transport().ownsMass()) { out.flipRefused += static_cast<long>(mine.size()); continue; }
        for (const MovingSolid& b : mine) wakeRecordedNear(av, b.centre, glm::length(b.halfExtents));   // §27 W1b: it displaces cells, not the record
        refreshOccupancy(av);
        ++out.volumes;
        WaterSolver::SolidsReport rep;
        if (av.backend == "gpu" && av.gpuVol && m_gpu) {
            rep = av.solver->updateSolidFields(mine, frameSeconds);
            static const std::vector<float> kNoWake;
            const WaterSolver::CellBox& box = av.solver->solidActiveBox();   // 26.12: only where a body is, was, or left its wake
            m_gpu->setSolidsBox(*av.gpuVol, av.grid->sData(), m_solidWakeRule ? av.solver->solidWake() : kNoWake, av.solver->solidFresh(), frameSeconds, box.lo, box.hi);
            if (rep.raster.cells > 0 || rep.wakeCells > 0 || rep.sChange > 0.0) av.gpuLast.asleep = false;   // setSolids woke the device volume
        } else {
            rep = av.solver->setMovingSolids(mine, frameSeconds);
        }
        out.bodies += rep.raster.bodies; out.rasterCells += rep.raster.cells; out.bodyVolume += rep.raster.volumeInside;
        out.freshCells += rep.freshCells; out.rateCells += rep.rateCells; out.clamped += rep.clamped; out.wakeCells += rep.wakeCells; out.rate += rep.rate;
        av.hadSolids = !mine.empty() || rep.wakeCells > 0;
    }
    return out;
}

WaterCoreManager::PressureWindowStats WaterCoreManager::setPressureWindows(const std::vector<PressureAsk>& asks, uint32_t tag, std::vector<uint32_t>* granted) {
    PressureWindowStats st; st.tag = tag; st.asked = static_cast<uint32_t>(asks.size());
    if (granted) granted->clear();
    if (!m_gpu) { m_pwStats = st; return st; }
    std::unordered_map<Av*, std::vector<PressureWindowReq>> per;
    for (auto& avp : m_avs) if (avp->gpuVol && avp->backend == "gpu" && avp->gpuVol->bricked) per[avp.get()];   // every GPU volume: cleared if none
    uint32_t offset = 0;
    const bool targets = m_gpu->pressureWindowTargetsSet();
    for (const PressureAsk& a : asks) {
        if (!targets) break;
        Av* home = nullptr;
        for (auto& [av, list] : per) {
            const glm::vec3 lo(av->minVoxel), hi = glm::vec3(av->maxVoxel) + glm::vec3(1.0f);
            if (glm::all(glm::greaterThanEqual(a.centre, lo)) && glm::all(glm::lessThan(a.centre, hi))) { home = av; break; }
        }
        if (!home) { ++st.outside; continue; }
        const float h = home->h;
        const float r = glm::length(a.halfExtents) + std::max(3.0f * h, a.speed * 0.1f);
        const glm::ivec3 o(glm::floor((a.centre - glm::vec3(r)) / h)), m(glm::floor((a.centre + glm::vec3(r)) / h));
        glm::ivec3 dims = m - o + glm::ivec3(1);
        dims.y = 1;   // 31.7: a 2-D map of the water's level over the columns round the body (wc_body_pressure.comp)
        // the clamps (solver_shared.h): a window wider than the cap, or past either budget, would overrun the fixed buffers. A refused
        // body is not rasterized (33, A2g: the editor filters by `granted`) - it keeps the table law and makes no splash
        if (glm::any(glm::greaterThan(dims, glm::ivec3(static_cast<int>(DebrisShared::PRESSURE_WINDOW_MAX_DIM))))) { ++st.tooBig; continue; }
        const uint32_t cells = static_cast<uint32_t>(dims.x * dims.y * dims.z);
        if (s_refuseWindowsForTest || st.windows >= DebrisShared::MAX_PRESSURE_WINDOWS || offset + cells > DebrisShared::PRESSURE_WINDOW_FLOATS) { ++st.overBudget; continue; }
        PressureWindowReq q;
        q.originWorld = o; q.originVol = o - home->grid->spec().origin; q.dims = dims;
        if (WaterCoreGpu::s_pwShiftForTest) q.originVol.x += 1;   // TEST: the red control
        q.dataOffset = static_cast<int32_t>(offset); q.index = static_cast<int32_t>(st.windows); q.slot = a.slot; q.h = h; std::memcpy(&q.tag, &tag, 4);
        q.pad0 = glm::length(a.halfExtents);   // 31.7: the footprint's radius at any orientation - the ring lies outside it
        q.pad1 = a.centre.y + glm::length(a.halfExtents);   // its top: a column's water is the run starting below it
        per[home].push_back(q);
        if (granted) granted->push_back(a.slot);
        offset += cells; ++st.windows; st.cells += cells;
    }
    for (auto& [av, list] : per) m_gpu->setPressureWindows(*av->gpuVol, std::move(list));
    m_pwStats = st;
    return st;
}

std::vector<std::pair<glm::vec3, glm::vec3>> WaterCoreManager::sleptBoxesWorld() const {
    std::vector<std::pair<glm::vec3, glm::vec3>> out;
    for (const SleptVolume& s : m_slept) out.push_back({glm::vec3(s.lo), glm::vec3(s.hi) + glm::vec3(1.0f)});
    return out;
}

int WaterCoreManager::wakeSlept(size_t i) {
    const auto t0 = std::chrono::steady_clock::now();
    const SleptVolume sv = m_slept[i];
    m_slept.erase(m_slept.begin() + static_cast<long>(i));   // woken (or found empty): forgotten either way
    std::string err;
    const int id = create(sv.lo, sv.hi, sv.cellSize, sv.transport.empty() ? "eulerian" : sv.transport, &err, sv.backend.empty() ? "auto" : sv.backend, sv.sweeps);
    if (!id) { ++m_sleptWake.refused; m_sleptWake.lastError = err; return 0; }
    setPartialWake(id, sv.partialWake);
    setTallBand(id, sv.tallBandM);
    if (sv.recordTiles) setRecordTiles(id, true);
    const double seeded = seedFromSpans(id);
    if (seeded <= 0.0) {
        // 35 (A2c): an empty seed is the record gone (drained, dug away) ONLY if every cell was readable. A chunk that is not resident
        // reads no runs (the span reader has no residency check), so a box at the edge of streaming - likeliest just after a reload -
        // seeded 0 and was forgotten for good: whether a pond wakes would depend on when its chunks loaded. Unknown occupancy in the
        // made volume (the 5.1 hold boundary sleep() uses) = not resident: keep the box, retry after kSleptRetryS (a wake is a ~35 ms
        // create - retrying every frame would stall every frame)
        bool unknown = false;
        for (const auto& av : m_avs)
            if (av->id == id) { unknown = std::find(av->occCache.begin(), av->occCache.end(), Occ::Unknown) != av->occCache.end(); break; }
        destroy(id);
        if (unknown) {
            SleptVolume kept = sv;
            kept.retryAfter = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count() + kSleptRetryS;
            m_slept.push_back(std::move(kept));
            ++m_sleptWake.deferredNotResident;
            return 0;
        }
        ++m_sleptWake.wokenEmpty;
        return 0;
    }
    for (auto& av : m_avs) if (av->id == id)
        av->noSleepBefore = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count() + kWokenNoSleepS;
    ++m_sleptWake.woken;
    m_sleptWake.allocMs = m_createSplit.allocMs; m_sleptWake.occMs = m_createSplit.occMs; m_sleptWake.solidsMs = m_createSplit.solidsMs;
    m_sleptWake.gpuMs = m_createSplit.gpuMs; m_sleptWake.cells = m_createSplit.cells;
    m_sleptWake.gpuOccCopyMs = m_createGpuSplit.occCopyMs; m_sleptWake.gpuCreateVolumeMs = m_createGpuSplit.createVolumeMs; m_sleptWake.gpuUploadMs = m_createGpuSplit.uploadMs; if (m_gpu) { const auto& cs = m_gpu->lastCreateSplit(); m_sleptWake.gpuBuffersMs = cs.buffersMs; m_sleptWake.gpuPipelinesMs = cs.pipelinesMs; m_sleptWake.gpuFillMs = cs.fillMs; m_sleptWake.gpuPipelines = cs.pipelines; m_sleptWake.gpuPipelinesReused = cs.pipelinesReused; }
    m_sleptWake.spanReadMs = m_seedSplit.spanReadMs; m_sleptWake.seedOccMs = m_seedSplit.occMs; m_sleptWake.seedMs = m_seedSplit.seedMs;
    m_sleptWake.lastMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    m_sleptWake.maxMs = std::max(m_sleptWake.maxMs, m_sleptWake.lastMs);
    return id;
}

std::vector<int> WaterCoreManager::wakeSleptTouching(const std::vector<MovingSolid>& bodies) {
    std::vector<int> ids;
    // 39.4: with a test delay a touched pond keeps waking after its body has gone (step 2 would finish a wake it started), so an
    // empty body list still runs the loop
    if (!s_wakeOnTouch || m_slept.empty() || (bodies.empty() && s_wakeDelaySForTest <= 0.0)) return ids;
    const double nowS = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    for (size_t i = m_slept.size(); i-- > 0;) {
        if (m_slept[i].retryAfter > nowS) continue;   // 35: deferred (its chunks were not resident) - not yet
        const glm::vec3 lo(m_slept[i].lo), hi = glm::vec3(m_slept[i].hi) + glm::vec3(1.0f);
        bool touched = false;
        for (const MovingSolid& b : bodies)
            if (glm::all(glm::greaterThanEqual(b.centre + b.halfExtents, lo)) && glm::all(glm::lessThanEqual(b.centre - b.halfExtents, hi))) { touched = true; break; }
        if (s_wakeDelaySForTest > 0.0) {
            if (touched && m_slept[i].touchedAt < 0.0) m_slept[i].touchedAt = nowS;
            touched = m_slept[i].touchedAt >= 0.0 && nowS - m_slept[i].touchedAt >= s_wakeDelaySForTest;
        }
        if (touched) { const int id = wakeSlept(i); if (id) ids.push_back(id); }
    }
    return ids;
}

std::vector<int> WaterCoreManager::wakeSleptNear(const glm::vec3& c, float radius) {
    std::vector<int> ids;
    if (!s_wakeOnTouch || m_slept.empty()) return ids;
    const double nowS = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    for (size_t i = m_slept.size(); i-- > 0;) {
        if (m_slept[i].retryAfter > nowS) continue;   // 35: deferred (its chunks were not resident) - not yet
        const glm::vec3 lo(m_slept[i].lo), hi = glm::vec3(m_slept[i].hi) + glm::vec3(1.0f);
        const glm::vec3 q = glm::clamp(c, lo, hi);
        if (glm::length(q - c) <= radius) { const int id = wakeSlept(i); if (id) ids.push_back(id); }
    }
    return ids;
}

std::string WaterCoreManager::serializeSlept() const {
    nlohmann::json boxes = nlohmann::json::array();
    for (const SleptVolume& s : m_slept)
        boxes.push_back({{"lo", {s.lo.x, s.lo.y, s.lo.z}}, {"hi", {s.hi.x, s.hi.y, s.hi.z}}, {"cellSize", s.cellSize}, {"transport", s.transport},
                         {"backend", s.backend}, {"sweeps", s.sweeps}, {"partialWake", s.partialWake}, {"recordTiles", s.recordTiles},
                         {"tallBandM", s.tallBandM}});
    return nlohmann::json{{"v", 1}, {"boxes", boxes}}.dump();
}

bool WaterCoreManager::loadSlept(const std::string& text) {
    m_slept.clear();   // a load is the world's state: whatever the last world remembered is not this world's (35, the world switch)
    m_sleptLoad = SleptLoadStats{};
    if (text.empty()) return true;   // a world saved before 35, or with no slept pond
    const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) { m_sleptLoad.error = "water_slept is not JSON"; return false; }
    const auto* boxes = j.contains("boxes") && j["boxes"].is_array() ? &j["boxes"] : nullptr;
    if (j.value("v", 0) != 1) {   // a format this build does not know: take nothing rather than guess at its fields
        m_sleptLoad.skipped = boxes ? static_cast<long>(boxes->size()) : 0;
        m_sleptLoad.error = "water_slept version " + std::to_string(j.value("v", 0)) + " unknown (this build reads 1)";
        return false;
    }
    if (!boxes) return true;
    for (const auto& b : *boxes) {
        auto ivec = [&](const char* k, glm::ivec3& out) {
            if (!b.contains(k) || !b[k].is_array() || b[k].size() != 3) return false;
            for (int i = 0; i < 3; ++i) { if (!b[k][i].is_number_integer()) return false; out[i] = b[k][i].get<int>(); }
            return true;
        };
        SleptVolume s;
        float snapped = 0.0f;
        if (!b.is_object() || !ivec("lo", s.lo) || !ivec("hi", s.hi)) { ++m_sleptLoad.skipped; continue; }
        // an inverted box would wake into create()'s min/max of its corners - a different box than the one that slept
        if (glm::any(glm::greaterThan(s.lo, s.hi))) { ++m_sleptLoad.skipped; continue; }
        // a cell that does not divide a microcube would sit half inside a wall (create() refuses it; refused here, before a wake)
        if (!snapCellSize(b.value("cellSize", 0.0f), &snapped)) { ++m_sleptLoad.skipped; continue; }
        s.cellSize = snapped;
        s.transport = b.value("transport", std::string("eulerian"));
        s.backend = b.value("backend", std::string("auto"));
        s.sweeps = b.value("sweeps", 0);
        s.partialWake = b.value("partialWake", true);
        s.recordTiles = b.value("recordTiles", false);
        s.tallBandM = b.value("tallBandM", 0.0f);
        m_slept.push_back(std::move(s));
        ++m_sleptLoad.restored;
    }
    return true;
}

void WaterCoreManager::markWritten(Av& av, const glm::vec3* wakeCentre, float wakeRadius) {
    av.solver->wake();
    av.gpuDirty = true; av.gpuLast.asleep = false;
    if (av.gpuVol && m_gpu) {
        if (wakeCentre) {   // 26.31: the columns the write touched (grid cells), the margin grows round them
            const glm::vec3 c = *wakeCentre / av.h - glm::vec3(av.grid->spec().origin);
            const float r = wakeRadius / av.h + 1.0f;
            m_gpu->wakeBox(*av.gpuVol, glm::ivec3(glm::floor(c - glm::vec3(r))), glm::ivec3(glm::floor(c + glm::vec3(r))));
        } else m_gpu->wakeAll(*av.gpuVol);
    }
}

bool WaterCoreManager::setRecordTiles(int id, bool on) {
    for (auto& av : m_avs) {
        if (av->id != id) continue;
        if (!av->gpuVol || !m_gpu || !av->gpuVol->sparse) return false;
        av->recordTiles = on;   // off: nothing new records; tiles in the record wake as the water reaches them, as before
        return true;
    }
    return false;
}

// §27 W1b - THE TILE LIFE CYCLE (docs/WaterCore.md 27.10). A tile is 8 x 8 m, world-aligned: 72 cells = 9 brick columns = 8 voxel
// columns, so it records and wakes whole bricks and whole record columns. Brick-box column bx is world brick bx + (origin - o) / 8.
namespace {
int floorDivI(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
constexpr int kTileBricks = 9, kTileVoxels = 8;
}

bool WaterCoreManager::tileBricks(const Av& av, int tx, int tz, glm::ivec2& bLo, glm::ivec2& bHi) const {
    const auto& vol = *av.gpuVol;
    const glm::ivec3 so = av.grid->spec().origin;
    const int offX = (so.x - vol.o.x) / 8, offZ = (so.z - vol.o.z) / 8;   // the world brick of brick-box column 0
    bLo = glm::ivec2(std::max(0, tx * kTileBricks - offX), std::max(0, tz * kTileBricks - offZ));
    bHi = glm::ivec2(std::min(vol.bn.x - 1, tx * kTileBricks + kTileBricks - 1 - offX), std::min(vol.bn.z - 1, tz * kTileBricks + kTileBricks - 1 - offZ));
    return bHi.x >= bLo.x && bHi.y >= bLo.y;
}

bool WaterCoreManager::columnRecorded(const Av& av, int wx, int wz) const {
    if (!av.gpuVol || av.recordedTileList.empty()) return false;
    const std::pair<int, int> t(floorDivI(wx, kTileVoxels), floorDivI(wz, kTileVoxels));
    return std::find(av.recordedTileList.begin(), av.recordedTileList.end(), t) != av.recordedTileList.end();
}

void WaterCoreManager::recordTilesStep(Av& av, float seconds) {
    if (!av.gpuVol || !m_gpu || !av.gpuVol->bricked || !av.gpuVol->sparse) return;
    auto& vol = *av.gpuVol;
    const glm::ivec3 so = av.grid->spec().origin;
    const int offX = (so.x - vol.o.x) / 8, offZ = (so.z - vol.o.z) / 8;
    // 1. wakes first (a recorded tile the awake margin or a wake box reached) - even with recording off, nothing stays stranded
    const std::vector<int> wakes = m_gpu->takeRecordedWakes(vol);
    if (!wakes.empty() || !av.pendingTileWakes.empty()) {
        std::set<std::pair<int, int>> tiles(av.pendingTileWakes.begin(), av.pendingTileWakes.end());   // + the droplets' landings (deferred)
        for (int c : wakes) tiles.insert({floorDivI(c % vol.bn.x + offX, kTileBricks), floorDivI(c / vol.bn.x + offZ, kTileBricks)});
        for (const auto& [tx, tz] : tiles)
            if (std::find(av.recordedTileList.begin(), av.recordedTileList.end(), std::make_pair(tx, tz)) != av.recordedTileList.end()) wakeTile(av, tx, tz);
        av.pendingTileWakes.clear();
    }
    if (!av.recordTiles || !vol.partialWake || !m_spanWriter || !m_spanReader) return;
    // 2. tiles frozen long enough record - never with droplets in flight for this volume (they land into cells)
    if (m_pool.anyFor(av.id)) return;
    const size_t nc = static_cast<size_t>(vol.bn.x) * vol.bn.z;
    // a body asleep as a WHOLE (its own sleep test - a still pond placed flat sleeps in ~0.5 s, before the per-column freeze has
    // run) is frozen everywhere; otherwise the per-column freeze says which columns are
    const bool wholeAsleep = vol.asleep;
    if (!wholeAsleep && vol.colFrozen.size() != nc) return;
    // frozen this long, in SECONDS of the water's time (the freeze test already held restTicks); counted in ticks it ran at the
    // frame rate live - a 2 s wait recorded tiles after 0.3-0.8 s at 400 fps
    const float needSeconds = s_recordAfterS;
    const int tx0 = floorDivI(offX, kTileBricks), tx1 = floorDivI(offX + vol.bn.x - 1, kTileBricks);
    const int tz0 = floorDivI(offZ, kTileBricks), tz1 = floorDivI(offZ + vol.bn.z - 1, kTileBricks);
    int done = 0;
    for (int tz = tz0; tz <= tz1 && done < kRecordTilesPerStep; ++tz) for (int tx = tx0; tx <= tx1 && done < kRecordTilesPerStep; ++tx) {
        if (std::find(av.recordedTileList.begin(), av.recordedTileList.end(), std::make_pair(tx, tz)) != av.recordedTileList.end()) continue;
        glm::ivec2 bLo, bHi;
        if (!tileBricks(av, tx, tz, bLo, bHi)) continue;
        bool any = false, ok = true;
        for (int bz = bLo.y; bz <= bHi.y && ok; ++bz) for (int bx = bLo.x; bx <= bHi.x && ok; ++bx) {
            const size_t c = static_cast<size_t>(bx) + static_cast<size_t>(vol.bn.x) * bz;
            if (!vol.colRecorded.empty() && vol.colRecorded[c]) { ok = false; break; }
            bool stored = false;
            for (int by = 0; by < vol.bn.y && !stored; ++by) stored = vol.map[static_cast<size_t>(bx) + static_cast<size_t>(vol.bn.x) * (by + static_cast<size_t>(vol.bn.y) * bz)] >= 2;
            if (!stored) continue;
            any = true;
            if (!wholeAsleep && !vol.colFrozen[c]) ok = false;
        }
        float& frozenFor = av.tileFrozenSeconds[{tx, tz}];
        if (!ok || !any) { frozenFor = 0.0f; continue; }
        frozenFor += std::max(seconds, 0.0f);
        if (frozenFor < needSeconds) continue;
        // a pump in the tile keeps it awake (its water is placed into cells); a body near it too (it displaces cells)
        const glm::ivec3 wLo(tx * kTileVoxels * av.per, 0, tz * kTileVoxels * av.per), wHi = wLo + glm::ivec3(kTileVoxels * av.per - 1, 0, kTileVoxels * av.per - 1);
        bool blocked = false;
        for (const SourceSpec& s : av.solver->sources()) {
            const glm::ivec3 w = so + s.cell;
            if (w.x >= wLo.x && w.x <= wHi.x && w.z >= wLo.z && w.z <= wHi.z) { blocked = true; break; }
        }
        for (const auto& [bid, centre] : m_solidHold) {
            if (centre.x >= tx * kTileVoxels - 2.0f && centre.x <= tx * kTileVoxels + kTileVoxels + 2.0f &&
                centre.z >= tz * kTileVoxels - 2.0f && centre.z <= tz * kTileVoxels + kTileVoxels + 2.0f) { blocked = true; break; }
        }
        if (blocked) continue;
        if (recordTile(av, tx, tz)) { ++done; frozenFor = 0.0f; }
    }
}

bool WaterCoreManager::recordTile(Av& av, int tx, int tz) {
    auto& vol = *av.gpuVol;
    const auto t0 = std::chrono::steady_clock::now();
    glm::ivec2 bLo, bHi;
    if (!tileBricks(av, tx, tz, bLo, bHi)) return false;
    auto tl = t0;
    auto lap = [&](double* last, double* mx, int k) { const auto n = std::chrono::steady_clock::now(); last[k] = std::chrono::duration<double, std::milli>(n - tl).count(); mx[k] = std::max(mx[k], last[k]); tl = n; };   // 27.19 measurement
    auto& P = s_tilePhases;
    WaterGrid g(GridSpec{glm::ivec3(0), glm::ivec3(1), av.h});
    if (!m_gpu->readColumns(vol, bLo, bHi, g)) return false;
    lap(P.rec, P.recMax, 0);
    for (int z = 0; z < g.nz(); ++z) for (int y = 0; y < g.ny(); ++y) for (int x = 0; x < g.nx(); ++x)
        if (g.occ(x, y, z) == Occ::Unknown && g.f(x, y, z) > 0.0f) return false;   // the 5.1 hold rule: not over unknown ground
    std::vector<ColumnRuns> runs;
    const WriteBackStats st = columnRunsFromGrid(g, nullptr, runs);
    if (st.thinDropped > 1e-6) return false;   // a film too thin to be a run: not at rest - recorded, it would be lost
    av.lastRecordSpreadMm = st.spreadMmMax;   // §27 W1b measurement: how far from flat the tile was inside a 1 m column when it recorded
    av.recordSpreadMmMax = std::max(av.recordSpreadMmMax, st.spreadMmMax);
    if (s_measureRecordJump) {   // the jump a wake will make: the record woken as a wake would, against the surface the tile had
        WaterGrid sg(g.spec());
        for (int z = 0; z < g.nz(); ++z) for (int y = 0; y < g.ny(); ++y) for (int x = 0; x < g.nx(); ++x) sg.occ(x, y, z) = g.occ(x, y, z);
        seedGridFromRuns(sg, runs);
        WaterSurfaceField had, back; extractSurfaceField(g, had); extractSurfaceField(sg, back);
        float jump = 0.0f;
        for (size_t c = 0; c < had.cols.size() && c < back.cols.size(); ++c) {
            const int ra = static_cast<int>(had.cols[c].runs + 0.5f), rb = static_cast<int>(back.cols[c].runs + 0.5f);
            if (ra > 0 && rb > 0) jump = std::max(jump, std::abs(had.cols[c].top[std::min(ra, kSurfaceMaxRuns) - 1] - back.cols[c].top[std::min(rb, kSurfaceMaxRuns) - 1]));
        }
        av.recordJumpMmMax = std::max(av.recordJumpMmMax, 1000.0f * jump);
    }
    lap(P.rec, P.recMax, 1);
    const float yLo = static_cast<float>(av.minVoxel.y), yHi = static_cast<float>(av.maxVoxel.y + 1);
    const glm::ivec3 vLo(std::max(av.minVoxel.x, tx * kTileVoxels), av.minVoxel.y, std::max(av.minVoxel.z, tz * kTileVoxels));
    const glm::ivec3 vHi(std::min(av.maxVoxel.x, tx * kTileVoxels + kTileVoxels - 1), av.maxVoxel.y, std::min(av.maxVoxel.z, tz * kTileVoxels + kTileVoxels - 1));
    std::vector<ColumnRuns> prior;
    m_spanReader(vLo, vHi, prior);
    long unwritten = 0, chunks = 0;
    writeSpans(runs, yLo, yHi, &unwritten, &chunks);
    if (unwritten > 0) {   // a non-resident chunk: put back what was there, keep the tile in cells
        long u2 = 0, c2 = 0; writeSpans(prior, yLo, yHi, &u2, &c2);
        return false;
    }
    std::vector<WaterBodyTable::ColumnMass> before, written;
    for (const auto& c : prior) { WaterBodyTable::ColumnMass m; m.x = c.x; m.z = c.z; for (const auto& r : c.runs) m.mass += static_cast<double>(r.topY - r.bottomY); before.push_back(m); }
    double stored = 0.0;
    for (const auto& c : runs) { if (c.runs.empty()) continue; WaterBodyTable::ColumnMass m; m.x = c.x; m.z = c.z; for (const auto& r : c.runs) { m.mass += static_cast<double>(r.topY - r.bottomY); m.top = std::max(m.top, r.topY); } written.push_back(m); stored += m.mass; }
    m_bodies.credit(written, before, m_bakeBodyAt);
    lap(P.rec, P.recMax, 2);
    m_gpu->setColumnsRecorded(vol, bLo, bHi);
    lap(P.rec, P.recMax, 3);
    av.recordedTileList.push_back({tx, tz});
    av.recordedM3 += stored;
    ++av.tilesRecorded;
    av.gpuStale = true;   // the grid mirror re-reads: the tile's cells are gone from the volume
    // the volume stops drawing the tile's columns: the record draws them (built here, this frame) - 27.16: marked the record's
    // (runs -1), never cleared to dry - dry, the live water beside the tile walled them off until the volume's next step (a dark
    // line swept across the pond as its tiles recorded; a pond asleep as a whole never steps again)
    buildRecordTilesNow(vLo, vHi);
    if (s_handoverRevertForTest & 1) {   // TEST ONLY: the 27.16 red control - cleared to dry
        const glm::ivec3 o = g.spec().origin - av.grid->spec().origin;
        for (int z = 0; z < g.nz(); ++z) for (int x = 0; x < g.nx(); ++x) {
            const int fx = o.x + x, fz = o.z + z;
            if (fx >= 0 && fz >= 0 && fx < av.field.nx && fz < av.field.nz && av.field.cols.size() == static_cast<size_t>(av.field.nx) * av.field.nz)
                av.field.cols[static_cast<size_t>(fx) + static_cast<size_t>(av.field.nx) * fz] = SurfaceColumn{};
        }
    } else if (av.field.cols.size() == static_cast<size_t>(av.field.nx) * av.field.nz) { markRecordedColumns(av, tx, tz); queueTileSurface(av, tx, tz); }   // 29 S1
    av.field.version = (1ull << 62) + (++m_fieldBumps);
    lap(P.rec, P.recMax, 4); ++P.recN;
    av.lastRecordMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return true;
}

bool WaterCoreManager::wakeTile(Av& av, int tx, int tz) {
    auto& vol = *av.gpuVol;
    const auto t0 = std::chrono::steady_clock::now();
    glm::ivec2 bLo, bHi;
    if (!tileBricks(av, tx, tz, bLo, bHi)) return false;
    const glm::ivec3 so = av.grid->spec().origin, n(av.grid->nx(), av.grid->ny(), av.grid->nz());
    const glm::ivec3 vLo(std::max(av.minVoxel.x, tx * kTileVoxels), av.minVoxel.y, std::max(av.minVoxel.z, tz * kTileVoxels));
    const glm::ivec3 vHi(std::min(av.maxVoxel.x, tx * kTileVoxels + kTileVoxels - 1), av.maxVoxel.y, std::min(av.maxVoxel.z, tz * kTileVoxels + kTileVoxels - 1));
    auto tl = t0;
    auto lap = [&](double* last, double* mx, int k) { const auto n = std::chrono::steady_clock::now(); last[k] = std::chrono::duration<double, std::milli>(n - tl).count(); mx[k] = std::max(mx[k], last[k]); tl = n; };   // 27.19 measurement
    auto& P = s_tilePhases;
    std::vector<ColumnRuns> runs;
    if (m_spanReader) m_spanReader(vLo, vHi, runs);
    lap(P.wake, P.wakeMax, 0);
    // the tile's cells in the volume (world-aligned tile, clipped to the box), its occupancy the volume's own
    const int x0 = std::max(0, tx * kTileVoxels * av.per - so.x), x1 = std::min(n.x - 1, (tx + 1) * kTileVoxels * av.per - 1 - so.x);
    const int z0 = std::max(0, tz * kTileVoxels * av.per - so.z), z1 = std::min(n.z - 1, (tz + 1) * kTileVoxels * av.per - 1 - so.z);
    if (x1 < x0 || z1 < z0) return false;
    WaterGrid g(GridSpec{so + glm::ivec3(x0, 0, z0), glm::ivec3(x1 - x0 + 1, n.y, z1 - z0 + 1), av.h});
    refreshOccupancy(av);
    for (int z = z0; z <= z1; ++z) for (int y = 0; y < n.y; ++y) for (int x = x0; x <= x1; ++x) g.occ(x - x0, y, z - z0) = av.occCache[av.grid->idx(x, y, z)];
    lap(P.wake, P.wakeMax, 1);
    seedGridFromRuns(g, runs);   // 27.9: level-filled into the air cells
    {   // the volume draws the woken tile at once from the seed (the GPU's surface replaces it at the next collected call)
        WaterSurfaceField sf; extractSurfaceField(g, sf);
        for (int z = 0; z < sf.nz; ++z) for (int x = 0; x < sf.nx; ++x) {
            const int fx = x0 + x, fz = z0 + z;
            if (fx < av.field.nx && fz < av.field.nz && av.field.cols.size() == static_cast<size_t>(av.field.nx) * av.field.nz)
                av.field.cols[static_cast<size_t>(fx) + static_cast<size_t>(av.field.nx) * fz] = sf.at(x, z);
        }
        av.field.version = (1ull << 62) + (++m_fieldBumps);
        if (av.field.cols.size() == static_cast<size_t>(av.field.nx) * av.field.nz)   // 29 S1: the seed's surface to the device (its next call)
            m_gpu->queueSurfaceColumns(vol, av.field, x0, z0, std::min(x0 + sf.nx, av.field.nx), std::min(z0 + sf.nz, av.field.nz));
        // 27.15: ... and keeps drawing it until a surface computed after the seed is read (surfaceFields re-applies it)
        Av::SeedPatch sp; sp.x0 = x0; sp.z0 = z0; sp.nx = sf.nx; sp.nz = sf.nz; sp.cols = sf.cols; sp.afterCall = m_gpu->callsSubmitted(vol);
        if (!(s_handoverRevertForTest & 4)) av.seedPatches.push_back(std::move(sp));   // TEST ONLY bit 4: the B red control
    }
    lap(P.wake, P.wakeMax, 2);
    m_gpu->seedColumns(vol, std::move(g), bLo, bHi);
    lap(P.wake, P.wakeMax, 3);
    if (s_handoverRevertForTest & 2) vol.surfaceFullNext = false;   // TEST ONLY: the A red control
    // the record gives the water back: its columns cleared, the body debited
    std::vector<ColumnRuns> clear;
    std::vector<WaterBodyTable::ColumnMass> before, written;
    double back = 0.0;
    for (const auto& c : runs) { WaterBodyTable::ColumnMass m; m.x = c.x; m.z = c.z; for (const auto& r : c.runs) m.mass += static_cast<double>(r.topY - r.bottomY); before.push_back(m); back += m.mass; }
    for (int vz = vLo.z; vz <= vHi.z; ++vz) for (int vx = vLo.x; vx <= vHi.x; ++vx) { ColumnRuns c; c.x = vx; c.z = vz; clear.push_back(c); }
    if (m_spanWriter) { long u = 0, ch = 0; writeSpans(clear, static_cast<float>(av.minVoxel.y), static_cast<float>(av.maxVoxel.y + 1), &u, &ch); }
    m_bodies.credit(written, before, m_bakeBodyAt);
    av.recordedTileList.erase(std::remove(av.recordedTileList.begin(), av.recordedTileList.end(), std::make_pair(tx, tz)), av.recordedTileList.end());
    av.recordedM3 -= back;
    ++av.tilesWoken;
    av.gpuStale = true;
    lap(P.wake, P.wakeMax, 4);
    buildRecordTilesNow(vLo, vHi);
    lap(P.wake, P.wakeMax, 5); ++P.wakeN;
    av.lastWakeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return true;
}

// §27 W1b: before anything writes into a volume's water (a push, placed water, debris momentum, a moving body), the recorded tiles
// it touches - and a 2 m margin, so its first waves meet cells - are woken from the record, synchronously: a write into a recorded
// tile's empty cells is lost (a push did nothing; a stone would fall through a recorded lake without a splash).
void WaterCoreManager::wakeRecordedNear(Av& av, const glm::vec3& world, float radius, bool deferred) {
    if (!av.gpuVol || av.recordedTileList.empty()) return;
    const float r = radius + s_writeWakeReachM;
    const int tx0 = floorDivI(static_cast<int>(std::floor(world.x - r)), kTileVoxels), tx1 = floorDivI(static_cast<int>(std::floor(world.x + r)), kTileVoxels);
    const int tz0 = floorDivI(static_cast<int>(std::floor(world.z - r)), kTileVoxels), tz1 = floorDivI(static_cast<int>(std::floor(world.z + r)), kTileVoxels);
    for (int tz = tz0; tz <= tz1; ++tz) for (int tx = tx0; tx <= tx1; ++tx) {
        if (std::find(av.recordedTileList.begin(), av.recordedTileList.end(), std::make_pair(tx, tz)) == av.recordedTileList.end()) continue;
        if (deferred) { av.pendingTileWakes.push_back({tx, tz}); ++av.tilesWokenByWrite; }
        else if (wakeTile(av, tx, tz)) ++av.tilesWokenByWrite;
    }
}

bool WaterCoreManager::setPartialWake(int id, bool on) {
    for (auto& av : m_avs) {
        if (av->id != id) continue;
        if (!av->gpuVol || !m_gpu || !av->gpuVol->sparse) return false;
        m_gpu->setPartialWake(*av->gpuVol, on);
        av->gpuLast.asleep = false;
        return true;
    }
    return false;
}

namespace {
bool sphereTouchesBox(const glm::vec3& c, float r, const glm::ivec3& lo, const glm::ivec3& hi) {
    const glm::vec3 q = glm::clamp(c, glm::vec3(lo), glm::vec3(hi) + glm::vec3(1.0f));
    return glm::dot(q - c, q - c) <= r * r;
}
}

bool WaterCoreManager::addImpulse(const glm::vec3& world, float radius, float deltaSpeed, const glm::vec3& dir) {
    // Phase E1 fix: the impulse was written to the CPU grid only - a GPU volume never saw it (no
    // upload, the next download overwrote it, a sleeping volume was skipped). Now: the current state
    // first, the write, then upload + wake - the placeBox discipline.
    bool any = false;
    for (auto& av : m_avs) {
        if (!sphereTouchesBox(world, radius, av->minVoxel, av->maxVoxel)) continue;
        wakeRecordedNear(*av, world, radius);   // §27 W1b: into cells, not into the record's empty ones
        if (av->gpuVol && m_gpu && !s_writesRoundTripForTest && !av->gpuDirty) {   // 27.23 W1d.2: on the device, no round trip
            const glm::vec3 d = glm::length(dir) > 0.0f ? glm::normalize(dir) : glm::vec3(0.0f);
            m_gpu->queueImpulse(*av->gpuVol, world, radius, deltaSpeed * d);
            av->gpuLast.asleep = false; av->solver->wake(); ++av->writesInPlace;
            any = true;
            continue;
        }
        syncFromGpu(*av);
        av->solver->addImpulse(world, radius, deltaSpeed, dir);
        markWritten(*av, &world, radius);   // 26.31: an impulse wakes its own columns
        ++av->writesRoundTrip;
        any = true;
    }
    return any;
}

WaterCoreManager::MomentumReport WaterCoreManager::applyMomentum(const std::vector<MomentumRecord>& records) {
    MomentumReport rep;
    rep.records = static_cast<long>(records.size());
    std::vector<char> synced(m_avs.size(), 0), touched(m_avs.size(), 0);
    for (const auto& rec : records) {
        const glm::ivec3 v(static_cast<int>(std::floor(rec.pos.x)), static_cast<int>(std::floor(rec.pos.y)), static_cast<int>(std::floor(rec.pos.z)));
        int which = -1;
        for (size_t i = 0; i < m_avs.size(); ++i) {
            const Av& av = *m_avs[i];
            if (v.x >= av.minVoxel.x && v.x <= av.maxVoxel.x && v.y >= av.minVoxel.y && v.y <= av.maxVoxel.y && v.z >= av.minVoxel.z && v.z <= av.maxVoxel.z) { which = static_cast<int>(i); break; }
        }
        if (which < 0) { ++rep.outside; continue; }
        Av& av = *m_avs[which];
        if (av.gpuVol && m_gpu) {
            // 26.26: on the device, applied at the start of the volume's next call - the CPU path below read the whole volume back
            // and re-uploaded it for every frame with a record (4 floats in the creek: 124 -> 7 FPS). Whether the sphere found water
            // is the device's to know: counted as applied (the CPU's `dry` is not known here).
            wakeRecordedNear(av, rec.pos, rec.radius);   // §27 W1b: a floater on a recorded tile wakes it (its push would find no water)
            m_gpu->queueMomentum(*av.gpuVol, rec.pos, rec.radius, rec.momentum);
            ++rep.applied; rep.total += rec.momentum;
            continue;
        }
        if (!synced[which]) { syncFromGpu(av); synced[which] = 1; }
        const WaterSolver::RadialKick k = av.solver->addMomentum(rec.pos, rec.radius, rec.momentum);
        if (k.faces == 0) { ++rep.dry; continue; }
        ++rep.applied; rep.clamped += k.clamped; rep.total += rec.momentum;
        touched[which] = 1;
    }
    for (size_t i = 0; i < m_avs.size(); ++i) if (touched[i]) { markWritten(*m_avs[i]); ++rep.volumes; }
    return rep;
}

WaterCoreManager::KickReport WaterCoreManager::addRadialImpulse(const glm::vec3& centre, float reach, float speedAtCentre, float upBias) {
    KickReport rep;
    for (auto& av : m_avs) {
        if (!sphereTouchesBox(centre, reach, av->minVoxel, av->maxVoxel)) continue;
        if (av->gpuVol && m_gpu && !s_writesRoundTripForTest && !av->gpuDirty) {   // 27.23 W1d.2: on the device (its face count is the device's)
            wakeRecordedNear(*av, centre, reach);
            m_gpu->queueRadialImpulse(*av->gpuVol, centre, reach, speedAtCentre, upBias, 20.0f /* addRadialImpulse default */);
            av->gpuLast.asleep = false; av->solver->wake(); ++av->writesInPlace;
            ++rep.volumes; rep.faces = -1;
            continue;
        }
        syncFromGpu(*av);
        const WaterSolver::RadialKick k = av->solver->addRadialImpulse(centre, reach, speedAtCentre, upBias);
        if (k.faces == 0) continue;
        markWritten(*av);
        ++av->writesRoundTrip;
        ++rep.volumes; rep.faces += k.faces; rep.clamped += k.clamped;
    }
    return rep;
}

ProbeResult WaterCoreManager::probe(const glm::vec3& world) {
    for (auto& av : m_avs) refreshOccupancy(*av);   // a probe right after a dig must see the dig (S5)
    for (auto& av : m_avs) syncFromGpu(*av);
    ProbeResult r;
    r.surfaceY = std::numeric_limits<float>::quiet_NaN();
    const glm::ivec3 v(static_cast<int>(std::floor(world.x)), static_cast<int>(std::floor(world.y)), static_cast<int>(std::floor(world.z)));
    const Av* av = volumeAtVoxel(v.x, v.y, v.z);
    if (!av) return r;
    const glm::ivec3 c = av->grid->worldToCell(world);
    if (!av->grid->inBounds(c.x, c.y, c.z)) return r;
    r.inVolume = true; r.avId = av->id;
    r.fill = av->grid->f(c.x, c.y, c.z);
    r.velocity = glm::vec3(0.5f * (av->grid->u(c.x, c.y, c.z) + av->grid->u(c.x + 1, c.y, c.z)),
                           0.5f * (av->grid->v(c.x, c.y, c.z) + av->grid->v(c.x, c.y + 1, c.z)),
                           0.5f * (av->grid->w(c.x, c.y, c.z) + av->grid->w(c.x, c.y, c.z + 1)));
    r.surfaceY = av->grid->surfaceWorldY(c.x, c.z);
    r.pressure = av->solver->pressure(c.x, c.y, c.z);
    r.occupancy = static_cast<int>(av->grid->occ(c.x, c.y, c.z));
    r.solidFraction = av->grid->s(c.x, c.y, c.z);
    return r;
}

std::vector<ColumnSample> WaterCoreManager::probeColumns(int x1, int z1, int x2, int z2, int yMin, int yMax) const {
    std::vector<ColumnSample> out;
    for (const auto& av : m_avs) const_cast<WaterCoreManager*>(this)->syncFromGpu(*av);   // the probe is the reader the design names
    const int lx = std::min(x1, x2), hx = std::max(x1, x2), lz = std::min(z1, z2), hz = std::max(z1, z2);
    for (int z = lz; z <= hz; ++z) for (int x = lx; x <= hx; ++x) {
        ColumnSample c; c.x = x; c.z = z; c.surfaceY = std::numeric_limits<float>::quiet_NaN();
        for (const auto& av : m_avs) {
            if (x < av->minVoxel.x || x > av->maxVoxel.x || z < av->minVoxel.z || z > av->maxVoxel.z) continue;
            c.inVolume = true;
            const int bx = x * av->per - av->grid->spec().origin.x, bz = z * av->per - av->grid->spec().origin.z;
            for (int k = 0; k < av->per; ++k) for (int i = 0; i < av->per; ++i) {
                const int gx = bx + i, gz = bz + k;
                if (gx < 0 || gz < 0 || gx >= av->grid->nx() || gz >= av->grid->nz()) continue;
                // cell rows whose world y (voxel units) lies in [yMin, yMax]
                const int gy0 = std::max(0, yMin == INT_MIN ? 0 : yMin * av->per - av->grid->spec().origin.y);
                const int gy1 = std::min(av->grid->ny() - 1, yMax == INT_MAX ? av->grid->ny() - 1 : (yMax + 1) * av->per - 1 - av->grid->spec().origin.y);
                for (int gy = gy0; gy <= gy1; ++gy) {
                    const float fv = av->grid->f(gx, gy, gz);
                    if (fv <= 0.0f) continue;
                    c.mass += static_cast<double>(fv) * av->grid->h() * av->grid->h() * av->grid->h();
                    if (fv > WaterGrid::kSurfaceMinDepth / av->grid->h()) {
                        const float s = (static_cast<float>(av->grid->spec().origin.y + gy) + std::min(fv, 1.0f)) * av->grid->h();
                        if (std::isnan(c.surfaceY) || s > c.surfaceY) c.surfaceY = s;
                    }
                }
            }
        }
        out.push_back(c);
    }
    return out;
}

double WaterCoreManager::totalMass() const {
    for (const auto& av : m_avs) const_cast<WaterCoreManager*>(this)->syncFromGpu(*av, m_realtime);
    double m = 0.0;
    for (const auto& av : m_avs) m += av->grid->totalMass();
    return m;
}

size_t WaterCoreManager::totalCells() const {
    size_t n = 0;
    for (const auto& av : m_avs) n += av->grid->cellCount();
    return n;
}

void WaterCoreManager::queueTileSurface(Av& av, int tx, int tz) {
    if (!av.gpuVol || !m_gpu || !WaterCoreGpu::s_gpuSurface) return;
    const WaterSurfaceField& f = av.field;
    if (f.cols.size() != static_cast<size_t>(f.nx) * f.nz) return;
    const glm::ivec3 so = av.grid->spec().origin;
    const int x0 = std::max(0, tx * kTileVoxels * av.per - so.x), x1 = std::min(f.nx - 1, (tx + 1) * kTileVoxels * av.per - 1 - so.x);
    const int z0 = std::max(0, tz * kTileVoxels * av.per - so.z), z1 = std::min(f.nz - 1, (tz + 1) * kTileVoxels * av.per - 1 - so.z);
    if (x1 >= x0 && z1 >= z0) m_gpu->queueSurfaceColumns(*av.gpuVol, f, x0, z0, x1 + 1, z1 + 1);
}

void WaterCoreManager::markRecordedColumns(Av& av, int tx, int tz) {
    // 27.15: a recorded tile's columns are the record's to draw - marked "not this field's" (runs -1), so the volume's mesh ends
    // there like at its box edge instead of walling them off as dry (a wall from the surface to the bed showed - yellow bed through
    // it, a dark crease along its top - wherever live water lay beside a recorded tile)
    WaterSurfaceField& f = av.field;
    const glm::ivec3 so = av.grid->spec().origin;
    const int x0 = std::max(0, tx * kTileVoxels * av.per - so.x), x1 = std::min(f.nx - 1, (tx + 1) * kTileVoxels * av.per - 1 - so.x);
    const int z0 = std::max(0, tz * kTileVoxels * av.per - so.z), z1 = std::min(f.nz - 1, (tz + 1) * kTileVoxels * av.per - 1 - so.z);
    // ... carrying the record's drawn level (its tile's field, the last built - absent out of the draw range), so the volume's
    // surface meets it at the seam (WaterSurfaceMesh cornerY)
    const auto rt = m_recordTiles.find({tx, tz});
    const WaterSurfaceField* rf = (rt != m_recordTiles.end() && rt->second.wet && rt->second.field.h == f.h) ? &rt->second.field : nullptr;
    for (int z = z0; z <= z1; ++z) for (int x = x0; x <= x1; ++x) {
        SurfaceColumn& c = f.cols[static_cast<size_t>(x) + static_cast<size_t>(f.nx) * z];
        const float st = c.solidTopY; c = SurfaceColumn{}; c.solidTopY = st; c.runs = -1.0f;
        if (rf) {
            const int rx = x + so.x - rf->origin.x, rz = z + so.z - rf->origin.z;
            if (rx >= 0 && rz >= 0 && rx < rf->nx && rz < rf->nz) {
                const SurfaceColumn& r = rf->at(rx, rz);
                const int n = std::min(static_cast<int>(r.runs + 0.5f), kSurfaceMaxRuns);
                if (n > 0) { c.top[0] = r.top[n - 1]; c.bottom[0] = r.bottom[n - 1]; }
            }
        }
    }
}

const std::vector<const WaterSurfaceField*>& WaterCoreManager::surfaceFields() {
    // 26.30 step 3: the volumes' own fields - no copy (it was cleared and every field copied each call, up to three calls a frame:
    // 0.42 ms a frame on a sleeping 20 x 20 m pond, ~1 ms on an awake 32 x 28 m one)
    m_fields.resize(m_avs.size());
    auto hp = WaterHostProfile::Clock::now();   // 27.28
    for (size_t i = 0; i < m_avs.size(); ++i) {
        auto& av = m_avs[i];
        if (av->fieldStep != av->stepCount) {
            WaterHostProfile::lap("surf.0 (before)", hp);
            bool got = false;
            if (av->gpuVol && m_gpu && m_gpu->surfaceReady(*av->gpuVol)) {
                WaterSurfaceField& f = av->field;
                // 26.33: the field already holds this volume's whole surface (its last read) - a partial-wake call may update only its awake columns
                const bool keep = f.source == av->id && f.origin == av->grid->spec().origin && f.nx == av->grid->nx() && f.nz == av->grid->nz()
                                  && f.cols.size() == static_cast<size_t>(f.nx) * f.nz;
                f.origin = av->grid->spec().origin; f.nx = av->grid->nx(); f.nz = av->grid->nz(); f.h = av->h;
                f.cols.resize(static_cast<size_t>(f.nx) * f.nz);
                got = m_gpu->readSurface(*av->gpuVol, reinterpret_cast<float*>(f.cols.data()), keep);
                WaterHostProfile::lap("surf.1 read the GPU surface", hp);
                // 27.15: a woken tile's seed stays drawn until the surface read is of a call submitted after the seed
                const uint64_t sc = m_gpu->surfaceCallId(*av->gpuVol);
                for (auto it = av->seedPatches.begin(); it != av->seedPatches.end();) {
                    if (got && sc > it->afterCall) { it = av->seedPatches.erase(it); continue; }
                    for (int z = 0; z < it->nz; ++z) for (int x = 0; x < it->nx; ++x) {
                        const int fx = it->x0 + x, fz = it->z0 + z;
                        if (fx < f.nx && fz < f.nz) f.cols[static_cast<size_t>(fx) + static_cast<size_t>(f.nx) * fz] = it->cols[static_cast<size_t>(x) + static_cast<size_t>(it->nx) * z];
                    }
                    ++it;
                }
            }
            WaterHostProfile::lap("surf.2 seed patches", hp);
            if (!got) { syncFromGpu(*av, false); extractSurfaceField(*av->grid, av->field); }
            WaterHostProfile::lap("surf.3 CPU fallback", hp);
            // 27.15: the recorded tiles' columns are the record's to draw - marked "not this field's" (runs -1), so the volume's mesh
            // ends there like at its box edge instead of walling them off as dry (a wall from the surface to the bed showed - yellow
            // bed through it, a dark crease along its top - wherever the woken water moved beside a recorded tile, live)
            const bool fullRead = !got || !av->gpuVol || !m_gpu || !m_gpu->surfacePartial(*av->gpuVol);   // 29 S4: a partial read kept the marks
            if (!(s_handoverRevertForTest & 8) && (fullRead || s_markEveryRefreshForTest))
                for (const auto& [tx, tz] : av->recordedTileList) markRecordedColumns(*av, tx, tz);   // bit 8: the C red control
            WaterHostProfile::lap("surf.4 mark recorded columns", hp);
            av->fieldStep = av->stepCount;
            av->field.source = av->id; av->field.version = av->fieldStep + 1;   // 26.30: the tiler skips a field whose version stands (an asleep volume)
            av->field.look = packLook(lookAt((av->minVoxel.x + av->maxVoxel.x) / 2, (av->minVoxel.z + av->maxVoxel.z) / 2));   // G3
            av->fieldStepSec = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }
        av->field.rippleLayer = (av->ripple && m_rstats.on) ? static_cast<int>(i) : -1;   // 22: parallel to rippleLayers()
        av->field.gpuDrawn = av->gpuVol && m_gpu && m_gpu->surfaceView(*av->gpuVol).ok;   // 29 S1
        m_fields[i] = &av->field;
    }
    WaterHostProfile::lap("surf.5 rest", hp);
    refreshRecordTiles();   // §27 W1a: the recorded water near the camera, after the volumes (a volume owns its columns first)
    WaterHostProfile::lap("surf.6 record tiles' surfaces", hp);
    for (auto& [key, t] : m_recordTiles) if (t.wet) m_fields.push_back(&t.field);
    return m_fields;
}

void WaterCoreManager::refreshRecordTiles() {
    m_recordStats.builtLastCall = 0; m_recordStats.buildMsLastCall = 0.0; m_recordStats.pending = 0;
    if (!s_recordDraw.on || !m_spanReader) {
        if (!m_recordTiles.empty()) { m_recordTiles.clear(); m_recordBoxes.clear(); ++m_recordRevision; }
        m_recordStats.tiles = m_recordStats.wetTiles = 0;
        return;
    }
    const uint64_t sr = m_spanRevision ? m_spanRevision() : 0, orv = m_revision ? m_revision() : 0;
    const float r = std::max(0.0f, s_recordDraw.radiusM);
    auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
    const int T = kRecordTile;
    const int tx0 = floorDiv(static_cast<int>(std::floor(m_view.x - r)), T), tx1 = floorDiv(static_cast<int>(std::floor(m_view.x + r)), T);
    const int tz0 = floorDiv(static_cast<int>(std::floor(m_view.z - r)), T), tz1 = floorDiv(static_cast<int>(std::floor(m_view.z + r)), T);
    // a tile is in range when its nearest point is within the radius (the bound is conservative: cost, never a cut-off mid-tile)
    auto inRange = [&](int tx, int tz) {
        const float nx = std::clamp(m_view.x, static_cast<float>(tx * T), static_cast<float>(tx * T + T)) - m_view.x;
        const float nz = std::clamp(m_view.z, static_cast<float>(tz * T), static_cast<float>(tz * T + T)) - m_view.z;
        return nx * nx + nz * nz <= r * r;
    };
    bool changed = false;
    for (auto it = m_recordTiles.begin(); it != m_recordTiles.end();) {
        if (!inRange(it->first.first, it->first.second)) { changed |= it->second.wet; it = m_recordTiles.erase(it); } else ++it;
    }
    const auto t0 = std::chrono::steady_clock::now();
    // stale tiles, nearest first (a stale tile keeps drawing its last field until it is rebuilt - late, never blank)
    std::vector<std::pair<float, std::pair<int, int>>> stale;
    for (int tz = tz0; tz <= tz1; ++tz) for (int tx = tx0; tx <= tx1; ++tx) {
        if (!inRange(tx, tz)) continue;
        RecordTile& t = m_recordTiles[{tx, tz}];
        if (t.spanRev == sr && t.occRev == orv && t.avRev == m_avRevision) continue;
        const float dx = (tx + 0.5f) * T - m_view.x, dz = (tz + 0.5f) * T - m_view.z;
        stale.push_back({dx * dx + dz * dz, {tx, tz}});
    }
    std::sort(stale.begin(), stale.end());
    for (const auto& [d2, key] : stale) {
        if (m_recordStats.builtLastCall >= kRecordBuildsPerCall) { ++m_recordStats.pending; continue; }
        RecordTile& t = m_recordTiles[key];
        const bool wasWet = t.wet;
        buildRecordTile(key.first, key.second, t);
        remarkRecordedTile(key.first, key.second);   // 29 S1
        t.spanRev = sr; t.occRev = orv; t.avRev = m_avRevision;
        ++m_recordStats.builtLastCall;
        changed |= wasWet || t.wet;
    }
    m_recordStats.buildMsLastCall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    m_recordStats.buildMsMax = std::max(m_recordStats.buildMsMax, m_recordStats.buildMsLastCall);
    m_recordStats.tiles = static_cast<int>(m_recordTiles.size());
    m_recordStats.wetTiles = 0;
    if (changed) {
        m_recordBoxes.clear(); ++m_recordRevision;
        for (const auto& [key, t] : m_recordTiles)
            if (t.wet) m_recordBoxes.push_back({glm::ivec3(key.first * T, -1000000, key.second * T), glm::ivec3(key.first * T + T - 1, 1000000, key.second * T + T - 1)});
    }
    for (const auto& [key, t] : m_recordTiles) if (t.wet) ++m_recordStats.wetTiles;
}

void WaterCoreManager::buildRecordTilesNow(const glm::ivec3& lo, const glm::ivec3& hi) {
    // a volume just slept over [lo, hi]: its record tiles are built at once, not budgeted - built later, the pond would be blank
    // between the volume's last frame and the tiles' turn (every tile in range goes stale on the span revision bump)
    if (!s_recordDraw.on || !m_spanReader) return;
    const uint64_t sr = m_spanRevision ? m_spanRevision() : 0, orv = m_revision ? m_revision() : 0;
    const int T = kRecordTile;
    auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
    const float r = std::max(0.0f, s_recordDraw.radiusM);
    const auto t0 = std::chrono::steady_clock::now();
    bool changed = false;
    for (int tz = floorDiv(lo.z, T); tz <= floorDiv(hi.z, T); ++tz) for (int tx = floorDiv(lo.x, T); tx <= floorDiv(hi.x, T); ++tx) {
        const float nx = std::clamp(m_view.x, static_cast<float>(tx * T), static_cast<float>(tx * T + T)) - m_view.x;
        const float nz = std::clamp(m_view.z, static_cast<float>(tz * T), static_cast<float>(tz * T + T)) - m_view.z;
        if (nx * nx + nz * nz > r * r) continue;   // out of range: W2's (the sheet / far tiles)
        RecordTile& t = m_recordTiles[{tx, tz}];
        const bool wasWet = t.wet;
        buildRecordTile(tx, tz, t);
        remarkRecordedTile(tx, tz);   // 29 S1
        t.spanRev = sr; t.occRev = orv; t.avRev = m_avRevision;
        changed |= wasWet || t.wet;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    m_recordStats.buildMsMax = std::max(m_recordStats.buildMsMax, ms);
    m_recordStats.buildMsOnSleepLast = ms;
    if (changed) {
        m_recordBoxes.clear(); ++m_recordRevision;
        for (const auto& [key, t] : m_recordTiles)
            if (t.wet) m_recordBoxes.push_back({glm::ivec3(key.first * T, -1000000, key.second * T), glm::ivec3(key.first * T + T - 1, 1000000, key.second * T + T - 1)});
    }
}

void WaterCoreManager::writeSpans(const std::vector<ColumnRuns>& runs, float yLo, float yHi, long* unwritten, long* chunks) {
    const uint64_t before = m_spanRevision ? m_spanRevision() : 0;
    m_spanWriter(runs, yLo, yHi, unwritten, chunks);
    const uint64_t after = m_spanRevision ? m_spanRevision() : 0;
    if (!m_spanRevision || after == before || s_recordTilesAllStaleForTest) return;
    std::set<std::pair<int, int>> touched;
    const int T = kRecordTile;
    auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
    for (const ColumnRuns& c : runs) touched.insert({floorDiv(c.x, T), floorDiv(c.z, T)});
    for (auto& [key, t] : m_recordTiles)   // up to date before this write and not touched by it: still up to date
        if (t.spanRev == before && !touched.count(key)) t.spanRev = after;
}

void WaterCoreManager::remarkRecordedTile(int tx, int tz) {
    // 29 S1: a record tile's drawn level changed - the volumes holding it mark its columns with the new level, on the device too
    if (!WaterCoreGpu::s_gpuSurface) return;
    for (auto& up : m_avs) {
        Av& av = *up;
        if (!av.gpuVol || av.field.cols.size() != static_cast<size_t>(av.field.nx) * av.field.nz) continue;
        if (std::find(av.recordedTileList.begin(), av.recordedTileList.end(), std::make_pair(tx, tz)) == av.recordedTileList.end()) continue;
        markRecordedColumns(av, tx, tz); queueTileSurface(av, tx, tz);
    }
}

void WaterCoreManager::buildRecordTile(int tx, int tz, RecordTile& t) {
    const int T = kRecordTile, per = kMicroPerVoxel;   // the record is drawn at the 1/9 m lattice (the pond's)
    const int vy = static_cast<int>(std::floor(m_view.y));
    const glm::ivec3 lo(tx * T, vy - kRecordBandVoxels, tz * T), hi(tx * T + T - 1, vy + kRecordBandVoxels, tz * T + T - 1);
    auto tb = std::chrono::steady_clock::now();   // 27.19 measurement
    auto bpart = [&](int k) { const auto n = std::chrono::steady_clock::now(); s_tilePhases.build[k] = std::chrono::duration<double, std::milli>(n - tb).count(); tb = n; };
    std::vector<ColumnRuns> runs;
    m_spanReader(lo, hi, runs);
    // columns a live volume owns are its own to draw (the volume's field comes first in surfaceFields, this keeps them apart) -
    // except its RECORDED tiles (§27 W1b): the record holds and draws those
    auto owned = [&](int x, int z) {
        for (const auto& av : m_avs)
            if (x >= av->minVoxel.x && x <= av->maxVoxel.x && z >= av->minVoxel.z && z <= av->maxVoxel.z && !columnRecorded(*av, x, z)) return true;
        return false;
    };
    float yMin = 1e30f, yMax = -1e30f;
    std::vector<ColumnRuns> kept;
    for (auto& c : runs) {
        if (c.held || c.runs.empty() || owned(c.x, c.z)) continue;
        for (const auto& r : c.runs) { yMin = std::min(yMin, r.bottomY); yMax = std::max(yMax, r.topY); }
        kept.push_back(std::move(c));
    }
    t.wet = !kept.empty();
    if (!t.wet) { t.field = WaterSurfaceField{}; return; }
    const int y0 = static_cast<int>(std::floor(yMin)) - 1, y1 = static_cast<int>(std::ceil(yMax)) + 1;   // voxels: a cell of ground below, air above
    WaterGrid scratch(GridSpec{glm::ivec3(lo.x * per, y0 * per, lo.z * per), glm::ivec3(T * per, (y1 - y0) * per, T * per), 1.0f / static_cast<float>(per)});
    bpart(0);
    const glm::ivec3 o = scratch.spec().origin;
    // 27.19: inside a 1/9 m volume's box its occupancy cache IS this lookup (sampleOccupancy: one m_state per micro cell), cached -
    // read from it while it is current (5-7 ms of a tile's record call was 140 k m_state calls); elsewhere, or stale, the world
    const uint64_t rev = m_revision ? m_revision() : 0;
    std::vector<const Av*> caches;
    for (const auto& av : m_avs)
        if (av->per == per && !av->occDirty && av->occRevision == rev && av->occCache.size() == av->grid->cellCount()) caches.push_back(av.get());
    for (int z = 0; z < scratch.nz(); ++z) for (int x = 0; x < scratch.nx(); ++x) {
        const Av* src = nullptr;
        glm::ivec3 l(0);
        for (const Av* av : caches) {
            l = o + glm::ivec3(x, 0, z) - av->grid->spec().origin;
            if (l.x >= 0 && l.z >= 0 && l.x < av->grid->nx() && l.z < av->grid->nz()) { src = av; break; }
        }
        for (int y = 0; y < scratch.ny(); ++y) {
            const int ly = o.y + y - (src ? src->grid->spec().origin.y : 0);
            if (src && ly >= 0 && ly < src->grid->ny()) { scratch.occ(x, y, z) = src->occCache[src->grid->idx(l.x, ly, l.z)]; continue; }
            const int s = m_state ? m_state(o + glm::ivec3(x, y, z)) : 0;   // the cell IS a micro cell at 1/9 m
            scratch.occ(x, y, z) = s == 1 ? Occ::Solid : (s == 2 ? Occ::Unknown : Occ::Air);
        }
    }
    bpart(1);
    recordSurfaceField(scratch, kept, t.field);
    bpart(2);
    t.field.source = 0x40000000 | ((tx & 0x7FFF) << 15) | (tz & 0x7FFF);   // never a volume id (those count up from 1)
    t.field.version = ++m_recordBuilds;
    t.field.rippleLayer = -1;
    t.field.look = packLook(lookAt(tx * T + T / 2, tz * T + T / 2));
}

std::vector<std::pair<glm::vec3, float>> WaterCoreManager::takeMotion(float moveM) {
    auto hp = WaterHostProfile::Clock::now();   // 27.28
    struct Lap { WaterHostProfile::Clock::time_point& t; ~Lap() { WaterHostProfile::lap("motion.1 takeMotion scan", t); } } lapAtExit{hp};
    std::vector<std::pair<glm::vec3, float>> out;
    m_motionStats = MotionStats{};
    if (m_motionPrevTops.size() != m_fields.size()) { m_motionPrevTops.assign(m_fields.size(), {}); m_motionSeen.assign(m_fields.size(), {}); }
    for (size_t fi = 0; fi < m_fields.size(); ++fi) {
        const WaterSurfaceField& f = *m_fields[fi];
        if (f.gpuDrawn && m_gpu) {   // 29 S2: the device scanned it in the water's call (the covered columns; a frozen column cannot move)
            Av* av = nullptr;
            for (auto& a : m_avs) if (a->id == f.source) av = a.get();
            WaterCoreGpu::Volume::Motion mo;
            if (av && av->gpuVol && m_gpu->motionResult(*av->gpuVol, mo, true)) {
                MotionSeen& seen = m_motionSeen[fi];
                if (!mo.fresh) { m_motionStats.maxFlow = std::max(m_motionStats.maxFlow, seen.maxFlow); m_motionStats.flowCols += seen.flowCols; continue; }
                seen = MotionSeen{f.source, f.version, mo.maxFlow, mo.flowCols};
                m_motionStats.maxRise = std::max(m_motionStats.maxRise, mo.maxRise); m_motionStats.maxFlow = std::max(m_motionStats.maxFlow, mo.maxFlow);
                m_motionStats.riseCols += mo.riseCols; m_motionStats.flowCols += mo.flowCols;
                if (mo.riseCols <= 0) continue;
                const glm::vec2 lo((f.origin.x + mo.x0 + 0.5f) * f.h, (f.origin.z + mo.z0 + 0.5f) * f.h), hi((f.origin.x + mo.x1 + 0.5f) * f.h, (f.origin.z + mo.z1 + 0.5f) * f.h);
                const glm::vec3 c(0.5f * (lo.x + hi.x), 0.5f * (mo.y0 + mo.y1), 0.5f * (lo.y + hi.y));
                const float r = 0.5f * glm::length(glm::vec3(hi.x - lo.x, mo.y1 - mo.y0, hi.y - lo.y)) + f.h;
                out.push_back({c, r}); ++m_motionStats.spheres; m_motionStats.lastRadius = r;
                continue;
            }
        }
        // 26.30 step 3: the same field as the last scan (source + version): no column moved since, so nothing rose; its flow
        // figures stand (the scan cost 0.24 ms a frame on a sleeping 20 x 20 m pond)
        MotionSeen& seen = m_motionSeen[fi];
        if (f.version != 0 && seen.source == f.source && seen.version == f.version) {
            m_motionStats.maxFlow = std::max(m_motionStats.maxFlow, seen.maxFlow); m_motionStats.flowCols += seen.flowCols;
            continue;
        }
        const float maxFlowBefore = m_motionStats.maxFlow; const int flowColsBefore = m_motionStats.flowCols;
        m_motionStats.maxFlow = 0.0f;
        std::vector<float>& prev = m_motionPrevTops[fi];
        const bool havePrev = prev.size() == f.cols.size();
        std::vector<float> cur(f.cols.size(), std::numeric_limits<float>::quiet_NaN());
        glm::vec2 lo(1e30f), hi(-1e30f); float yLo = 1e30f, yHi = -1e30f; bool any = false;
        for (int z = 0; z < f.nz; ++z) for (int x = 0; x < f.nx; ++x) {
            const size_t i = static_cast<size_t>(x) + static_cast<size_t>(f.nx) * z;
            const SurfaceColumn& c = f.cols[i];
            const int runs = static_cast<int>(c.runs + 0.5f);
            if (runs <= 0) continue;
            // 20 (M2): a body at the top of the column - its drawn level is the body's (it flips ~25 cm as a floater
            // shifts across the cell), not water motion; counted, it woke every floater every frame (10 floaters
            // never settled). No reference is kept, so the column leaving the body does not read as motion either.
            if (c.bodyAtSurface > 0.0f) continue;
            const float top = c.top[std::min(runs, kSurfaceMaxRuns) - 1];
            const bool haveRef = havePrev && !std::isnan(prev[i]);
            const float dTop = haveRef ? std::abs(top - prev[i]) : 0.0f;
            cur[i] = (haveRef && dTop <= moveM) ? prev[i] : top;   // the reference moves only when the surface did
            const float flow = std::sqrt(c.u * c.u + c.w * c.w);
            m_motionStats.maxRise = std::max(m_motionStats.maxRise, dTop); m_motionStats.maxFlow = std::max(m_motionStats.maxFlow, flow);
            const bool rose = dTop > moveM;
            m_motionStats.riseCols += rose; m_motionStats.flowCols += flow > 0.08f;
            if (!rose) continue;
            const glm::vec2 wc((f.origin.x + x + 0.5f) * f.h, (f.origin.z + z + 0.5f) * f.h);
            lo = glm::min(lo, wc); hi = glm::max(hi, wc); yLo = std::min(yLo, top); yHi = std::max(yHi, top); any = true;
        }
        prev.swap(cur);
        seen = MotionSeen{f.source, f.version, m_motionStats.maxFlow, m_motionStats.flowCols - flowColsBefore};
        m_motionStats.maxFlow = std::max(m_motionStats.maxFlow, maxFlowBefore);
        if (!any) continue;
        const glm::vec3 c(0.5f * (lo.x + hi.x), 0.5f * (yLo + yHi), 0.5f * (lo.y + hi.y));
        const float r = 0.5f * glm::length(glm::vec3(hi.x - lo.x, yHi - yLo, hi.y - lo.y)) + f.h;
        out.push_back({c, r}); ++m_motionStats.spheres; m_motionStats.lastRadius = r;
    }
    return out;
}

void WaterCoreManager::collectBirths(Av& av, float dt) {
    if (!m_dstats.on || av.transport != "eulerian" || av.solver->transport().ownsMass()) return;
    std::vector<DropletBirth> births;
    if (av.backend == "gpu") {
        std::vector<float> rec;
        // 26.30 step 3: the packed list - only the columns with births (it spread them over every column and scanned them all)
        if (!av.gpuVol || !m_gpu || !m_gpu->readBirthRecords(*av.gpuVol, rec)) return;
        const GridSpec& sp = av.grid->spec();
        const double V = static_cast<double>(av.h) * av.h * av.h;
        std::vector<std::pair<uint32_t, size_t>> order;   // column order, as the dense scan visited them (the pool's spawn order)
        for (size_t k = 0; k + 7 < rec.size(); k += 8) { uint32_t c; std::memcpy(&c, &rec[k + 7], 4); if (c < static_cast<uint32_t>(sp.dims.x * sp.dims.z)) order.push_back({c, k}); }
        std::sort(order.begin(), order.end());
        for (size_t oi = 0; oi < order.size(); ++oi) {
            if (oi + 1 < order.size() && order[oi + 1].first == order[oi].first) continue;   // a column listed twice: the later record (as readBirths)
            const uint32_t c = order[oi].first; const size_t k = order[oi].second;
            const float vol = rec[k];
            if (vol <= 0.0f) continue;
            const int x = static_cast<int>(c % static_cast<uint32_t>(sp.dims.x)), z = static_cast<int>(c / static_cast<uint32_t>(sp.dims.x));
            DropletBirth b;
            b.volume = static_cast<float>(vol * V);
            b.pos = glm::vec3((sp.origin.x + x + 0.5f) * av.h, (sp.origin.y + rec[k + 1] / vol) * av.h, (sp.origin.z + z + 0.5f) * av.h);
            b.vel = glm::vec3(rec[k + 2], rec[k + 3], rec[k + 4]) / vol;
            b.column = static_cast<int>(c);
            births.push_back(b);
        }
    } else {
        const BirthReport r = birthDroplets(*av.grid, dt, av.solver->params().gravity, births);
        m_dstats.scraps += r.scraps; m_dstats.sprays += r.sprays;
    }
    ++m_dropletTick;
    for (const DropletBirth& b : births) {
        m_dstats.bornM3 += b.volume; ++m_dstats.births;
        const double refused = m_pool.spawn(b, av.id, av.h, m_dstats.size, m_dropletTick);
        if (refused > 0.0) { m_dstats.refusedM3 += refused; depositDroplet(av, b.pos, refused); }   // pool full: straight back
    }
}

void WaterCoreManager::depositDroplet(Av& av, const glm::vec3& at, double volume) {
    if (volume <= 0.0) return;
    if (av.backend == "gpu" && av.gpuVol && m_gpu) {
        // §27 W1b: a landing on a recorded tile wakes it first - its cells are the GROUND sentinel's until then, and the deposit
        // would have written its water into the sentinel every ground brick shares
        // would have written its water into the sentinel every ground brick shares. Deferred: the deposit lands at the next call, and
        // the tile is woken just before it (27.13: a wake here, after the call was submitted, collected it early)
        wakeRecordedNear(av, at, 0.0f, true);
        // the GPU applies it at its next step (wc_column_ops mode 5); the cell is chosen as depositIntoGrid does
        const WaterGrid& g = *av.grid;
        glm::ivec3 c = g.worldToCell(at);
        c.x = std::clamp(c.x, 0, g.nx() - 1); c.z = std::clamp(c.z, 0, g.nz() - 1); c.y = std::clamp(c.y, 0, g.ny() - 1);
        while (c.y < g.ny() - 1 && av.occCache[g.idx(c.x, c.y, c.z)] != Occ::Air) ++c.y;
        if (av.pendingDeposit.size() != g.cellCount()) av.pendingDeposit.assign(g.cellCount(), 0.0f);
        av.pendingDeposit[g.idx(c.x, c.y, c.z)] += static_cast<float>(volume / g.cellVolume());
        av.hasPendingDeposit = true;
        m_gpu->wakeBox(*av.gpuVol, c, c);   // 26.31: the landing's column (the partial wake; a no-op without it)
        return;
    }
    const double left = depositIntoGrid(*av.grid, at, volume);
    av.solver->wake();
    if (left > 0.0) {   // a ceiling over it: it flies again (never dropped)
        DropletBirth b; b.pos = at; b.volume = static_cast<float>(left);
        const double refused = m_pool.spawn(b, av.id, av.h, m_dstats.size, m_dropletTick);
        if (refused > 0.0) av.grid->f(0, 0, 0) += static_cast<float>(refused / av.grid->cellVolume());   // unreachable in practice (pool full AND a ceiling): kept in the grid, mass exact
    }
}

void WaterCoreManager::stepDroplets(float dt) {
    m_dropletDraw.clear();
    if (m_pool.droplets().empty()) { m_dstats.alive = 0; return; }
    surfaceFields();   // each volume's field from its last step (cached per step)
    const float g = m_avs.empty() ? 9.81f : m_avs.front()->solver->params().gravity;
    std::vector<Droplet> landed;
    std::vector<glm::vec3> landAt;
    auto land = [&](Droplet& d, const glm::vec3& prev) -> bool {
        (void)prev;
        Av* av = volumeById(d.volumeId);
        if (!av) { landAt.push_back(d.pos); return true; }   // its volume is gone (destroy already discarded it; defensive)
        const WaterSurfaceField& f = av->field;
        if (f.nx <= 0 || f.cols.empty()) return false;
        int cx = static_cast<int>(std::floor(d.pos.x / f.h)) - f.origin.x, cz = static_cast<int>(std::floor(d.pos.z / f.h)) - f.origin.z;
        const bool outside = cx < 0 || cz < 0 || cx >= f.nx || cz >= f.nz;
        cx = std::clamp(cx, 0, f.nx - 1); cz = std::clamp(cz, 0, f.nz - 1);
        const SurfaceColumn& c = f.at(cx, cz);
        const glm::vec3 colXZ((f.origin.x + cx + 0.5f) * f.h, 0.0f, (f.origin.z + cz + 0.5f) * f.h);
        const int runs = std::min(static_cast<int>(c.runs + 0.5f), kSurfaceMaxRuns);
        float top = -1e30f;
        for (int r = 0; r < runs; ++r) if (d.pos.y >= c.bottom[r] - 0.5f * f.h && d.pos.y <= c.top[r]) top = std::max(top, c.top[r]);
        // the ground: the highest solid cell in the column, or the box's floor - the solver treats everything outside the
        // grid as solid, so a box whose bottom IS the floor has no solid cell inside it (D-T1: its droplets fell forever)
        const float ground = std::max(c.solidTopY, static_cast<float>(f.origin.y) * f.h);
        if (outside) {   // left its volume's box: lands at the nearest column's surface (21.9 F1 - no wall in mid-air), counted
            const float y = runs > 0 ? c.top[runs - 1] : ground;
            if (d.pos.y > y) return false;   // still above the water there: let it fly
            m_dstats.relocatedM3 += d.volume;
            landAt.push_back(glm::vec3(colXZ.x, y - 0.01f, colXZ.z));
            return true;
        }
        if (d.vel.y <= 0.0f && top > -1e29f) { landAt.push_back(glm::vec3(d.pos.x, std::min(d.pos.y, top - 0.01f), d.pos.z)); return true; }   // into the water
        if (d.pos.y <= ground) { landAt.push_back(glm::vec3(d.pos.x, ground + 0.01f, d.pos.z)); return true; }                                 // onto the ground
        if (d.age > 10.0f) { landAt.push_back(glm::vec3(d.pos.x, runs > 0 ? c.top[runs - 1] - 0.01f : ground + 0.01f, d.pos.z)); return true; }   // defensive: never fly forever
        return false;
    };
    m_pool.step(dt, g, land, landed);
    for (size_t i = 0; i < landed.size(); ++i) {
        Av* av = volumeById(landed[i].volumeId);
        if (!av) continue;
        m_dstats.landedM3 += landed[i].volume; ++m_dstats.landings;
        if (av->ripple && m_rstats.on && landed[i].vel.y < 0.0f) {   // 22.3.1: the drop's downward momentum, per unit area of its lattice cell
            const float d = RippleLayer::kPitch;
            av->ripple->addImpulse(landAt[i].x, landAt[i].z, 1000.0f * landed[i].volume * -landed[i].vel.y / (d * d));
            ++m_rstats.impulses;
        }
        depositDroplet(*av, landAt[i], landed[i].volume);
    }
    for (auto& av : m_avs)
        if (av->hasPendingDeposit && av->gpuVol && m_gpu) { m_gpu->setDeposits(*av->gpuVol, av->pendingDeposit); std::fill(av->pendingDeposit.begin(), av->pendingDeposit.end(), 0.0f); av->hasPendingDeposit = false; av->gpuLast.asleep = false; }
    Phyxel::Core::Water::dropletDrawList(m_pool, m_dropletDraw);
    m_dstats.alive = static_cast<int>(m_pool.droplets().size());
}

std::vector<const RippleLayer*> WaterCoreManager::rippleLayers() const {
    std::vector<const RippleLayer*> out;
    for (const auto& av : m_avs) out.push_back(m_rstats.on ? av->ripple.get() : nullptr);
    return out;
}

RippleLayer& WaterCoreManager::rippleWindow(std::unique_ptr<RippleLayer>& layer, const glm::ivec3& minVoxel, const glm::ivec3& maxVoxel, bool gpu) {
    const int boxX = (maxVoxel.x - minVoxel.x + 1) * 9, boxZ = (maxVoxel.z - minVoxel.z + 1) * 9;
    const int lnx = std::min(kColumnRippleWindow, boxX), lnz = std::min(kColumnRippleWindow, boxZ);
    const glm::ivec2 lo(minVoxel.x * 9, minVoxel.z * 9);
    const glm::ivec2 hi((maxVoxel.x + 1) * 9 - lnx, (maxVoxel.z + 1) * 9 - lnz);
    glm::ivec2 want(static_cast<int>(std::floor(m_view.x * 9.0f)) - lnx / 2, static_cast<int>(std::floor(m_view.z * 9.0f)) - lnz / 2);
    want = glm::clamp(want, lo, glm::max(lo, hi));
    if (!layer) layer = std::make_unique<RippleLayer>(want, lnx, lnz, 9.81f);   // the border absorbs (RippleLayer's default - the pond's layer always had it)
    RippleLayer& L = *layer;
    // the window moves in 4 m steps (36 cells) - not every frame; the field keeps its world position across a move
    if (std::abs(want.x - L.origin().x) > 36 || std::abs(want.y - L.origin().y) > 36) {
        if (gpu) m_rgpu->syncPrev(L, liveRippleLayers());   // 25.34: the shift moves BOTH levels - the ring drained, the previous one back
        L.shiftOrigin(want);
        if (gpu) m_rgpu->markDirty(L);
    }
    return L;
}

void WaterCoreManager::stepRipples(float dt) {
    const auto t0 = std::chrono::steady_clock::now();
    m_rstats.layers = 0; m_rstats.awake = 0; m_rstats.cells = 0; m_rstats.kinematicCells = 0;
    if (!m_rstats.on) { m_rstats.ms = 0.0; return; }
    auto hpr = WaterHostProfile::Clock::now();   // 27.28
    surfaceFields();   // masks and body waterlines read each volume's field (cached per step)
    WaterHostProfile::lap("ripples.1 surfaceFields (= surf.*)", hpr);
    const bool gpu = rippleGpuOn();
    using RClock = std::chrono::steady_clock;
    auto rms = [](RClock::time_point a) { return std::chrono::duration<double, std::milli>(RClock::now() - a).count(); };
    { const auto tc = RClock::now(); if (gpu) m_rgpu->collect(liveRippleLayers()); m_updateCpu.rCollect = rms(tc); }   // 25.34: last frame's GPU step into the layers (heights, sleep)
    const float d = RippleLayer::kPitch;
    for (auto& up : m_avs) {
        Av& av = *up;
        if (av.transport != "eulerian" || av.solver->transport().ownsMass()) continue;
        if (!av.ripple && !av.rippleRefused) {
            int live = 0; for (const auto& a : m_avs) live += a->ripple ? 1 : 0;
            if (live >= kRippleMaxLayers) { av.rippleRefused = true; ++m_rstats.refused; continue; }   // the renderer's atlas: refused, never coarsened (22.5)
        }
        if (av.rippleRefused) continue;
        // 26.13: a WINDOW over the volume (the regions' rule - one code path); a pond that fits is the whole box,
        // exactly the layer it had (the 512-cell refusal is gone: a 100 m creek volume gets a window instead of nothing)
        RippleLayer& L = rippleWindow(av.ripple, av.minVoxel, av.maxVoxel, gpu);
        const WaterSurfaceField& f = av.field;
        if (f.nx <= 0 || f.cols.empty()) continue;
        auto column = [&](float wx, float wz) -> const SurfaceColumn* {
            const int cx = static_cast<int>(std::floor(wx / f.h)) - f.origin.x, cz = static_cast<int>(std::floor(wz / f.h)) - f.origin.z;
            return (cx < 0 || cz < 0 || cx >= f.nx || cz >= f.nz) ? nullptr : &f.at(cx, cz);
        };
        // the mask: water where the field's sub-column holds a run (solid and dry are masked: rings reflect)
        auto& mask = L.mask();
        const auto tMask = RClock::now();
        // 26.30 step 3: only when the window moved or a column gained or lost its water - 0.70 ms a frame on a sleeping 20 x 20 m
        // pond, ~1.5 ms on an awake 32 x 28 m one (a new field every step, the same water columns)
        const glm::ivec4 maskKey(L.origin().x, L.origin().y, L.nx(), L.nz());
        bool wetChanged = f.version == 0 || av.rippleMaskVersion != f.version;
        if (wetChanged && av.rippleMaskVersion != 0 && av.rippleWet.size() == f.cols.size()) {
            wetChanged = false;
            for (size_t i = 0; i < f.cols.size(); ++i) {
                const uint8_t wet = f.cols[i].runs >= 0.5f ? 1 : 0;
                if (wet != av.rippleWet[i]) { wetChanged = true; break; }
            }
            av.rippleMaskVersion = f.version;
        }
        if (wetChanged || av.rippleMaskKey != maskKey || mask.size() != static_cast<size_t>(L.nx()) * L.nz()) {
            av.rippleWet.resize(f.cols.size());
            for (size_t i = 0; i < f.cols.size(); ++i) av.rippleWet[i] = f.cols[i].runs >= 0.5f ? 1 : 0;
            for (int z = 0; z < L.nz(); ++z) for (int x = 0; x < L.nx(); ++x) {
                const SurfaceColumn* c = column((L.origin().x + x + 0.5f) * d, (L.origin().y + z + 0.5f) * d);
                mask[L.idx(x, z)] = (c && c->runs >= 0.5f) ? 1 : 0;
            }
            av.rippleMaskVersion = f.version; av.rippleMaskKey = maskKey;
        }
        m_updateCpu.rMask += rms(tMask);
        if (m_rpattern.slope >= 0.0f) {   // 22.11 measurement: a uniform tilt, held (no bodies, no step)
            const float a = glm::radians(m_rpattern.azimuthDeg), gx = m_rpattern.slope * std::cos(a) * d, gz = m_rpattern.slope * std::sin(a) * d;
            std::vector<float> hp(static_cast<size_t>(L.nx()) * L.nz());
            for (int z = 0; z < L.nz(); ++z) for (int x = 0; x < L.nx(); ++x) hp[L.idx(x, z)] = gx * (x - 0.5f * L.nx()) + gz * (z - 0.5f * L.nz());
            L.pin(hp);
            if (gpu) m_rgpu->markDirty(L);   // 25.34: the pinned levels go up before any GPU step
        }
        // 22.3.2: bodies crossing the surface - the remainder of the kinematic condition the grid cannot hold
        const int per = std::max(1, static_cast<int>(std::lround(f.h / d)));   // lattice cells per grid column (3 at 1/3 m)
        for (const MovingSolid& b : m_rippleBodies) {
            if (std::abs(b.velocity.y) < 0.01f) continue;
            const SurfaceColumn* cc = column(b.centre.x, b.centre.z);
            if (!cc || cc->runs < 0.5f) continue;
            const float top = cc->top[std::min(static_cast<int>(cc->runs + 0.5f), kSurfaceMaxRuns) - 1];
            if (!(b.centre.y - b.halfExtents.y < top && b.centre.y + b.halfExtents.y > top)) continue;   // not at the waterline
            const glm::vec2 lo(b.centre.x - b.halfExtents.x, b.centre.z - b.halfExtents.z), hi(b.centre.x + b.halfExtents.x, b.centre.z + b.halfExtents.z);
            // grid columns the footprint touches, in lattice coordinates (world-aligned: column c spans lattice [c per, c per + per))
            const int gx0 = static_cast<int>(std::floor(lo.x / f.h)), gx1 = static_cast<int>(std::floor(hi.x / f.h));
            const int gz0 = static_cast<int>(std::floor(lo.y / f.h)), gz1 = static_cast<int>(std::floor(hi.y / f.h));
            for (int gz = gz0; gz <= gz1; ++gz) for (int gx = gx0; gx <= gx1; ++gx) {
                float fine[9 * 9]; float sum = 0.0f; const int n = std::min(per, 9);
                for (int j = 0; j < n; ++j) for (int i = 0; i < n; ++i) {
                    const float x0 = (gx * per + i) * d, z0 = (gz * per + j) * d;
                    const float ox = std::max(0.0f, std::min(x0 + d, hi.x) - std::max(x0, lo.x)), oz = std::max(0.0f, std::min(z0 + d, hi.y) - std::max(z0, lo.y));
                    fine[j * n + i] = ox * oz / (d * d); sum += fine[j * n + i];
                }
                const float coarse = sum / static_cast<float>(n * n);
                for (int j = 0; j < n; ++j) for (int i = 0; i < n; ++i) {
                    const float vhp = b.velocity.y * (fine[j * n + i] - coarse);
                    if (std::abs(vhp) < 1e-5f) continue;
                    L.setKinematic(gx * per + i - L.origin().x, gz * per + j - L.origin().y, vhp);
                    ++m_rstats.kinematicCells;
                }
            }
        }
        // 26.13: a volume with a bed roughness (a creek) is CARRIED by its surface velocity and FORCED by the flow's bed shear -
        // the regions' rule (25.26 RC) read from the volume's surface field; a pond (n = 0) steps exactly as before
        const float nBed = av.solver->params().bedManning;
        std::vector<glm::vec2> vel;
        if (nBed > 0.0f && m_rpattern.slope < 0.0f) {
            const int nx = L.nx(), nz = L.nz();
            vel.assign(static_cast<size_t>(nx) * nz, glm::vec2(0.0f));
            const uint32_t frame = ++av.rippleFrame;
            std::vector<float> raw;
            for (int z = 0; z < nz; ++z) for (int x = 0; x < nx; ++x) {
                const size_t i = L.idx(x, z);
                if (!mask[i]) continue;
                const SurfaceColumn* c = column((L.origin().x + x + 0.5f) * d, (L.origin().y + z + 0.5f) * d);
                if (!c) continue;
                vel[i] = glm::vec2(c->u, c->w);
                if (m_flowForcing <= 0.0f) continue;
                const float speed = std::sqrt(c->u * c->u + c->w * c->w);
                if (speed < 1e-3f) continue;
                const int top = std::min(static_cast<int>(c->runs + 0.5f), kSurfaceMaxRuns) - 1;
                const float depth = std::max(c->top[top] - c->bottom[top], 1e-3f);
                const float ustar = speed * nBed * std::sqrt(9.81f) / std::pow(depth, 1.0f / 6.0f);
                const float a = m_flowForcing * 1000.0f * ustar * ustar * dt;
                // fade over the window's outer 8 m where its border is open (the ripples die away before the absorbing rim)
                const float edge = static_cast<float>(std::min(std::min(x, z), std::min(nx - 1 - x, nz - 1 - z)));
                const float fade = std::clamp(edge / 72.0f, 0.0f, 1.0f);
                if (fade <= 0.0f) continue;
                uint32_t hh = static_cast<uint32_t>(L.origin().x + x) * 73856093u ^ static_cast<uint32_t>(L.origin().y + z) * 19349663u ^ frame * 83492791u;
                hh ^= hh >> 13; hh *= 0x5bd1e995u; hh ^= hh >> 15;
                const float xi = (hh & 0xFFFFu) / 32767.5f - 1.0f;
                if (raw.empty()) raw.assign(static_cast<size_t>(nx) * nz, 0.0f);
                raw[i] = a * xi * fade;
            }
            if (!raw.empty()) L.addImpulseField(raw);
        }
        L.setBreakSlope(m_rstats.breakSlope);
        { const auto tl = RClock::now(); if (m_rpattern.slope < 0.0f) stepLayer(L, dt, vel.empty() ? nullptr : &vel); m_updateCpu.rLayer += rms(tl); }
        m_rstats.brokenM3 += L.takeBroken();
        ++m_rstats.layers; m_rstats.cells += static_cast<long>(L.nx()) * L.nz();
        if (!L.asleep()) ++m_rstats.awake;   // the shape stats: refreshRippleShapeStats (on demand)
    }
    WaterHostProfile::lap("ripples.2 masks + layers", hpr);
    m_rstats.gpu = gpu;
    if (gpu) {
        { const auto ts = RClock::now(); m_rgpu->submit(); m_updateCpu.rSubmit = rms(ts); }   // 25.34: no wait - the next frame collects it
        const auto& g = m_rgpu->stats();
        m_rstats.gpuMs = g.gpuMs; m_rstats.gpuWaitMs = g.waitMs; m_rstats.gpuRecordMs = g.recordMs; m_rstats.gpuCollectMs = g.collectMs; m_rstats.gpuDispatches = g.dispatches; m_rstats.gpuLayers = g.layers;
    }
    m_rstats.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

std::vector<WaterCoreManager::JetRun> WaterCoreManager::scanDetachedRuns() {
    std::vector<JetRun> out;
    std::vector<float> f, s;
    for (auto& up : m_avs) {
        Av& av = *up;
        if (av.transport != "eulerian" || !av.grid) continue;
        const glm::ivec3 dims = av.grid->spec().dims;
        if (av.backend == "gpu" && av.gpuVol && m_gpu) { m_gpu->readCells(*av.gpuVol, "f", f); m_gpu->readCells(*av.gpuVol, "solid_s", s); }
        else { f = av.grid->fData(); s = av.grid->sData(); }
        if (f.size() != av.grid->cellCount()) continue;
        const bool haveS = s.size() == f.size();
        for (const DetachedRun& r : findDetachedRuns(dims, f.data(), haveS ? s.data() : nullptr, 0.001f / av.h)) {
            JetRun j; j.bottom = av.grid->cellCenterWorld(r.bottom.x, r.bottom.y, r.bottom.z);
            j.sumF = r.sumF; j.volume = r.sumF * av.h * av.h * av.h; j.cells = r.cells; j.isolated = r.isolated;
            out.push_back(j);
        }
    }
    return out;
}

bool WaterCoreManager::surfaceAtWorld(float x, float z, float& level, float& top) const {
    for (const WaterSurfaceField* fp : m_fields) {
        const WaterSurfaceField& f = *fp;
        const int cx = static_cast<int>(std::floor(x / f.h)) - f.origin.x, cz = static_cast<int>(std::floor(z / f.h)) - f.origin.z;
        if (cx < 0 || cz < 0 || cx >= f.nx || cz >= f.nz) continue;
        const SurfaceColumn& c = f.at(cx, cz);
        const int runs = std::min(static_cast<int>(c.runs + 0.5f), kSurfaceMaxRuns);
        if (runs <= 0) return false;
        level = c.top[0]; top = c.top[runs - 1];
        return true;
    }
    return false;
}

bool WaterCoreManager::drawnAtWorld(float x, float z, float& top, bool& recorded) const {
    bool any = false; top = -1e30f; recorded = false;
    for (const WaterSurfaceField* fp : m_fields) {
        const WaterSurfaceField& f = *fp;
        const int cx = static_cast<int>(std::floor(x / f.h)) - f.origin.x, cz = static_cast<int>(std::floor(z / f.h)) - f.origin.z;
        if (cx < 0 || cz < 0 || cx >= f.nx || cz >= f.nz) continue;
        const SurfaceColumn& c = f.at(cx, cz);
        const int runs = std::min(static_cast<int>(c.runs + 0.5f), kSurfaceMaxRuns);
        if (runs <= 0) continue;
        any = true; top = std::max(top, c.top[runs - 1]);
    }
    const int wx = static_cast<int>(std::floor(x)), wz = static_cast<int>(std::floor(z));
    for (const auto& av : m_avs) if (columnRecorded(*av, wx, wz)) recorded = true;
    return any;
}

double WaterCoreManager::recordedWaterIn(const glm::ivec3& lo, const glm::ivec3& hi) const {
    if (!m_spanReader) return -1.0;
    std::vector<ColumnRuns> runs;
    m_spanReader(lo, hi, runs);
    double m = 0.0;
    for (const auto& c : runs) for (const auto& r : c.runs) m += static_cast<double>(r.topY - r.bottomY);
    return m;
}

int WaterCoreManager::dryRecordedColumns() const {
    // 27.16 measurement: columns a volume's field marks DRY (runs 0) inside its recorded tiles - the record draws them, the
    // volume's mesh walls them off as dry ground (a wall from its surface to the bed); a correct hand-over marks them -1
    int n = 0;
    for (const auto& av : m_avs) {
        const WaterSurfaceField& f = av->field;
        if (av->recordedTileList.empty() || f.cols.size() != static_cast<size_t>(f.nx) * f.nz) continue;
        const glm::ivec3 so = f.origin;
        for (const auto& [tx, tz] : av->recordedTileList) {
            const int x0 = std::max(0, tx * kTileVoxels * av->per - so.x), x1 = std::min(f.nx - 1, (tx + 1) * kTileVoxels * av->per - 1 - so.x);
            const int z0 = std::max(0, tz * kTileVoxels * av->per - so.z), z1 = std::min(f.nz - 1, (tz + 1) * kTileVoxels * av->per - 1 - so.z);
            for (int z = z0; z <= z1; ++z) for (int x = x0; x <= x1; ++x) if (f.at(x, z).runs > -0.5f) ++n;
        }
    }
    return n;
}

uint64_t WaterCoreManager::fieldsSignature(int x0, int z0, int x1, int z1) const {
    uint64_t sig = 1469598103934665603ull;
    int owners = 0;
    auto mix = [&sig](uint64_t v) { sig = (sig ^ v) * 1099511628211ull; sig ^= sig >> 29; };
    auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
    for (const WaterSurfaceField* fp : m_fields) {
        const WaterSurfaceField& f = *fp;
        const int per = std::max(1, static_cast<int>(std::lround(1.0f / f.h)));
        // the world columns columnWater answers from this field: wx * per - origin.x in [0, nx - per]
        const int fx0 = -floorDiv(-f.origin.x, per), fz0 = -floorDiv(-f.origin.z, per);
        const int fx1 = floorDiv(f.origin.x + f.nx - per, per), fz1 = floorDiv(f.origin.z + f.nz - per, per);
        if (fx1 < x0 || fx0 > x1 || fz1 < z0 || fz0 > z1) continue;
        if (f.version == 0) return 0;
        ++owners;
        mix(static_cast<uint64_t>(f.source)); if (!s_signatureIgnoresVersionForTest) mix(f.version);
    }
    {   // 32: where no field owns a column, columnWater reads the record - the tile's key carries what the record holds there
        bool recorded = false;
        for (int cz = floorDiv(z0, 32); cz <= floorDiv(z1, 32); ++cz)
            for (int cx = floorDiv(x0, 32); cx <= floorDiv(x1, 32); ++cx) {
                const RecordCol& e = recordCol(cx, cz);
                if (e.any) { recorded = true; mix(e.gen); mix((static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) | static_cast<uint32_t>(cz)); }
            }
        if (owners == 0 && !recorded) return kNoFieldsSignature;
    }
    return sig <= kNoFieldsSignature ? sig + 2 : sig;   // never 0 (unknown) or 1 (no fields)
}

bool WaterCoreManager::columnWater(int wx, int wz, float& surfaceY, glm::vec2& flow) const {
    for (const WaterSurfaceField* fp : m_fields) {
        const WaterSurfaceField& f = *fp;
        const int per = std::max(1, static_cast<int>(std::lround(1.0f / f.h)));
        const int bx = wx * per - f.origin.x, bz = wz * per - f.origin.z;
        if (bx < 0 || bz < 0 || bx + per > f.nx || bz + per > f.nz) continue;
        // 20 (M2): skip sub-columns whose top holds a moving solid - the level there is the body's own, and a
        // floater reading it is a feedback loop (its displacement moved the surface it reads two frames later:
        // one wood piece bobbed forever, 16.33 <-> 16.58 m at its own column). Buoyancy refers to the water
        // AROUND it; only when every sub-column holds a body are they used.
        bool anyClear = false;
        for (int k = 0; k < per && !anyClear; ++k) for (int i = 0; i < per; ++i) { const SurfaceColumn& c = f.at(bx + i, bz + k); if (c.runs >= 0.5f && c.bodyAtSurface <= 0.0f) { anyClear = true; break; } }
        float top = -1e30f; glm::vec2 sum(0.0f); int wet = 0;
        for (int k = 0; k < per; ++k) for (int i = 0; i < per; ++i) {
            const SurfaceColumn& c = f.at(bx + i, bz + k);
            const int runs = static_cast<int>(c.runs + 0.5f);
            if (runs <= 0) {
                // 32: a recorded tile's column inside a live volume (runs -1, the record's level in top[0] over bottom[0], 27.15) is
                // water - read as dry, a floater over a recorded tile sank whatever the camera did
                if (c.runs < 0.0f && c.top[0] > c.bottom[0]) { top = std::max(top, c.top[0]); ++wet; }
                continue;
            }
            if (anyClear && c.bodyAtSurface > 0.0f) continue;
            top = std::max(top, c.top[std::min(runs, kSurfaceMaxRuns) - 1]);
            sum += glm::vec2(c.u, c.w); ++wet;
        }
        if (wet == 0) return false;   // the volume owns this column and it is dry
        surfaceY = top; flow = sum / static_cast<float>(wet);
        return true;
    }
    // 32: no field owns the column - the record itself (the chunk spans), wherever the camera is
    if (recordColumnWater(wx, wz, surfaceY)) { flow = glm::vec2(0.0f); return true; }
    return false;
}

namespace {
// 37: water intervals [bottom, top] of one world column (sub-column runs, span runs) merged where they overlap or touch, the two
// highest kept top-down, the rest counted. Few merged runs in practice (one, for a plain pond), so merging into a short list costs
// O(intervals) - no sort of the up to 81 x 4 sub-column runs.
struct RunMerge {
    static constexpr int kCap = 8;
    float b[kCap], t[kCap]; int n = 0, dropped = 0;
    void add(float bottom, float top) {
        if (!(top > bottom + 1e-4f)) return;
        for (int i = 0; i < n;) {
            if (top >= b[i] - 1e-4f && bottom <= t[i] + 1e-4f) {   // overlaps or touches: absorb it, re-scan (the union may reach others)
                bottom = std::min(bottom, b[i]); top = std::max(top, t[i]);
                b[i] = b[n - 1]; t[i] = t[n - 1]; --n; i = 0; continue;
            }
            ++i;
        }
        if (n < kCap) { b[n] = bottom; t[n] = top; ++n; } else ++dropped;   // past 8 separate runs in one column: counted, read dry
    }
    void out(WaterCoreManager::ColumnLevels& o) const {
        int idx[kCap]; for (int i = 0; i < n; ++i) idx[i] = i;
        std::sort(idx, idx + n, [this](int a, int c) { return t[a] > t[c]; });
        o.n = std::min(n, 2); o.more = std::max(0, n - 2) + dropped;
        for (int k = 0; k < 2; ++k) { o.top[k] = k < n ? t[idx[k]] : -1e30f; o.bottom[k] = k < n ? b[idx[k]] : -1e30f; }
    }
};
}  // namespace

bool WaterCoreManager::columnRuns(int wx, int wz, ColumnLevels& out) const {
    out = ColumnLevels{};
    for (const WaterSurfaceField* fp : m_fields) {
        const WaterSurfaceField& f = *fp;
        const int per = std::max(1, static_cast<int>(std::lround(1.0f / f.h)));
        const int bx = wx * per - f.origin.x, bz = wz * per - f.origin.z;
        if (bx < 0 || bz < 0 || bx + per > f.nx || bz + per > f.nz) continue;
        // the same sub-columns columnWater reads (a body's own level at the top skipped unless every one holds a body)
        bool anyClear = false;
        for (int k = 0; k < per && !anyClear; ++k) for (int i = 0; i < per; ++i) { const SurfaceColumn& c = f.at(bx + i, bz + k); if (c.runs >= 0.5f && c.bodyAtSurface <= 0.0f) { anyClear = true; break; } }
        RunMerge m; glm::vec2 sum(0.0f); int wet = 0;
        for (int k = 0; k < per; ++k) for (int i = 0; i < per; ++i) {
            const SurfaceColumn& c = f.at(bx + i, bz + k);
            const int runs = static_cast<int>(c.runs + 0.5f);
            if (runs <= 0) {
                if (c.runs < 0.0f && c.top[0] > c.bottom[0]) { m.add(c.bottom[0], c.top[0]); ++wet; }   // a recorded tile's column (27.15)
                continue;
            }
            if (anyClear && c.bodyAtSurface > 0.0f) continue;
            for (int r = 0; r < std::min(runs, kSurfaceMaxRuns); ++r) m.add(c.bottom[r], c.top[r]);
            sum += glm::vec2(c.u, c.w); ++wet;
        }
        if (wet == 0) return false;   // the volume owns this column and it is dry
        m.out(out);
        if (out.n == 0) return false;
        out.flow = sum / static_cast<float>(wet);
        return true;
    }
    return recordColumnRuns(wx, wz, out);
}

const WaterCoreManager::RecordCol& WaterCoreManager::recordCol(int cx, int cz) const {
    const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) | static_cast<uint32_t>(cz);
    RecordCol& e = m_recordCols[key];
    const uint64_t rev = m_spanRevision ? m_spanRevision() : 0;
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    // kRecordColumnTtlS: the age limit - streaming does not bump the revision; residency changes re-read at once (32.4)
    if (e.rev == rev && e.t >= 0.0 && now - e.t < kRecordColumnTtlS) return e;
    std::vector<float> lv(static_cast<size_t>(1024) * kRecordLevels, -1e30f);
    for (size_t i = 0; i < 1024; ++i) lv[i * kRecordLevels + 4] = 0.0f;   // more = 0
    bool any = false;
    if (m_spanReader) {
        // every height in [kRecordMinY, kRecordMaxY]: one reader call per 32 x 32 chunk column (it assembles the column's chunks once)
        std::vector<ColumnRuns> runs;
        const int x0 = cx * 32, z0 = cz * 32;
        m_spanReader(glm::ivec3(x0, kRecordMinY, z0), glm::ivec3(x0 + 31, kRecordMaxY, z0 + 31), runs);
        for (const ColumnRuns& c : runs) {
            const int lx = c.x - x0, lz = c.z - z0;
            if (lx < 0 || lz < 0 || lx > 31 || lz > 31) continue;
            RunMerge m;
            for (const ColumnRun& r : c.runs) m.add(r.bottomY, r.topY);
            if (m.n == 0) continue;
            ColumnLevels o; m.out(o);
            float* p = &lv[static_cast<size_t>(lx + 32 * lz) * kRecordLevels];
            p[0] = o.top[0]; p[1] = o.bottom[0]; p[2] = o.top[1]; p[3] = o.bottom[1]; p[4] = static_cast<float>(o.more);
            any = true;
        }
    }
    if (lv != e.lv) { e.gen = ++m_recordGen; e.lv.swap(lv); }
    e.any = any; e.rev = rev; e.t = now;
    e.res = m_resColumn ? m_resColumn(cx, cz) : 0;
    return e;
}

void WaterCoreManager::checkRecordResidency() {
    // 32.4 (A2b): streaming loads and unloads chunks without bumping the span revision - a pond whose chunk just loaded read dry
    // (and one whose chunk left read wet) until the 2 s age ran out, and a STATIC debris tile was never asked again at all
    if (!m_resGlobal || !m_resColumn) return;
    const uint64_t g = m_resGlobal();
    if (g == m_resGlobalSeen) return;
    m_resGlobalSeen = g;
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    for (auto it = m_recordCols.begin(); it != m_recordCols.end();) {
        const int cx = static_cast<int>(static_cast<int32_t>(it->first >> 32)), cz = static_cast<int>(static_cast<int32_t>(it->first & 0xffffffffull));
        const uint64_t s = m_resColumn(cx, cz);
        ++m_recordCacheStats.checks;
        if (s == it->second.res) {
            // read while nothing was resident: KEPT and watched (its load is what this check exists to see), but dropped once nothing
            // has asked for kRecordIdleEvictS - the debris tiles' rotation re-asks every column it holds within ~4 s
            if (s == 0 && it->second.t >= 0.0 && now - it->second.t > kRecordIdleEvictS) { ++m_recordCacheStats.evicted; it = m_recordCols.erase(it); continue; }
            ++it; continue;
        }
        m_recordChanged.push_back({cx, cz});
        if (s == 0) { ++m_recordCacheStats.evicted; it = m_recordCols.erase(it); continue; }   // nothing resident: forget it (bounded cache)
        ++m_recordCacheStats.markedStale; it->second.t = -1.0; ++it;                             // re-read on the next ask
    }
}

bool WaterCoreManager::recordColumnWater(int wx, int wz, float& surfaceY) const {
    // floor division: a truncating index puts x = -1..-31 in chunk column 0 and reads another chunk's water there
    // (WaterRecordSeam.ThePerChunkReadEqualsEachColumnsOwnSpansAcrossSeams - red with truncation, 2026-10-25)
    auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
    const int cx = floorDiv(wx, 32), cz = floorDiv(wz, 32);
    const RecordCol& e = recordCol(cx, cz);
    if (!e.any) return false;
    const float t = e.lv[static_cast<size_t>((wx - cx * 32) + 32 * (wz - cz * 32)) * kRecordLevels];
    if (t < -1e29f) return false;
    surfaceY = t;
    return true;
}

bool WaterCoreManager::recordColumnRuns(int wx, int wz, ColumnLevels& out) const {
    out = ColumnLevels{};
    auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };   // as recordColumnWater (A2a)
    const int cx = floorDiv(wx, 32), cz = floorDiv(wz, 32);
    const RecordCol& e = recordCol(cx, cz);
    if (!e.any) return false;
    const float* p = &e.lv[static_cast<size_t>((wx - cx * 32) + 32 * (wz - cz * 32)) * kRecordLevels];
    if (p[0] < -1e29f) return false;
    out.top[0] = p[0]; out.bottom[0] = p[1]; out.top[1] = p[2]; out.bottom[1] = p[3];
    out.n = p[2] > -1e29f ? 2 : 1; out.more = static_cast<int>(p[4]);
    return true;
}

bool WaterCoreManager::columnSurfaceStats(int wx, int wz, ColumnSurfaceStats& out) const {
    for (const WaterSurfaceField* fp : m_fields) {
        const WaterSurfaceField& f = *fp;
        const int per = std::max(1, static_cast<int>(std::lround(1.0f / f.h)));
        const int bx = wx * per - f.origin.x, bz = wz * per - f.origin.z;
        if (bx < 0 || bz < 0 || bx + per > f.nx || bz + per > f.nz) continue;
        double sum = 0.0; out = ColumnSurfaceStats{}; out.lowest = 1e30f; out.highest = -1e30f; out.sub = per * per;
        for (int k = 0; k < per; ++k) for (int i = 0; i < per; ++i) {
            const SurfaceColumn& c = f.at(bx + i, bz + k);
            const int runs = static_cast<int>(c.runs + 0.5f);
            if (runs <= 0) continue;
            const float t = c.top[std::min(runs, kSurfaceMaxRuns) - 1];
            sum += t; ++out.wet; out.lowest = std::min(out.lowest, t); out.highest = std::max(out.highest, t);
        }
        if (out.wet == 0) return false;
        out.mean = static_cast<float>(sum / out.wet);
        return true;
    }
    return false;
}

bool WaterCoreManager::columnMassDepth(int wx, int wz, double& depthM) {
    for (auto& av : m_avs) {
        if (wx < av->minVoxel.x || wx > av->maxVoxel.x || wz < av->minVoxel.z || wz > av->maxVoxel.z) continue;
        syncFromGpu(*av, m_realtime);
        const int bx = wx * av->per - av->grid->spec().origin.x, bz = wz * av->per - av->grid->spec().origin.z;
        double mass = 0.0;
        for (int k = 0; k < av->per; ++k) for (int i = 0; i < av->per; ++i) mass += av->grid->columnMass(bx + i, bz + k);
        depthM = mass;   // m^3 over the voxel column's 1 m^2
        return true;
    }
    return false;
}

bool WaterCoreManager::columnCells(float wx, float wz, std::vector<glm::vec2>& fs, float& y0, float& h) {
    for (auto& av : m_avs) {
        const glm::ivec3 c = av->grid->worldToCell(glm::vec3(wx, static_cast<float>(av->grid->spec().origin.y) * av->h, wz));
        if (!av->grid->inBounds(c.x, 0, c.z)) continue;
        syncFromGpu(*av);
        fs.clear();
        for (int y = 0; y < av->grid->ny(); ++y) fs.push_back({av->grid->f(c.x, y, c.z), av->grid->s(c.x, y, c.z)});
        y0 = static_cast<float>(av->grid->spec().origin.y) * av->h; h = av->h;
        return true;
    }
    return false;
}

bool WaterCoreManager::census(int id, Census& out) {
    for (auto& av : m_avs) {
        if (av->id != id) continue;
        refreshOccupancy(*av);
        syncFromGpu(*av, false);   // the device's state, not the rate-limited mirror
        out = Census{};
        const double h3 = static_cast<double>(av->h) * av->h * av->h;
        for (int z = 0; z < av->grid->nz(); ++z) for (int y = 0; y < av->grid->ny(); ++y) for (int x = 0; x < av->grid->nx(); ++x) {
            const float f = av->grid->f(x, y, z);
            if (f == 0.0f) continue;
            const Occ o = av->occCache[av->grid->idx(x, y, z)];
            const double m = static_cast<double>(f) * h3;
            out.massM3 += m;
            if (o == Occ::Solid) { out.inSolidM3 += m; ++out.inSolidCells; }
            else if (o == Occ::Unknown) { out.inUnknownM3 += m; ++out.inUnknownCells; }
            else { out.inAirM3 += m; ++out.wetCells; }
            if (f > 1.0f) { out.overFullM3 += (static_cast<double>(f) - 1.0) * h3; ++out.overFullCells; }
        }
        if (av->gpuVol && m_gpu) out.sentinel = m_gpu->sentinelMaxAbs(*av->gpuVol);   // §27 W1b: must stay 0
        if (m_spanReader) {   // §27 W1b: the record over the box in the SAME call - read apart, a tile recording between the reads skews the books
            std::vector<ColumnRuns> runs; m_spanReader(av->minVoxel, av->maxVoxel, runs);
            out.recordM3 = 0.0;
            for (const auto& c : runs) for (const auto& r : c.runs) out.recordM3 += static_cast<double>(r.topY - r.bottomY);
        }
        return true;
    }
    return false;
}

const std::vector<WaterSurfaceCell>& WaterCoreManager::surfaceCells() {
    for (auto& av : m_avs) syncFromGpu(*av, m_realtime);   // the renderer: once a second in realtime, every step otherwise
    m_surface.clear();
    for (const auto& av : m_avs) {
        for (int vz = av->minVoxel.z; vz <= av->maxVoxel.z; ++vz) for (int vx = av->minVoxel.x; vx <= av->maxVoxel.x; ++vx) {
            const int bx = vx * av->per - av->grid->spec().origin.x, bz = vz * av->per - av->grid->spec().origin.z;
            double mass = 0.0; float top = -1e30f, bottom = 1e30f;
            for (int k = 0; k < av->per; ++k) for (int i = 0; i < av->per; ++i) {
                const int gx = bx + i, gz = bz + k;
                mass += av->grid->columnMass(gx, gz);
                const float s = av->grid->surfaceWorldY(gx, gz);
                if (!std::isnan(s)) top = std::max(top, s);
                for (int gy = 0; gy < av->grid->ny(); ++gy)
                    if (av->grid->f(gx, gy, gz) > 0.0f) { bottom = std::min(bottom, (av->grid->spec().origin.y + gy) * av->h); break; }
            }
            if (mass <= 0.0 || top < -1e29f) continue;
            WaterSurfaceCell cell;
            cell.centerDepth = glm::vec4(vx + 0.5f, top, vz + 0.5f, static_cast<float>(mass));
            cell.corners = glm::vec4(top);
            cell.skirt = glm::vec4(bottom);
            cell.flow = glm::vec4(0.0f);
            m_surface.push_back(cell);
        }
    }
    return m_surface;
}

} // namespace Water
} // namespace Core
} // namespace Phyxel

namespace Phyxel::Core::Water {

std::vector<GpuSurfaceDraw> WaterCoreManager::gpuSurfaceDraws() {
    std::vector<GpuSurfaceDraw> out;
    if (!m_gpu || !WaterCoreGpu::s_gpuSurface) return out;
    for (auto& up : m_avs) {
        Av& av = *up;
        if (!av.gpuVol) continue;
        const WaterCoreGpu::SurfaceView v = m_gpu->surfaceView(*av.gpuVol);
        if (!v.ok) continue;
        GpuSurfaceDraw d;
        d.surf = v.surf; d.corners = v.corners; d.meta = v.meta; d.list = v.list; d.args = v.args;
        d.surfBytes = v.surfBytes; d.cornersBytes = v.cornersBytes; d.metaBytes = v.metaBytes; d.listBytes = v.listBytes;
        d.originX = v.origin.x; d.originZ = v.origin.z; d.nx = v.nx; d.nz = v.nz; d.h = v.h; d.serial = v.serial;
        d.rippleLayer = av.field.rippleLayer; d.look = av.field.look;
        out.push_back(d);
    }
    return out;
}

WaterCoreManager::MeshParity WaterCoreManager::surfaceMeshParityForTest(int id) {
    MeshParity r;
    Av* av = nullptr;
    for (auto& a : m_avs) if (a->id == id) av = a.get();
    if (!av || !av->gpuVol || !m_gpu) return r;
    std::vector<float> dv;
    if (!m_gpu->expandSurfaceForTest(*av->gpuVol, dv)) return r;
    surfaceFields();
    WaterSurfaceField f = av->field; f.rippleLayer = -1; f.gpuDrawn = false;
    WaterSurfaceMesh hm; buildWaterSurfaceMesh(f, hm);
    using Q = std::array<float, 48>;
    std::vector<Q> hq, dq;
    for (size_t q = 0; q + 3 < hm.vertices.size(); q += 4) {
        Q a{};
        for (int k = 0; k < 4; ++k) {
            const WaterSurfaceVertex& v = hm.vertices[q + k];
            const float row[12] = {v.pos.x, v.pos.y, v.pos.z, v.depth, v.normal.x, v.normal.y, v.normal.z, v.side, v.foam, v.flow.x, v.flow.y, v.pad};
            for (int j = 0; j < 12; ++j) a[k * 12 + j] = row[j];
        }
        hq.push_back(a);
        r.sideQuads += a[7] > 0.5f ? 1 : 0;
    }
    for (size_t q = 0; q + 47 < dv.size(); q += 48) { Q a{}; for (int j = 0; j < 48; ++j) a[j] = dv[q + j]; dq.push_back(a); }
    r.hostQuads = static_cast<long>(hq.size()); r.deviceQuads = static_cast<long>(dq.size());
    // a quad's key: its side flag, then its first two corners' positions rounded to 0.1 mm (the same quad has the same corners in the
    // same order on both sides; a host/device difference in the last bits does not move a rounded key except at a rounding edge -
    // a key mismatch is then counted as unmatched and shows in the numbers)
    auto key = [](const Q& a) { std::array<long long, 7> k{}; k[0] = std::llround(a[7]); for (int j = 0; j < 3; ++j) { k[1 + j] = std::llround(a[j] * 1e4); k[4 + j] = std::llround(a[12 + j] * 1e4); } return k; };
    std::multimap<std::array<long long, 7>, size_t> dk;
    for (size_t i = 0; i < dq.size(); ++i) dk.insert({key(dq[i]), i});
    std::vector<char> used(dq.size(), 0);
    for (const Q& a : hq) {
        auto range = dk.equal_range(key(a));
        bool found = false;
        for (auto it = range.first; it != range.second; ++it) {
            if (used[it->second]) continue;
            const Q& b = dq[it->second];
            float wp = 0.0f, wo = 0.0f;
            for (int k = 0; k < 4; ++k) for (int j = 0; j < 12; ++j) {
                const float d = std::abs(a[k * 12 + j] - b[k * 12 + j]);
                if (j < 3) wp = std::max(wp, d); else if (j != 11) wo = std::max(wo, d);
            }
            used[it->second] = 1; found = true;
            r.worstPos = std::max(r.worstPos, wp); r.worstOther = std::max(r.worstOther, wo);
            break;
        }
        if (!found) ++r.unmatched;
    }
    for (char u : used) if (!u) ++r.unmatched;
    r.ok = true;
    return r;
}

}  // namespace Phyxel::Core::Water

namespace Phyxel::Core::Water {

bool WaterCoreManager::tallStatsParityForTest(int id, float& devMin, float& hostMin, double& devWater, double& hostWater) {
    Av* av = nullptr;
    for (auto& a : m_avs) if (a->id == id) av = a.get();
    if (!av || !av->gpuVol || !m_gpu) return false;
    m_gpu->settle(*av->gpuVol);
    surfaceFields();
    if (!m_gpu->surfaceStats(*av->gpuVol, devMin, devWater)) return false;
    const WaterSurfaceField& f = av->field;
    hostMin = std::numeric_limits<float>::infinity();
    for (const SurfaceColumn& c : f.cols) { const int runs = static_cast<int>(c.runs + 0.5f); if (runs >= 1) hostMin = std::min(hostMin, c.top[std::min(runs, kSurfaceMaxRuns) - 1]); }
    hostWater = fieldWater(f);
    return true;
}

}  // namespace Phyxel::Core::Water

namespace Phyxel::Core::Water {

WaterCoreManager::MotionParity WaterCoreManager::motionParityForTest(int id) {
    MotionParity r;
    Av* av = nullptr;
    for (auto& a : m_avs) if (a->id == id) av = a.get();
    if (!av || !av->gpuVol || !m_gpu) return r;
    m_gpu->settle(*av->gpuVol);
    surfaceFields();
    WaterCoreGpu::Volume::Motion mo;
    if (!m_gpu->motionResult(*av->gpuVol, mo, false)) return r;
    const WaterSurfaceField& f = av->field;
    const float moveM = WaterCoreGpu::s_motionMoveM;
    if (m_motionTestRef.size() != f.cols.size()) m_motionTestRef.assign(f.cols.size(), std::numeric_limits<float>::quiet_NaN());
    int x0 = INT32_MAX, x1 = INT32_MIN, z0 = INT32_MAX, z1 = INT32_MIN; float maxRise = 0.0f;
    for (int z = 0; z < f.nz; ++z) for (int x = 0; x < f.nx; ++x) {   // takeMotion's rule exactly
        const size_t i = static_cast<size_t>(x) + static_cast<size_t>(f.nx) * z;
        const SurfaceColumn& c = f.cols[i];
        const int runs = static_cast<int>(c.runs + 0.5f);
        if (runs <= 0 || c.bodyAtSurface > 0.0f) { m_motionTestRef[i] = std::numeric_limits<float>::quiet_NaN(); continue; }
        const float top = c.top[std::min(runs, kSurfaceMaxRuns) - 1];
        const bool haveRef = !std::isnan(m_motionTestRef[i]);
        const float dTop = haveRef ? std::abs(top - m_motionTestRef[i]) : 0.0f;
        const bool rose = dTop > moveM;
        m_motionTestRef[i] = (haveRef && !rose) ? m_motionTestRef[i] : top;
        maxRise = std::max(maxRise, dTop);
        if (!rose) continue;
        ++r.hostRise; x0 = std::min(x0, x); x1 = std::max(x1, x); z0 = std::min(z0, z); z1 = std::max(z1, z);
    }
    r.devRise = mo.fresh ? mo.riseCols : 0;
    r.boxSame = r.hostRise == 0 ? r.devRise == 0 : (mo.x0 == x0 && mo.x1 == x1 && mo.z0 == z0 && mo.z1 == z1);
    r.riseDiff = std::abs((mo.fresh ? mo.maxRise : 0.0f) - maxRise);
    r.ok = true;
    return r;
}

}  // namespace Phyxel::Core::Water
