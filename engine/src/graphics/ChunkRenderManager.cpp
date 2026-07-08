#include "graphics/ChunkRenderManager.h"
#include "graphics/ChunkUpdatePerf.h"   // B0 diagnostic timers (docs/ChunkUpdateHitchPlan.md)
#include "core/Cube.h"
#include "core/Subcube.h"
#include "core/Microcube.h"
#include "core/MaterialRegistry.h"
#include "core/Types.h"
#include "utils/Logger.h"
#include <cstring>
#include <iostream>
#include <vector>
#include <string>
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

// Smooth-lighting globals (see header). Default: smooth ON, tolerance 0 (pure smooth — no snapping,
// so gentle gradients never band into flat blocky steps). Raise tolerance (or toggle smooth off) only
// as an opt-in perf lever; the default prioritizes look.
bool ChunkRenderManager::s_smoothLighting = true;
int  ChunkRenderManager::s_mergeTolerance = 0;
bool ChunkRenderManager::s_foliageEnabled = true;
// Shipped ON by default (2026-07-07): greedy-merging sub/microcube faces recovers ~5-8x FPS on
// face-bound dense scenes (docs/RenderOptimization.md "Heavy-scene FPS validation"). The per-face
// path stays reachable via POST /api/debug/fine_merge {"enabled":false} for A/B. Re-mesh cost of the
// merged path measured acceptable before flipping this default.
bool ChunkRenderManager::s_fineGreedyMerge = true;

ChunkRenderManager::ChunkRenderManager()
    : numInstances(0)
    , needsUpdate(false)
    , renderBuffer(VK_NULL_HANDLE, VK_NULL_HANDLE)
    , grassBuffer(VK_NULL_HANDLE, VK_NULL_HANDLE)
    , foliageBuffer(VK_NULL_HANDLE, VK_NULL_HANDLE)
    , device(VK_NULL_HANDLE)
    , physicalDevice(VK_NULL_HANDLE)
{
    faces.reserve(32 * 32 * 32 * 6); // Reserve for maximum faces
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
    if (x >= 0 && x < 32 && y >= 0 && y < 32 && z >= 0 && z < 32) {
        if (m_skyLight.empty()) return 15;
        return m_skyLight[static_cast<size_t>(z + y * 32 + x * 1024)];
    }
    // Out-of-chunk: read the neighbour chunk's baked light if available, else assume open sky.
    if (m_neighborLight) {
        BakedLight nl;
        if (m_neighborLight(m_lightWorldOrigin + glm::ivec3(x, y, z), nl)) return nl.sky;
    }
    return 15;
}

void ChunkRenderManager::blockLightAt(int x, int y, int z, uint8_t& r, uint8_t& g, uint8_t& b) const {
    if (x >= 0 && x < 32 && y >= 0 && y < 32 && z >= 0 && z < 32) {
        if (m_blockR.empty()) { r = g = b = 0; return; }
        size_t i = static_cast<size_t>(z + y * 32 + x * 1024);
        r = m_blockR[i]; g = m_blockG[i]; b = m_blockB[i];
        return;
    }
    // Out-of-chunk: read the neighbour chunk's baked block colour if available, else none.
    r = g = b = 0;
    if (m_neighborLight) {
        BakedLight nl;
        if (m_neighborLight(m_lightWorldOrigin + glm::ivec3(x, y, z), nl)) { r = nl.r; g = nl.g; b = nl.b; }
    }
}

bool ChunkRenderManager::bakedLightAt(int x, int y, int z, BakedLight& out) const {
    if (m_skyLight.empty() || m_blockR.empty()) return false;
    if (x < 0 || x >= 32 || y < 0 || y >= 32 || z < 0 || z >= 32) return false;
    size_t i = static_cast<size_t>(z + y * 32 + x * 1024);
    out.sky = m_skyLight[i];
    out.r = m_blockR[i]; out.g = m_blockG[i]; out.b = m_blockB[i];
    return true;
}

void ChunkRenderManager::rebuildAllFaces(
    const std::vector<std::unique_ptr<Cube>>& cubes,
    const std::vector<std::unique_ptr<Subcube>>& subcubes,
    const std::vector<std::unique_ptr<Microcube>>& microcubes,
    const glm::ivec3& worldOrigin,
    const NeighborLookupFunc& getNeighborCube,
    const NeighborLightFunc& getNeighborLight,
    const std::vector<uint8_t>* columnOpenMask)
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

    // Rebuild faces for each voxel type. Cubes first: rebuildCubeFaces fills m_solidVis (cube-level
    // occupancy) which the sub/micro occlusion reuses. Then build the leaf sub/micro occupancy so
    // rebuildSubcube/MicrocubeFaces can cull hidden faces.
    rebuildCubeFaces(cubes, subcubes, microcubes, worldOrigin, getNeighborCube, columnOpenMask);
    buildSubMicroOccupancy(subcubes, microcubes, worldOrigin);
    rebuildSubcubeFaces(subcubes, worldOrigin);
    rebuildMicrocubeFaces(microcubes, worldOrigin);

    numInstances = static_cast<uint32_t>(faces.size());
    needsUpdate = true;
    m_neighborLight = nullptr;  // don't hold the closure past the rebuild
}

