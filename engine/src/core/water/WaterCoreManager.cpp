// WaterCoreManager — docs/WaterCore.md §5, §15.1 (Phase B engine integration).
#include "core/water/WaterCoreManager.h"
#include "core/water/WaterDroplets.h"
#include <array>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <cmath>
#include <limits>

namespace Phyxel {
namespace Core {
namespace Water {

namespace {
constexpr int kMicroPerVoxel = 9;

int floorDiv(int a, int b) { return (a >= 0) ? (a / b) : -(((-a) + b - 1) / b); }
}

WaterCoreManager::WaterCoreManager(MicroStateQuery state, SolidsRevisionQuery revision)
    : m_state(std::move(state)), m_revision(std::move(revision)) {}

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
    const glm::ivec3 dims = (hi - lo + glm::ivec3(1)) * per;
    const size_t cells = static_cast<size_t>(dims.x) * dims.y * dims.z;
    if (cells > kMaxCellsPerVolume) { if (err) *err = "volume would be " + std::to_string(cells) + " cells; the CPU reference caps at " + std::to_string(kMaxCellsPerVolume); return 0; }
    if (transport != "eulerian" && transport != "flip") { if (err) *err = "transport must be 'eulerian' or 'flip'"; return 0; }
    std::string be = backend;
    if (be.empty() || be == "auto") be = (gpuReady() && transport == "eulerian") ? "gpu" : "cpu";   // the default: fills on the device once their parity rows passed (15.13-15.15); particles stay on the CPU until the splash row closes
    if (be != "cpu" && be != "gpu") { if (err) *err = "backend must be 'cpu', 'gpu' or 'auto'"; return 0; }
    if (be == "gpu" && !gpuReady()) { if (err) *err = "no GPU backend: WaterCoreGpu was not initialised (no device, or a kernel failed to load) - the CPU reference is the fallback, say so"; return 0; }

