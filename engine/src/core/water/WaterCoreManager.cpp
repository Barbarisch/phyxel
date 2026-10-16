// WaterCoreManager — docs/WaterCore.md §5, §15.1 (Phase B engine integration).
#include "core/water/WaterCoreManager.h"
#include "core/water/WaterDroplets.h"
#include <array>
#include <algorithm>
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
                             const std::string& transport, std::string* err, const std::string& backend, int gpuSweeps, int brickHeight) {
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
    if (transport == "flip") av->solver->setTransport(std::make_unique<FlipTransport>());   // empty until water is placed
    av->occDirty = true;
    av->backend = be;
    // SOR needs O(N) sweeps for a Poisson problem N cells across: 1.5 x the longest grid dimension
    // (40 on the 1 m Basin, 117 at 1/3 m where 40 left a residual of 0.48 and 100 gave 5e-4 for
    // +0.2 ms, 2026-10-08). An explicit `sweeps` overrides; both are clamped and echoed.
    const int longest = std::max({dims.x, dims.y, dims.z});
    const int autoSweeps = static_cast<int>(std::lround(1.5 * longest));
    av->gpuSweeps = std::clamp(gpuSweeps > 0 ? gpuSweeps : autoSweeps, kGpuSweepsMin, kGpuSweepsMax);
    refreshOccupancy(*av);
    av->solver->refreshSolids();
    if (be == "gpu") {
        // the grid's occupancy must hold the hold boundary the GPU reads (it is the solver's query on the CPU)
        for (int z = 0; z < av->grid->nz(); ++z) for (int y = 0; y < av->grid->ny(); ++y) for (int x = 0; x < av->grid->nx(); ++x)
            av->grid->occ(x, y, z) = av->occCache[av->grid->idx(x, y, z)];
        // 26.12 P2b: a fill volume is SPARSE - it stores and steps only the bricks holding water and their halo (26.6), bit-identical
        // to keeping every brick (WaterBrickTest); the particle transport stays dense (26.6 point 7)
        av->gpuVol = m_gpu->createVolume(av->grid->spec(), err, transport == "flip", 0, BrickOptions{transport != "flip", 0, false, brickHeight});   // 26.17: the brick height (refused with the reason unless 2 / 4 / 8)
        if (!av->gpuVol) return 0;   // the refusal carries the byte count
        m_gpu->upload(*av->gpuVol, *av->grid);
        av->gpuDirty = false;
    }
    m_avs.push_back(std::move(av));
    refreshBoxes();
    return raw->id;
}

bool WaterCoreManager::destroy(int id) {
    auto it = std::find_if(m_avs.begin(), m_avs.end(), [id](const std::unique_ptr<Av>& a) { return a->id == id; });
    if (it == m_avs.end()) return false;
    for (const Droplet& d : m_pool.take(id)) m_dstats.destroyedM3 += d.volume;   // 21: discarded with the volume's water
    if ((*it)->gpuVol && m_gpu) m_gpu->destroyVolume((*it)->gpuVol);
    m_avs.erase(it);
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
    r.backend = av.backend; r.rbgsResidual = av.gpuLast.rbgsResidualMax; r.gpuSweeps = av.backend == "gpu" ? av.gpuSweeps : 0; r.gpuMs = av.gpuLast.gpuMs; r.gpuDeviceMs = av.gpuLast.deviceMs; r.gpuHostMs = av.gpuLast.hostResidencyMs; r.gpuWaitMs = av.gpuLast.submitWaitMs; r.gpuTicks = av.gpuLast.ticks; r.gpuAsync = av.gpuVol && av.gpuVol->async; r.gpuBricks = av.gpuLast.bricks; r.gpuBrickCapacity = av.gpuLast.capacity; r.gpuBrickHeight = av.gpuVol ? av.gpuVol->by : 0;
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
    if (!av.floorY.empty()) {   // 25.39 H2: a creek window stands on the region's bed (the line on the carve), not on the voxel staircase
        const float floorY = av.floorY[static_cast<size_t>(cellLocal.x) + static_cast<size_t>(av.grid->nx()) * cellLocal.z];
        if ((cellWorld.y + 0.5) * h < floorY) return Occ::Solid;
    }
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
        if (rev != av.occRevision) { av.occDirty = true; av.occRevision = rev; av.solver->wake(); }
    }
    if (!av.occDirty) return;
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
}