void ChunkRenderManager::rebuildCubeFaces(
    const std::vector<std::unique_ptr<Cube>>& cubes,
    const std::vector<std::unique_ptr<Subcube>>& subcubes,
    const std::vector<std::unique_ptr<Microcube>>& microcubes,
    const glm::ivec3& worldOrigin,
    const NeighborLookupFunc& getNeighborCube,
    const std::vector<uint8_t>* columnOpenMask)
{
    // Greedy meshing for cube faces: merge coplanar, same-material visible faces into
    // rectangles, emitting one sized instance per rectangle (packCubeFaceDataSized)
    // instead of one per voxel face. Large reduction (~3.7x natural terrain, far more on
    // flat/built surfaces). Subcube/microcube faces keep their per-face path below.
    constexpr int N = 32;
    auto cellIdx = [](int x, int y, int z) { return z + y * 32 + x * 1024; };

    // Reset the flaming-voxel seed list; the sub/micro light-seed loops below refill it.
    m_flamingVoxels.clear();
    // Reset grass blade instances; refilled by the grass scan after neighbour solidity is known.
    m_grassInstances.clear();
    // Reset foliage card instances; refilled by the leaf scans in this (cube) + the subcube pass.
    m_foliageInstances.clear();

    // Per-material face textures + flags (computed once per distinct material in chunk).
    // emR/G/B = emissive light colour (0-15 per channel, hue from physics.colorTint, brightest
    // channel scaled to 15) used to seed coloured block light; 0 for non-emissive materials.
    struct MatFace { uint16_t tex[6]; uint16_t reserved; uint8_t emR, emG, emB; uint8_t isGrass; uint8_t isBillboarded; };
    std::unordered_map<std::string, int> matIdByName;
    std::vector<MatFace> matFaces;
    // Reused member scratch buffers (.assign re-zeros without reallocating once warm) — avoids
    // a per-rebuild heap alloc/free of these three N^3 arrays. See header m_solidVis/m_cellMat/m_cellDamage.
    m_solidVis.assign(N * N * N, 0);   // 1 = a visible cube occupies the cell
    m_cellMat.assign(N * N * N, -1);   // index into matFaces
    m_cellDamage.assign(N * N * N, 0); // quantized 0-15 voxel damage (roughness driver)
    std::vector<uint8_t>& solidVis   = m_solidVis;
    std::vector<int>&     cellMat    = m_cellMat;
    std::vector<uint8_t>& cellDamage = m_cellDamage;

    auto& reg = Phyxel::Core::MaterialRegistry::instance();
    for (size_t ci = 0; ci < cubes.size(); ++ci) {
        const Cube* cube = cubes[ci].get();
        if (!cube || !cube->isVisible()) continue;
        glm::ivec3 p = cube->getPosition();
        if (p.x < 0 || p.x >= N || p.y < 0 || p.y >= N || p.z < 0 || p.z >= N) continue;
        int cell = cellIdx(p.x, p.y, p.z);
        solidVis[cell] = 1;
        // Per-voxel accumulated damage (DamageSystem) -> 0..15 for shader roughness modulation.
        // kDamageRef = energy at which a voxel reads as fully worn (prototype constant; a proper
        // version would normalise by the material's break toughness).
        {
            constexpr float kDamageRef = 30.0f;
            float f = cube->getAccumulatedDamage() / kDamageRef;
            f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
            cellDamage[cell] = static_cast<uint8_t>(f * 15.0f + 0.5f);
        }
        const std::string& mname = cube->getMaterialName();
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
            bool tr = md && md->alpha < 0.99f;
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
            int newId = static_cast<int>(matFaces.size());
            matFaces.push_back(mf);
            matIdByName[mname] = newId;
            cellMat[cell] = newId;
        } else {
            cellMat[cell] = it->second;
        }
    }

    // --- Baked skylight (Phase 1, per-chunk) ---
    // Air cells open to the top of the chunk receive full sky (15) straight down through air
    // (lossless), then light spreads to neighbouring air cells at -1 per step via BFS. Air that
    // can't reach a source (a sealed room) stays 0 -> genuinely dark interiors. Boundaries fall
    // back to open sky in skyLightAt(); cross-chunk bleed is a later phase.
    m_skyLight.assign(N * N * N, 0);
    {
        std::deque<int> q;
        // Is the world directly above this column's chunk-top open to the sky? We can't assume
        // the chunk top (y=31) is exposed — a structure's roof often lives in the chunk ABOVE
        // (e.g. a room ceiling at world y=32). Walk upward through neighbouring chunks; if any
        // opaque cube is found the column is roofed, so its top air must NOT be seeded as sky
        // (BFS will instead light it through windows/holes). Without a neighbour lookup we fall
        // back to assuming open (correct for single-chunk content like a freestanding box).
        constexpr int kSkyProbeHeight = 96;  // ~3 chunks; enough for typical buildings
        // Fast path: the caller precomputed which columns are open to the sky (from the chunks
        // above) — an O(1) lookup instead of probing ~96 cells per column. Fallback (no mask):
        // the original per-cell getNeighborCube probe.
        auto columnOpenAbove = [&](int x, int z) -> bool {
            if (columnOpenMask) return (*columnOpenMask)[x * 32 + z] != 0;
            if (!getNeighborCube) return true;
            for (int wy = N; wy < N + kSkyProbeHeight; ++wy) {
                const Cube* nc = getNeighborCube(worldOrigin + glm::ivec3(x, wy, z));
                if (nc && nc->isVisible()) return false;  // roofed somewhere above
            }
            return true;
        };
        // Column seed: from the top, full sky straight down through air until the first solid —
        // but only for columns actually exposed to the sky above the chunk.
        for (int x = 0; x < N; ++x) {
            for (int z = 0; z < N; ++z) {
                if (!columnOpenAbove(x, z)) continue;  // roofed: no direct sky into this column
                for (int y = N - 1; y >= 0; --y) {
                    int cell = cellIdx(x, y, z);
                    if (solidVis[cell]) break;  // blocked: cells below are not direct sky
                    m_skyLight[cell] = 15;
                    q.push_back(cell);
                }
            }
        }
        // Cross-chunk seed: pull skylight in from neighbouring chunks across the 6 boundary planes
        // so light bleeds across chunk seams (e.g. an opening/window in the adjacent chunk).
        if (m_neighborLight) {
            auto seed = [&](int x, int y, int z, int ox, int oy, int oz) {
                int cell = cellIdx(x, y, z);
                if (solidVis[cell]) return;
                BakedLight nl;
                if (m_neighborLight(worldOrigin + glm::ivec3(x + ox, y + oy, z + oz), nl) && nl.sky > 1) {
                    uint8_t v = static_cast<uint8_t>(nl.sky - 1);
                    if (m_skyLight[cell] < v) { m_skyLight[cell] = v; q.push_back(cell); }
                }
            };
            for (int a = 0; a < N; ++a) for (int b = 0; b < N; ++b) {
                seed(0, a, b, -1, 0, 0); seed(N - 1, a, b, 1, 0, 0);
                seed(a, 0, b, 0, -1, 0); seed(a, N - 1, b, 0, 1, 0);
                seed(a, b, 0, 0, 0, -1); seed(a, b, N - 1, 0, 0, 1);
            }
        }
        // BFS relaxation: spread to air neighbours at -1 per step.
        const int ndx[6] = {1, -1, 0, 0, 0, 0};
        const int ndy[6] = {0, 0, 1, -1, 0, 0};
        const int ndz[6] = {0, 0, 0, 0, 1, -1};
        while (!q.empty()) {
            int cell = q.front(); q.pop_front();
            int level = m_skyLight[cell];
            if (level <= 1) continue;
            int cz = cell % 32;
            int cy = (cell / 32) % 32;
            int cx = cell / 1024;
            for (int d = 0; d < 6; ++d) {
                int nx = cx + ndx[d], ny = cy + ndy[d], nz = cz + ndz[d];
                if (nx < 0 || nx >= N || ny < 0 || ny >= N || nz < 0 || nz >= N) continue;
                int ncell = cellIdx(nx, ny, nz);
                if (solidVis[ncell]) continue;  // opaque blocks light
                uint8_t nl = static_cast<uint8_t>(level - 1);
                if (m_skyLight[ncell] < nl) {
                    m_skyLight[ncell] = nl;
                    q.push_back(ncell);
                }
            }
        }
    }

    // --- Baked coloured block light (Phase 2 + colour + cross-chunk bleed) ---
    // Emissive voxels flood-fill light into surrounding air at -1 per step (per RGB channel),
    // blocked by opaque voxels, tinted by each material's colour, AND bleeding in from emissive
    // sources in neighbouring chunks via the boundary seed. So a glow block lights its room warm,
    // a blue crystal lights it blue, even across a chunk seam.
    m_blockR.assign(N * N * N, 0);
    m_blockG.assign(N * N * N, 0);
    m_blockB.assign(N * N * N, 0);
    {
        std::deque<int> q;
        auto bump = [&](int cell, uint8_t r, uint8_t g, uint8_t b) {
            bool up = false;
            if (m_blockR[cell] < r) { m_blockR[cell] = r; up = true; }
            if (m_blockG[cell] < g) { m_blockG[cell] = g; up = true; }
            if (m_blockB[cell] < b) { m_blockB[cell] = b; up = true; }
            if (up) q.push_back(cell);
        };
        for (int cell = 0; cell < N * N * N; ++cell) {
            int m = cellMat[cell];
            // Seed on the precomputed emissive COLOUR being non-zero (not the reserved emissive bit),
            // so MASKED-emissive materials (enchanted log) cast their dim crack-light too.
            if (m >= 0 && (matFaces[m].emR | matFaces[m].emG | matFaces[m].emB)) {
                bump(cell, matFaces[m].emR, matFaces[m].emG, matFaces[m].emB);
            }
        }
        // Seed block light from emissive (glow material) OR flaming/smoldering sub-microcubes,
        // at their parent cube cell. This reuses the torch/glow firelight path for state=flaming
        // (Phase 2b) AND makes emissive subcube light sources actually illuminate their room.
        // Hue from the material colorTint (glow) or the per-voxel tint (flaming); brightest channel
        // scaled to 15 (flaming) / 9 (smoldering), like the cube emissive seed.
        auto seedVoxelLight = [&](const glm::ivec3& parentWorldPos, const std::string& matName,
                                  uint32_t tint, uint8_t state) {
            glm::ivec3 lp = parentWorldPos - worldOrigin;
            if (lp.x < 0 || lp.x >= N || lp.y < 0 || lp.y >= N || lp.z < 0 || lp.z >= N) return;
            glm::vec3 hue(0.0f); float scale = 0.0f;
            if (state == 1u || state == 2u) {                 // flaming / smoldering -> per-voxel tint
                hue = glm::vec3((tint >> 16) & 0xFFu, (tint >> 8) & 0xFFu, tint & 0xFFu) / 255.0f;
                scale = (state == 1u) ? 15.0f : 9.0f;
            } else {
                const auto* md = reg.getMaterial(matName);
                if (!md || (!md->emissive && md->emissiveStrength <= 0.0f)) return;
                hue = md->physics.colorTint;
                scale = md->emissive ? 15.0f : glm::clamp(md->emissiveStrength * 4.0f, 2.0f, 10.0f);
            }
            float mx = std::max(hue.x, std::max(hue.y, std::max(hue.z, 0.0001f)));
            float s = scale / mx;
            bump(cellIdx(lp.x, lp.y, lp.z),
                 static_cast<uint8_t>(glm::clamp(hue.x * s, 0.0f, 15.0f) + 0.5f),
                 static_cast<uint8_t>(glm::clamp(hue.y * s, 0.0f, 15.0f) + 0.5f),
                 static_cast<uint8_t>(glm::clamp(hue.z * s, 0.0f, 15.0f) + 0.5f));
        };
        for (const auto& sc : subcubes) {
            if (!sc || sc->isBroken() || !sc->isVisible()) continue;
            if (sc->getState() == 1u) m_flamingVoxels.push_back(sc->getWorldPosition()); // fire VFX seed
            if (sc->getState() == 0) {
                const auto* scMd = reg.getMaterial(sc->getMaterialName());
                if (scMd && !scMd->emissive && scMd->emissiveStrength <= 0.0f) continue;
            }
            seedVoxelLight(sc->getPosition(), sc->getMaterialName(), sc->getTint(), sc->getState());
        }
        for (const auto& mc : microcubes) {
            if (!mc || mc->isBroken() || !mc->isVisible()) continue;
            if (mc->getState() == 1u) m_flamingVoxels.push_back(mc->getWorldPosition()); // fire VFX seed
            if (mc->getState() == 0) {
                const auto* mcMd = reg.getMaterial(mc->getMaterialName());
                if (mcMd && !mcMd->emissive && mcMd->emissiveStrength <= 0.0f) continue;
            }
            seedVoxelLight(mc->getParentCubePosition(), mc->getMaterialName(), mc->getTint(), mc->getState());
        }
        // Cross-chunk seed from neighbouring chunks' baked block colour across the 6 boundary planes.
        if (m_neighborLight) {
            auto seed = [&](int x, int y, int z, int ox, int oy, int oz) {
                int cell = cellIdx(x, y, z);
                if (solidVis[cell]) return;
                BakedLight nl;
                if (m_neighborLight(worldOrigin + glm::ivec3(x + ox, y + oy, z + oz), nl)) {
                    bump(cell, nl.r > 0 ? nl.r - 1 : 0, nl.g > 0 ? nl.g - 1 : 0, nl.b > 0 ? nl.b - 1 : 0);
                }
            };
            for (int a = 0; a < N; ++a) for (int b = 0; b < N; ++b) {
                seed(0, a, b, -1, 0, 0); seed(N - 1, a, b, 1, 0, 0);
                seed(a, 0, b, 0, -1, 0); seed(a, N - 1, b, 0, 1, 0);
                seed(a, b, 0, 0, 0, -1); seed(a, b, N - 1, 0, 0, 1);
            }
        }
        const int ndx[6] = {1, -1, 0, 0, 0, 0};
        const int ndy[6] = {0, 0, 1, -1, 0, 0};
        const int ndz[6] = {0, 0, 0, 0, 1, -1};
        while (!q.empty()) {
            int cell = q.front(); q.pop_front();
            uint8_t r = m_blockR[cell], g = m_blockG[cell], b = m_blockB[cell];
            if (r <= 1 && g <= 1 && b <= 1) continue;
            int cz = cell % 32;
            int cy = (cell / 32) % 32;
            int cx = cell / 1024;
            uint8_t pr = r > 0 ? r - 1 : 0, pg = g > 0 ? g - 1 : 0, pb = b > 0 ? b - 1 : 0;
            for (int d = 0; d < 6; ++d) {
                int nx = cx + ndx[d], ny = cy + ndy[d], nz = cz + ndz[d];
                if (nx < 0 || nx >= N || ny < 0 || ny >= N || nz < 0 || nz >= N) continue;
                int ncell = cellIdx(nx, ny, nz);
                if (solidVis[ncell]) continue;  // light fills air, blocked by opaque
                bump(ncell, pr, pg, pb);
            }
        }
    }

    // Snapshot this chunk's boundary light (the 6 faces neighbours sample) and flag if it changed
    // since last rebuild. ChunkManager re-meshes neighbours when it did, so cross-chunk bleed
    // ripples outward and converges (light is monotonic, capped at 15).
    {
        std::vector<uint8_t> border;
        border.reserve(6 * N * N * 2);
        auto pk = [&](int x, int y, int z) {
            int c = cellIdx(x, y, z);
            border.push_back(static_cast<uint8_t>((m_skyLight[c] & 0xF) | ((m_blockR[c] & 0xF) << 4)));
            border.push_back(static_cast<uint8_t>((m_blockG[c] & 0xF) | ((m_blockB[c] & 0xF) << 4)));
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
            const Cube* nc = getNeighborCube(worldOrigin + glm::ivec3(x, y, z));
            return nc && nc->isVisible();
        }
        return false;  // chunk boundary, no lookup → face exposed
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
            bool exposed = !neighborSolid(x + 1, y, z) || !neighborSolid(x - 1, y, z) ||
                           !neighborSolid(x, y + 1, z) || !neighborSolid(x, y - 1, z) ||
                           !neighborSolid(x, y, z + 1) || !neighborSolid(x, y, z - 1);
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
        GrassInstanceData gi;
        gi.packed = (static_cast<uint32_t>(x) & 0x1F)
                  | ((static_cast<uint32_t>(y) & 0x1F) << 5)
                  | ((static_cast<uint32_t>(z) & 0x1F) << 10)
                  | ((static_cast<uint32_t>(sky) & 0xF) << 15)
                  | ((static_cast<uint32_t>(br) & 0xF) << 19)
                  | ((static_cast<uint32_t>(bg) & 0xF) << 23)
                  | ((static_cast<uint32_t>(bb) & 0xF) << 27);
        gi.tex = matFaces[m].tex[4];  // +Y (top) grass texture — colour source
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
                    if (neighborSolid(x + fdx[faceID], y + fdy[faceID], z + fdz[faceID])) continue;
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
                                int sSum = 0, rSum = 0, gSum = 0, bSum = 0;
                                for (int j = 0; j < 4; ++j) {
                                    sSum += skyLightAt(c[j].x, c[j].y, c[j].z) & 0xF;
                                    uint8_t r = 0, gg = 0, bb = 0;
                                    blockLightAt(c[j].x, c[j].y, c[j].z, r, gg, bb);
                                    rSum += r; gSum += gg; bSum += bb;
                                }
                                skyC[vid] = sSum / 4; rC[vid] = rSum / 4; gC[vid] = gSum / 4; bC[vid] = bSum / 4;
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
                    inst.light2 = lite2;  // per-corner block RGB (corners 0,1)
                    inst.light3 = lite3;  // per-corner block RGB (corners 2,3)
                    faces.push_back(inst);
                }
            }
        }
    }
}

