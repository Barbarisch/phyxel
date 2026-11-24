#include "graphics/ChunkRenderManager.h"
#include "graphics/ChunkArenaSystem.h"  // Phase 4.3 arena mode (docs/RegionArenaPlan.md)
#include "graphics/ChunkUpdatePerf.h"   // B0 diagnostic timers (docs/ChunkUpdateHitchPlan.md)
#include "core/Cube.h"
#include "core/DamageStage.h"
#include "core/DamageSystem.h"
#include "core/ChunkVoxelStore.h"
#include "core/MaterialRegistry.h"
#include "core/Types.h"
#include "utils/Logger.h"
#include <cstring>
#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <deque>
#include <atomic>
#include <chrono>

namespace Phyxel {
namespace Graphics {

// --- T0: mesh-cost instrumentation (docs/OffThreadMeshingPlan.md) ---
// Aggregated wall time of rebuildAllFaces. Stored as integer microseconds in atomics so it's
// lock-free and stays correct when the mesh moves to a worker thread (T2+). getMeshTimingStats()
// converts to milliseconds for reporting.
namespace {
    std::atomic<uint64_t> g_meshCount{0};        // calls measured since last reset
    std::atomic<uint64_t> g_meshLastMicros{0};   // most recent call
    std::atomic<uint64_t> g_meshMaxMicros{0};    // slowest call seen
    std::atomic<uint64_t> g_meshTotalMicros{0};  // sum, for the running mean
}

ChunkRenderManager::MeshTimingStats ChunkRenderManager::getMeshTimingStats() {
    MeshTimingStats s;
    s.count  = g_meshCount.load(std::memory_order_relaxed);
    s.lastMs = g_meshLastMicros.load(std::memory_order_relaxed) / 1000.0;
    s.maxMs  = g_meshMaxMicros.load(std::memory_order_relaxed) / 1000.0;
    uint64_t total = g_meshTotalMicros.load(std::memory_order_relaxed);
    s.avgMs = s.count ? (double(total) / double(s.count)) / 1000.0 : 0.0;
    return s;
}

void ChunkRenderManager::resetMeshTimingStats() {
    g_meshCount.store(0, std::memory_order_relaxed);
    g_meshLastMicros.store(0, std::memory_order_relaxed);
    g_meshMaxMicros.store(0, std::memory_order_relaxed);
    g_meshTotalMicros.store(0, std::memory_order_relaxed);
}

// --- I7: per-phase mesh timing + micro-count histogram (docs/PerfProgram2026-09.md) ---
namespace {
    constexpr int kPh = ChunkRenderManager::kMeshPhases;
    constexpr int kMb = ChunkRenderManager::kMicroBuckets;
    std::atomic<uint64_t> g_phaseCalls{0};
    std::atomic<uint64_t> g_phaseTotal[kPh]{};
    std::atomic<uint64_t> g_phaseMax[kPh]{};
    std::atomic<uint64_t> g_bucketCalls[kMb]{};
    std::atomic<uint64_t> g_bucketTotal[kMb]{};
    std::atomic<uint64_t> g_bucketMax[kMb]{};
    std::atomic<uint64_t> g_bucketMicros[kMb]{};   // microcubes meshed (count, not time)