void WaterCoreManager::markSolidsDirty() {
    for (auto& av : m_avs) { av->occDirty = true; av->solver->wake(); }
    for (auto& r : m_columns) r.occRevision = ~0ull;   // 25.18: column regions re-read their beds next step
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
    for (auto& av : m_avs) {
        refreshOccupancy(*av);
        if (av->backend == "gpu") {
            if (av->gpuVol && m_gpu) m_gpu->setDroplets(*av->gpuVol, m_dstats.on);
            if (!av->gpuLast.asleep || (av->gpuVol && !av->gpuVol->asleep)) { stepGpu(*av, 1, dt); ++av->stepCount; collectBirths(*av, dt); }
        }
        else if (!av->solver->asleep()) { av->last = av->solver->step(dt); av->residueDropped += av->last.residueDropped; ++av->stepCount; collectBirths(*av, dt); }
    }
    stepColumnSplashes(dt);   // 25.30 SC: the creek's cosmetic entry crown
    stepDroplets(dt);   // 21: flight + landing (deposits reach a GPU volume at its next step)
    stepRipples(dt);    // 22: the sub-cell wave layer
    stepColumns(dt);    // 25.18 H1: column-water regions
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
}

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
    for (auto& r : m_columns) if (r.ripple) out.push_back(r.ripple.get());
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
        syncFromGpu(av); m_gpu->upload(*av.gpuVol, *av.grid);
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
    // 25.30 SC - column regions: each displacing body's submerged thickness over the columns it covers is the region's LOAD
    // (ShoreSolver::setSurfaceLoad - it presses the water aside and holds it there). Submerged = the body's span between the
    // bed and the free surface AROUND it (its footprint's ring, last step). Entering applies at once (the crater); a body leaving or coming
    // to rest releases over kLoadReleaseSeconds.
    const float H = ColumnWater::kH;
    for (ColumnRegion& reg : m_columns) {
        ColumnWater& cw = *reg.cw;
        const ShoreSolver& sv = cw.solver();
        const int nx = cw.nx(), nz = cw.nz();
        const glm::vec3 lo(cw.minVoxel()), hi = glm::vec3(cw.maxVoxel()) + glm::vec3(1.0f);
        const glm::vec2 org = sv.origin();
        std::vector<float> target(static_cast<size_t>(nx) * nz, 0.0f);
        int mine = 0;
        for (const MovingSolid& b : bodies) {
            const glm::vec3 bl = b.centre - b.halfExtents, bh = b.centre + b.halfExtents;
            if (!glm::all(glm::greaterThanEqual(bh, lo)) || !glm::all(glm::lessThanEqual(bl, hi))) continue;
            ++mine;
            const int x0 = std::max(0, static_cast<int>(std::floor((bl.x - org.x) / H))), x1 = std::min(nx - 1, static_cast<int>(std::floor((bh.x - org.x) / H)));
            const int z0 = std::max(0, static_cast<int>(std::floor((bl.z - org.y) / H))), z1 = std::min(nz - 1, static_cast<int>(std::floor((bh.z - org.y) / H)));
            // the free surface the body is measured against: the mean over the RING of wet columns around its footprint. Its
            // own columns' free surface (water + load) includes the load it is setting - read there, each frame's load lifted
            // the surface it was measured against and it ratcheted up to the body's top: water drawn 23 cm up a stone in an
            // 11 cm creek (live, 2026-10-12)
            double ringSum = 0.0; int ringN = 0;
            for (int z = z0 - 1; z <= z1 + 1; ++z) for (int x = x0 - 1; x <= x1 + 1; ++x) {
                if (x >= x0 && x <= x1 && z >= z0 && z <= z1) continue;
                if (x < 0 || z < 0 || x >= nx || z >= nz) continue;
                const ShoreColumn& c = sv.col(x, z);
                if (c.wall || c.eta - c.bed <= sv.params().dryDepth) continue;
                ringSum += c.eta + sv.load(x, z); ++ringN;
            }
            if (ringN == 0) continue;
            const double S = ringSum / ringN;
            for (int z = z0; z <= z1; ++z) for (int x = x0; x <= x1; ++x) {
                const ShoreColumn& c = sv.col(x, z);
                if (c.wall) continue;
                const float cx0 = org.x + x * H, cz0 = org.y + z * H;
                const float ox = std::max(0.0f, std::min(cx0 + H, bh.x) - std::max(cx0, bl.x)), oz = std::max(0.0f, std::min(cz0 + H, bh.z) - std::max(cz0, bl.z));
                const double subm = std::max(0.0, std::min(static_cast<double>(bh.y), S) - std::max(static_cast<double>(bl.y), static_cast<double>(c.bed)));
                target[x + static_cast<size_t>(nx) * z] += static_cast<float>(ox * oz / (H * H) * subm);
            }
        }
        if (reg.load.empty() && mine == 0) continue;
        reg.load.resize(target.size(), 0.0f);
        const float rel = std::min(1.0f, std::max(0.0f, frameSeconds) / kLoadReleaseSeconds);
        bool any = false;
        for (size_t i = 0; i < target.size(); ++i) {
            float& s = reg.load[i];
            s = target[i] >= s ? target[i] : s + (target[i] - s) * rel;
            if (s < 1e-5f) s = 0.0f;
            any = any || s > 0.0f;
        }
        if (!any) reg.load.clear();
        cw.setLoad(reg.load);
    }
    return out;
}

std::vector<std::pair<glm::ivec3, glm::ivec3>> WaterCoreManager::columnBoxes() const {
    std::vector<std::pair<glm::ivec3, glm::ivec3>> out;
    for (const auto& r : m_columns) out.push_back({r.cw->minVoxel(), r.cw->maxVoxel()});
    return out;
}