// Populate m_subOcc / m_microOcc from the chunk's leaf subcubes/microcubes. Cube occupancy is
// already in m_solidVis (rebuildCubeFaces ran first). Built once per rebuild and queried by the
// sub/micro face culling below.
void ChunkRenderManager::buildSubMicroOccupancy(
    const std::vector<std::unique_ptr<Subcube>>& subcubes,
    const std::vector<std::unique_ptr<Microcube>>& microcubes,
    const glm::ivec3& worldOrigin)
{
    m_subOcc.clear();
    m_microOcc.clear();

    for (const auto& sc : subcubes) {
        if (!sc || sc->isBroken() || !sc->isVisible()) continue;
        glm::ivec3 lp = sc->getPosition() - worldOrigin;
        if (lp.x < 0 || lp.x >= 32 || lp.y < 0 || lp.y >= 32 || lp.z < 0 || lp.z >= 32) continue;
        glm::ivec3 sp = sc->getLocalPosition();
        if (sp.x < 0 || sp.x >= 3 || sp.y < 0 || sp.y >= 3 || sp.z < 0 || sp.z >= 3) continue;
        uint32_t cubeIdx = static_cast<uint32_t>(lp.z + lp.y * 32 + lp.x * 1024);
        uint32_t subKey  = cubeIdx * 27u + static_cast<uint32_t>(sp.z + sp.y * 3 + sp.x * 9);
        m_subOcc.insert(subKey);
    }

    for (const auto& mc : microcubes) {
        if (!mc || mc->isBroken() || !mc->isVisible()) continue;
        glm::ivec3 lp = mc->getParentCubePosition() - worldOrigin;
        if (lp.x < 0 || lp.x >= 32 || lp.y < 0 || lp.y >= 32 || lp.z < 0 || lp.z >= 32) continue;
        glm::ivec3 sp = mc->getSubcubeLocalPosition();
        glm::ivec3 mp = mc->getMicrocubeLocalPosition();
        if (sp.x < 0 || sp.x >= 3 || sp.y < 0 || sp.y >= 3 || sp.z < 0 || sp.z >= 3) continue;
        if (mp.x < 0 || mp.x >= 3 || mp.y < 0 || mp.y >= 3 || mp.z < 0 || mp.z >= 3) continue;
        uint32_t cubeIdx  = static_cast<uint32_t>(lp.z + lp.y * 32 + lp.x * 1024);
        uint32_t subKey   = cubeIdx * 27u + static_cast<uint32_t>(sp.z + sp.y * 3 + sp.x * 9);
        uint32_t microKey = subKey * 27u + static_cast<uint32_t>(mp.z + mp.y * 3 + mp.x * 9);
        m_microOcc.insert(microKey);
    }
}

bool ChunkRenderManager::cubeCellSolid(int lx, int ly, int lz) const {
    if (lx < 0 || lx >= 32 || ly < 0 || ly >= 32 || lz < 0 || lz >= 32) return false;
    if (m_solidVis.empty()) return false;
    return m_solidVis[static_cast<size_t>(lz + ly * 32 + lx * 1024)] != 0;
}

bool ChunkRenderManager::subCellSolid(int lx, int ly, int lz, int sx, int sy, int sz) const {
    if (cubeCellSolid(lx, ly, lz)) return true;  // parent cube is a full solid cube
    uint32_t cubeIdx = static_cast<uint32_t>(lz + ly * 32 + lx * 1024);
    uint32_t subKey  = cubeIdx * 27u + static_cast<uint32_t>(sz + sy * 3 + sx * 9);
    return m_subOcc.find(subKey) != m_subOcc.end();
}

bool ChunkRenderManager::microCellSolid(int lx, int ly, int lz, int sx, int sy, int sz,
                                        int mx, int my, int mz) const {
    if (cubeCellSolid(lx, ly, lz)) return true;  // parent cube fully solid
    uint32_t cubeIdx = static_cast<uint32_t>(lz + ly * 32 + lx * 1024);
    uint32_t subKey  = cubeIdx * 27u + static_cast<uint32_t>(sz + sy * 3 + sx * 9);
    if (m_subOcc.find(subKey) != m_subOcc.end()) return true;  // parent subcube fully solid
    uint32_t microKey = subKey * 27u + static_cast<uint32_t>(mz + my * 3 + mx * 9);
    return m_microOcc.find(microKey) != m_microOcc.end();
}