    void atomicMax(std::atomic<uint64_t>& a, uint64_t v) {
        uint64_t prev = a.load(std::memory_order_relaxed);
        while (v > prev && !a.compare_exchange_weak(prev, v, std::memory_order_relaxed)) {}
    }
    int microBucket(size_t n) {
        if (n == 0) return 0;
        if (n < 100) return 1;
        if (n < 1000) return 2;
        if (n < 10000) return 3;
        return 4;
    }
}

ChunkRenderManager::MeshPhaseStats ChunkRenderManager::getMeshPhaseStats() {
    MeshPhaseStats s;
    s.calls = g_phaseCalls.load(std::memory_order_relaxed);
    for (int i = 0; i < kPh; ++i) {
        s.phaseTotalMs[i] = g_phaseTotal[i].load(std::memory_order_relaxed) / 1000.0;
        s.phaseMaxMs[i] = g_phaseMax[i].load(std::memory_order_relaxed) / 1000.0;
    }
    for (int i = 0; i < kMb; ++i) {
        s.bucketCalls[i] = g_bucketCalls[i].load(std::memory_order_relaxed);
        s.bucketTotalMs[i] = g_bucketTotal[i].load(std::memory_order_relaxed) / 1000.0;
        s.bucketMaxMs[i] = g_bucketMax[i].load(std::memory_order_relaxed) / 1000.0;
        s.bucketMicrocubes[i] = g_bucketMicros[i].load(std::memory_order_relaxed);
    }
    return s;
}

void ChunkRenderManager::resetMeshPhaseStats() {
    g_phaseCalls.store(0, std::memory_order_relaxed);
    for (int i = 0; i < kPh; ++i) {
        g_phaseTotal[i].store(0, std::memory_order_relaxed);
        g_phaseMax[i].store(0, std::memory_order_relaxed);
    }
    for (int i = 0; i < kMb; ++i) {
        g_bucketCalls[i].store(0, std::memory_order_relaxed);
        g_bucketTotal[i].store(0, std::memory_order_relaxed);
        g_bucketMax[i].store(0, std::memory_order_relaxed);
        g_bucketMicros[i].store(0, std::memory_order_relaxed);
    }
}

const char* ChunkRenderManager::meshPhaseName(int i) {
    static const char* k[kPh] = {"cube_greedy", "fine_occupancy", "sub_faces", "micro_faces", "sort"};
    return (i >= 0 && i < kPh) ? k[i] : "?";
}

const char* ChunkRenderManager::microBucketName(int i) {
    static const char* k[kMb] = {"0", "1-99", "100-999", "1000-9999", "10000+"};
    return (i >= 0 && i < kMb) ? k[i] : "?";
}

// Smooth-lighting globals (see header). Default: smooth ON, tolerance 0 (pure smooth — no snapping,
// so gentle gradients never band into flat blocky steps). Raise tolerance (or toggle smooth off) only
// as an opt-in perf lever; the default prioritizes look.
bool ChunkRenderManager::s_smoothLighting = true;
// L1 duplicate-emitter merge (docs/PerfProgram2026-09.md). Default ON; POST /api/debug/emitter_merge
// flips it and re-meshes every chunk, so the win is measured by an interleaved A/B in one process.
bool ChunkRenderManager::s_mergeEmitters = true;
// M3-REDESIGN. Default OFF until the bake cost is measured on a real chunk load.
ChunkRenderManager::SkyVisibilityFn ChunkRenderManager::s_skyVisibility;
int  ChunkRenderManager::s_mergeTolerance = 0;
bool ChunkRenderManager::s_foliageEnabled = true;
float ChunkRenderManager::s_foliageDensity = 0.25f;   // 0.5 still read as a solid mass

namespace {
/// Deterministic 0..1 from a world cell — stable across rebuilds, chunk boundaries and
/// machines (no rand, no per-frame state), so canopy thinning never shimmers or re-rolls.
inline float foliageCellHash01(int wx, int wy, int wz) {
    uint32_t h = uint32_t(wx) * 0x8DA6B343u ^ uint32_t(wy) * 0xD8163841u ^
                 uint32_t(wz) * 0xCB1AB31Fu;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return float(h & 0xFFFFFFu) / float(0x1000000u);
}
} // namespace
// Shipped ON by default (2026-07-07): greedy-merging sub/microcube faces recovers ~5-8x FPS on
// face-bound dense scenes (docs/RenderOptimization.md "Heavy-scene FPS validation"). The per-face
// path stays reachable via POST /api/debug/fine_merge {"enabled":false} for A/B. Re-mesh cost of the
// merged path measured acceptable before flipping this default.
bool ChunkRenderManager::s_fineGreedyMerge = true;
int  ChunkRenderManager::s_floodBypass = 0;

ChunkRenderManager::ChunkRenderManager()
    : numInstances(0)
    , needsUpdate(false)
    , renderBuffer(VK_NULL_HANDLE, VK_NULL_HANDLE)
    , grassBuffer(VK_NULL_HANDLE, VK_NULL_HANDLE)
    , foliageBuffer(VK_NULL_HANDLE, VK_NULL_HANDLE)
    , device(VK_NULL_HANDLE)
    , physicalDevice(VK_NULL_HANDLE)
{
    // No eager faces.reserve: the old 32³·6 reserve was ~4.5 MB of commit charge PER CHUNK
    // (Phase 4.4 survey) for a vector that greedy meshing keeps in the hundreds-to-thousands.
    // Geometric growth during the first mesh costs a handful of reallocs once, ever.
}

ChunkRenderManager::~ChunkRenderManager() {
    cleanupVulkanResources();
}

ChunkRenderManager::ChunkRenderManager(ChunkRenderManager&& other) noexcept
    : faces(std::move(other.faces))
    , numInstances(other.numInstances)
    , needsUpdate(other.needsUpdate)
    , m_grassInstances(std::move(other.m_grassInstances))
    , m_foliageInstances(std::move(other.m_foliageInstances))
    , renderBuffer(std::move(other.renderBuffer))
    , grassBuffer(std::move(other.grassBuffer))
    , foliageBuffer(std::move(other.foliageBuffer))
    , device(other.device)
    , physicalDevice(other.physicalDevice)
{
    other.numInstances = 0;
    other.needsUpdate = false;
    other.device = VK_NULL_HANDLE;
    other.physicalDevice = VK_NULL_HANDLE;
}

ChunkRenderManager& ChunkRenderManager::operator=(ChunkRenderManager&& other) noexcept {
    if (this != &other) {
        cleanupVulkanResources();
        
        faces = std::move(other.faces);
        numInstances = other.numInstances;
        needsUpdate = other.needsUpdate;
        m_grassInstances = std::move(other.m_grassInstances);
        m_foliageInstances = std::move(other.m_foliageInstances);
        renderBuffer = std::move(other.renderBuffer);
        grassBuffer = std::move(other.grassBuffer);
        foliageBuffer = std::move(other.foliageBuffer);
        device = other.device;
        physicalDevice = other.physicalDevice;
        
        other.numInstances = 0;
        other.needsUpdate = false;
        other.device = VK_NULL_HANDLE;
        other.physicalDevice = VK_NULL_HANDLE;
    }
    return *this;
}

void ChunkRenderManager::initialize(VkDevice dev, VkPhysicalDevice physDev) {
    device = dev;
    physicalDevice = physDev;
    renderBuffer  = ChunkRenderBuffer(device, physicalDevice);
    grassBuffer   = ChunkRenderBuffer(device, physicalDevice);
    foliageBuffer = ChunkRenderBuffer(device, physicalDevice);
}

uint8_t ChunkRenderManager::skyLightAt(int x, int y, int z) const {
    // M4/U7: the per-cell skylight ARRAY IS GONE. Enclosure is resolved by the per-fragment trace
    // (phxSkyVisibility), not by a stored field -- which is what the directive asked for, and what
    // M0 deleted the flood to achieve. This returns uniform open sky so the vertex path stays a
    // no-op multiply until the varying itself is retired.
    if (x >= 0 && x < 32 && y >= 0 && y < 32 && z >= 0 && z < 32) return 15;
    // Out-of-chunk: read the neighbour chunk's baked light if available, else assume open sky.
    if (m_neighborLight) {
        BakedLight nl;
        if (m_neighborLight(m_lightWorldOrigin + glm::ivec3(x, y, z), nl)) return nl.sky;
    }
    return 15;
}

void ChunkRenderManager::blockLightAt(int x, int y, int z, uint8_t& r, uint8_t& g, uint8_t& b) const {
    // U7 stage 2: the per-cell block-light ARRAYS ARE GONE. Emissive voxels are real point lights
    // now (U3.2), so there is no baked block-light field to read -- and there had been no writer
    // since M0 deleted the flood, so this returned zeros regardless.
    if (x >= 0 && x < 32 && y >= 0 && y < 32 && z >= 0 && z < 32) { r = g = b = 0; return; }
    // Out-of-chunk: read the neighbour chunk's baked block colour if available, else none.
    r = g = b = 0;
    if (m_neighborLight) {
        BakedLight nl;
        if (m_neighborLight(m_lightWorldOrigin + glm::ivec3(x, y, z), nl)) { r = nl.r; g = nl.g; b = nl.b; }
    }
}

bool ChunkRenderManager::bakedLightResolvable(int x, int y, int z, BakedLight& out) const {
    if (x >= 0 && x < 32 && y >= 0 && y < 32 && z >= 0 && z < 32) {
        size_t i = static_cast<size_t>(z + y * 32 + x * 1024);
        out.sky = 15;   // M4: no stored skylight; the trace owns enclosure
        out.r = out.g = out.b = 0;   // U7: no block-light field exists
        return true;
    }
    if (m_neighborLight) return m_neighborLight(m_lightWorldOrigin + glm::ivec3(x, y, z), out);
    return false;
}

void ChunkRenderManager::clearForUniform() {
    // Release, don't just clear — a chunk that transitions to sealed must give back its mesh
    // memory, and a never-meshed chunk must not allocate any of this in the first place.
    std::vector<InstanceData>().swap(faces);
    std::vector<InstanceData>().swap(m_dirScratch);
    numInstances = 0;
    m_dirRangeOffsets.fill(0);
    m_dirTierOffsets.fill(0);
    m_tierFaces.fill(0);
    m_tierUnitFaces.fill(0);
    std::vector<uint8_t>().swap(m_solidVis);
    std::vector<int>().swap(m_cellMat);
    std::vector<uint8_t>().swap(m_cellDamage);
    std::vector<uint8_t>().swap(m_lightOpaque);
    std::vector<uint8_t>().swap(m_prevBorderLight);
    std::vector<uint8_t>().swap(m_cellTransparent);
    m_grassInstances.clear();
    m_foliageInstances.clear();
    m_flamingVoxels.clear();
    m_emissiveLights.clear();
    m_lightBordersChanged = false;
    needsUpdate = false;   // nothing to upload; draws are skipped by numInstances == 0
}

bool ChunkRenderManager::bakedLightAt(int x, int y, int z, BakedLight& out) const {
    if (x < 0 || x >= 32 || y < 0 || y >= 32 || z < 0 || z >= 32) return false;
    size_t i = static_cast<size_t>(z + y * 32 + x * 1024);
    out.sky = 15;   // M4: no stored skylight; the trace owns enclosure
        out.r = out.g = out.b = 0;   // U7: no block-light field exists
    return true;
}

void ChunkRenderManager::setArenaRegionKey(const glm::ivec3& worldOrigin) {
    const uint64_t key = ChunkArenaSystem::regionKeyForChunkOrigin(worldOrigin);
    renderBuffer.setRegionKey(key);
    grassBuffer.setRegionKey(key);
    foliageBuffer.setRegionKey(key);
}

void ChunkRenderManager::rebuildAllFaces(
    const std::vector<std::unique_ptr<Cube>>& cubes,
    const glm::ivec3& worldOrigin,
    const NeighborLookupFunc& getNeighborCube,
    const NeighborLightFunc& getNeighborLight,
    const std::vector<uint8_t>* columnOpenMask,
    const ChunkVoxelStore* voxelStore)
{
    // T0 instrumentation: time the whole mesh op (greedy mesh + light bake). Records on scope exit
    // so every return path is covered. See docs/OffThreadMeshingPlan.md.
    const auto t_meshStart = std::chrono::high_resolution_clock::now();
    struct MeshTimerGuard {
        std::chrono::high_resolution_clock::time_point start;
        ~MeshTimerGuard() {
            const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start).count();
            const uint64_t u = static_cast<uint64_t>(micros < 0 ? 0 : micros);
            g_meshLastMicros.store(u, std::memory_order_relaxed);
            g_meshTotalMicros.fetch_add(u, std::memory_order_relaxed);
            g_meshCount.fetch_add(1, std::memory_order_relaxed);
            uint64_t prevMax = g_meshMaxMicros.load(std::memory_order_relaxed);
            while (u > prevMax &&
                   !g_meshMaxMicros.compare_exchange_weak(prevMax, u, std::memory_order_relaxed)) {}
        }
    } meshTimerGuard{t_meshStart};

    faces.clear();

    // Make the cross-chunk light lookup available to skyLightAt/blockLightAt + the BFS seeding.
    m_neighborLight = getNeighborLight;
    m_lightWorldOrigin = worldOrigin;

    // I7: per-phase timing. t[0] = start, t[i+1] = end of phase i. The sub/micro phases (1-3) are
    // empty since the 1/9 m subcube/microcube store was deleted (D3c-5); the slots are kept so the
    // stats layout and API stay stable.
    using PhaseClock = std::chrono::steady_clock;
    PhaseClock::time_point t[kMeshPhases + 1];
    t[0] = PhaseClock::now();
    rebuildCubeFaces(cubes, worldOrigin, getNeighborCube, columnOpenMask, voxelStore);
    t[1] = PhaseClock::now();
    t[2] = t[3] = t[4] = t[1];

    // Phase 3 face-direction bucketing (docs/LargeWorldScalePlan.md): reorder the
    // instance buffer direction-major so draw passes can skip whole ranges the GPU
    // would cull anyway (a +X face can never be visible from a camera on its -X side).
    reorderFacesByDirection();
    t[5] = PhaseClock::now();

    {
        g_phaseCalls.fetch_add(1, std::memory_order_relaxed);
        for (int i = 0; i < kMeshPhases; ++i) {
            const auto us = static_cast<uint64_t>(
                std::max<long long>(0, std::chrono::duration_cast<std::chrono::microseconds>(t[i + 1] - t[i]).count()));
            g_phaseTotal[i].fetch_add(us, std::memory_order_relaxed);
            atomicMax(g_phaseMax[i], us);
        }
        const auto whole = static_cast<uint64_t>(
            std::max<long long>(0, std::chrono::duration_cast<std::chrono::microseconds>(t[kMeshPhases] - t[0]).count()));
        const int b = microBucket(0);
        g_bucketCalls[b].fetch_add(1, std::memory_order_relaxed);
        g_bucketTotal[b].fetch_add(whole, std::memory_order_relaxed);
        atomicMax(g_bucketMax[b], whole);
    }