void WaterCoreManager::stepColumnSplashes(float dt) {
    if (!m_dstats.on || m_creekSplash <= 0.0f || dt <= 0.0f) return;
    bool any = false;
    for (ColumnRegion& reg : m_columns) {
        ColumnWater& cw = *reg.cw;
        const ShoreSolver& sv = cw.solver();
        for (const MovingSolid& b : m_rippleBodies) {
            const float speed = -b.velocity.y;
            if (speed < kSplashMinSpeed) continue;
            int cx, cz;
            if (!cw.columnAt(b.centre.x, b.centre.z, cx, cz)) continue;
            const ShoreColumn& c = sv.col(cx, cz);
            if (c.wall || c.eta - c.bed <= sv.params().dryDepth) continue;
            const float S = static_cast<float>(c.eta) + sv.load(cx, cz);
            if (!(b.centre.y - b.halfExtents.y < S && b.centre.y + b.halfExtents.y > S)) continue;   // not crossing the surface
            const float V = m_creekSplash * 4.0f * b.halfExtents.x * b.halfExtents.z * speed * dt;
            // COUNT from the volume: one droplet per droplet volume (the pool's size knob x the column pitch), the remainder by a
            // deterministic hash - the first cut threw 8 directions every frame whatever the volume, ~60 droplets per stone and
            // as many for a 1/9 m pebble as for a 1/3 m stone (20-stone stress, 2026-10-12)
            const float edge = std::clamp(m_dstats.size, 1.0f / 27.0f, 1.0f) * ColumnWater::kH;
            const float vd = edge * edge * edge;
            if (!any) { ++m_dropletTick; any = true; }
            uint32_t h = static_cast<uint32_t>(std::floor(b.centre.x * 9.0f)) * 73856093u ^ static_cast<uint32_t>(std::floor(b.centre.z * 9.0f)) * 19349663u ^ m_dropletTick * 83492791u;
            h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
            const float frac = (h & 0xFFFFu) / 65536.0f, turn = ((h >> 16) & 0xFFFFu) / 65536.0f;
            const int K = std::min(16, static_cast<int>(std::floor(V / vd + frac)));
            for (int k = 0; k < K; ++k) {
                const float a = 6.2831853f * (k + turn) / K;
                const glm::vec2 dir(std::cos(a), std::sin(a));
                DropletBirth bb;
                bb.pos = glm::vec3(b.centre.x + dir.x * (b.halfExtents.x + 0.02f), S + 0.01f, b.centre.z + dir.y * (b.halfExtents.z + 0.02f));
                bb.vel = glm::vec3(dir.x * 0.4f * speed + c.u, 0.8f * speed, dir.y * 0.4f * speed + c.w);
                bb.volume = 0.999f * vd;   // ONE droplet: the pool splits ceil(V / vd), and vd's own float rounding gave 2 (706 for 353)
                bb.column = k + 16 * static_cast<int>(m_dropletTick & 0xFFFFu);
                const size_t before = m_pool.droplets().size();
                m_pool.spawn(bb, -cw.id(), ColumnWater::kH, m_dstats.size, m_dropletTick);   // a refused share is simply not drawn (no water)
                m_dstats.cosmeticM3 += bb.volume; m_dstats.cosmeticBirths += static_cast<long>(m_pool.droplets().size() - before);   // droplets, as landed
            }
        }
    }
}

void WaterCoreManager::markWritten(Av& av) {
    av.solver->wake();
    av.gpuDirty = true; av.gpuLast.asleep = false;
    if (av.gpuVol) { av.gpuVol->asleep = false; av.gpuVol->quietTicks = 0; }
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
        markWritten(*av);
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

const std::vector<WaterSurfaceField>& WaterCoreManager::surfaceFields() {
    m_fields.clear();
    for (auto& av : m_avs) {
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
            av->field.look = packLook(lookAt((av->minVoxel.x + av->maxVoxel.x) / 2, (av->minVoxel.z + av->maxVoxel.z) / 2));   // G3
            av->fieldStepSec = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }
        av->field.rippleLayer = (av->ripple && m_rstats.on) ? static_cast<int>(m_fields.size()) : -1;   // 22: parallel to rippleLayers()
        m_fields.push_back(av->field);
    }
    for (ColumnRegion& r : m_columns) {   // 25.18 H1 (25.15 F1): the column tier drawn by the same mesher and shader
        r.cw->publish(r.field);
        r.field.rippleLayer = (r.ripple && m_rstats.on) ? static_cast<int>(m_fields.size()) : -1;   // 25.26 RC (publish resets it)
        const glm::ivec3 c = (r.cw->minVoxel() + r.cw->maxVoxel()) / 2;
        r.field.look = packLook(lookAt(c.x, c.z));
        m_fields.push_back(r.field);
    }
    return m_fields;
}