void ChunkRenderManager::rebuildSubcubeFaces(
    const std::vector<std::unique_ptr<Subcube>>& subcubes,
    const glm::ivec3& worldOrigin)
{
    // (m_foliageInstances is cleared in rebuildCubeFaces, which runs first via rebuildAllFaces, so
    // cube-leaf + subcube-leaf foliage instances accumulate into the same list.)

    // Greedy-merged path (Increment 3): same algorithm as the microcube merger, at the 3x3 subcube
    // grid. Falls back to the per-face path below when the toggle is off.
    if (s_fineGreedyMerge) { rebuildSubcubeFacesMerged(subcubes, worldOrigin); return; }

    // Process subcubes (from subdivided cubes)
    for (const auto& subcube : subcubes) {
        // Skip broken or hidden subcubes
        if (!subcube || subcube->isBroken() || !subcube->isVisible()) {
            continue;
        }
        
        // Get subcube properties
        glm::ivec3 parentPos = subcube->getPosition();     // Parent cube's world position
        glm::ivec3 localPos = subcube->getLocalPosition(); // 0-2 for each axis within parent
        
        // Convert parent world position to chunk-relative position
        glm::ivec3 parentChunkPos = parentPos - worldOrigin;
        
        // Validate parent position is within chunk bounds
        if (parentChunkPos.x < 0 || parentChunkPos.x >= 32 ||
            parentChunkPos.y < 0 || parentChunkPos.y >= 32 ||
            parentChunkPos.z < 0 || parentChunkPos.z >= 32) {
            continue; // Skip subcubes with invalid parent positions
        }
        
        // Cull subcube faces whose neighbour sub-cell is fully solid (in-chunk occlusion; faces
        // at the chunk boundary are treated as exposed — cross-chunk sub/micro occlusion is a
        // later phase). The subcube grid is 96^3 cells across the chunk (32 cubes * 3).
        bool faceVisible[6];
        {
            const int gsx = parentChunkPos.x * 3 + localPos.x;
            const int gsy = parentChunkPos.y * 3 + localPos.y;
            const int gsz = parentChunkPos.z * 3 + localPos.z;
            static const int OFX[6] = {0, 0, 1, -1, 0, 0};  // faceID 0=+Z,1=-Z,2=+X,3=-X,4=+Y,5=-Y
            static const int OFY[6] = {0, 0, 0, 0, 1, -1};
            static const int OFZ[6] = {1, -1, 0, 0, 0, 0};
            for (int f = 0; f < 6; ++f) {
                int nx = gsx + OFX[f], ny = gsy + OFY[f], nz = gsz + OFZ[f];
                if (nx < 0 || nx >= 96 || ny < 0 || ny >= 96 || nz < 0 || nz >= 96) {
                    faceVisible[f] = true;  // out of chunk: assume exposed
                } else {
                    faceVisible[f] = !subCellSolid(nx / 3, ny / 3, nz / 3, nx % 3, ny % 3, nz % 3);
                }
            }
        }

        // Billboarded foliage (leaf materials): skip solid faces entirely — the FoliageRenderPipeline
        // draws cutout leaf cards instead. Emit ONE foliage instance for exposed (canopy-shell) leaf
        // subcubes; buried ones are invisible and contribute nothing. The subcube stays in occupancy
        // (buildSubMicroOccupancy) so interior wood faces still cull. Gated on s_foliageEnabled.
        if (s_foliageEnabled) {
            const auto* md = Phyxel::Core::MaterialRegistry::instance().getMaterial(subcube->getMaterialName());
            if (md && md->billboarded) {
                bool exposed = faceVisible[0] || faceVisible[1] || faceVisible[2] ||
                               faceVisible[3] || faceVisible[4] || faceVisible[5];
                if (exposed) {
                    // Baked light at the leaf's own (air) cell — canopy gets dappled sky/block light.
                    uint8_t skyV = skyLightAt(parentChunkPos.x, parentChunkPos.y, parentChunkPos.z) & 0xF;
                    uint8_t br = 0, bg = 0, bb = 0;
                    blockLightAt(parentChunkPos.x, parentChunkPos.y, parentChunkPos.z, br, bg, bb);
                    FoliageInstanceData fi;
                    fi.packed = (static_cast<uint32_t>(parentChunkPos.x) & 0x1F)
                              | ((static_cast<uint32_t>(parentChunkPos.y) & 0x1F) << 5)
                              | ((static_cast<uint32_t>(parentChunkPos.z) & 0x1F) << 10)
                              | ((static_cast<uint32_t>(localPos.x) & 0x3) << 15)
                              | ((static_cast<uint32_t>(localPos.y) & 0x3) << 17)
                              | ((static_cast<uint32_t>(localPos.z) & 0x3) << 19)
                              | ((static_cast<uint32_t>(skyV) & 0xF) << 21);
                    uint16_t leafTex = Phyxel::Core::MaterialRegistry::instance().getTextureIndex(
                                           subcube->getMaterialName(), 0 /*side_n*/);
                    fi.tex = static_cast<uint32_t>(leafTex)
                           | ((static_cast<uint32_t>(br) & 0xF) << 16)
                           | ((static_cast<uint32_t>(bg) & 0xF) << 20)
                           | ((static_cast<uint32_t>(bb) & 0xF) << 24);
                    m_foliageInstances.push_back(fi);
                }
                continue;  // billboarded leaf: no solid faces emitted
            }
        }

        // Generate instance data for each visible face of the subcube
        for (int faceID = 0; faceID < 6; ++faceID) {
            if (faceVisible[faceID]) {
                InstanceData faceInstance;
                
                // Pack parent cube position, face ID, and subcube local position
                // Scale level 1 = subcube
                faceInstance.packedData = Phyxel::InstanceDataUtils::packSubcubeFaceData(
                    parentChunkPos.x, parentChunkPos.y, parentChunkPos.z,
                    faceID,
                    localPos.x, localPos.y, localPos.z
                );
                
                // Assign texture based on material and face ID. STATE can REPLACE the surface
                // (docs/VoxelAppearanceModel.md): a flaming/smoldering voxel renders the
                // "burning_wood" ember surface instead of its base material texture — material
                // (physics) is unchanged. So a flaming log looks like burning wood, not tinted wood.
                {
                    uint8_t st = subcube->getState();
                    const char* surfMat = (st == 1 || st == 2) ? "burning_wood" : subcube->getMaterialName().c_str();
                    faceInstance.textureIndex = Phyxel::Core::MaterialRegistry::instance().getTextureIndex(surfMat, faceID);
                }
                // Pack 0xRRGGBB tint in bits 0-23 + voxel state in bits 24-31 (decoded in static_voxel.vert)
                faceInstance.tint = (static_cast<uint32_t>(subcube->getState()) << 24) | (subcube->getTint() & 0xFFFFFFu);
                {
                    const auto* matDef = Phyxel::Core::MaterialRegistry::instance().getMaterial(subcube->getMaterialName());
                    bool isEmissive    = matDef && matDef->emissive;
                    bool isTransparent = matDef && matDef->alpha < 0.99f;
                    bool isMirror      = matDef && matDef->isMirror;
                    uint16_t quantAlpha = isTransparent ? static_cast<uint16_t>(matDef->alpha * 255.0f) : 255u;
                    faceInstance.reserved = static_cast<uint16_t>(
                        (isEmissive ? 1u : 0u) | (isTransparent ? 2u : 0u) | (quantAlpha << 2u) | (isMirror ? (1u << 10) : 0u));
                }
                // Baked light: sample the air cell the parent cube's face looks into (sky low
                // nibble, block light high nibble).
                static const int FDX[6] = {0, 0, 1, -1, 0, 0};
                static const int FDY[6] = {0, 0, 0, 0, 1, -1};
                static const int FDZ[6] = {1, -1, 0, 0, 0, 0};
                {
                    int nbx = parentChunkPos.x + FDX[faceID];
                    int nby = parentChunkPos.y + FDY[faceID];
                    int nbz = parentChunkPos.z + FDZ[faceID];
                    uint8_t skyV = skyLightAt(nbx, nby, nbz) & 0xF;
                    uint8_t br = 0, bg = 0, bb = 0;
                    blockLightAt(nbx, nby, nbz, br, bg, bb);
                    // New light layout (matches static_voxel.vert): replicate the single value to all
                    // 4 corners (subcubes/microcubes don't get smooth lighting). Sky → light nibbles,
                    // block RGB → light2/light3 (12 bits per corner: R|G<<4|B<<8).
                    {
                        uint32_t rgb12 = (static_cast<uint32_t>(br & 0xF))
                                       | (static_cast<uint32_t>(bg & 0xF) << 4)
                                       | (static_cast<uint32_t>(bb & 0xF) << 8);
                        faceInstance.light  = static_cast<uint32_t>(skyV & 0xF) * 0x1111u;
                        faceInstance.light2 = rgb12 | (rgb12 << 12);  // corners 0,1
                        faceInstance.light3 = rgb12 | (rgb12 << 12);  // corners 2,3
                    }
                }
                faces.push_back(faceInstance);
            }
        }
    }
}

