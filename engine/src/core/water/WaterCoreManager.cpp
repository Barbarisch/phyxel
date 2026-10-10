// WaterCoreManager — docs/WaterCore.md §5, §15.1 (Phase B engine integration).
#include "core/water/WaterCoreManager.h"
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
                             const std::string& transport, std::string* err, const std::string& backend, int gpuSweeps) {
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
        av->gpuVol = m_gpu->createVolume(av->grid->spec(), err, transport == "flip");
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
    r.backend = av.backend; r.rbgsResidual = av.gpuLast.rbgsResidualMax; r.gpuSweeps = av.backend == "gpu" ? av.gpuSweeps : 0; r.gpuMs = av.gpuLast.gpuMs;
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
    for (const auto& src : av.solver->sources()) { r.sourcePlaced += src.placedTotal; ++r.sourceCount; }
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
}

bool WaterCoreManager::step(int id, int ticks, float dt, AvRecord* out) {
    for (auto& av : m_avs) {
        if (av->id != id) continue;
        refreshOccupancy(*av);
        if (av->backend == "gpu") stepGpu(*av, ticks, dt);
        else for (int i = 0; i < ticks; ++i) { av->last = av->solver->step(dt); av->residueDropped += av->last.residueDropped; }
        ++av->stepCount;
        if (out) *out = record(*av);
        return true;
    }
    return false;
}

void WaterCoreManager::update(float dt) {
    if (!m_realtime) return;
    for (auto& av : m_avs) {
        refreshOccupancy(*av);
        if (av->backend == "gpu") { if (!av->gpuLast.asleep) { stepGpu(*av, 1, dt); ++av->stepCount; } }
        else if (!av->solver->asleep()) { av->last = av->solver->step(dt); av->residueDropped += av->last.residueDropped; ++av->stepCount; }
    }
    // Phase D (16.2): a volume that slept this tick writes back and frees itself (auto_sleep). Ids
    // first - sleep() destroys. A refused write-back (held or non-resident columns) leaves the
    // volume alive and is reported through the drained records, never silent.
    std::vector<int> slept;
    for (const auto& av : m_avs) if (av->autoSleep && isAsleep(*av)) slept.push_back(av->id);
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
            av->grid->f(cx, cy, cz) = std::clamp(fill, 0.0f, 1.0f);
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
    return true;
}

// Phase C slice 2: the grid is the interface. Upload when it is newer than the GPU copy, run the
// ticks on the device (fenced), download so probes, the surface feed and the ledger keep reading
// the grid exactly as they do for the CPU reference. StepReport mirrors the GPU reductions.
void WaterCoreManager::syncFromGpu(Av& av, bool rateLimited) {
    if (!av.gpuVol || !m_gpu || !av.gpuStale) return;
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (rateLimited && av.gpuLastDownloadSec >= 0.0 && now - av.gpuLastDownloadSec < 1.0) return;   // §15.11: realtime readers see the grid once a second
    m_gpu->download(*av.gpuVol, *av.grid);
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
        std::vector<GpuSource> gs; m_gpu->readSources(*av.gpuVol, gs);
        auto& srcs = av.solver->sourcesMutable();
        for (size_t i = 0; i < gs.size() && i < srcs.size(); ++i) { srcs[i].placedTotal = gs[i].placedTotal; srcs[i].pending = gs[i].pending; srcs[i].unplaced = gs[i].unplaced; }
        av.last.sourceUnplaced = 0.0; for (const auto& g : gs) av.last.sourceUnplaced += g.unplaced;
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
bool WaterCoreManager::pushSourcesToGpu(Av& av, std::string* err) {
    if (!av.gpuVol || !m_gpu) return false;
    std::vector<GpuSource> gs;
    for (const SourceSpec& s : av.solver->sources()) {
        GpuSource g; g.cell = static_cast<int32_t>(av.grid->idx(s.cell.x, s.cell.y, s.cell.z)); g.rate = s.rate;
        g.pending = static_cast<float>(s.pending); g.placedTotal = static_cast<float>(s.placedTotal); g.unplaced = static_cast<float>(s.unplaced);
        gs.push_back(g);
    }
    return m_gpu->setSources(*av.gpuVol, gs, err);
}

bool WaterCoreManager::addSource(int id, const glm::vec3& world, float rate, std::string* err) {
    for (auto& av : m_avs) {
        if (av->id != id) continue;
        const glm::ivec3 c = av->grid->worldToCell(world);
        if (!av->grid->inBounds(c.x, c.y, c.z)) { if (err) *err = "source position is outside the volume"; return false; }
        av->solver->addSource(c, rate);
        if (av->backend == "gpu" && av->solver->transport().ownsMass()) { if (err) *err = "sources on a GPU particle volume are not built yet (particles emit on the CPU transport only)"; return false; }
        if (av->backend == "gpu") return pushSourcesToGpu(*av, err);
        return true;
    }
    if (err) *err = "no such volume";
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
            m_gpu->setSolids(*av.gpuVol, av.grid->sData(), m_solidWakeRule ? av.solver->solidWake() : kNoWake, av.solver->solidFresh(), frameSeconds);
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
        m_fields.push_back(av->field);
    }
    return m_fields;
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