// ---- 25.18 H1: column-water regions ----
void WaterCoreManager::refreshColumnSolids(ColumnRegion& r, bool force) {
    const uint64_t rev = m_revision ? m_revision() : 0;
    if (!force && rev == r.occRevision) return;
    r.occRevision = rev;
    ColumnWater& cw = *r.cw;
    const int nx = cw.nx(), nz = cw.nz();
    const int yLo = cw.minVoxel().y * kMicroPerVoxel, yHi = (cw.maxVoxel().y + 1) * kMicroPerVoxel - 1;
    std::vector<float> tops(static_cast<size_t>(nx) * nz, static_cast<float>(cw.minVoxel().y));
    std::vector<uint8_t> walls(tops.size(), 0);
    for (int z = 0; z < nz; ++z) for (int x = 0; x < nx; ++x) {
        // the column's centre micro column (1/9 m) - the bed is the top of the highest solid micro cell in the box's range
        const glm::vec2 cc = cw.columnCentre(x, z);
        const int mx = static_cast<int>(std::floor(cc.x * kMicroPerVoxel)), mz = static_cast<int>(std::floor(cc.y * kMicroPerVoxel));
        const size_t i = static_cast<size_t>(x) + static_cast<size_t>(nx) * z;
        if (m_state && m_state(glm::ivec3(mx, yHi, mz)) == 1) { walls[i] = 1; continue; }   // solid to the box top: a bank
        for (int my = yHi; my >= yLo; --my) {
            if (m_state && m_state(glm::ivec3(mx, my, mz)) == 1) { tops[i] = static_cast<float>(my + 1) / kMicroPerVoxel; break; }
        }
    }
    cw.setSolids(tops, walls);
    // 25.22 option (a): the generator's continuous height where the ground is as generated (its drawn top matches what the
    // generator draws there), bilinear between voxel centres so a 1/3 m column is not a 1 m staircase sideways, and never
    // below the drawn top (a neighbour's lower height must not sink the bed under this voxel's subcubes). Edited ground:
    // the voxel top.
    if (!m_genBed) return;
    const glm::ivec3 lo = cw.minVoxel() - glm::ivec3(1, 0, 1), hi = cw.maxVoxel() + glm::ivec3(1, 0, 1);
    const int vx = hi.x - lo.x + 1, vz = hi.z - lo.z + 1;
    std::vector<float> H(static_cast<size_t>(vx) * vz, 0.0f), drawn(H.size(), 0.0f);
    std::vector<uint8_t> known(H.size(), 0);
    for (int z = 0; z < vz; ++z) for (int x = 0; x < vx; ++x) {
        float bh = 0.0f, dt = 0.0f;
        const size_t k = static_cast<size_t>(x) + static_cast<size_t>(vx) * z;
        if (m_genBed(lo.x + x, lo.z + z, bh, dt)) { H[k] = bh; drawn[k] = dt; known[k] = 1; }
    }
    std::vector<float> beds(tops.size());
    for (int z = 0; z < nz; ++z) for (int x = 0; x < nx; ++x) {
        const size_t i = static_cast<size_t>(x) + static_cast<size_t>(nx) * z;
        beds[i] = tops[i];
        if (walls[i]) continue;
        const glm::vec2 c = cw.columnCentre(x, z);
        const int ox = static_cast<int>(std::floor(c.x)) - lo.x, oz = static_cast<int>(std::floor(c.y)) - lo.z;
        const size_t own = static_cast<size_t>(ox) + static_cast<size_t>(vx) * oz;
        if (!known[own] || std::abs(drawn[own] - tops[i]) > 1e-3f) continue;   // edited (or unknown): the voxels
        const float fx = c.x - 0.5f - lo.x, fz = c.y - 0.5f - lo.z;           // in voxel-centre coordinates
        const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, vx - 2), z0 = std::clamp(static_cast<int>(std::floor(fz)), 0, vz - 2);
        const float tx = std::clamp(fx - x0, 0.0f, 1.0f), tz = std::clamp(fz - z0, 0.0f, 1.0f);
        auto at = [&](int xx, int zz) { const size_t k = static_cast<size_t>(xx) + static_cast<size_t>(vx) * zz; return known[k] ? H[k] : H[own]; };
        const float h = (at(x0, z0) * (1 - tx) + at(x0 + 1, z0) * tx) * (1 - tz) + (at(x0, z0 + 1) * (1 - tx) + at(x0 + 1, z0 + 1) * tx) * tz;
        beds[i] = std::max(h, tops[i]);
    }
    cw.setGeneratorBeds(std::move(beds));
}

int WaterCoreManager::createColumns(const glm::ivec3& minVoxel, const glm::ivec3& maxVoxel, float manningN, std::string* err) {
    const glm::ivec3 lo = glm::min(minVoxel, maxVoxel), hi = glm::max(minVoxel, maxVoxel);
    const glm::ivec3 span = hi - lo + 1;
    // 600 x 600 columns (200 m square at 1/3 m) is the CPU step's ceiling here (H0: ~150-285 ns per column-tick)
    if (span.x * 3 > 600 || span.z * 3 > 600 || span.y > 64) { if (err) *err = "box too large (max 200 x 64 x 200 voxels)"; return 0; }
    ShoreParams p; p.manningN = manningN;
    ColumnRegion r;
    r.cw = std::make_unique<ColumnWater>(m_nextColumnId++, lo, hi, p);
    refreshColumnSolids(r, true);
    if (r.cw->hasGeneratorBeds()) r.cw->setBedRule(ColumnBedRule::Generator);   // 25.21: option (a) is the default where it exists
    m_columns.push_back(std::move(r));
    return m_columns.back().cw->id();
}

bool WaterCoreManager::destroyColumns(int id) {
    for (size_t i = 0; i < m_columns.size(); ++i) if (m_columns[i].cw->id() == id) { m_columns.erase(m_columns.begin() + static_cast<long>(i)); return true; }
    return false;
}

ColumnWater* WaterCoreManager::columns(int id) {
    for (ColumnRegion& r : m_columns) if (r.cw->id() == id) return r.cw.get();
    return nullptr;
}

void WaterCoreManager::stepColumns(float dt) {
    for (ColumnRegion& r : m_columns) {
        refreshColumnSolids(r);
        Av* win = r.windowAv ? volumeById(r.windowAv) : nullptr;
        if (r.windowAv && !win) { r.cw->setHeld({}); r.windowAv = 0; }   // the volume was removed elsewhere: its columns keep their last ghosts
        if (win) windowGhosts(r, *win);      // 1. the held columns from the window (it stepped earlier this frame)
        r.cw->step(dt);                      // 2. T2, frozen inside the window: a held column's change is what crossed the edge
        if (win) windowTransfers(r, *win);   // 3. that water into (or out of) the window, column by column
    }
}

namespace {
// 25.39: one held column's 3 x 3 sub-columns in a window's grid (1/9 m cells, 3 per 1/3 m column)
inline glm::ivec2 windowCell(const WaterGrid& g, const ColumnWater& cw, int cx, int cz) {
    return glm::ivec2(cw.minVoxel().x * 9 + cx * 3 - g.spec().origin.x, cw.minVoxel().z * 9 + cz * 3 - g.spec().origin.z);
}
}  // namespace