void ChunkRenderManager::rebuildMicrocubeFaces(
    const std::vector<std::unique_ptr<Microcube>>& microcubes,
    const glm::ivec3& worldOrigin)
{
    // Greedy-merged path (Increment 2): collapse coplanar same-appearance microcube faces within a
    // parent cube into rectangles. Falls back to the per-face path below when the toggle is off.
    if (s_fineGreedyMerge) { rebuildMicrocubeFacesMerged(microcubes, worldOrigin); return; }

    // Process microcubes (from subdivided subcubes)
    for (const auto& microcube : microcubes) {
        // Skip broken or hidden microcubes
        if (!microcube || microcube->isBroken() || !microcube->isVisible()) {
            continue;
        }
        
        // Get microcube properties
        glm::ivec3 parentPos = microcube->getParentCubePosition();     // Parent cube's world position
        glm::ivec3 subcubePos = microcube->getSubcubeLocalPosition();  // 0-2 for each axis within parent cube
        glm::ivec3 microcubePos = microcube->getMicrocubeLocalPosition(); // 0-2 for each axis within parent subcube
        
        // Convert parent world position to chunk-relative position
        glm::ivec3 parentChunkPos = parentPos - worldOrigin;
        
        // Validate parent position is within chunk bounds
        if (parentChunkPos.x < 0 || parentChunkPos.x >= 32 ||
            parentChunkPos.y < 0 || parentChunkPos.y >= 32 ||
            parentChunkPos.z < 0 || parentChunkPos.z >= 32) {
            continue;
        }
        
        // Validate subcube position
        if (subcubePos.x < 0 || subcubePos.x >= 3 ||
            subcubePos.y < 0 || subcubePos.y >= 3 ||
            subcubePos.z < 0 || subcubePos.z >= 3) {
            continue;
        }
        
        // Validate microcube position
        if (microcubePos.x < 0 || microcubePos.x >= 3 ||
            microcubePos.y < 0 || microcubePos.y >= 3 ||
            microcubePos.z < 0 || microcubePos.z >= 3) {
            continue;
        }
        
        // Cull microcube faces whose neighbour micro-cell is fully solid (in-chunk occlusion; faces
        // at the chunk boundary are treated as exposed). The microcube grid is 288^3 cells across
        // the chunk (32 cubes * 3 subcubes * 3 microcubes).
        bool faceVisible[6];
        {
            const int gmx = parentChunkPos.x * 9 + subcubePos.x * 3 + microcubePos.x;
            const int gmy = parentChunkPos.y * 9 + subcubePos.y * 3 + microcubePos.y;
            const int gmz = parentChunkPos.z * 9 + subcubePos.z * 3 + microcubePos.z;
            static const int OFX[6] = {0, 0, 1, -1, 0, 0};  // faceID 0=+Z,1=-Z,2=+X,3=-X,4=+Y,5=-Y
            static const int OFY[6] = {0, 0, 0, 0, 1, -1};
            static const int OFZ[6] = {1, -1, 0, 0, 0, 0};
            for (int f = 0; f < 6; ++f) {
                int nx = gmx + OFX[f], ny = gmy + OFY[f], nz = gmz + OFZ[f];
                if (nx < 0 || nx >= 288 || ny < 0 || ny >= 288 || nz < 0 || nz >= 288) {
                    faceVisible[f] = true;  // out of chunk: assume exposed
                } else {
                    int nlx = nx / 9, nrx = nx % 9, nsx = nrx / 3, nmx = nrx % 3;
                    int nly = ny / 9, nry = ny % 9, nsy = nry / 3, nmy = nry % 3;
                    int nlz = nz / 9, nrz = nz % 9, nsz = nrz / 3, nmz = nrz % 3;
                    faceVisible[f] = !microCellSolid(nlx, nly, nlz, nsx, nsy, nsz, nmx, nmy, nmz);
                }
            }
        }

        // Billboarded leaf MICROCUBE (rare — trees prune micro leaves): skip solid faces; emit one
        // foliage instance at the parent subcube position if exposed. (Multiple micro leaves in one
        // subcube overlap into a denser sprig — acceptable for this rare case.)
        if (s_foliageEnabled) {
            const auto* md = Phyxel::Core::MaterialRegistry::instance().getMaterial(microcube->getMaterialName());
            if (md && md->billboarded) {
                bool exposed = faceVisible[0] || faceVisible[1] || faceVisible[2] ||
                               faceVisible[3] || faceVisible[4] || faceVisible[5];
                if (exposed) {
                    uint8_t skyV = skyLightAt(parentChunkPos.x, parentChunkPos.y, parentChunkPos.z) & 0xF;
                    uint8_t br = 0, bg = 0, bb = 0;
                    blockLightAt(parentChunkPos.x, parentChunkPos.y, parentChunkPos.z, br, bg, bb);
                    FoliageInstanceData fi;
                    fi.packed = (static_cast<uint32_t>(parentChunkPos.x) & 0x1F)
                              | ((static_cast<uint32_t>(parentChunkPos.y) & 0x1F) << 5)
                              | ((static_cast<uint32_t>(parentChunkPos.z) & 0x1F) << 10)
                              | ((static_cast<uint32_t>(subcubePos.x) & 0x3) << 15)
                              | ((static_cast<uint32_t>(subcubePos.y) & 0x3) << 17)
                              | ((static_cast<uint32_t>(subcubePos.z) & 0x3) << 19)
                              | ((static_cast<uint32_t>(skyV) & 0xF) << 21);
                    uint16_t leafTex = Phyxel::Core::MaterialRegistry::instance().getTextureIndex(
                                           microcube->getMaterialName(), 0);
                    fi.tex = static_cast<uint32_t>(leafTex)
                           | ((static_cast<uint32_t>(br) & 0xF) << 16)
                           | ((static_cast<uint32_t>(bg) & 0xF) << 20)
                           | ((static_cast<uint32_t>(bb) & 0xF) << 24);
                    m_foliageInstances.push_back(fi);
                }
                continue;  // billboarded leaf microcube: no solid faces emitted
            }
        }

        // Generate instance data for each visible face of the microcube
        for (int faceID = 0; faceID < 6; ++faceID) {
            if (faceVisible[faceID]) {
                InstanceData faceInstance;
                
                // Pack parent cube position, face ID, subcube position, and microcube position
                // Scale level 2 = microcube
                faceInstance.packedData = Phyxel::InstanceDataUtils::packMicrocubeFaceData(
                    parentChunkPos.x, parentChunkPos.y, parentChunkPos.z,
                    faceID,
                    subcubePos.x, subcubePos.y, subcubePos.z,
                    microcubePos.x, microcubePos.y, microcubePos.z
                );
                
                // Assign texture based on material and face ID. State=flaming/smoldering
                // swaps the surface to "burning_wood" (see subcube path above).
                {
                    uint8_t st = microcube->getState();
                    const char* surfMat = (st == 1 || st == 2) ? "burning_wood" : microcube->getMaterialName().c_str();
                    faceInstance.textureIndex = Phyxel::Core::MaterialRegistry::instance().getTextureIndex(surfMat, faceID);
                }
                // Pack 0xRRGGBB tint in bits 0-23 + voxel state in bits 24-31 (decoded in static_voxel.vert)
                faceInstance.tint = (static_cast<uint32_t>(microcube->getState()) << 24) | (microcube->getTint() & 0xFFFFFFu);
                {
                    const auto* matDef = Phyxel::Core::MaterialRegistry::instance().getMaterial(microcube->getMaterialName());
                    bool isEmissive    = matDef && matDef->emissive;
                    bool isTransparent = matDef && matDef->alpha < 0.99f;
                    bool isMirror      = matDef && matDef->isMirror;
                    uint16_t quantAlpha = isTransparent ? static_cast<uint16_t>(matDef->alpha * 255.0f) : 255u;
                    faceInstance.reserved = static_cast<uint16_t>(
                        (isEmissive ? 1u : 0u) | (isTransparent ? 2u : 0u) | (quantAlpha << 2u) | (isMirror ? (1u << 10) : 0u));
                }
                // Baked light: sample the air cell the parent cube's face looks into (sky low
                // nibble, block light high nibble).
                static const int FDX[6] = {0, 0, 1, -1, 0, 0};
                static const int FDY[6] = {0, 0, 0, 0, 1, -1};
                static const int FDZ[6] = {1, -1, 0, 0, 0, 0};
                {
                    int nbx = parentChunkPos.x + FDX[faceID];
                    int nby = parentChunkPos.y + FDY[faceID];
                    int nbz = parentChunkPos.z + FDZ[faceID];
                    uint8_t skyV = skyLightAt(nbx, nby, nbz) & 0xF;
                    uint8_t br = 0, bg = 0, bb = 0;
                    blockLightAt(nbx, nby, nbz, br, bg, bb);
                    // New light layout (matches static_voxel.vert): replicate the single value to all
                    // 4 corners (subcubes/microcubes don't get smooth lighting). Sky → light nibbles,
                    // block RGB → light2/light3 (12 bits per corner: R|G<<4|B<<8).
                    {
                        uint32_t rgb12 = (static_cast<uint32_t>(br & 0xF))
                                       | (static_cast<uint32_t>(bg & 0xF) << 4)
                                       | (static_cast<uint32_t>(bb & 0xF) << 8);
                        faceInstance.light  = static_cast<uint32_t>(skyV & 0xF) * 0x1111u;
                        faceInstance.light2 = rgb12 | (rgb12 << 12);  // corners 0,1
                        faceInstance.light3 = rgb12 | (rgb12 << 12);  // corners 2,3
                    }
                }
                faces.push_back(faceInstance);
            }
        }
    }
}