    numInstances = static_cast<uint32_t>(faces.size());
    needsUpdate = true;
    m_neighborLight = nullptr;  // don't hold the closure past the rebuild
}

void ChunkRenderManager::reorderFacesByDirection() {
    // Counting sort by (faceID, tier): faceID bits 15-17 (0=+Z 1=-Z 2=+X 3=-X 4=+Y 5=-Y) major,
    // scaleLevel bits 18-19 (0 cube, 1 sub, 2 micro, 3 LOD cell) minor. Stable, one pass + one
    // scatter. m_dirRangeOffsets[d] = first instance of direction d, [6] = total; consumers verify
    // [6] == numInstances before trusting the ranges.
    //
    // The tier key reorders NOTHING: faces are built cubes, then subcubes, then microcubes, so
    // within one direction a stable sort already left them in tier order. It only records where
    // each tier's run starts (I5 census, I6 per-tier draws; docs/PerfProgram2026-09.md).
    namespace TR = TierRanges;
    std::array<uint32_t, TR::kBuckets> counts{};
    m_tierFaces.fill(0);
    m_tierUnitFaces.fill(0);
    for (const auto& f : faces) {
        counts[TR::bucketOf(f.packedData)]++;
        const uint32_t t = TR::tierOf(f.packedData);
        m_tierFaces[t]++;
        m_tierUnitFaces[t] += TR::unitFaces(f.packedData, f.light);
    }
    uint32_t run = 0;
    for (uint32_t b = 0; b < TR::kBuckets; ++b) {
        m_dirTierOffsets[b] = run;
        run += counts[b];
    }
    m_dirTierOffsets[TR::kBuckets] = run;
    for (int d = 0; d < 6; ++d) m_dirRangeOffsets[d] = m_dirTierOffsets[d * TR::kTiers];
    m_dirRangeOffsets[6] = run;

    m_dirScratch.resize(faces.size());
    std::array<uint32_t, TR::kBuckets> cursor;
    std::copy(m_dirTierOffsets.begin(), m_dirTierOffsets.begin() + TR::kBuckets, cursor.begin());
    for (const auto& f : faces) m_dirScratch[cursor[TR::bucketOf(f.packedData)]++] = f;
    faces.swap(m_dirScratch);
}

void ChunkRenderManager::countCoveredCubeFaces(uint64_t& covered, uint64_t& unknown) const {
    covered = 0;
    unknown = 0;
    if (m_solidVis.empty()) return;
    // Face direction -> outward normal, and the (u, v) in-plane axes of a merged cube face. These
    // MUST match rebuildCubeFaces: +Z/-Z u=x v=y, +X/-X u=z v=y, +Y/-Y u=x v=z; origin = min corner.
    static const glm::ivec3 kNormal[6] = {{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
    static const glm::ivec3 kU[6] = {{1,0,0},{1,0,0},{0,0,1},{0,0,1},{1,0,0},{1,0,0}};
    static const glm::ivec3 kV[6] = {{0,1,0},{0,1,0},{0,1,0},{0,1,0},{0,0,1},{0,0,1}};

    // Only cube content exists since D3c-5 (the 1/9 m subcube/microcube store was deleted), and a
    // cube face is never emitted against an opaque cube, so `covered` is always 0 now.
    for (const auto& f : faces) {
        if (TierRanges::tierOf(f.packedData) != TierRanges::kCube) continue;
        const int dir = static_cast<int>(TierRanges::dirOf(f.packedData));
        const glm::ivec3 o(f.packedData & 0x1F, (f.packedData >> 5) & 0x1F, (f.packedData >> 10) & 0x1F);
        const int su = static_cast<int>(((f.packedData >> 20) & 0x3Fu) + 1u);
        const int sv = static_cast<int>(((f.packedData >> 26) & 0x3Fu) + 1u);
        const glm::ivec3 n = kNormal[dir];
        for (int u = 0; u < su; ++u)
            for (int v = 0; v < sv; ++v) {
                const glm::ivec3 nb = o + kU[dir] * u + kV[dir] * v + n;
                if (nb.x < 0 || nb.x >= 32 || nb.y < 0 || nb.y >= 32 || nb.z < 0 || nb.z >= 32) ++unknown;
            }
    }
}

void ChunkRenderManager::rebuildCubeFaces(
    const std::vector<std::unique_ptr<Cube>>& cubes,
    const glm::ivec3& worldOrigin,
    const NeighborLookupFunc& getNeighborCube,
    const std::vector<uint8_t>* columnOpenMask,
    const ChunkVoxelStore* voxelStore)
{
    // Greedy meshing for cube faces: merge coplanar, same-material visible faces into
    // rectangles, emitting one sized instance per rectangle (packCubeFaceDataSized)
    // instead of one per voxel face. Large reduction (~3.7x natural terrain, far more on
    // flat/built surfaces). Subcube/microcube faces keep their per-face path below.
    constexpr int N = 32;
    auto cellIdx = [](int x, int y, int z) { return z + y * 32 + x * 1024; };

    // Reset the flaming-voxel seed list; the sub/micro light-seed loops below refill it.
    m_flamingVoxels.clear();
    m_emissiveLights.clear();
    // Reset grass blade instances; refilled by the grass scan after neighbour solidity is known.
    m_grassInstances.clear();
    // Reset foliage card instances; refilled by the leaf scans in this (cube) + the subcube pass.
    m_foliageInstances.clear();

    // Per-material face textures + flags (computed once per distinct material in chunk).
    // emR/G/B = emissive light colour (0-15 per channel, hue from physics.colorTint, brightest
    // channel scaled to 15) used to seed coloured block light; 0 for non-emissive materials.
    // `toughness` is the DAMAGE DISPLAY DENOMINATOR for this material (3.2). Cached per
    // material rather than resolved per voxel: responseFor() does a registry lookup, and a
    // chunk rebuild visits up to 32768 cells but only a handful of distinct materials.
    struct MatFace { uint16_t tex[6]; uint16_t reserved; uint8_t emR, emG, emB; uint8_t isGrass; uint8_t isBillboarded; float toughness; };
    std::unordered_map<std::string, int> matIdByName;
    std::vector<MatFace> matFaces;
    // Reused member scratch buffers (.assign re-zeros without reallocating once warm) — avoids
    // a per-rebuild heap alloc/free of these three N^3 arrays. See header m_solidVis/m_cellMat/m_cellDamage.
    m_solidVis.assign(N * N * N, 0);   // 1 = a visible cube occupies the cell
    m_cellMat.assign(N * N * N, -1);   // index into matFaces
    m_cellDamage.assign(N * N * N, 0); // quantized 0-15 voxel damage (roughness driver)
    m_cellTransparent.assign(N * N * N, 0);
    std::vector<uint8_t>& solidVis   = m_solidVis;
    std::vector<int>&     cellMat    = m_cellMat;
    std::vector<uint8_t>& cellDamage = m_cellDamage;

    // Stage count read ONCE for this rebuild (P4, DamageStage.h): it is atomic and must not
    // be re-read inside the 32,768-cell loop below, and one consistent value per rebuild is
    // what makes a mid-flight knob change safe.
    const int stagesVisible = Phyxel::Core::damageStagesVisible();

    auto& reg = Phyxel::Core::MaterialRegistry::instance();
    // 4.2b hybrid scan: every solid voxel is in the palette store; a materialized overlay Cube
    // (physics/damage state) wins where present. Without a store (unit tests) this degenerates
    // to the old pure-Cube scan.
    const size_t scanCount = voxelStore ? ChunkVoxelStore::kVoxels : cubes.size();
    for (size_t ci = 0; ci < scanCount; ++ci) {
        const Cube* cube = ci < cubes.size() ? cubes[ci].get() : nullptr;
        float damage = 0.0f;
        const std::string* mnamePtr;
        if (cube) {
            if (!cube->isVisible()) continue;
            mnamePtr = &cube->getMaterialName();
            damage = cube->getAccumulatedDamage();
        } else if (voxelStore && voxelStore->solid(ci)) {
            if (!voxelStore->visible(ci)) continue;
            mnamePtr = &voxelStore->material(ci);   // store voxels carry no damage
        } else {
            continue;
        }
        // Cube voxels carry their own position (hand-built test vectors may not be
        // index-aligned); store voxels derive it from the index (z + y*32 + x*1024).
        glm::ivec3 p = cube ? cube->getPosition()
                            : glm::ivec3(static_cast<int>(ci / 1024),
                                         static_cast<int>((ci % 1024) / 32),
                                         static_cast<int>(ci % 32));
        if (p.x < 0 || p.x >= N || p.y < 0 || p.y >= N || p.z < 0 || p.z >= N) continue;
        int cell = cellIdx(p.x, p.y, p.z);
        solidVis[cell] = 1;
        const std::string& mname = *mnamePtr;
        auto it = matIdByName.find(mname);
        if (it == matIdByName.end()) {
            MatFace mf{};
            for (int f = 0; f < 6; ++f) mf.tex[f] = reg.getTextureIndex(mname, f);
            // Grass-topped materials get a procedural blade layer (see grass scan below).
            mf.isGrass = (mname == "Grass" || mname == "GrassForest" || mname == "GrassSavanna") ? 1u : 0u;
            const auto* md = reg.getMaterial(mname);
            // Billboarded (leaf) materials: solid faces are skipped and foliage cards drawn instead.
            mf.isBillboarded = (s_foliageEnabled && md && md->billboarded) ? 1u : 0u;
            bool em = md && md->emissive;
            bool tr = Phyxel::Core::isTransparentMaterial(md);
            bool mi = md && md->isMirror;
            bool va = md && md->varied;   // procedural tiling variation (docs/VoxelOrientation.md)
            uint16_t qa = tr ? static_cast<uint16_t>(md->alpha * 255.0f) : 255u;
            // bit15 = varied. Constant per material, so it does NOT split greedy merges.
            mf.reserved = static_cast<uint16_t>((em ? 1u : 0u) | (tr ? 2u : 0u) |
                                                (qa << 2u) | (mi ? (1u << 10) : 0u) |
                                                (va ? (1u << 15) : 0u));
            // Emissive light colour from the material tint, normalised so the brightest channel
            // emits at `peak` and the hue is preserved. Full-emissive blocks (glow/torch) emit at
            // max (15); MASKED-emissive materials (enchanted log — only cracks glow) cast a dimmer
            // light scaled by emissiveStrength, so a cracked log lights the forest softly, not like a
            // lantern. Block-light seeding below is gated on this colour being non-zero, so it fires
            // for masked materials too even though they don't set the shader's full-emissive bit.
            mf.emR = mf.emG = mf.emB = 0;
            bool castsLight = md && (md->emissive || md->emissiveStrength > 0.0f);
            if (castsLight) {
                glm::vec3 t = md->physics.colorTint;
                float mx = std::max(t.x, std::max(t.y, std::max(t.z, 0.0001f)));
                float peak = md->emissive ? 15.0f
                                          : glm::clamp(md->emissiveStrength * 4.0f, 2.0f, 10.0f);
                float s = peak / mx;
                mf.emR = static_cast<uint8_t>(glm::clamp(t.x * s, 0.0f, 15.0f) + 0.5f);
                mf.emG = static_cast<uint8_t>(glm::clamp(t.y * s, 0.0f, 15.0f) + 0.5f);
                mf.emB = static_cast<uint8_t>(glm::clamp(t.z * s, 0.0f, 15.0f) + 0.5f);
            }
            // Damage display denominator (3.2): this material's own break toughness, so a
            // rendered stage means the same fraction of the way to failure on Glass as on
            // Steel. Resolved ONCE per material here, not per voxel.
            mf.toughness = Phyxel::DamageSystem::responseFor(mname).toughness;
            int newId = static_cast<int>(matFaces.size());
            matFaces.push_back(mf);
            matIdByName[mname] = newId;
            cellMat[cell] = newId;
        } else {
            cellMat[cell] = it->second;
        }

        m_cellTransparent[cell] = (matFaces[cellMat[cell]].reserved & 2u) ? 1u : 0u;

        // Per-voxel accumulated damage -> stage 0..15, packed into instance bits 11-14 and read
        // by voxel.frag. Core::damageStage is the SINGLE SOURCE OF TRUTH: DamageSystem's graze
        // path quantizes with the same function against the same denominator to decide whether a
        // hit changed anything VISIBLE and the chunk needs re-meshing (3.7). If the two
        // disagreed, the engine would rebuild for changes no one can see, or skip a rebuild for
        // one they can -- and a state-only test could not tell.
        //
        // CLAMPED inside damageStage(), and the clamp is load-bearing: bit 15 of this word is the
        // `varied` texture-rotation flag (set per material just above), so a stage >= 16 would
        // overflow bits 11-14 into it and silently hash-rotate the face texture -- which on
        // coursed materials breaks pattern continuity at voxel edges.
        // Quantized to kDamageStagesVisible (P2) and then spread back across the 4-bit field,
        // so this word takes only 4 distinct values {0,5,10,15} instead of 16. Damage is part of
        // the merge key below, so every distinct value is a potential merge-run break: fewer
        // values means wider bands and fewer faces. voxel.frag still divides by 15.0 and needs
        // no change.
        cellDamage[cell] = Phyxel::Core::packDamageStage(
            Phyxel::Core::damageStage(damage, matFaces[cellMat[cell]].toughness, stagesVisible),
            stagesVisible);
    }

    // --- LIGHT FIELD REMOVED (lighting rebuild M0, 2026-08-29) --------------------------------
    // The Minecraft-style per-cell flood lived here: a per-cube-cell opacity mask
    // (m_lightOpaque / m_subFill) plus two BFS floods — 4-bit skylight and 4-bit RGB block light,
    // each decaying 1 per cell.
    //
    // Deleted rather than repaired, because its defects are properties of the MODEL and not bugs
    // in the implementation:
    //   * ONE light value per CUBE cell, while generated walls are 2 micro and floors 3 micro. A
    //     cell that is 1/3 floor and 2/3 standing room can hold only one answer, so it is marked
    //     opaque and carries sky=0 — a hard black band one cube tall along every interior wall
    //     base. Measured live: 240/255 on the wall above the junction, 3.6/255 at it.
    //   * Linear decay, not attenuation. Measured 14,13,12,11,10,9,8,7 along a row from a single
    //     doorway: 47% of full daylight eight cells inside a building.
    //   * No direction, no inverse-square, no bounce, no dependence on the receiving normal.
    //
    // Until the replacement lands (traced sky-dome visibility + traced point-light visibility
    // against the sub-voxel occupancy grid), the field carries a DEFINED PLACEHOLDER rather than
    // being left at zero: skylight uniform 15, block light zero.
    //
    // ⚑ Why not zero: skylight also GATES the sun and the ambient term (voxel.frag multiplies both
    // by phxSkyGate(vSkyLight)), so zeroing it renders the entire world black and hides the
    // replacement's behaviour instead of exposing it. With 15 the outdoors is approximately right
    // and interiors are uniformly, visibly WRONG — which is the state M2/M3 have to remove, and it
    // stays honest about the fact that nothing is lighting interiors yet.
    //
    // The per-corner vertex-light words in InstanceData are deliberately KEPT and still fed from
    // these arrays. Whether the replacement needs them at all is decided in M4.
    m_lightOpaque.clear();

    // ---- M3-REDESIGN: BAKE sky visibility by TRACING, not flooding -----------------------------
    // The flood decayed 1 per cube cell from the nearest opening, which is not light transport;
    // this traces the real sub-voxel geometry and stores the answer in the same slot, so the
    // shader reads one interpolated value again instead of marching 9 rays per fragment.
    //
    // Only AIR cells adjacent to something solid are traced. Those are the only cells a face ever
    // samples, and tracing all 32768 would be wasted on solid rock and open air alike.
    //
    // ⚠️ Traced from the cell CENTRE with an up normal. The wall-base band this rebuild exists to
    // kill came from a MIXED cell being marked opaque and forced to 0; tracing has no notion of
    // opacity, so a cell that is one-third floor and two-thirds room reports what its air actually
    // sees. The per-corner interpolation in static_voxel.vert then smooths across cells as before.
    // ---- U3.2: EMISSIVE VOXELS ARE LIGHTS -------------------------------------------------------
    //
    // The deleted flood already walked exactly these voxels -- it just seeded a per-cell BFS with
    // them, which is not light transport: no inverse-square, no direction, no dependence on the
    // receiving normal, and no occlusion beyond a per-cell opacity bit. This restores the walk and
    // emits real point lights, so a glow block obeys the same rule as every other emitter and is
    // occluded by M2's traced visibility term.
    //
    // Seeding rules recovered from the flood (commit 089ff2cb):
    //   state 1/2 (flaming/smoldering) -> hue from the PER-VOXEL tint, scale 15 / 9
    //   otherwise                      -> require md->emissive || md->emissiveStrength > 0,
    //                                     hue from md->physics.colorTint,
    //                                     scale 15 (emissive) or clamp(strength*4, 2, 10)
    // The masked-emissive branch is what gives the enchanted log its dim crack-light, so it keeps
    // its own scale rather than collapsing into the boolean.
    //
    // Cubes carry no tint/state (only sub- and microcubes can burn), so they take the material path.
    {
        auto& matReg = Phyxel::Core::MaterialRegistry::instance();
        auto unpackTint = [](uint32_t t) -> glm::vec3 {
            if (t == 0u) return glm::vec3(1.0f);            // untinted == white, not black
            return glm::vec3(float((t >> 16) & 0xFFu), float((t >> 8) & 0xFFu),
                             float(t & 0xFFu)) / 255.0f;
        };
        // ---- G-158: ONE LIGHT PER FIXTURE, AT THE EMITTING VOXEL --------------------------
        //
        // This used to push one light per emissive SUB-VOXEL, positioned at the PARENT CUBE
        // centre -- so a flame or a bed of coals became dozens of CO-LOCATED duplicates, which
        // measured 3518 lights from 199 distinct positions across the shipped asset library
        // (17.7x) and filled the entire 32-light GPU cap with copies of ONE emitter in the
        // Ravenmere town. That cost three separate things: ~31 of every 32 per-fragment
        // occupancy marches were exact duplicates, every other light in the scene was starved
        // out of the cap, and the copies SUMMED so the fixture ran ~32x over-bright.
        //
        // Emitters are therefore accumulated per PARENT CUBE and emitted once. Per parent cube
        // and not per fixture: a cube belongs to exactly one chunk, so this merge is chunk-local
        // and seam-free by construction, where merging across cubes would need exactly the
        // cross-chunk lookup the design keys forbid.
        struct EmissiveAccum {
            glm::vec3 weightedPos{0.0f};   // sum of voxel centre * power
            glm::vec3 weightedHue{0.0f};   // sum of hue          * power
            // G-157: the AABB of the emitting CELLS (not their centres), so the light can report
            // how big its own body is and the shadow march can exclude that and nothing more.
            glm::vec3 bmin{ 1e30f};
            glm::vec3 bmax{-1e30f};
            float     power  = 0.0f;       // sum of t * volume, in full-cube equivalents
            float     volume = 0.0f;       // sum of volume, in cube units
            float     maxT   = 0.0f;       // brightest constituent
        };
        // std::map, not unordered_map: RenderCoordinator hashes this list IN ORDER to decide
        // whether to re-register, and LightManager's stable selection tie-breaks on the ids that
        // fall out of it. An order that wobbles between rebuilds would churn both every frame.
        struct CellLess {
            bool operator()(const glm::ivec3& a, const glm::ivec3& b) const {
                if (a.x != b.x) return a.x < b.x;
                if (a.y != b.y) return a.y < b.y;
                return a.z < b.z;
            }
        };
        std::map<glm::ivec3, EmissiveAccum, CellLess> accum;

        // cellHalf = half the emitting voxel's side, so the AABB covers the CELL, not its centre.
        auto emit = [&](const glm::ivec3& parentCube, const glm::vec3& voxelCentre,
                        const std::string& matName, const glm::vec3& voxelTint, uint32_t state,
                        float volumeFraction, float cellHalf) {
            glm::vec3 hue(1.0f);
            float scale15 = 0.0f;
            if (state == 1u || state == 2u) {                // burning: per-voxel tint
                hue = voxelTint;
                scale15 = (state == 1u) ? 15.0f : 9.0f;
            } else {
                const auto* md = matName.empty() ? nullptr : matReg.getMaterial(matName);
                if (!md || (!md->emissive && md->emissiveStrength <= 0.0f)) return;
                hue = md->physics.colorTint;
                scale15 = md->emissive ? 15.0f
                                       : glm::clamp(md->emissiveStrength * 4.0f, 2.0f, 10.0f);
            }
            if (scale15 <= 0.0f) return;
            const float t     = scale15 / 15.0f;             // 0..1
            const float power = t * volumeFraction;
            auto& a = accum[parentCube];
            a.weightedPos += voxelCentre * power;
            a.weightedHue += glm::max(hue, glm::vec3(0.0f)) * power;
            a.power       += power;
            a.volume      += volumeFraction;
            a.maxT         = glm::max(a.maxT, t);
            a.bmin         = glm::min(a.bmin, voxelCentre - glm::vec3(cellHalf));
            a.bmax         = glm::max(a.bmax, voxelCentre + glm::vec3(cellHalf));
        };

        // ⚠️ CUBES COME FROM THE PALETTE STORE, NOT THE `cubes` VECTOR.
        // Phase 4.2b flipped authority to ChunkVoxelStore, so a chunk built the normal way has an
        // EMPTY `cubes` vector and all its voxels in the store. Walking `cubes` found zero emitters
        // on a world with six glow blocks in it -- measured, not guessed. This mirrors
        // rebuildCubeFaces' own scan so the two cannot disagree about what a cube is.
        // Store voxels derive their position from the index and are chunk-LOCAL, so worldOrigin is
        // added here; subcubes and microcubes already report world positions.
        {
            const size_t scanN = voxelStore ? ChunkVoxelStore::kVoxels : cubes.size();
            for (size_t ci = 0; ci < scanN; ++ci) {
                const Cube* cube = ci < cubes.size() ? cubes[ci].get() : nullptr;
                const std::string* mn = nullptr;
                if (cube) {
                    if (cube->isBroken() || !cube->isVisible()) continue;
                    mn = &cube->getMaterialName();
                } else if (voxelStore && voxelStore->solid(ci)) {
                    if (!voxelStore->visible(ci)) continue;
                    mn = &voxelStore->material(ci);
                } else {
                    continue;
                }
                const glm::ivec3 lp = cube
                    ? cube->getPosition()
                    : glm::ivec3(static_cast<int>(ci / 1024),
                                 static_cast<int>((ci % 1024) / 32),
                                 static_cast<int>(ci % 32));
                if (lp.x < 0 || lp.x >= N || lp.y < 0 || lp.y >= N || lp.z < 0 || lp.z >= N) continue;
                emit(worldOrigin + lp, glm::vec3(worldOrigin + lp) + glm::vec3(0.5f),
                     *mn, glm::vec3(1.0f), 0u, 1.0f, 0.5f);
            }
        }
        m_emissiveLights.reserve(m_emissiveLights.size() + accum.size());
        for (const auto& kv : accum) {
            const auto& a = kv.second;
            if (a.power <= 0.0f || a.maxT <= 0.0f) continue;
            EmissiveLight e;
            // The centre of EMISSION, weighted by each voxel's power -- not the centre of the
            // cube that happens to contain it. A wall sconce lights from its flame.
            e.worldPos  = a.weightedPos / a.power;
            e.color     = glm::max(a.weightedHue / a.power, glm::vec3(0.0f));
            // Brightness is the brightest constituent, NOT the sum: 66 glow cells are one bed of
            // coals, not 66 torches stacked. Summing is precisely the over-brightness bug.
            e.intensity = a.maxT;
            // Reach comes from EMITTING VOLUME. The shipped per-resolution constants (cube 1.0,
            // subcube 0.75, microcube 0.5) are exactly linear in log27(volume) -- 1, 1/27 and
            // 1/729 map to 0, -1, -2 -- so this is those same constants made continuous rather
            // than a new invented rule: every single-voxel emitter keeps the reach it ships with
            // today, a cube fully packed with glow microcubes reaches exactly like a glow cube,
            // and a bed of coals lands between them instead of matching a single spark.
            const float sizeScale = glm::clamp(
                1.0f + 0.25f * std::log(glm::max(a.volume, 1e-6f)) / std::log(27.0f), 0.5f, 1.0f);
            // A full-strength glow block lit roughly its room under the old flood (15 cells at
            // 1-per-cell decay), so that reach is the anchor rather than a new invented constant.
            e.radius    = glm::max(2.0f, 15.0f * a.maxT * sizeScale);
            // G-157: how much solid this light may exclude around itself -- the distance from the
            // emission centroid to the far corner of the emitting cells' AABB, i.e. its bounding
            // sphere. A lone microcube reports 1/18 * sqrt(3) = 0.096 u, so a wall one micro cell
            // away is still tested and still blocks; a full glow cube reports 0.866 and excludes
            // exactly itself. The march used to walk up to 32 micro cells with no such bound,
            // which is why a flush-mounted sconce lit straight through its wall at any thickness.
            const glm::vec3 far = glm::max(e.worldPos - a.bmin, a.bmax - e.worldPos);
            e.extent    = glm::length(glm::max(far, glm::vec3(0.0f)));
            m_emissiveLights.push_back(e);
        }

        // L1 (docs/PerfProgram2026-09.md §5): MERGE COINCIDENT EMITTERS. Every emissive sub/micro
        // emits at its PARENT CUBE centre, so a chandelier cube with 8 glow micros produced 8
        // identical lights, and each one ran its own per-fragment occupancy march (measured: 40-45%
        // of uploaded light slots were duplicates). Key = (cube cell, radius). Members of one group
        // share position and radius, and shading is linear (lightColor * intensity * atten(d, r)),
        // so one light with intensity = sum(t) and color = sum(c*t) / sum(t) shades identically.
        // Radii stay separate: a subcube emitter reaches further than a micro in the same cell.
        // A cube cell lies in exactly one chunk, so the result cannot depend on the chunk grid, and
        // sorting by the key makes the output a pure function of the voxels, not of their order.
        // MERGED 2026-10-14: G-158 above (written 2026-09-22, in another session) already emits ONE
        // light per parent cube, at the power-weighted centroid, with intensity = the brightest
        // constituent rather than the sum (summing was the measured over-brightness), so this pass
        // now sees no coincident pairs and is a no-op guard kept for the A/B switch. The two agree
        // on the perf outcome (one march per fixture) and differ on brightness; G-158's look is the
        // one the user confirmed, and EmitterMergeTest pins it.
        if (s_mergeEmitters && m_emissiveLights.size() > 1) {
            auto keyLess = [](const EmissiveLight& a, const EmissiveLight& b) {
                if (a.worldPos.x != b.worldPos.x) return a.worldPos.x < b.worldPos.x;
                if (a.worldPos.y != b.worldPos.y) return a.worldPos.y < b.worldPos.y;
                if (a.worldPos.z != b.worldPos.z) return a.worldPos.z < b.worldPos.z;
                return a.radius < b.radius;
            };
            std::stable_sort(m_emissiveLights.begin(), m_emissiveLights.end(), keyLess);
            std::vector<EmissiveLight> merged;
            merged.reserve(m_emissiveLights.size());
            for (const auto& e : m_emissiveLights) {
                if (!merged.empty() && merged.back().worldPos == e.worldPos && merged.back().radius == e.radius) {
                    EmissiveLight& m = merged.back();
                    // m.color holds sum(c*t) during accumulation; normalised below.
                    m.color += e.color * e.intensity;
                    m.intensity += e.intensity;
                } else {
                    EmissiveLight m = e;
                    m.color = e.color * e.intensity;
                    merged.push_back(m);
                }
            }
            for (auto& m : merged)
                if (m.intensity > 0.0f) m.color /= m.intensity;
            m_emissiveLights.swap(merged);
        }
    }

    // ---- SKY VISIBILITY IS TRACED PER FRAGMENT, NOT BAKED ------------------------------------
    // The chunk-bake that used to live here wrote traced sky access into m_skyLight -- the same
    // per-cell field the deleted flood used. It existed only because D1 measured per-fragment
    // tracing at 24.6 ms/frame, and that measurement was taken at 9 rays / reach 24 / 512 cells,
    // NOT at the 5 rays / reach 16 the bake itself shipped at. Re-measured at the bake's own
    // settings, plus a normal-ray gate, the per-fragment path costs +2.68 ms -- 9.1x less -- and
    // seals a generated interior (sky_probe 0.0 inside, 0.79-0.92 outside).
    //
    // So the storage M0 deleted stays deleted. See docs/UnifiedLightingPlan.md M3-REDESIGN.

    // Snapshot this chunk's boundary light (the 6 faces neighbours sample) and flag if it changed
    // since last rebuild. ChunkManager re-meshes neighbours when it did, so cross-chunk bleed
    // ripples outward and converges (light is monotonic, capped at 15).
    {
        std::vector<uint8_t> border;
        border.reserve(6 * N * N * 2);
        // U7: both bytes are now constants -- sky is uniform (traced per fragment instead) and
        // block light no longer exists. The snapshot is kept because ChunkManager uses its CHANGE
        // to decide whether to re-mesh neighbours; with constants it simply never reports a change,
        // which is correct: there is no cross-chunk baked light left to ripple.
        auto pk = [&](int, int, int) {
            border.push_back(static_cast<uint8_t>(15u));
            border.push_back(static_cast<uint8_t>(0u));
        };
        for (int a = 0; a < N; ++a) for (int b = 0; b < N; ++b) {
            pk(0, a, b);     pk(N - 1, a, b);
            pk(a, 0, b);     pk(a, N - 1, b);
            pk(a, b, 0);     pk(a, b, N - 1);
        }
        m_lightBordersChanged = (border != m_prevBorderLight);
        m_prevBorderLight = std::move(border);
    }

    // Neighbor solidity (handles cross-chunk via getNeighborCube). A face is visible when
    // its neighbor in that direction is NOT a visible solid.
    auto neighborSolid = [&](int x, int y, int z) -> bool {
        if (x >= 0 && x < N && y >= 0 && y < N && z >= 0 && z < N)
            return solidVis[cellIdx(x, y, z)] != 0;
        if (getNeighborCube) {
            // "Occupied": glass across the border is still physical cover for grass (GlassTransparency.md §5).
            return getNeighborCube(worldOrigin + glm::ivec3(x, y, z)) != NeighborOccupancy::Empty;
        }
        return false;  // chunk boundary, no lookup → face exposed
    };

    // FACE EMISSION uses this, not neighborSolid (docs/GlassTransparency.md §5). A face is
    // hidden iff the neighbour is occupied AND (the neighbour is opaque OR this face's own voxel is
    // transparent): stone behind glass is drawn; glass against glass stays culled, so a thick pane
    // does not stack OIT layers; glass lying on stone stays culled. neighborSolid keeps answering
    // "is there physical cover here" for grass — glass on a grass top still covers it.
    auto faceHiddenByNeighbor = [&](int x, int y, int z, bool selfTransparent) -> bool {
        if (x >= 0 && x < N && y >= 0 && y < N && z >= 0 && z < N) {
            const int c = cellIdx(x, y, z);
            if (!solidVis[c]) return false;
            return !m_cellTransparent[c] || selfTransparent;
        }
        if (getNeighborCube) {
            // The same rule across a chunk border, so it cannot depend on where borders fall.
            const NeighborOccupancy o = getNeighborCube(worldOrigin + glm::ivec3(x, y, z));
            if (o == NeighborOccupancy::Empty) return false;
            return o == NeighborOccupancy::Opaque || selfTransparent;
        }
        return false;
    };

    // --- Grass blade instances: one per exposed grass-topped voxel ---
    // A grass voxel sprouts blades when its +Y face is exposed (the same visibility test the top
    // face mesh uses via neighborSolid, so grass never appears under a structure/overhang or in a
    // filled cave). The air cell directly above supplies the baked skylight + block light so blades
    // darken in shadow/caves and pick up glow. Colour comes from the voxel's grass-top texture
    // (matFaces[m].tex[4]); the vertex shader fans this single instance into a clump of blades.
    for (int x = 0; x < N; ++x)
    for (int z = 0; z < N; ++z)
    for (int y = 0; y < N; ++y) {
        int cell = cellIdx(x, y, z);
        if (!solidVis[cell]) continue;
        int m = cellMat[cell];
        if (m < 0) continue;
        // Billboarded leaf CUBE (rare — most leaves are subcubes): its solid faces were skipped in
        // the mesh above; emit ONE foliage instance (cards at the cube centre) if the cube is exposed.
        if (matFaces[m].isBillboarded) {
            // A leaf seen through glass is exposed (GlassTransparency.md §5). Leaves are not transparent, so this is the
            // opaque-owner case of the face rule (a leaf against glass is visible).
            bool exposed = !faceHiddenByNeighbor(x + 1, y, z, false) || !faceHiddenByNeighbor(x - 1, y, z, false) ||
                           !faceHiddenByNeighbor(x, y + 1, z, false) || !faceHiddenByNeighbor(x, y - 1, z, false) ||
                           !faceHiddenByNeighbor(x, y, z + 1, false) || !faceHiddenByNeighbor(x, y, z - 1, false);
            // Canopy thinning (s_foliageDensity): hashed on the ABSOLUTE world cell so the
            // pattern is stable across rebuilds and continuous across chunk seams.
            if (s_foliageDensity < 0.999f &&
                foliageCellHash01(m_lightWorldOrigin.x + x, m_lightWorldOrigin.y + y,
                                  m_lightWorldOrigin.z + z) >= s_foliageDensity) {
                continue;
            }
            if (exposed) {
                uint8_t sky = skyLightAt(x, y + 1, z) & 0xF;  // sample air above the canopy cube
                uint8_t br = 0, bg = 0, bb = 0;
                blockLightAt(x, y + 1, z, br, bg, bb);
                FoliageInstanceData fi;
                fi.packed = (static_cast<uint32_t>(x) & 0x1F)
                          | ((static_cast<uint32_t>(y) & 0x1F) << 5)
                          | ((static_cast<uint32_t>(z) & 0x1F) << 10)
                          | (1u << 15) | (1u << 17) | (1u << 19)   // sub = (1,1,1): cube centre
                          | ((static_cast<uint32_t>(sky) & 0xF) << 21);
                fi.tex = static_cast<uint32_t>(matFaces[m].tex[0])
                       | ((static_cast<uint32_t>(br) & 0xF) << 16)
                       | ((static_cast<uint32_t>(bg) & 0xF) << 20)
                       | ((static_cast<uint32_t>(bb) & 0xF) << 24);
                m_foliageInstances.push_back(fi);
            }
            continue;  // billboarded leaf cube is not grass
        }
        if (!matFaces[m].isGrass) continue;
        if (neighborSolid(x, y + 1, z)) continue;   // top face covered → no grass
        uint8_t sky = skyLightAt(x, y + 1, z) & 0xF;
        uint8_t br = 0, bg = 0, bb = 0;
        blockLightAt(x, y + 1, z, br, bg, bb);

        // EDGE-NESS (world-look C4): how many of the 8 horizontal neighbour columns are also
        // grass-topped, within a ±1 voxel step (the tolerance walking terrain has — a terrace
        // inside a meadow must not taper). Baked here because the shader has no neighbour
        // knowledge; grass.vert consumes the count as a height multiplier so a lawn tapers into
        // dirt/sand/drops instead of ending in a hard green cliff.
        // CROSS-CHUNK neighbours count as GRASSY by construction: solidity is queryable across
        // the border but MATERIAL is not, and a wrong "edge" there would draw a taper seam
        // along every chunk boundary through open meadow — the exact per-chunk artifact class
        // the grass density LOD just had to remove. A missing taper on a genuine cross-chunk
        // edge is the cheap half of that trade.
        auto grassyColumn = [&](int nx, int nz) -> bool {
            if (nx < 0 || nx >= N || nz < 0 || nz >= N) return true;
            for (int dy = 1; dy >= -1; --dy) {
                const int yy = y + dy;
                if (yy < 0 || yy >= N) continue;
                if (!solidVis[cellIdx(nx, yy, nz)]) continue;
                // Topmost solid in the band. Covered top = a wall face, not a meadow column.
                if (neighborSolid(nx, yy + 1, nz)) return false;
                const int nm = cellMat[cellIdx(nx, yy, nz)];
                return nm >= 0 && matFaces[nm].isGrass;
            }
            return false;   // no solid within ±1: a drop — the meadow ends here
        };
        uint32_t grassy = 0;
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz) {
                if (dx == 0 && dz == 0) continue;
                if (grassyColumn(x + dx, z + dz)) ++grassy;
            }

        GrassInstanceData gi;
        gi.packed = (static_cast<uint32_t>(x) & 0x1F)
                  | ((static_cast<uint32_t>(y) & 0x1F) << 5)
                  | ((static_cast<uint32_t>(z) & 0x1F) << 10)
                  | ((static_cast<uint32_t>(sky) & 0xF) << 15)
                  | ((static_cast<uint32_t>(br) & 0xF) << 19)
                  | ((static_cast<uint32_t>(bg) & 0xF) << 23)
                  | ((static_cast<uint32_t>(bb) & 0xF) << 27);
        gi.tex = matFaces[m].tex[4]           // +Y (top) grass texture — colour source
               | ((grassy & 0xFu) << 16);     // C4 edge-ness: grassy neighbour count 0-8
        m_grassInstances.push_back(gi);
    }

    // Face direction offsets: 0=+Z, 1=-Z, 2=+X, 3=-X, 4=+Y, 5=-Y
    const int fdx[6] = {0, 0, 1, -1, 0, 0};
    const int fdy[6] = {0, 0, 0, 0, 1, -1};
    const int fdz[6] = {1, -1, 0, 0, 0, 0};

    std::vector<uint8_t> hasFace(N * N);
    std::vector<int>     faceKey(N * N);
    std::vector<int>     faceMat(N * N);
    std::vector<uint8_t> faceDmg(N * N);
    // Packed baked light, NEW layout for smooth (per-corner) skylight:
    //   bits 0-15  = 4 per-vertex skylight nibbles (corner v at bits v*4..v*4+3), AO-averaged
    //   bits 16-27 = block light R(16-19) G(20-23) B(24-27), per-face (flat)
    std::vector<uint32_t> faceLight(N * N);   // 4 corner skies (bits 0-15)
    std::vector<uint32_t> faceLight2(N * N);  // per-corner block: corner0 RGB | corner1 RGB
    std::vector<uint32_t> faceLight3(N * N);  // per-corner block: corner2 RGB | corner3 RGB
    std::vector<uint8_t>  faceUniform(N * N); // 1 = all 4 corners equal (sky+block) → safe to greedy-merge
    std::vector<uint8_t>  used(N * N);

    // In-plane axes per face for smooth corner lighting — MUST match static_voxel.vert's
    // vertexID bit0 (u) / bit1 (v) face-offset directions. A = the air cell the face looks into.
    const glm::ivec3 faceA[6] = {{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
    const glm::ivec3 faceU[6] = {{1,0,0},{-1,0,0},{0,0,-1},{0,0,1},{1,0,0},{1,0,0}};
    const glm::ivec3 faceV[6] = {{0,1,0},{0,1,0},{0,1,0},{0,1,0},{0,0,-1},{0,0,1}};

    // Per face direction, greedy-merge each slice in the (u,v) plane. Axis roles
    // (u = sizeU / vertexID bit0 axis, v = sizeV / bit1 axis):
    //   +Z/-Z: normal=z, u=x, v=y    +X/-X: normal=x, u=z, v=y    +Y/-Y: normal=y, u=x, v=z
    for (int faceID = 0; faceID < 6; ++faceID) {
        for (int s = 0; s < N; ++s) {
            std::fill(hasFace.begin(), hasFace.end(), 0);
            std::fill(used.begin(), used.end(), 0);
            // Build the visible-face mask for this slice.
            for (int u = 0; u < N; ++u) {
                for (int v = 0; v < N; ++v) {
                    int x, y, z;
                    if (faceID <= 1)      { x = u; y = v; z = s; }
                    else if (faceID <= 3) { x = s; y = v; z = u; }
                    else                  { x = u; y = s; z = v; }
                    int cell = cellIdx(x, y, z);
                    if (!solidVis[cell]) continue;
                    if (faceHiddenByNeighbor(x + fdx[faceID], y + fdy[faceID], z + fdz[faceID],
                                             m_cellTransparent[cell] != 0)) continue;
                    int m = cellMat[cell];
                    if (m >= 0 && matFaces[m].isBillboarded) continue;  // leaf cube → foliage cards, no solid face
                    int mi = u * N + v;
                    hasFace[mi] = 1;
                    // Fold damage into the merge key so damaged voxels don't merge with pristine.
                    uint16_t dmgBits = static_cast<uint16_t>(cellDamage[cell]) << 11;
                    faceKey[mi] = (static_cast<int>(matFaces[m].tex[faceID]) << 16) |
                                  (matFaces[m].reserved | dmgBits);
                    faceMat[mi] = m;
                    faceDmg[mi] = cellDamage[cell];
                    // Smooth per-corner skylight + block light + ambient occlusion. For each of the
                    // 4 quad corners (indexed by vertexID), average the light of the 4 cells touching
                    // that corner in the air cell's plane; solid cells read 0 (m_skyLight/m_blockR..
                    // are 0 for solids), so concave corners darken (AO). The GPU interpolates the
                    // 4 corners across the face → smooth gradients instead of per-voxel steps.
                    {
                        const glm::ivec3 A = glm::ivec3(x, y, z) + faceA[faceID];
                        const glm::ivec3& uD = faceU[faceID];
                        const glm::ivec3& vD = faceV[faceID];
                        // Compute the 4 per-corner light values (sky + block RGB). When smooth
                        // lighting is OFF, collapse to a single per-face sample so every face is
                        // uniform and greedy-merges freely (fast/blocky).
                        int skyC[4], rC[4], gC[4], bC[4];
                        const bool smooth = s_smoothLighting;
                        for (int vid = 0; vid < 4; ++vid) {
                            if (smooth) {
                                glm::ivec3 us = (vid & 1) ? uD : -uD;
                                glm::ivec3 vs = (vid & 2) ? vD : -vD;
                                const glm::ivec3 c[4] = { A, A + us, A + vs, A + us + vs };
                                // Average only the samples that are REAL. An out-of-chunk cell with
                                // no baked neighbour is unknown, and skyLightAt()'s optimistic
                                // "open sky" guess used to be averaged in as 15 — which stamped up
                                // to 11/15 skylight onto interior faces at a chunk seam whose bake
                                // was 0, i.e. AO inverted into a bright halo exactly where it
                                // should darken. Skipping the unknown samples clamps the average to
                                // what this chunk can actually see. See bakedLightResolvable().
                                int sSum = 0, rSum = 0, gSum = 0, bSum = 0, n = 0;
                                for (int j = 0; j < 4; ++j) {
                                    BakedLight bl{};
                                    if (!bakedLightResolvable(c[j].x, c[j].y, c[j].z, bl)) continue;
                                    sSum += bl.sky & 0xF;
                                    rSum += bl.r; gSum += bl.g; bSum += bl.b;
                                    ++n;
                                }
                                if (n > 0) {
                                    skyC[vid] = sSum / n; rC[vid] = rSum / n;
                                    gC[vid] = gSum / n; bC[vid] = bSum / n;
                                } else {
                                    // Nothing resolvable at all (the face's own air cell is across
                                    // an unloaded seam): fall back to the optimistic primary sample
                                    // so an exterior face does not go black waiting for a stream-in.
                                    skyC[vid] = skyLightAt(A.x, A.y, A.z) & 0xF;
                                    uint8_t r = 0, gg = 0, bb = 0;
                                    blockLightAt(A.x, A.y, A.z, r, gg, bb);
                                    rC[vid] = r; gC[vid] = gg; bC[vid] = bb;
                                }
                            } else {
                                skyC[vid] = skyLightAt(A.x, A.y, A.z) & 0xF;
                                uint8_t r = 0, gg = 0, bb = 0;
                                blockLightAt(A.x, A.y, A.z, r, gg, bb);
                                rC[vid] = r; gC[vid] = gg; bC[vid] = bb;
                            }
                        }
                        // Merge tolerance: if every channel's corner spread is within tolerance, snap
                        // each channel to its average and mark uniform so the face can greedy-merge.
                        auto spread = [](const int* a) { int mn = a[0], mx = a[0]; for (int i=1;i<4;++i){ mn=a[i]<mn?a[i]:mn; mx=a[i]>mx?a[i]:mx;} return mx-mn; };
                        const int tol = smooth ? s_mergeTolerance : 99;
                        bool uniform = false;
                        if (spread(skyC) <= tol && spread(rC) <= tol && spread(gC) <= tol && spread(bC) <= tol) {
                            int sa=(skyC[0]+skyC[1]+skyC[2]+skyC[3])/4, ra=(rC[0]+rC[1]+rC[2]+rC[3])/4;
                            int ga=(gC[0]+gC[1]+gC[2]+gC[3])/4, ba=(bC[0]+bC[1]+bC[2]+bC[3])/4;
                            for (int i=0;i<4;++i){ skyC[i]=sa; rC[i]=ra; gC[i]=ga; bC[i]=ba; }
                            uniform = true;
                        }
                        uint32_t skyPack = 0, blkLo = 0, blkHi = 0;
                        for (int vid = 0; vid < 4; ++vid) {
                            skyPack |= static_cast<uint32_t>(skyC[vid] & 0xF) << (vid * 4);
                            uint32_t rgb12 = (static_cast<uint32_t>(rC[vid] & 0xF))
                                           | (static_cast<uint32_t>(gC[vid] & 0xF) << 4)
                                           | (static_cast<uint32_t>(bC[vid] & 0xF) << 8);
                            if (vid < 2) blkLo |= rgb12 << (vid * 12);
                            else         blkHi |= rgb12 << ((vid - 2) * 12);
                        }
                        faceLight[mi]  = skyPack;  // 4 corner skies (bits 0-15)
                        faceLight2[mi] = blkLo;    // corner0 RGB | corner1 RGB
                        faceLight3[mi] = blkHi;    // corner2 RGB | corner3 RGB
                        faceUniform[mi] = uniform ? 1u : 0u;
                    }
                }
            }
            // Greedy rectangle merge: width along v, then height along u (same key).
            for (int u = 0; u < N; ++u) {
                for (int v = 0; v < N; ++v) {
                    int mi = u * N + v;
                    if (!hasFace[mi] || used[mi]) continue;
                    int k = faceKey[mi];
                    uint32_t lite = faceLight[mi], lite2 = faceLight2[mi], lite3 = faceLight3[mi];
                    // Only greedy-merge faces with identical packed light (sky + block) AND uniform
                    // corners (a non-uniform/AO/gradient face would render wrong if stretched across
                    // a rectangle, so it stays a 1x1 quad and the GPU interpolates its 4 corners).
                    auto sameLight = [&](int t) {
                        return faceLight[t] == lite && faceLight2[t] == lite2 && faceLight3[t] == lite3 && faceUniform[t];
                    };
                    bool canMerge = faceUniform[mi] != 0;
                    int w = 1;
                    while (canMerge && v + w < N) {
                        int t = u * N + (v + w);
                        if (!hasFace[t] || used[t] || faceKey[t] != k || !sameLight(t)) break;
                        ++w;
                    }
                    int h = 1; bool ok = canMerge;
                    while (canMerge && u + h < N && ok) {
                        for (int vv = v; vv < v + w; ++vv) {
                            int t = (u + h) * N + vv;
                            if (!hasFace[t] || used[t] || faceKey[t] != k || !sameLight(t)) { ok = false; break; }
                        }
                        if (ok) ++h;
                    }
                    for (int uu = u; uu < u + h; ++uu)
                        for (int vv = v; vv < v + w; ++vv) used[uu * N + vv] = 1;

                    // Rectangle origin (u,v) at slice s; sizeU = h (u extent), sizeV = w (v extent).
                    int ox, oy, oz;
                    if (faceID <= 1)      { ox = u; oy = v; oz = s; }
                    else if (faceID <= 3) { ox = s; oy = v; oz = u; }
                    else                  { ox = u; oy = s; oz = v; }
                    const MatFace& mf = matFaces[faceMat[mi]];
                    InstanceData inst;
                    inst.packedData = Phyxel::InstanceDataUtils::packCubeFaceDataSized(
                        ox, oy, oz, faceID, static_cast<uint32_t>(h), static_cast<uint32_t>(w));
                    inst.textureIndex = mf.tex[faceID];
                    inst.reserved = mf.reserved | (static_cast<uint16_t>(faceDmg[mi]) << 11);
                    inst.light  = lite;   // 4 corner skies (bits0-15)
                    faces.push_back(inst);
                }
            }
        }
    }
}

void ChunkRenderManager::updateVulkanBuffer() {
    ScopedChunkPerf _perf(ChunkPerfPhase::BufferUpload);  // B0: ensureCapacity(+realloc) + memcpy, all 3 buffers
    // Face buffer (cube/sub/micro). A chunk may legitimately have no solid faces but still have
    // foliage/grass (e.g. a leaf-only bush, whose billboarded leaves emit no faces), so the grass
    // and foliage uploads below are NOT gated on faces being non-empty.
    if (!faces.empty()) {
        if (!renderBuffer.getMappedMemory()) {
            // Deferred creation (Phase 4.4): this chunk skipped its face buffer at stream-in
            // (sealed/air) and has just unsealed — allocate now, which also uploads `faces`.
            if (device != VK_NULL_HANDLE && physicalDevice != VK_NULL_HANDLE)
                renderBuffer.createBuffer(faces);
        } else {
            // ensureBufferCapacity may call reallocateBuffer() which remaps memory —
            // fetch the pointer AFTER this call so we never write to a freed mapping.
            ensureBufferCapacity(faces.size());
            void* mappedMem = renderBuffer.getMappedMemory();
            if (mappedMem) {
                renderBuffer.updateMaxUsage(faces.size());
                memcpy(mappedMem, faces.data(), sizeof(InstanceData) * faces.size());
            }
        }
    }

    // Grass parallel buffer: grow if a terrain edit added grass beyond capacity, then upload.
    // (Grass only exists on visible top faces, so faces is non-empty whenever grass is.)
    if (!m_grassInstances.empty() && !grassBuffer.getMappedMemory() &&
        device != VK_NULL_HANDLE && physicalDevice != VK_NULL_HANDLE) {
        grassBuffer.createBufferRaw(m_grassInstances.data(), m_grassInstances.size(),
                                    sizeof(GrassInstanceData), m_grassInstances.size());
    } else if (!m_grassInstances.empty() && grassBuffer.getMappedMemory()) {
        if (m_grassInstances.size() > grassBuffer.getCapacity()) {
            grassBuffer.reallocateBuffer(m_grassInstances.size());
        }
        void* grassMem = grassBuffer.getMappedMemory();  // re-fetch: reallocate may remap
        if (grassMem) {
            memcpy(grassMem, m_grassInstances.data(),
                   sizeof(GrassInstanceData) * m_grassInstances.size());
        }
    }

    // Foliage parallel buffer (same pattern as grass).
    if (!m_foliageInstances.empty() && !foliageBuffer.getMappedMemory() &&
        device != VK_NULL_HANDLE && physicalDevice != VK_NULL_HANDLE) {
        foliageBuffer.createBufferRaw(m_foliageInstances.data(), m_foliageInstances.size(),
                                      sizeof(FoliageInstanceData), m_foliageInstances.size());
    } else if (!m_foliageInstances.empty() && foliageBuffer.getMappedMemory()) {
        if (m_foliageInstances.size() > foliageBuffer.getCapacity()) {
            foliageBuffer.reallocateBuffer(m_foliageInstances.size());
        }
        void* foliageMem = foliageBuffer.getMappedMemory();  // re-fetch: reallocate may remap
        if (foliageMem) {
            memcpy(foliageMem, m_foliageInstances.data(),
                   sizeof(FoliageInstanceData) * m_foliageInstances.size());
        }
    }

    needsUpdate = false;
    
    // Periodic utilization logging
    static int updateCount = 0;
    if (++updateCount % 50 == 0) {
        logBufferUtilization();
    }
}

void ChunkRenderManager::updateSingleCubeTexture(
    const glm::ivec3& localPos,
    uint16_t textureIndex,
    const std::vector<std::unique_ptr<Cube>>& cubes)
{
    // Presence is gated by the caller (Chunk checks the voxel store — 4.2b: a solid voxel may
    // have no Cube object). `cubes` is kept in the signature for interface stability.
    (void)cubes;

    // Efficiently update only the affected faces in the buffer
    if (!renderBuffer.getMappedMemory()) return;
    
    bool updatedAnyFaces = false;
    
    // Find all face instances for this cube and update their texture indices
    for (size_t i = 0; i < faces.size(); ++i) {
        InstanceData& face = faces[i];
        
        // Extract position from packed data
        int faceX = face.packedData & 0x1F;
        int faceY = (face.packedData >> 5) & 0x1F;
        int faceZ = (face.packedData >> 10) & 0x1F;
        
        // Check if this face belongs to our cube
        if (faceX == localPos.x && faceY == localPos.y && faceZ == localPos.z) {
            // Update the texture index in the faces vector
            faces[i].textureIndex = textureIndex;
            
            // Update the GPU buffer directly (partial update)
            VkDeviceSize offset = i * sizeof(InstanceData) + offsetof(InstanceData, textureIndex);
            memcpy(static_cast<char*>(renderBuffer.getMappedMemory()) + offset, &textureIndex, sizeof(uint16_t));
            
            updatedAnyFaces = true;
        }
    }
}

void ChunkRenderManager::updateSingleCubeColor(
    const glm::ivec3& localPos,
    const glm::vec3& newColor,
    const std::vector<std::unique_ptr<Cube>>& cubes)
{
    // Color updates would require rebuilding faces since colors are baked into vertex data
    // For now, this is a placeholder - actual implementation depends on rendering architecture
    // TODO: Implement color updates if needed
}

void ChunkRenderManager::createVulkanBuffer() {
    ScopedChunkPerf _perf(ChunkPerfPhase::BufferCreate);  // B0: first-time per-chunk alloc (all 3 buffers)
    if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE) {
        // Headless context (unit tests drive the streaming/load paths with null
        // devices) — nothing to allocate. In the editor/runtime this state would
        // mean initialize() was skipped; those chunks render invisible, which is
        // the same observable failure the old throw surfaced, minus the crash.
        LOG_DEBUG("ChunkRender", "createVulkanBuffer skipped: no Vulkan device (headless)");
        return;
    }
    // Phase 4.4 empty-guard: each buffer is created only when it has data. Sealed/buried and
    // pure-air chunks (the majority of residency on tall terrain) allocate NOTHING here — the
    // old unconditional path gave every such chunk a ~586 KB mapped face buffer plus two 8 B
    // buffers (3 vkAllocateMemory against the AMD/Intel 4096 ceiling). updateVulkanBuffer
    // creates on demand the first time a mesh produces instances. Every draw pass already
    // skips chunks by instance/count before binding, so a never-created buffer is never bound.
    if (!faces.empty()) renderBuffer.createBuffer(faces);
    if (!m_grassInstances.empty())
        grassBuffer.createBufferRaw(m_grassInstances.data(), m_grassInstances.size(),
                                    sizeof(GrassInstanceData), m_grassInstances.size());
    if (!m_foliageInstances.empty())
        foliageBuffer.createBufferRaw(m_foliageInstances.data(), m_foliageInstances.size(),
                                      sizeof(FoliageInstanceData), m_foliageInstances.size());
}

void ChunkRenderManager::cleanupVulkanResources() {
    renderBuffer.cleanup();
    grassBuffer.cleanup();
    foliageBuffer.cleanup();
}

void ChunkRenderManager::ensureBufferCapacity(size_t requiredInstances) {
    if (requiredInstances > renderBuffer.getCapacity()) {
        renderBuffer.reallocateBuffer(requiredInstances);
    }
}

void ChunkRenderManager::logBufferUtilization() const {
    renderBuffer.logUtilization(faces.size());
}

// Helper methods

bool ChunkRenderManager::isCubeFaceVisible(
    const glm::ivec3& cubePos,
    int faceID,
    const std::vector<std::unique_ptr<Cube>>& cubes,
    const glm::ivec3& worldOrigin,
    const NeighborLookupFunc& getNeighborCube) const
{
    // This is a helper that could be used for more sophisticated culling
    // For now, it's not used, but kept for potential future optimization
    return true;
}

const Cube* ChunkRenderManager::getCubeAtPosition(
    const glm::ivec3& localPos,
    const std::vector<std::unique_ptr<Cube>>& cubes) const
{
    // PERFORMANCE CRITICAL: Use indexed lookup - cubes vector is arranged in X-major order
    // Index formula: z + y*32 + x*32*32 (must match Chunk::localToIndex)
    // DO NOT use linear search - with 32K cubes × 6 faces × N chunks = billions of lookups!
    if (localPos.x < 0 || localPos.x >= 32 ||
        localPos.y < 0 || localPos.y >= 32 ||
        localPos.z < 0 || localPos.z >= 32) {
        return nullptr;
    }
    
    size_t index = localPos.z + localPos.y * 32 + localPos.x * 32 * 32;
    if (index >= cubes.size()) {
        return nullptr;
    }
    
    return cubes[index].get();  // Could be nullptr for deleted cubes
}

} // namespace Graphics
} // namespace Phyxel
