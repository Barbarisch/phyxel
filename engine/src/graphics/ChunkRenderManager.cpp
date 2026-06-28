#include "graphics/ChunkRenderManager.h"
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

namespace Phyxel {
namespace Graphics {

// Smooth-lighting globals (see header). Default: smooth ON, tolerance 0 (pure smooth — no snapping,
// so gentle gradients never band into flat blocky steps). Raise tolerance (or toggle smooth off) only
// as an opt-in perf lever; the default prioritizes look.
bool ChunkRenderManager::s_smoothLighting = true;
int  ChunkRenderManager::s_mergeTolerance = 0;

ChunkRenderManager::ChunkRenderManager()
    : numInstances(0)
    , needsUpdate(false)
    , renderBuffer(VK_NULL_HANDLE, VK_NULL_HANDLE)
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
    , renderBuffer(std::move(other.renderBuffer))
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
        renderBuffer = std::move(other.renderBuffer);
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
    renderBuffer = ChunkRenderBuffer(device, physicalDevice);
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
    faces.clear();

    // Make the cross-chunk light lookup available to skyLightAt/blockLightAt + the BFS seeding.
    m_neighborLight = getNeighborLight;
    m_lightWorldOrigin = worldOrigin;

    // Rebuild faces for each voxel type. Cubes first: rebuildCubeFaces fills m_solidVis (cube-level
    // occupancy) which the sub/micro occlusion reuses. Then build the leaf sub/micro occupancy so
    // rebuildSubcube/MicrocubeFaces can cull hidden faces.
    rebuildCubeFaces(cubes, worldOrigin, getNeighborCube, columnOpenMask);
    buildSubMicroOccupancy(subcubes, microcubes, worldOrigin);
    rebuildSubcubeFaces(subcubes, worldOrigin);
    rebuildMicrocubeFaces(microcubes, worldOrigin);

    numInstances = static_cast<uint32_t>(faces.size());
    needsUpdate = true;
    m_neighborLight = nullptr;  // don't hold the closure past the rebuild
}

void ChunkRenderManager::rebuildCubeFaces(
    const std::vector<std::unique_ptr<Cube>>& cubes,
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

    // Per-material face textures + flags (computed once per distinct material in chunk).
    // emR/G/B = emissive light colour (0-15 per channel, hue from physics.colorTint, brightest
    // channel scaled to 15) used to seed coloured block light; 0 for non-emissive materials.
    struct MatFace { uint16_t tex[6]; uint16_t reserved; uint8_t emR, emG, emB; };
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
            const auto* md = reg.getMaterial(mname);
            bool em = md && md->emissive;
            bool tr = md && md->alpha < 0.99f;
            bool mi = md && md->isMirror;
            uint16_t qa = tr ? static_cast<uint16_t>(md->alpha * 255.0f) : 255u;
            mf.reserved = static_cast<uint16_t>((em ? 1u : 0u) | (tr ? 2u : 0u) |
                                                (qa << 2u) | (mi ? (1u << 10) : 0u));
            // Emissive light colour from the material tint, normalised so the brightest channel
            // emits at full range (15) and the hue is preserved.
            mf.emR = mf.emG = mf.emB = 0;
            if (em && md) {
                glm::vec3 t = md->physics.colorTint;
                float mx = std::max(t.x, std::max(t.y, std::max(t.z, 0.0001f)));
                float s = 15.0f / mx;
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
            if (m >= 0 && (matFaces[m].reserved & 1u)) {  // emissive flag (reserved bit 0)
                bump(cell, matFaces[m].emR, matFaces[m].emG, matFaces[m].emB);
            }
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
                
                // Assign texture based on material and face ID
                faceInstance.textureIndex = Phyxel::Core::MaterialRegistry::instance().getTextureIndex(subcube->getMaterialName(), faceID);
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
                
                // Assign texture based on material and face ID
                faceInstance.textureIndex = Phyxel::Core::MaterialRegistry::instance().getTextureIndex(microcube->getMaterialName(), faceID);
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

void ChunkRenderManager::updateVulkanBuffer() {
    if (faces.empty()) return;
    if (!renderBuffer.getMappedMemory()) return;

    // ensureBufferCapacity may call reallocateBuffer() which remaps memory —
    // fetch the pointer AFTER this call so we never write to a freed mapping.
    ensureBufferCapacity(faces.size());

    void* mappedMem = renderBuffer.getMappedMemory();
    if (!mappedMem) return;

    // Track peak usage for analysis
    renderBuffer.updateMaxUsage(faces.size());

    // Copy data to GPU buffer (only the used portion)
    VkDeviceSize copySize = sizeof(InstanceData) * faces.size();
    memcpy(mappedMem, faces.data(), copySize);
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
    if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("ChunkRenderManager::createVulkanBuffer() called before initialize()!");
    }
    renderBuffer.createBuffer(faces);
}

void ChunkRenderManager::cleanupVulkanResources() {
    renderBuffer.cleanup();
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