// Increment 2 — within-cube microcube greedy merge (docs/BinaryGreedyMeshingPlan.md).
// For each parent cube, per face direction, per depth slice, collapse coplanar microcube faces of
// identical appearance into maximal rectangles, emitting ONE InstanceData per rectangle with the
// merge extents stored in the light word. Light is constant per (cube, face) by construction
// (sampled at the parent cube's neighbour cell), so it never splits a merge. Occlusion is decided
// per-cell with the same microCellSolid oracle as the per-face path, so a rectangle only ever covers
// individually-visible cells — the result is visually identical, with far fewer instances. Cross-cube
// merging is out of scope (Increment 4): runs stop at the 9x9x9 parent-cube border.
void ChunkRenderManager::rebuildMicrocubeFacesMerged(
    const std::vector<std::unique_ptr<Microcube>>& microcubes,
    const glm::ivec3& worldOrigin)
{
    auto& reg = Phyxel::Core::MaterialRegistry::instance();

    // Two microcube faces merge iff their appearance keys are equal (light is equal by construction).
    struct Key {
        uint16_t tex = 0; uint16_t reserved = 0; uint32_t tint = 0; bool solid = false;
        bool operator==(const Key& o) const {
            return solid == o.solid && tex == o.tex && reserved == o.reserved && tint == o.tint;
        }
    };

    static const int FDX[6] = {0, 0, 1, -1, 0, 0};  // faceID 0=+Z,1=-Z,2=+X,3=-X,4=+Y,5=-Y
    static const int FDY[6] = {0, 0, 0, 0, 1, -1};
    static const int FDZ[6] = {1, -1, 0, 0, 0, 0};

    // Group visible (non-billboarded) microcubes by parent cube; billboarded leaves emit a foliage
    // instance exactly like the per-face path (not merged).
    struct Cell { const Microcube* mc; uint8_t lx, ly, lz; };  // lx/ly/lz = 0..8 within the cube
    std::unordered_map<uint32_t, std::vector<Cell>> byCube;
    for (const auto& mcp : microcubes) {
        const Microcube* mc = mcp.get();
        if (!mc || mc->isBroken() || !mc->isVisible()) continue;
        glm::ivec3 pcp = mc->getParentCubePosition() - worldOrigin;
        if (pcp.x < 0 || pcp.x >= 32 || pcp.y < 0 || pcp.y >= 32 || pcp.z < 0 || pcp.z >= 32) continue;
        glm::ivec3 sp = mc->getSubcubeLocalPosition();
        glm::ivec3 mp = mc->getMicrocubeLocalPosition();
        if (sp.x < 0 || sp.x >= 3 || sp.y < 0 || sp.y >= 3 || sp.z < 0 || sp.z >= 3) continue;
        if (mp.x < 0 || mp.x >= 3 || mp.y < 0 || mp.y >= 3 || mp.z < 0 || mp.z >= 3) continue;

        if (s_foliageEnabled) {
            const auto* md = reg.getMaterial(mc->getMaterialName());
            if (md && md->billboarded) {
                int gmx = pcp.x * 9 + sp.x * 3 + mp.x;
                int gmy = pcp.y * 9 + sp.y * 3 + mp.y;
                int gmz = pcp.z * 9 + sp.z * 3 + mp.z;
                bool exposed = false;
                for (int f = 0; f < 6 && !exposed; ++f) {
                    int nx = gmx + FDX[f], ny = gmy + FDY[f], nz = gmz + FDZ[f];
                    if (nx < 0 || nx >= 288 || ny < 0 || ny >= 288 || nz < 0 || nz >= 288) exposed = true;
                    else exposed = !microCellSolid(nx/9, ny/9, nz/9, (nx%9)/3, (ny%9)/3, (nz%9)/3,
                                                   (nx%9)%3, (ny%9)%3, (nz%9)%3);
                }
                if (exposed) {
                    uint8_t skyV = skyLightAt(pcp.x, pcp.y, pcp.z) & 0xF;
                    uint8_t br = 0, bg = 0, bb = 0; blockLightAt(pcp.x, pcp.y, pcp.z, br, bg, bb);
                    FoliageInstanceData fi;
                    fi.packed = (uint32_t(pcp.x) & 0x1F) | ((uint32_t(pcp.y) & 0x1F) << 5)
                              | ((uint32_t(pcp.z) & 0x1F) << 10) | ((uint32_t(sp.x) & 0x3) << 15)
                              | ((uint32_t(sp.y) & 0x3) << 17) | ((uint32_t(sp.z) & 0x3) << 19)
                              | ((uint32_t(skyV) & 0xF) << 21);
                    uint16_t leafTex = reg.getTextureIndex(mc->getMaterialName(), 0);
                    fi.tex = uint32_t(leafTex) | ((uint32_t(br) & 0xF) << 16)
                           | ((uint32_t(bg) & 0xF) << 20) | ((uint32_t(bb) & 0xF) << 24);
                    m_foliageInstances.push_back(fi);
                }
                continue;
            }
        }
        uint32_t cubeIdx = uint32_t(pcp.z + pcp.y * 32 + pcp.x * 1024);
        byCube[cubeIdx].push_back({ mc, uint8_t(sp.x*3 + mp.x), uint8_t(sp.y*3 + mp.y), uint8_t(sp.z*3 + mp.z) });
    }

    for (auto& kv : byCube) {
        uint32_t cubeIdx = kv.first;
        int pcx = int(cubeIdx / 1024); int rem = int(cubeIdx % 1024); int pcy = rem / 32; int pcz = rem % 32;

        const Microcube* grid[9][9][9];
        std::memset(grid, 0, sizeof(grid));
        for (const Cell& c : kv.second) grid[c.lx][c.ly][c.lz] = c.mc;

        auto keyOf = [&](const Microcube* mc, int faceID) -> Key {
            Key k; k.solid = true;
            uint8_t st = mc->getState();
            const char* surf = (st == 1 || st == 2) ? "burning_wood" : mc->getMaterialName().c_str();
            k.tex = reg.getTextureIndex(surf, faceID);
            const auto* md = reg.getMaterial(mc->getMaterialName());
            bool em = md && md->emissive, tr = md && md->alpha < 0.99f, mi = md && md->isMirror;
            uint16_t qa = tr ? uint16_t(md->alpha * 255.0f) : 255u;
            k.reserved = uint16_t((em?1u:0u) | (tr?2u:0u) | (qa << 2u) | (mi?(1u<<10):0u));
            k.tint = (uint32_t(st) << 24) | (mc->getTint() & 0xFFFFFFu);
            return k;
        };

        for (int faceID = 0; faceID < 6; ++faceID) {
            // Per-cube face light (constant across the cube's microcubes for this direction).
            int nbx = pcx + FDX[faceID], nby = pcy + FDY[faceID], nbz = pcz + FDZ[faceID];
            uint8_t skyV = skyLightAt(nbx, nby, nbz) & 0xF;
            uint8_t br = 0, bg = 0, bb = 0; blockLightAt(nbx, nby, nbz, br, bg, bb);
            uint32_t rgb12 = (uint32_t(br & 0xF)) | (uint32_t(bg & 0xF) << 4) | (uint32_t(bb & 0xF) << 8);
            uint32_t lightSky = uint32_t(skyV & 0xF) * 0x1111u;
            uint32_t light23  = rgb12 | (rgb12 << 12);

            // (u,v,depth) -> (lx,ly,lz): Z faces u=x v=y d=z; X faces u=z v=y d=x; Y faces u=x v=z d=y.
            for (int depth = 0; depth < 9; ++depth) {
                Key mask[9][9];
                for (int u = 0; u < 9; ++u) for (int v = 0; v < 9; ++v) {
                    int lx, ly, lz;
                    if (faceID == 0 || faceID == 1) { lx = u; ly = v; lz = depth; }
                    else if (faceID == 2 || faceID == 3) { lz = u; ly = v; lx = depth; }
                    else { lx = u; lz = v; ly = depth; }
                    const Microcube* mc = grid[lx][ly][lz];
                    if (!mc) continue;
                    int gmx = pcx*9 + lx, gmy = pcy*9 + ly, gmz = pcz*9 + lz;
                    int nx = gmx + FDX[faceID], ny = gmy + FDY[faceID], nz = gmz + FDZ[faceID];
                    bool vis;
                    if (nx < 0 || nx >= 288 || ny < 0 || ny >= 288 || nz < 0 || nz >= 288) vis = true;
                    else vis = !microCellSolid(nx/9, ny/9, nz/9, (nx%9)/3, (ny%9)/3, (nz%9)/3,
                                               (nx%9)%3, (ny%9)%3, (nz%9)%3);
                    if (!vis) continue;
                    mask[u][v] = keyOf(mc, faceID);
                }

                bool used[9][9]; std::memset(used, 0, sizeof(used));
                for (int v = 0; v < 9; ++v) for (int u = 0; u < 9; ++u) {
                    if (used[u][v] || !mask[u][v].solid) continue;
                    const Key key = mask[u][v];
                    int uExt = 1;
                    while (u + uExt < 9 && !used[u+uExt][v] && mask[u+uExt][v] == key) ++uExt;
                    int vExt = 1; bool ok = true;
                    while (v + vExt < 9 && ok) {
                        for (int uu = u; uu < u + uExt; ++uu)
                            if (used[uu][v+vExt] || !(mask[uu][v+vExt] == key)) { ok = false; break; }
                        if (ok) ++vExt;
                    }
                    for (int vv = v; vv < v + vExt; ++vv)
                        for (int uu = u; uu < u + uExt; ++uu) used[uu][vv] = true;

                    int lx, ly, lz;  // origin cell (min-u, min-v)
                    if (faceID == 0 || faceID == 1) { lx = u; ly = v; lz = depth; }
                    else if (faceID == 2 || faceID == 3) { lz = u; ly = v; lx = depth; }
                    else { lx = u; lz = v; ly = depth; }

                    InstanceData inst;
                    inst.packedData = Phyxel::InstanceDataUtils::packMicrocubeFaceData(
                        pcx, pcy, pcz, uint32_t(faceID),
                        lx/3, ly/3, lz/3, lx%3, ly%3, lz%3);
                    inst.textureIndex = key.tex;
                    inst.reserved = key.reserved;
                    inst.tint = key.tint;
                    inst.light  = Phyxel::InstanceDataUtils::packFineExtentsIntoLight(
                                      lightSky, uint32_t(uExt), uint32_t(vExt));
                    inst.light2 = light23;
                    inst.light3 = light23;
                    faces.push_back(inst);
                }
            }
        }
    }
}