void WaterCoreManager::windowGhosts(ColumnRegion& r, Av& av) {
    ColumnWater& cw = *r.cw;
    const WaterGrid& g = *av.grid;
    const double V = g.cellVolume(), A = ColumnWater::kH * ColumnWater::kH;
    for (int cz = 0; cz < cw.nz(); ++cz) for (int cx = 0; cx < cw.nx(); ++cx) {
        if (!cw.isHeld(cx, cz)) continue;
        const glm::ivec2 c0 = windowCell(g, cw, cx, cz);
        double m = 0.0, mu = 0.0, mw = 0.0;
        for (int dz = 0; dz < 3; ++dz) for (int dx = 0; dx < 3; ++dx) {
            const int x = c0.x + dx, z = c0.y + dz;
            if (x < 0 || z < 0 || x >= g.nx() || z >= g.nz()) continue;
            for (int y = 0; y < g.ny(); ++y) {
                const float f = g.f(x, y, z);
                if (f <= 0.0f) continue;
                m += f; mu += f * 0.5 * (g.u(x, y, z) + g.u(x + 1, y, z)); mw += f * 0.5 * (g.w(x, y, z) + g.w(x, y, z + 1));
            }
        }
        cw.setGhost(cx, cz, m * V / A, m > 0.0 ? glm::vec2(static_cast<float>(mu / m), static_cast<float>(mw / m)) : glm::vec2(0.0f));
    }
}

void WaterCoreManager::windowTransfers(ColumnRegion& r, Av& av) {
    ColumnWater& cw = *r.cw;
    WaterGrid& g = *av.grid;
    const double V = g.cellVolume();
    const auto& tr = cw.transfers();
    bool any = false;
    for (int cz = 0; cz < cw.nz(); ++cz) for (int cx = 0; cx < cw.nx(); ++cx) {
        const double t = tr.empty() ? 0.0 : tr[static_cast<size_t>(cx) + static_cast<size_t>(cw.nx()) * cz];
        if (t == 0.0) continue;
        any = true;
        if (t > 0.0) r.windowIn += t; else r.windowOut -= t;
        const glm::ivec2 c0 = windowCell(g, cw, cx, cz);
        double left = std::abs(t) / V;   // in cell fills
        // spread over the column's 9 sub-columns: each in turn takes (or gives) up to an equal share, then whoever can
        for (int pass = 0; pass < 2 && left > 1e-12; ++pass) {
            const double share = pass == 0 ? left / 9.0 : left;
            for (int k = 0; k < 9 && left > 1e-12; ++k) {
                const int x = c0.x + k % 3, z = c0.y + k / 3;
                if (x < 0 || z < 0 || x >= g.nx() || z >= g.nz()) continue;
                double want = std::min(share, left);
                if (t > 0.0) {   // into the topmost water, upward (the floor's first air cell when the sub-column is dry)
                    int y = g.ny() - 1;
                    while (y > 0 && !(g.f(x, y - 1, z) > 0.0f || av.occCache[g.idx(x, y - 1, z)] != Occ::Air)) --y;
                    for (; y < g.ny() && want > 1e-12; ++y) {
                        if (av.occCache[g.idx(x, y, z)] != Occ::Air) continue;
                        float& f = g.f(x, y, z);
                        const double add = std::min(want, 1.0 - static_cast<double>(f));
                        f += static_cast<float>(add); want -= add; left -= add;
                    }
                } else {         // out of the topmost water, downward
                    for (int y = g.ny() - 1; y >= 0 && want > 1e-12; --y) {
                        float& f = g.f(x, y, z);
                        if (f <= 0.0f) continue;
                        const double take = std::min(want, static_cast<double>(f));
                        f -= static_cast<float>(take); want -= take; left -= take;
                    }
                }
            }
        }
        if (left > 1e-9) r.windowShortfall += left * V;   // counted, never silent (asserted 0 in the tests)
    }
    if (any) av.solver->wake();
}