    if (transport == "flip" && cells * static_cast<size_t>(FlipTransport::kParticlesPerCell) > FlipTransport::kMaxParticlesPerVolume) {
        if (err) *err = "a FLIP volume of " + std::to_string(cells) + " cells could hold " + std::to_string(cells * FlipTransport::kParticlesPerCell) + " particles; the CPU reference caps at " + std::to_string(FlipTransport::kMaxParticlesPerVolume);
        return 0;
    }
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
    av->solver = std::make_unique<WaterSolver>(*av->grid, q);
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
    refreshOccupancy(*av);
    av->solver->refreshSolids();
    if (be == "gpu") {
        // the grid's occupancy must hold the hold boundary the GPU reads (it is the solver's query on the CPU)
        for (int z = 0; z < av->grid->nz(); ++z) for (int y = 0; y < av->grid->ny(); ++y) for (int x = 0; x < av->grid->nx(); ++x)
            av->grid->occ(x, y, z) = av->occCache[av->grid->idx(x, y, z)];
        // 26.12 P2b: a fill volume is SPARSE - it stores and steps only the bricks holding water and their halo (26.6), bit-identical
        // to keeping every brick (WaterBrickTest); the particle transport stays dense (26.6 point 7)
        av->gpuVol = m_gpu->createVolume(av->grid->spec(), err, transport == "flip", 0, BrickOptions{transport != "flip", 0, false, brickHeight, groundBricks});   // 26.17: the brick height (refused with the reason unless 2 / 4 / 8)
        if (!av->gpuVol) return 0;   // the refusal carries the byte count
        m_gpu->upload(*av->gpuVol, *av->grid);
        av->gpuDirty = false;
    }
    m_avs.push_back(std::move(av));
    refreshBoxes();
    if (s_partialWakeDefault && raw->gpuVol && m_gpu && raw->gpuVol->sparse) m_gpu->setPartialWake(*raw->gpuVol, true);   // 26.31
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

void WaterCoreManager::refreshBoxes() {
    m_boxes.clear();
    for (const auto& av : m_avs) m_boxes.emplace_back(av->minVoxel, av->maxVoxel);
    ++m_avRevision;
}

AvRecord WaterCoreManager::record(const Av& av) const {
    AvRecord r;
    r.id = av.id; r.minVoxel = av.minVoxel; r.maxVoxel = av.maxVoxel; r.cellSize = av.h; r.transport = av.solver->transport().name();
    r.particles = av.solver->transport().particleCount();
    r.backend = av.backend; r.rbgsResidual = av.gpuLast.rbgsResidualMax; r.gpuSweeps = av.backend == "gpu" ? av.gpuSweeps : 0; r.gpuMs = av.gpuLast.gpuMs; r.gpuDeviceMs = av.gpuLast.deviceMs; r.partialWake = av.gpuVol && av.gpuVol->partialWake; r.awakeColumns = av.gpuLast.awakeColumns; r.frozenColumns = av.gpuLast.frozenColumns; r.movingColumns = av.gpuLast.movingColumns; r.stepSlots = av.gpuLast.stepSlots; r.partitionMoves = av.gpuLast.partitionMoves; r.gpuHostMs = av.gpuLast.hostResidencyMs; r.gpuWaitMs = av.gpuLast.submitWaitMs; r.gpuTicks = av.gpuLast.ticks; r.gpuAsync = av.gpuVol && av.gpuVol->async; r.gpuBricks = av.gpuLast.bricks; r.gpuBrickCapacity = av.gpuLast.capacity; r.gpuBrickHeight = av.gpuVol ? av.gpuVol->by : 0; r.gpuGroundBricks = av.gpuLast.groundBricks; r.gpuUploadMs = av.gpuUploadMs; r.gpuUploads = av.gpuUploads; r.occRefreshMs = av.occRefreshMs;
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
    for (int z = 0; z < g.nz(); ++z)
        for (int y = 0; y < g.ny(); ++y)
            for (int x = 0; x < g.nx(); ++x)
                av.occCache[g.idx(x, y, z)] = sampleOccupancy(av, glm::ivec3(x, y, z));
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
    if (!m_realtime) return;
    using UClock = std::chrono::steady_clock;
    auto ums = [](UClock::time_point a) { return std::chrono::duration<double, std::milli>(UClock::now() - a).count(); };
    m_updateCpu = UpdateCpu{};
    for (auto& av : m_avs) {
        { const auto t = UClock::now(); refreshOccupancy(*av); m_updateCpu.occupancy += ums(t); }
        const auto t = UClock::now();
        if (av->backend == "gpu") {
            if (av->gpuVol && m_gpu) m_gpu->setDroplets(*av->gpuVol, m_dstats.on);
            if (!av->gpuLast.asleep || (av->gpuVol && !av->gpuVol->asleep)) {
                { const auto tc = UClock::now(); stepGpu(*av, 1, dt); m_updateCpu.stepCall += ums(tc); }
                ++av->stepCount;
                { const auto tb = UClock::now(); collectBirths(*av, dt); m_updateCpu.births += ums(tb); }
            }
        }
        else if (!av->solver->asleep()) { av->last = av->solver->step(dt); av->residueDropped += av->last.residueDropped; ++av->stepCount; collectBirths(*av, dt); }
        m_updateCpu.step += ums(t);
    }
    { const auto t = UClock::now(); stepDroplets(dt); m_updateCpu.droplets = ums(t); }   // 21: flight + landing (deposits reach a GPU volume at its next step)
    { const auto t = UClock::now(); stepRipples(dt); m_updateCpu.ripples = ums(t); }     // 22: the sub-cell wave layer
    const auto tSleep = UClock::now();
    // Phase D (16.2): a volume that slept this tick writes back and frees itself (auto_sleep). Ids
    // first - sleep() destroys. A refused write-back (held or non-resident columns) leaves the
    // volume alive and is reported through the drained records, never silent.
    std::vector<int> slept;
    for (const auto& av : m_avs) if (av->autoSleep && isAsleep(*av) && !m_pool.anyFor(av->id) && !(av->ripple && !av->ripple->asleep())) slept.push_back(av->id);   // 21: not with droplets in flight; 22 (F5): nor with rings running
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
        std::vector<ColumnRuns> runs;
        m_spanReader(av->minVoxel, av->maxVoxel, runs);
        const double placed = seedGridFromRuns(*av->grid, runs);
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
    m_spanWriter(runs, yLo, yHi, &unwritten, &chunks);
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
    destroy(id);
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
        if (av->gpuVol) { av->gpuVol->asleep = false; av->gpuVol->quietTicks = 0; }
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

void WaterCoreManager::stepGpu(Av& av, int ticks, float dt) {
    if (!av.gpuVol || !m_gpu) return;
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
    m_gpu->setAsync(*av.gpuVol, m_realtime);   // 26.16: realtime frames never wait on the water's call (collected next frame)
    const GpuStepStats st = m_gpu->step(*av.gpuVol, av.solver->params(), dt, ticks, av.gpuSweeps);
    av.gpuStale = true;
    av.gpuLast = st;
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
        syncFromGpu(*av);
        av->solver->addImpulse(world, radius, deltaSpeed, dir);
        markWritten(*av, &world, radius);   // 26.31: an impulse wakes its own columns
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
        syncFromGpu(*av);
        const WaterSolver::RadialKick k = av->solver->addRadialImpulse(centre, reach, speedAtCentre, upBias);
        if (k.faces == 0) continue;
        markWritten(*av);
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

const std::vector<const WaterSurfaceField*>& WaterCoreManager::surfaceFields() {
    // 26.30 step 3: the volumes' own fields - no copy (it was cleared and every field copied each call, up to three calls a frame:
    // 0.42 ms a frame on a sleeping 20 x 20 m pond, ~1 ms on an awake 32 x 28 m one)
    m_fields.resize(m_avs.size());
    for (size_t i = 0; i < m_avs.size(); ++i) {
        auto& av = m_avs[i];
        if (av->fieldStep != av->stepCount) {
            bool got = false;
            if (av->gpuVol && m_gpu && m_gpu->surfaceReady(*av->gpuVol)) {
                WaterSurfaceField& f = av->field;
                f.origin = av->grid->spec().origin; f.nx = av->grid->nx(); f.nz = av->grid->nz(); f.h = av->h;
                f.cols.resize(static_cast<size_t>(f.nx) * f.nz);
                got = m_gpu->readSurface(*av->gpuVol, reinterpret_cast<float*>(f.cols.data()));
            }
            if (!got) { syncFromGpu(*av, false); extractSurfaceField(*av->grid, av->field); }
            av->fieldStep = av->stepCount;
            av->field.source = av->id; av->field.version = av->fieldStep + 1;   // 26.30: the tiler skips a field whose version stands (an asleep volume)
            av->field.look = packLook(lookAt((av->minVoxel.x + av->maxVoxel.x) / 2, (av->minVoxel.z + av->maxVoxel.z) / 2));   // G3
            av->fieldStepSec = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }
        av->field.rippleLayer = (av->ripple && m_rstats.on) ? static_cast<int>(i) : -1;   // 22: parallel to rippleLayers()
        m_fields[i] = &av->field;
    }
    return m_fields;
}

std::vector<std::pair<glm::vec3, float>> WaterCoreManager::takeMotion(float moveM) {
    std::vector<std::pair<glm::vec3, float>> out;
    m_motionStats = MotionStats{};
    if (m_motionPrevTops.size() != m_fields.size()) { m_motionPrevTops.assign(m_fields.size(), {}); m_motionSeen.assign(m_fields.size(), {}); }
    for (size_t fi = 0; fi < m_fields.size(); ++fi) {
        const WaterSurfaceField& f = *m_fields[fi];
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
    surfaceFields();   // masks and body waterlines read each volume's field (cached per step)
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
            if (runs <= 0) continue;
            if (anyClear && c.bodyAtSurface > 0.0f) continue;
            top = std::max(top, c.top[std::min(runs, kSurfaceMaxRuns) - 1]);
            sum += glm::vec2(c.u, c.w); ++wet;
        }
        if (wet == 0) return false;   // the volume owns this column and it is dry
        surfaceY = top; flow = sum / static_cast<float>(wet);
        return true;
    }
    return false;
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