// Increment 3 — within-cube subcube greedy merge. Identical structure to the microcube merger at
// the 3x3x3 subcube grid (3x3 masks, 3 depth slices). Subcube walls dominate a v2 structure's face
// count, so this is the biggest single reduction after Increment 2. Billboarded-leaf subcubes emit a
// foliage card (as in the per-face path) and are not merged. Cross-cube runs stop at the cube border.
void ChunkRenderManager::rebuildSubcubeFacesMerged(
    const std::vector<std::unique_ptr<Subcube>>& subcubes,
    const glm::ivec3& worldOrigin)
{
    // Increment 4a: CROSS-CUBE subcube merging. Merges coplanar same-appearance subcube faces across
    // parent-cube boundaries, chunk-wide, so a long same-material wall becomes one rectangle instead
    // of one-per-cube. The merge key now includes LIGHT (baked per cube-face), so runs split exactly
    // at light gradients — matching the cube greedy path. A single isolated cube reduces to the
    // Increment-3 within-cube result (no neighbours to merge with). The shader is unchanged: a run's
    // extent simply spans multiple cubes and the REPEAT-wrap sampler restarts the tile at each cube
    // (§4.2 of BinaryGreedyMeshingPlan.md). Cross-chunk merging is still out of scope (runs stop at
    // the 96³ chunk border). Microcube cross-cube (288³) is deferred (Increment 4b).
    auto& reg = Phyxel::Core::MaterialRegistry::instance();
    static const int FDX[6] = {0, 0, 1, -1, 0, 0};  // faceID 0=+Z,1=-Z,2=+X,3=-X,4=+Y,5=-Y
    static const int FDY[6] = {0, 0, 0, 0, 1, -1};
    static const int FDZ[6] = {1, -1, 0, 0, 0, 0};
    constexpr int G = 96;  // chunk-wide subcube grid (32 cubes × 3)

    // Merge key = appearance (texture+flags+tint) + baked light. Cross-cube neighbours merge only
    // when ALL of these match.
    struct Key {
        uint16_t tex = 0; uint16_t reserved = 0; uint32_t tint = 0;
        uint32_t lightSky = 0, light23 = 0; bool solid = false;
        bool operator==(const Key& o) const {
            return solid == o.solid && tex == o.tex && reserved == o.reserved && tint == o.tint
                && lightSky == o.lightSky && light23 == o.light23;
        }
    };

    // Gather visible non-billboarded subcubes with their 96-grid coords + parent cube; billboarded
    // leaves emit a foliage card (as in the per-face path) and are not merged.
    struct SC { const Subcube* sc; int gx, gy, gz, cx, cy, cz; };
    std::vector<SC> cells; cells.reserve(subcubes.size());
    for (const auto& scp : subcubes) {
        const Subcube* sc = scp.get();
        if (!sc || sc->isBroken() || !sc->isVisible()) continue;
        glm::ivec3 pcp = sc->getPosition() - worldOrigin;
        if (pcp.x < 0 || pcp.x >= 32 || pcp.y < 0 || pcp.y >= 32 || pcp.z < 0 || pcp.z >= 32) continue;
        glm::ivec3 lp = sc->getLocalPosition();
        if (lp.x < 0 || lp.x >= 3 || lp.y < 0 || lp.y >= 3 || lp.z < 0 || lp.z >= 3) continue;
        int gx = pcp.x*3 + lp.x, gy = pcp.y*3 + lp.y, gz = pcp.z*3 + lp.z;
        if (s_foliageEnabled) {
            const auto* md = reg.getMaterial(sc->getMaterialName());
            if (md && md->billboarded) {
                bool exposed = false;
                for (int f = 0; f < 6 && !exposed; ++f) {
                    int nx = gx + FDX[f], ny = gy + FDY[f], nz = gz + FDZ[f];
                    if (nx < 0 || nx >= G || ny < 0 || ny >= G || nz < 0 || nz >= G) exposed = true;
                    else exposed = !subCellSolid(nx/3, ny/3, nz/3, nx%3, ny%3, nz%3);
                }
                if (exposed) {
                    uint8_t skyV = skyLightAt(pcp.x, pcp.y, pcp.z) & 0xF;
                    uint8_t br = 0, bg = 0, bb = 0; blockLightAt(pcp.x, pcp.y, pcp.z, br, bg, bb);
                    FoliageInstanceData fi;
                    fi.packed = (uint32_t(pcp.x) & 0x1F) | ((uint32_t(pcp.y) & 0x1F) << 5)
                              | ((uint32_t(pcp.z) & 0x1F) << 10) | ((uint32_t(lp.x) & 0x3) << 15)
                              | ((uint32_t(lp.y) & 0x3) << 17) | ((uint32_t(lp.z) & 0x3) << 19)
                              | ((uint32_t(skyV) & 0xF) << 21);
                    uint16_t leafTex = reg.getTextureIndex(sc->getMaterialName(), 0);
                    fi.tex = uint32_t(leafTex) | ((uint32_t(br) & 0xF) << 16)
                           | ((uint32_t(bg) & 0xF) << 20) | ((uint32_t(bb) & 0xF) << 24);
                    m_foliageInstances.push_back(fi);
                }
                continue;
            }
        }
        cells.push_back({ sc, gx, gy, gz, pcp.x, pcp.y, pcp.z });
    }
    if (cells.empty()) return;

    auto keyOf = [&](const SC& c, int faceID) -> Key {
        Key k; k.solid = true;
        uint8_t st = c.sc->getState();
        const char* surf = (st == 1 || st == 2) ? "burning_wood" : c.sc->getMaterialName().c_str();
        k.tex = reg.getTextureIndex(surf, faceID);
        const auto* md = reg.getMaterial(c.sc->getMaterialName());
        bool em = md && md->emissive, tr = md && md->alpha < 0.99f, mi = md && md->isMirror;
        uint16_t qa = tr ? uint16_t(md->alpha * 255.0f) : 255u;
        k.reserved = uint16_t((em?1u:0u) | (tr?2u:0u) | (qa << 2u) | (mi?(1u<<10):0u));
        k.tint = (uint32_t(st) << 24) | (c.sc->getTint() & 0xFFFFFFu);
        int nbx = c.cx + FDX[faceID], nby = c.cy + FDY[faceID], nbz = c.cz + FDZ[faceID];
        uint8_t skyV = skyLightAt(nbx, nby, nbz) & 0xF;
        uint8_t br = 0, bg = 0, bb = 0; blockLightAt(nbx, nby, nbz, br, bg, bb);
        uint32_t rgb12 = (uint32_t(br & 0xF)) | (uint32_t(bg & 0xF) << 4) | (uint32_t(bb & 0xF) << 8);
        k.lightSky = uint32_t(skyV & 0xF) * 0x1111u;
        k.light23  = rgb12 | (rgb12 << 12);
        return k;
    };

    // Reused chunk-wide plane mask + used marks. Only touched cells are ever written/cleared, so the
    // per-slice cost is O(occupied), not O(G²).
    std::vector<Key> mask(static_cast<size_t>(G) * G);
    std::vector<uint8_t> used(static_cast<size_t>(G) * G, 0);

    for (int faceID = 0; faceID < 6; ++faceID) {
        // (u,v,depth) per face: Z faces u=x v=y d=z; X faces u=z v=y d=x; Y faces u=x v=z d=y.
        std::unordered_map<int, std::vector<int>> byDepth;  // depth coord -> indices into `cells`
        for (int ci = 0; ci < static_cast<int>(cells.size()); ++ci) {
            const SC& c = cells[ci];
            int nx = c.gx + FDX[faceID], ny = c.gy + FDY[faceID], nz = c.gz + FDZ[faceID];
            bool vis;
            if (nx < 0 || nx >= G || ny < 0 || ny >= G || nz < 0 || nz >= G) vis = true;
            else vis = !subCellSolid(nx/3, ny/3, nz/3, nx%3, ny%3, nz%3);
            if (!vis) continue;
            int depth = (faceID == 0 || faceID == 1) ? c.gz : (faceID == 2 || faceID == 3) ? c.gx : c.gy;
            byDepth[depth].push_back(ci);
        }
        auto uvOf = [&](const SC& c, int& u, int& v) {
            if (faceID == 0 || faceID == 1) { u = c.gx; v = c.gy; }
            else if (faceID == 2 || faceID == 3) { u = c.gz; v = c.gy; }
            else { u = c.gx; v = c.gz; }
        };
        for (auto& db : byDepth) {
            const int depth = db.first;
            const std::vector<int>& idxs = db.second;
            int minU = G, minV = G, maxU = -1, maxV = -1;
            for (int ci : idxs) {
                int u, v; uvOf(cells[ci], u, v);
                mask[static_cast<size_t>(v)*G + u] = keyOf(cells[ci], faceID);
                minU = std::min(minU, u); maxU = std::max(maxU, u);
                minV = std::min(minV, v); maxV = std::max(maxV, v);
            }
            for (int v = minV; v <= maxV; ++v) for (int u = minU; u <= maxU; ++u) {
                size_t idx = static_cast<size_t>(v)*G + u;
                if (used[idx] || !mask[idx].solid) continue;
                const Key key = mask[idx];
                int uExt = 1;
                while (u + uExt <= maxU && !used[idx + uExt] && mask[idx + uExt] == key) ++uExt;
                int vExt = 1; bool ok = true;
                while (v + vExt <= maxV && ok) {
                    size_t row = static_cast<size_t>(v + vExt)*G + u;
                    for (int uu = 0; uu < uExt; ++uu)
                        if (used[row + uu] || !(mask[row + uu] == key)) { ok = false; break; }
                    if (ok) ++vExt;
                }
                for (int vv = 0; vv < vExt; ++vv) {
                    size_t row = static_cast<size_t>(v + vv)*G + u;
                    for (int uu = 0; uu < uExt; ++uu) used[row + uu] = 1;
                }
                int gsx, gsy, gsz;  // origin global cell (min u,v; slice depth)
                if (faceID == 0 || faceID == 1) { gsx = u; gsy = v; gsz = depth; }
                else if (faceID == 2 || faceID == 3) { gsz = u; gsy = v; gsx = depth; }
                else { gsx = u; gsz = v; gsy = depth; }
                InstanceData inst;
                inst.packedData = Phyxel::InstanceDataUtils::packSubcubeFaceData(
                    gsx/3, gsy/3, gsz/3, uint32_t(faceID), gsx%3, gsy%3, gsz%3);
                inst.textureIndex = key.tex;
                inst.reserved = key.reserved;
                inst.tint = key.tint;
                inst.light  = Phyxel::InstanceDataUtils::packFineExtentsIntoLight(
                                  key.lightSky, uint32_t(uExt), uint32_t(vExt));
                inst.light2 = key.light23;
                inst.light3 = key.light23;
                faces.push_back(inst);
            }
            // Reset only the cells we touched, so the mask/used stay clean for the next slice.
            for (int ci : idxs) {
                int u, v; uvOf(cells[ci], u, v);
                size_t idx = static_cast<size_t>(v)*G + u;
                mask[idx].solid = false; used[idx] = 0;
            }
        }
    }
}