int WaterCoreManager::openWindow(int regionId, const glm::ivec2& loIn, const glm::ivec2& hiIn, std::string* err) {
    ColumnRegion* rp = regionById(regionId);
    if (!rp) { if (err) *err = "no column region " + std::to_string(regionId); return 0; }
    ColumnRegion& r = *rp;
    if (r.windowAv) { if (err) *err = "the region already has a window (one per region in H2a)"; return 0; }
    ColumnWater& cw = *r.cw;
    const glm::ivec2 lo(std::max(std::min(loIn.x, hiIn.x), cw.minVoxel().x), std::max(std::min(loIn.y, hiIn.y), cw.minVoxel().z));
    const glm::ivec2 hi(std::min(std::max(loIn.x, hiIn.x), cw.maxVoxel().x), std::min(std::max(loIn.y, hiIn.y), cw.maxVoxel().z));
    if (lo.x > hi.x || lo.y > hi.y) { if (err) *err = "the window is outside the region"; return 0; }
    // the held columns, and the window's floor: the lowest bed under it
    const int nx = cw.nx();
    std::vector<uint8_t> held(static_cast<size_t>(nx) * cw.nz(), 0);
    float lowBed = 1e30f;
    for (int cz = (lo.y - cw.minVoxel().z) * 3; cz < (hi.y - cw.minVoxel().z + 1) * 3; ++cz)
        for (int cx = (lo.x - cw.minVoxel().x) * 3; cx < (hi.x - cw.minVoxel().x + 1) * 3; ++cx) {
            held[static_cast<size_t>(cx) + static_cast<size_t>(nx) * cz] = 1;
            const ShoreColumn& c = cw.solver().col(cx, cz);
            if (!c.wall) lowBed = std::min(lowBed, c.bed);
        }
    if (cw.overlapsSpringOrOutfall(held)) { if (err) *err = "the window covers a spring or an outfall (they belong to the column tier)"; return 0; }
    if (lowBed > 1e29f) { if (err) *err = "the window covers no water-bearing column"; return 0; }
    const int yLo = std::max(cw.minVoxel().y, static_cast<int>(std::floor(lowBed)) - 1);
    const int id = create(glm::ivec3(lo.x, yLo, lo.y), glm::ivec3(hi.x, cw.maxVoxel().y, hi.y), 1.0f / 9.0f, "eulerian", err, "cpu");
    if (!id) return 0;
    Av& av = *volumeById(id);
    av.window = true; av.autoSleep = false;
    av.solver->setPondFriction(0.0f);
    av.solver->setBedManning(cw.manningN());
    // the floor: each cell column stands on its 1/3 m column's bed (walls: the box top - solid all the way up)
    const WaterGrid& g = *av.grid;
    av.floorY.assign(static_cast<size_t>(g.nx()) * g.nz(), 0.0f);
    for (int z = 0; z < g.nz(); ++z) for (int x = 0; x < g.nx(); ++x) {
        const int cx = (g.spec().origin.x + x) / 3 - cw.minVoxel().x * 3, cz = (g.spec().origin.z + z) / 3 - cw.minVoxel().z * 3;
        const ShoreColumn& c = cw.solver().col(cx, cz);
        av.floorY[static_cast<size_t>(x) + static_cast<size_t>(g.nx()) * z] = c.wall ? static_cast<float>(cw.maxVoxel().y + 2) : c.bed;
    }
    av.occDirty = true;
    refreshOccupancy(av);
    av.solver->refreshSolids();
    // seed: each held column's depth into its 9 sub-columns from the floor up (mass-exact), its velocity on their faces
    WaterGrid& gw = *av.grid;
    const double V = gw.cellVolume();
    for (int cz = 0; cz < cw.nz(); ++cz) for (int cx = 0; cx < nx; ++cx) {
        if (!held[static_cast<size_t>(cx) + static_cast<size_t>(nx) * cz]) continue;
        const ShoreColumn& c = cw.solver().col(cx, cz);
        if (c.wall) continue;
        const double depth = std::max(c.eta - static_cast<double>(c.bed), 0.0);
        if (depth <= 0.0) continue;
        const glm::ivec2 c0 = windowCell(gw, cw, cx, cz);
        double left = depth * ColumnWater::kH * ColumnWater::kH / V;   // the column's water in cell fills
        for (int k = 0; k < 9; ++k) {
            const int x = c0.x + k % 3, z = c0.y + k / 3;
            double want = left / static_cast<double>(9 - k);
            for (int y = 0; y < gw.ny() && want > 1e-12; ++y) {
                if (av.occCache[gw.idx(x, y, z)] != Occ::Air) continue;
                const double add = std::min(want, 1.0);
                gw.f(x, y, z) = static_cast<float>(add); want -= add; left -= add;
                gw.u(x, y, z) = c.u; gw.u(x + 1, y, z) = c.u; gw.w(x, y, z) = c.w; gw.w(x, y, z + 1) = c.w;
            }
        }
        if (left > 1e-9) r.windowShortfall += left * V;
    }
    av.solver->wake();
    cw.setHeld(held);   // the held water is the window's now (the ledger moves with it)
    r.windowAv = id; r.wLo = lo; r.wHi = hi; r.windowIn = r.windowOut = 0.0;
    return id;
}

bool WaterCoreManager::closeWindow(int regionId) {
    ColumnRegion* rp = regionById(regionId);
    if (!rp || !rp->windowAv) return false;
    ColumnRegion& r = *rp;
    if (Av* av = volumeById(r.windowAv)) windowGhosts(r, *av);   // the window's water into its columns, exactly
    r.cw->setHeld({});                                           // ... and back in the region's count
    destroy(r.windowAv);
    r.windowAv = 0;
    return true;
}

bool WaterCoreManager::windowInfo(int regionId, WindowInfo& out) const {
    for (const ColumnRegion& r : m_columns) {
        if (r.cw->id() != regionId || !r.windowAv) continue;
        for (const auto& a : m_avs) {
            if (a->id != r.windowAv) continue;
            out.av = a->id; out.lo = r.wLo; out.hi = r.wHi; out.cells = static_cast<long>(a->grid->cellCount());
            out.mass = a->grid->totalMass() + m_pool.volumeInFlight(a->id); out.in = r.windowIn;   // the grid + its droplets in flight
            out.residue = a->residueDropped; out.droplets = m_pool.volumeInFlight(a->id); out.out = r.windowOut; out.shortfall = r.windowShortfall;
            int held = 0; for (uint8_t h : r.cw->heldMask()) held += h; out.held = held;
            return true;
        }
    }
    return false;
}