void ChunkRenderManager::updateVulkanBuffer() {
    ScopedChunkPerf _perf(ChunkPerfPhase::BufferUpload);  // B0: ensureCapacity(+realloc) + memcpy, all 3 buffers
    // Face buffer (cube/sub/micro). A chunk may legitimately have no solid faces but still have
    // foliage/grass (e.g. a leaf-only bush, whose billboarded leaves emit no faces), so the grass
    // and foliage uploads below are NOT gated on faces being non-empty.
    if (!faces.empty() && renderBuffer.getMappedMemory()) {
        // ensureBufferCapacity may call reallocateBuffer() which remaps memory —
        // fetch the pointer AFTER this call so we never write to a freed mapping.
        ensureBufferCapacity(faces.size());
        void* mappedMem = renderBuffer.getMappedMemory();
        if (mappedMem) {
            renderBuffer.updateMaxUsage(faces.size());
            memcpy(mappedMem, faces.data(), sizeof(InstanceData) * faces.size());
        }
    }

    // Grass parallel buffer: grow if a terrain edit added grass beyond capacity, then upload.
    // (Grass only exists on visible top faces, so faces is non-empty whenever grass is.)
    if (!m_grassInstances.empty() && grassBuffer.getMappedMemory()) {
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
    if (!m_foliageInstances.empty() && foliageBuffer.getMappedMemory()) {
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
    // Find the cube
    const Cube* cube = getCubeAtPosition(localPos, cubes);
    if (!cube) return;
    
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

void ChunkRenderManager::updateSingleSubcubeTexture(
    const glm::ivec3& parentLocalPos,
    const glm::ivec3& subcubePos,
    uint16_t textureIndex,
    const std::vector<std::unique_ptr<Subcube>>& subcubes,
    const glm::ivec3& worldOrigin)
{
    // Validate positions
    if (subcubePos.x < 0 || subcubePos.x >= 3 || 
        subcubePos.y < 0 || subcubePos.y >= 3 || 
        subcubePos.z < 0 || subcubePos.z >= 3) return;
    
    // Efficiently update only the affected faces in the buffer
    if (!renderBuffer.getMappedMemory()) return;
    
    bool updatedAnyFaces = false;
    
    // Find all face instances for this subcube and update their texture indices
    for (size_t i = 0; i < faces.size(); ++i) {
        InstanceData& face = faces[i];
        
        // Extract data from packed format for subcubes
        int parentX = face.packedData & 0x1F;
        int parentY = (face.packedData >> 5) & 0x1F;
        int parentZ = (face.packedData >> 10) & 0x1F;
        uint32_t subcubeData = (face.packedData >> 18);
        bool isSubcubeFace = (subcubeData & 0x1) != 0;
        
        // Check if this is a subcube face belonging to our specific subcube
        if (isSubcubeFace && 
            parentX == parentLocalPos.x && parentY == parentLocalPos.y && parentZ == parentLocalPos.z) {
            
            // Extract subcube local position from packed data
            int localX = (subcubeData >> 1) & 0x3;
            int localY = (subcubeData >> 3) & 0x3;
            int localZ = (subcubeData >> 5) & 0x3;
            
            // Check if this face belongs to our specific subcube
            if (localX == subcubePos.x && localY == subcubePos.y && localZ == subcubePos.z) {
                // Update the texture index in the faces vector
                faces[i].textureIndex = textureIndex;
                
                // Update the GPU buffer directly (partial update)
                VkDeviceSize offset = i * sizeof(InstanceData) + offsetof(InstanceData, textureIndex);
                memcpy(static_cast<char*>(renderBuffer.getMappedMemory()) + offset, &textureIndex, sizeof(uint16_t));
                
                updatedAnyFaces = true;
            }
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

void ChunkRenderManager::updateSingleSubcubeColor(
    const glm::ivec3& localPos,
    const glm::ivec3& subcubePos,
    const glm::vec3& newColor,
    const std::vector<std::unique_ptr<Subcube>>& subcubes,
    const glm::ivec3& worldOrigin)
{
    // Color updates would require rebuilding faces since colors are baked into vertex data
    // For now, this is a placeholder - actual implementation depends on rendering architecture
    // TODO: Implement color updates if needed
}

void ChunkRenderManager::createVulkanBuffer() {
    ScopedChunkPerf _perf(ChunkPerfPhase::BufferCreate);  // B0: first-time per-chunk alloc (all 3 buffers)
    if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("ChunkRenderManager::createVulkanBuffer() called before initialize()!");
    }
    renderBuffer.createBuffer(faces);
    // Parallel grass buffer, sized to the grass instances (small: ≤1024/chunk). Capacity floor of 1
    // keeps a valid mapped buffer even for grassless chunks (the renderer skips them by count).
    grassBuffer.createBufferRaw(m_grassInstances.data(), m_grassInstances.size(),
                                sizeof(GrassInstanceData),
                                std::max<size_t>(m_grassInstances.size(), 1));
    foliageBuffer.createBufferRaw(m_foliageInstances.data(), m_foliageInstances.size(),
                                  sizeof(FoliageInstanceData),
                                  std::max<size_t>(m_foliageInstances.size(), 1));
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