std::vector<std::pair<glm::vec3, float>> WaterCoreManager::takeMotion(float moveM) {
    std::vector<std::pair<glm::vec3, float>> out;
    m_motionStats = MotionStats{};
    if (m_motionPrevTops.size() != m_fields.size()) m_motionPrevTops.assign(m_fields.size(), {});
    for (size_t fi = 0; fi < m_fields.size(); ++fi) {
        const WaterSurfaceField& f = m_fields[fi];
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
        if (!av.gpuVol || !m_gpu || !m_gpu->readBirths(*av.gpuVol, rec)) return;
        const GridSpec& sp = av.grid->spec();
        const double V = static_cast<double>(av.h) * av.h * av.h;
        for (int z = 0; z < sp.dims.z; ++z) for (int x = 0; x < sp.dims.x; ++x) {
            const size_t c = static_cast<size_t>(x + sp.dims.x * z);
            const float vol = rec[c * 8];
            if (vol <= 0.0f) continue;
            DropletBirth b;
            b.volume = static_cast<float>(vol * V);
            b.pos = glm::vec3((sp.origin.x + x + 0.5f) * av.h, (sp.origin.y + rec[c * 8 + 1] / vol) * av.h, (sp.origin.z + z + 0.5f) * av.h);
            b.vel = glm::vec3(rec[c * 8 + 2], rec[c * 8 + 3], rec[c * 8 + 4]) / vol;
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
        if (d.volumeId < 0) {   // 25.30 SC: a creek's cosmetic droplet - lands on its region's free surface or ground
            ColumnRegion* reg = regionById(-d.volumeId);
            if (!reg) { landAt.push_back(d.pos); return true; }
            const ColumnWater& cw = *reg->cw;
            int cx, cz;
            if (!cw.columnAt(d.pos.x, d.pos.z, cx, cz)) {   // flew off the region: gone at its floor (no water to put back)
                if (d.pos.y < static_cast<float>(cw.minVoxel().y) || d.age > 10.0f) { landAt.push_back(d.pos); return true; }
                return false;
            }
            const ShoreSolver& sv = cw.solver();
            const ShoreColumn& c = sv.col(cx, cz);
            const float ground = cw.voxelTops()[static_cast<size_t>(cx) + static_cast<size_t>(cw.nx()) * cz];
            const bool wet = !c.wall && c.eta - c.bed > sv.params().dryDepth;
            const float S = static_cast<float>(c.eta) + sv.load(cx, cz);
            if (wet && d.vel.y <= 0.0f && d.pos.y <= S) { landAt.push_back(glm::vec3(d.pos.x, S - 0.01f, d.pos.z)); return true; }
            if (d.pos.y <= std::max(ground, wet ? -1e30f : static_cast<float>(c.bed)) || d.age > 10.0f) { landAt.push_back(d.pos); return true; }
            return false;
        }
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
        if (landed[i].volumeId < 0) {   // 25.30 SC: no water lands - only its momentum, into the creek's ripple window
            ++m_dstats.cosmeticLandings;
            ColumnRegion* reg = regionById(-landed[i].volumeId);
            if (reg && reg->ripple && m_rstats.on && landed[i].vel.y < 0.0f) {
                const float d = RippleLayer::kPitch;
                reg->ripple->addImpulse(landAt[i].x, landAt[i].z, 1000.0f * landed[i].volume * -landed[i].vel.y / (d * d));
                ++m_rstats.impulses;
            }
            continue;
        }
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
    for (const auto& r : m_columns) out.push_back(m_rstats.on ? r.ripple.get() : nullptr);   // 25.26 RC: parallel to the regions' fields
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
    if (gpu) m_rgpu->collect(liveRippleLayers());   // 25.34: last frame's GPU step into the layers (heights, sleep)
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
        for (int z = 0; z < L.nz(); ++z) for (int x = 0; x < L.nx(); ++x) {
            const SurfaceColumn* c = column((L.origin().x + x + 0.5f) * d, (L.origin().y + z + 0.5f) * d);
            mask[L.idx(x, z)] = (c && c->runs >= 0.5f) ? 1 : 0;
        }
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
        if (m_rpattern.slope < 0.0f) stepLayer(L, dt, vel.empty() ? nullptr : &vel);
        m_rstats.brokenM3 += L.takeBroken();
        ++m_rstats.layers; m_rstats.cells += static_cast<long>(L.nx()) * L.nz();
        if (!L.asleep()) ++m_rstats.awake;   // the shape stats: refreshRippleShapeStats (on demand)
    }
    // ---- 25.26 RC: column regions - a ripple window following the camera, carried by the current, forced by the flow ----
    for (ColumnRegion& reg : m_columns) {
        ColumnWater& cw = *reg.cw;
        const ShoreSolver& sv = cw.solver();
        RippleLayer& L = rippleWindow(reg.ripple, cw.minVoxel(), cw.maxVoxel(), gpu);   // 26.13: the one window rule
        const int nx = L.nx(), nz = L.nz();
        std::vector<glm::vec2> vel(static_cast<size_t>(nx) * nz, glm::vec2(0.0f));
        auto& mask = L.mask();
        const float dry = sv.params().dryDepth, n = sv.params().manningN;
        const uint32_t frame = ++reg.rippleFrame;
        int forced = 0;
        // per COLUMN first (9 lattice cells share one 1/3 m column): wet, velocity, the forcing's amplitude
        // C_f rho u*^2 dt (-1 = no water) - then the lattice from integer column indices (25.34: the per-cell float lookup
        // and pow were 0.49 ms per frame of the creek window, measured)
        auto fdiv3 = [](int a) { return a >= 0 ? a / 3 : -((-a + 2) / 3); };
        const int colX0 = cw.minVoxel().x * 3, colZ0 = cw.minVoxel().z * 3;
        const int cxa = fdiv3(L.origin().x) - colX0, cza = fdiv3(L.origin().y) - colZ0;
        const int ncx = fdiv3(L.origin().x + nx - 1) - colX0 - cxa + 1, ncz = fdiv3(L.origin().y + nz - 1) - colZ0 - cza + 1;
        std::vector<float> colAmp(static_cast<size_t>(ncx) * ncz, -1.0f);
        std::vector<glm::vec2> colVel(colAmp.size(), glm::vec2(0.0f));
        const bool forcing = m_flowForcing > 0.0f && m_rpattern.slope < 0.0f;
        for (int k = 0; k < ncz; ++k) for (int j2 = 0; j2 < ncx; ++j2) {
            const int cx = cxa + j2, cz = cza + k;
            if (cx < 0 || cz < 0 || cx >= cw.nx() || cz >= cw.nz()) continue;
            const ShoreColumn& col = sv.col(cx, cz);
            const double depth = col.eta - col.bed;
            if (col.wall || depth <= dry) continue;
            const size_t ci = static_cast<size_t>(j2) + static_cast<size_t>(ncx) * k;
            colVel[ci] = glm::vec2(col.u, col.w);
            colAmp[ci] = 0.0f;
            if (!forcing) continue;
            const float speed = std::sqrt(col.u * col.u + col.w * col.w);
            if (speed < 1e-3f) continue;
            const float ustar = speed * n * std::sqrt(9.81f) / std::pow(static_cast<float>(std::max(depth, 1e-3)), 1.0f / 6.0f);
            colAmp[ci] = m_flowForcing * 1000.0f * ustar * ustar * dt;
        }
        std::vector<float> raw;
        for (int z = 0; z < nz; ++z) {
            const int k = fdiv3(L.origin().y + z) - colZ0 - cza;
            for (int x = 0; x < nx; ++x) {
                const size_t i = L.idx(x, z);
                const size_t ci = static_cast<size_t>(fdiv3(L.origin().x + x) - colX0 - cxa) + static_cast<size_t>(ncx) * k;
                const float a = colAmp[ci];
                if (a < 0.0f) { mask[i] = 0; continue; }
                mask[i] = 1;
                vel[i] = colVel[ci];
                if (a <= 0.0f) continue;
                // fade over the window's outer 8 m so its edge is never seen (the ripples die away before the absorbing rim)
                const float edge = static_cast<float>(std::min(std::min(x, z), std::min(nx - 1 - x, nz - 1 - z)));
                const float fade = std::clamp(edge / 72.0f, 0.0f, 1.0f);
                if (fade <= 0.0f) continue;
                uint32_t h = static_cast<uint32_t>(L.origin().x + x) * 73856093u ^ static_cast<uint32_t>(L.origin().y + z) * 19349663u ^ frame * 83492791u;
                h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
                const float xi = (h & 0xFFFFu) / 32767.5f - 1.0f;
                if (raw.empty()) raw.assign(static_cast<size_t>(nx) * nz, 0.0f);
                raw[i] = a * xi * fade;
                ++forced;
            }
        }
        if (!raw.empty()) L.addImpulseField(raw);
        // 25.30 SC: bodies crossing the region's surface - the pond's sharp-edge remainder (22.3.2) at the column pitch
        // (3 lattice cells per 1/3 m column, world-aligned as on the pond)
        for (const MovingSolid& b : m_rippleBodies) {
            if (std::abs(b.velocity.y) < 0.01f || m_rpattern.slope >= 0.0f) continue;
            int bx, bz;
            if (!cw.columnAt(b.centre.x, b.centre.z, bx, bz)) continue;
            const ShoreColumn& bc = sv.col(bx, bz);
            if (bc.wall || bc.eta - bc.bed <= dry) continue;
            const float S = static_cast<float>(bc.eta) + sv.load(bx, bz);
            if (!(b.centre.y - b.halfExtents.y < S && b.centre.y + b.halfExtents.y > S)) continue;
            const float Hc = ColumnWater::kH;
            const int per = 3;
            const glm::vec2 blo(b.centre.x - b.halfExtents.x, b.centre.z - b.halfExtents.z), bhi(b.centre.x + b.halfExtents.x, b.centre.z + b.halfExtents.z);
            const int gx0 = static_cast<int>(std::floor(blo.x / Hc)), gx1 = static_cast<int>(std::floor(bhi.x / Hc));
            const int gz0 = static_cast<int>(std::floor(blo.y / Hc)), gz1 = static_cast<int>(std::floor(bhi.y / Hc));
            for (int gz = gz0; gz <= gz1; ++gz) for (int gx = gx0; gx <= gx1; ++gx) {
                float fine[9]; float sum = 0.0f;
                for (int j = 0; j < per; ++j) for (int i = 0; i < per; ++i) {
                    const float x0 = (gx * per + i) * d, z0 = (gz * per + j) * d;
                    const float ox = std::max(0.0f, std::min(x0 + d, bhi.x) - std::max(x0, blo.x)), oz = std::max(0.0f, std::min(z0 + d, bhi.y) - std::max(z0, blo.y));
                    fine[j * per + i] = ox * oz / (d * d); sum += fine[j * per + i];
                }
                const float coarse = sum / static_cast<float>(per * per);
                for (int j = 0; j < per; ++j) for (int i = 0; i < per; ++i) {
                    const float vhp = b.velocity.y * (fine[j * per + i] - coarse);
                    if (std::abs(vhp) < 1e-5f) continue;
                    L.setKinematic(gx * per + i - L.origin().x, gz * per + j - L.origin().y, vhp);
                    ++m_rstats.kinematicCells;
                }
            }
        }
        L.setBreakSlope(m_rstats.breakSlope);
        if (m_rpattern.slope < 0.0f) stepLayer(L, dt, &vel);
        ++m_rstats.layers; m_rstats.cells += static_cast<long>(nx) * nz;
        if (!L.asleep()) ++m_rstats.awake;
        m_rstats.brokenM3 += L.takeBroken();
        (void)forced;
    }
    m_rstats.gpu = gpu;
    if (gpu) {
        m_rgpu->submit();   // 25.34: no wait - the next frame collects it
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
    for (const auto& f : m_fields) {
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
    for (const auto& f : m_fields) {
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
