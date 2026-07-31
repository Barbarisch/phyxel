#include "core/WaterManager.h"
#include "core/ChunkManager.h"
#include "core/AssetManager.h"
#include "vulkan/VulkanDevice.h"
#include "utils/Logger.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>

namespace Phyxel {
namespace Core {

namespace {
// Push constants for water_flow.comp (32 bytes).
struct FlowPC {
    int32_t  sx, sy, sz;
    uint32_t evapEnabled;
    float    evapThreshold, evapRate, pad0, pad1;
};

uint32_t findMemoryType(VkPhysicalDevice phys, uint32_t typeFilter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((typeFilter & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
    return 0;
}

// Create a host-visible, host-coherent storage buffer and persistently map it.
bool makeHostBuffer(VkDevice dev, VkPhysicalDevice phys, VkDeviceSize size,
                    VkBuffer& buf, VkDeviceMemory& mem, void*& mapped) {
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(dev, &bi, nullptr, &buf) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev, buf, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemoryType(phys, req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(dev, &ai, nullptr, &mem) != VK_SUCCESS) return false;
    vkBindBufferMemory(dev, buf, mem, 0);
    return vkMapMemory(dev, mem, 0, size, 0, &mapped) == VK_SUCCESS;
}
} // namespace

WaterManager::WaterManager(ChunkManager* chunkManager, const glm::ivec3& origin, const glm::ivec3& dims)
    : m_cm(chunkManager), m_origin(origin), m_dims(dims),
      m_sim(dims.x, dims.y, dims.z) {
    // Evaporation off by default: water keeps flowing and volume is conserved (draining
    // one crater into another preserves total mass). Opt in via setEvaporation(true)
    // for the bounded-spread / drying-films behaviour where a level wants it.
    m_sim.setEvaporation(false);
    syncSolidsFromChunks();
    rebuildSurface();
}

void WaterManager::syncSolidsFromChunks() {
    if (!m_cm) return;
    for (int z = 0; z < m_dims.z; ++z)
    for (int y = 0; y < m_dims.y; ++y)
    for (int x = 0; x < m_dims.x; ++x) {
        glm::ivec3 world(m_origin.x + x, m_origin.y + y, m_origin.z + z);
        // SUB-VOXEL FLOOR (Phase 4B): a voxel holding nothing but flat sub-voxel layers stacked from
        // its base — a low platform — is PASSABLE, and water rests on top of those layers instead of
        // on the voxel's base. Everything else keeps the old all-or-nothing behaviour: a negative
        // reading means "treat as fully solid", which is what walls, table legs, thin vertical
        // sheets and mixed subcube/microcube content all return. That conservatism is deliberate —
        // see ChunkVoxelManager::subVoxelFloor.
        const float floor = m_cm->subVoxelFloor(world);
        const bool solid = (floor < 0.0f) || (floor >= 0.999f);
        m_sim.setSolid(x, y, z, solid);
        m_sim.setFloor(x, y, z, solid ? 0.0f : floor);
    }
}

void WaterManager::update(float dt) {
    if (m_oceanDirty) rebuildOcean(); // re-flood once before stepping
    m_ripple.tick(dt);   // visual disturbance field (Phase 3); O(1) while asleep
    m_accum += std::min(dt, 0.25f);
    int steps = 0;
    const unsigned long long sweepsBefore = m_sim.sweepsRun();
    bool gpuStepped = false;
    while (m_accum >= STEP_DT && steps < MAX_STEPS_PER_UPDATE) {
        if (m_useGpu) {
            stepGpu();
            gpuStepped = true;
            // The GPU stepper writes the mass field directly (bypassing WaterSimulation's tracked
            // mutators), so it must keep the field awake — otherwise switching back to the CPU stepper
            // would trust a stale "settled" flag and freeze the field mid-flow. (The settle-skip is a
            // CPU-path optimization; the resident-GPU dirty-page path is Phase D.)
            m_sim.wake();
        } else {
            m_sim.step();
        }
        m_accum -= STEP_DT;
        ++steps;
    }
    if (steps == MAX_STEPS_PER_UPDATE) m_accum = 0.0f; // drop backlog after a stall
    drainOutflowToBank();   // P4: bank whatever the edge bled this tick (no-op when clean)
    // Rebuild the renderable surface only if a step actually ran (settled skips don't advance
    // sweepsRun) — otherwise a fully-at-rest field pays this O(box) scan at 20 Hz for nothing.
    // The GPU stepper doesn't advance sweepsRun, so it forces the rebuild explicitly.
    if (steps > 0 && (gpuStepped || m_sim.sweepsRun() != sweepsBefore)) rebuildSurface();
}

void WaterManager::recenter(const glm::ivec3& newOrigin) {
    const glm::ivec3 delta = newOrigin - m_origin;
    if (delta == glm::ivec3(0)) return;
    drainOutflowToBank();   // P4: the accumulator is window-local — bank it before the window moves
    // P3 (poured-water persistence): before the shift silently drops water off the frontier,
    // capture every departing column's unpinned surface level. A column departs whole (XZ exit)
    // or as a vertical slice (the window also follows the camera in Y — the way live pours were
    // first observed draining). Pinned columns re-derive from the bake and are skipped inside
    // captureColumnOverride.
    for (int lz = 0; lz < m_dims.z; ++lz)
        for (int lx = 0; lx < m_dims.x; ++lx) {
            const int wx = m_origin.x + lx, wz = m_origin.z + lz;
            const bool exitsXZ = wx < newOrigin.x || wx >= newOrigin.x + m_dims.x ||
                                 wz < newOrigin.z || wz >= newOrigin.z + m_dims.z;
            int yLo = 0, yHi = m_dims.y - 1;
            if (!exitsXZ) {
                if (delta.y > 0)      yHi = std::min(yHi, delta.y - 1);          // slice below new floor
                else if (delta.y < 0) yLo = std::max(yLo, m_dims.y + delta.y);   // slice above new top
                else continue;                                                    // column fully survives
            }
            captureColumnOverride(lx, lz, yLo, yHi);
        }
    // Translate the field so world content stays put, then move the window origin.
    m_sim.shift(delta);
    m_origin = newOrigin;
    // Terrain solidity is window-local → re-read it for the moved window (covers the frontier).
    syncSolidsFromChunks();
    // Re-tag channels for the new window from the world-space authoring list (shift preserved the
    // in-window tags; this restores any the frontier now covers). Idempotent; leaves m_channelCells.
    for (const glm::ivec3& c : m_channelCells) {
        const int lx = c.x - m_origin.x, ly = c.y - m_origin.y, lz = c.z - m_origin.z;
        if (m_sim.inBounds(lx, ly, lz)) m_sim.setChannel(lx, ly, lz, true);
    }
    // Re-derive the source pins for the new window: rebuildOcean re-floods the ocean from its
    // world-space seeds (or, with none, clears sources) and re-pins springs, then rebuilds the
    // surface. Correct whether or not an ocean/springs are authored.
    rebuildOcean();
}

bool WaterManager::followTo(const glm::vec3& focusWorld, int hysteresisCells) {
    // The ripple window follows the same focus (XZ only — it is a surface field). Tight
    // hysteresis: its window (~64 units) is the near field, and a whole-cell shift is cheap.
    m_ripple.followTo(glm::vec2(focusWorld.x, focusWorld.z), 4.0f);
    const int cx = m_origin.x + m_dims.x / 2;   // current box centre (world)
    const int cy = m_origin.y + m_dims.y / 2;
    const int cz = m_origin.z + m_dims.z / 2;
    const int fx = static_cast<int>(std::floor(focusWorld.x));
    const int fy = static_cast<int>(std::floor(focusWorld.y));
    const int fz = static_cast<int>(std::floor(focusWorld.z));
    // Vertical dead zone is sized to the (shorter) box height. Vertical following exists so
    // inland water WORKS at altitude: river beds/lakes sit wherever the terrain is (a 2.6 km
    // inland order-3 valley floor measured y≈72), while a Y-anchored box only ever covered the
    // sea band — every river above origin.y+dims.y stayed dry. The baked table made vertical
    // travel safe: pins re-derive per column at any altitude (sea fills only where columns are
    // wet; a box above sea level simply holds no ocean).
    const int vHyst = std::max(4, m_dims.y / 4);
    const bool moveXZ = std::abs(fx - cx) > hysteresisCells || std::abs(fz - cz) > hysteresisCells;
    const bool moveY  = std::abs(fy - cy) > vHyst;
    if (!moveXZ && !moveY) return false;         // inside both dead zones → don't thrash
    int newY = moveY ? std::max(0, fy - m_dims.y / 2) : m_origin.y;
    // WATER-AWARE vertical clamp: naive camera-following lifted the band above the sea whenever the
    // viewer stood on a coastal clifftop (measured: camera y=45 at a shore → band 29..61, sea level
    // 16 below it, simulated ocean mass 0). If the baked table has water anywhere under the box's
    // new footprint, keep the band centered no higher than that water's surface — the sim exists
    // for the water; a camera high above it needs no box at its own altitude. Dry footprints
    // (mountains with no lake) still follow the camera, which is what rivers at altitude need.
    if (m_tableFn) {
        const int nox = fx - m_dims.x / 2, noz = fz - m_dims.z / 2;
        float maxLevel = TABLE_DRY;
        for (int lz = 0; lz < m_dims.z; lz += 8)
            for (int lx = 0; lx < m_dims.x; lx += 8)
                maxLevel = std::max(maxLevel, m_tableFn(static_cast<float>(nox + lx) + 0.5f,
                                                        static_cast<float>(noz + lz) + 0.5f));
        if (maxLevel > TABLE_DRY)
            newY = std::min(newY,
                            std::max(0, static_cast<int>(std::floor(maxLevel)) - m_dims.y / 2));
    }
    const glm::ivec3 newOrigin(fx - m_dims.x / 2, newY, fz - m_dims.z / 2);
    if (newOrigin == m_origin) return false;     // clamp landed on the current origin — no move
    recenter(newOrigin);
    return true;
}

void WaterManager::rebuildSurface() {
    ++m_surfaceRebuilds;
    m_surface.clear();
    m_waterfalls.clear();
    const int sx = m_dims.x, sy = m_dims.y, sz = m_dims.z;
    auto colIdx = [sx](int x, int z) { return x + sx * z; };

    // Topmost surface height (world Y) per column, for corner smoothing. -inf = dry.
    std::vector<float> colTop(static_cast<size_t>(sx) * sz, -1e9f);

    // Pass 1: locate surface cells, record column depth, track the topmost per column.
    // SEA SUPPRESSION (interim far/near handoff, 2026-07-11): a surface cell that IS the
    // undisturbed sea — source-pinned at the sea band (y == floor(seaLevel)) — is NOT emitted:
    // the flat sea plane already draws that water, inside and outside the region, so emitting it
    // per-cell double-drew the ocean as a darker, hard-edged 64×64 "slab" that followed the
    // camera (the region frontier's skirts read as a wall in open sea). Suppressed cells still
    // feed colTop, so neighboring rendered water (splashes, lakes) closes its skirts against the
    // sea surface instead of dropping to the seabed. Lakes (level ≠ seaLevel) and any unpinned
    // water (splashes, spills) render per-cell as before. The REAL far/near LOD handoff is
    // Phase B (docs/WaterSystemV2.md).
    const int seaLocalY = static_cast<int>(std::floor(m_seaLevel)) - m_origin.y;
    const std::vector<float>& srcMask = m_sim.sourceMask();
    struct Cell { int x, y, z; float surfaceY, depth; };
    std::vector<Cell> cells;
    for (int z = 0; z < sz; ++z)
    for (int x = 0; x < sx; ++x)
    for (int y = 0; y < sy; ++y) {
        float m = m_sim.massAt(x, y, z);
        if (m <= RENDER_MIN) continue;
        // Surface cell: the one above is empty (or solid / out of bounds).
        if (m_sim.massAt(x, y + 1, z) > RENDER_MIN && !m_sim.isSolid(x, y + 1, z)) continue;
        float fill = std::min(m, 1.0f);
        // SUB-VOXEL FLOOR (Phase 4B): water in a cell with a low platform under it rests ON that
        // platform, so its surface sits floor + fill*(1-floor) up the cell rather than fill. Without
        // this a puddle on a 1/3-height subcube step renders as though the step were a full voxel.
        const float cellFloor = m_sim.floorAt(x, y, z);
        float surfaceY = static_cast<float>(m_origin.y + y) + cellFloor + fill * (1.0f - cellFloor);
        // Column depth: contiguous water cells stacked below this surface cell. A floored cell
        // contributes only the part above its floor, so depth-based shading matches what is drawn.
        float depth = fill * (1.0f - cellFloor);
        for (int dy = y - 1; dy >= 0; --dy) {
            float md = m_sim.massAt(x, dy, z);
            if (md <= RENDER_MIN || m_sim.isSolid(x, dy, z)) break;
            depth += std::min(md, 1.0f) * (1.0f - m_sim.floorAt(x, dy, z));
        }
        float& top = colTop[colIdx(x, z)];
        if (surfaceY > top) top = surfaceY;
        // FAR-LAYER SUPPRESSION (water-layer P1, generalizing the 2026-07-11 sea rule above):
        // any source-pinned surface cell sitting at its column's baked level — the sea at the
        // sea band, a lake at its spill (snapped grid) — is drawn by the water-layer clipmap,
        // one look inside and outside the region, so emitting it per-cell double-draws a
        // camera-following slab. Unpinned water (pours, splashes, spills), river/creek bed pins
        // (below the level or on table-dry columns), and waterfall cells still render per-cell.
        const bool pinned =
            srcMask[static_cast<size_t>(x) +
                    static_cast<size_t>(sx) * (static_cast<size_t>(y) +
                    static_cast<size_t>(sy) * static_cast<size_t>(z))] >= 0.0f;
        const bool atLayerLevel = m_tableLvlLocal.empty()
            ? (y == seaLocalY)
            : (y == m_tableLvlLocal[colIdx(x, z)]);
        if (pinned && atLayerLevel) continue;   // the water-layer clipmap draws this water
        // Finite bodies (Phase C): an UNPINNED at-rest pond cell sitting exactly at its body's
        // hydration level is likewise the clipmap's to draw (same coarseness as the pinned
        // rule); transients above/below the level — a splash, mid-scoop churn — still
        // cell-render, which is exactly the near-field job.
        if (!pinned && !m_bodyLvlLocal.empty() &&
            m_bodyIdLocal[colIdx(x, z)] >= 0 && y == m_bodyLvlLocal[colIdx(x, z)])
            continue;
        cells.push_back({x, y, z, surfaceY, depth});
    }

    // SHARED, SMOOTHED FLOW GRID (WaterSystemV3 Phase 3), one entry per surface column.
    //
    // Why this exists: flow is a PER-INSTANCE attribute, so it is constant across each cell's quad.
    // The shader advects the wave field along it, which means two neighbouring cells with different
    // flow warp the same world position differently — the wave field TEARS at every cell boundary
    // and the surface reads as a checkerboard of tiles. (Observed live the first time this shipped;
    // it is exactly the blocky look this whole plan is trying to remove.) Averaging each column with
    // its N4 neighbours makes the field vary slowly enough that the residual step per boundary is
    // imperceptible — the same trick the corner-height grid below uses for the surface itself.
    std::vector<glm::vec2> colFlow(static_cast<size_t>(sx) * sz, glm::vec2(0.0f));
    for (const Cell& c : cells) colFlow[colIdx(c.x, c.z)] = m_sim.flowAt(c.x, c.y, c.z);
    std::vector<glm::vec2> colFlowSmooth = colFlow;
    for (int z = 0; z < sz; ++z)
    for (int x = 0; x < sx; ++x) {
        glm::vec2 sum = colFlow[colIdx(x, z)];
        float n = 1.0f;
        if (x > 0)      { sum += colFlow[colIdx(x - 1, z)]; n += 1.0f; }
        if (x + 1 < sx) { sum += colFlow[colIdx(x + 1, z)]; n += 1.0f; }
        if (z > 0)      { sum += colFlow[colIdx(x, z - 1)]; n += 1.0f; }
        if (z + 1 < sz) { sum += colFlow[colIdx(x, z + 1)]; n += 1.0f; }
        colFlowSmooth[colIdx(x, z)] = sum / n;
    }

    // Shared corner-height grid, (sx+1) x (sz+1). Each grid corner is touched by up to
    // four columns; its height is the average of the *wet* ones that lie within 1.25 of
    // the lowest wet column there (so a cliff lip — where only the inner column is wet —
    // stays flush, and a far stacked surface doesn't drag it). Computed once and shared,
    // so every quad referencing a corner uses the IDENTICAL height => a crack-free C0
    // surface with no drooping past edges.
    const int cw = sx + 1;
    std::vector<float> cornerH(static_cast<size_t>(cw) * (sz + 1), -1e9f);
    auto wetTop = [&](int x, int z) -> float {
        if (x < 0 || x >= sx || z < 0 || z >= sz) return -1e9f;
        return colTop[colIdx(x, z)];
    };
    for (int cz = 0; cz <= sz; ++cz)
    for (int cx = 0; cx <= sx; ++cx) {
        const float v[4] = { wetTop(cx - 1, cz - 1), wetTop(cx, cz - 1),
                             wetTop(cx - 1, cz),     wetTop(cx, cz) };
        float lo = 1e9f;
        for (float f : v) if (f > -1e8f) lo = std::min(lo, f);
        if (lo > 1e8f) continue; // no water at this corner
        float sum = 0.0f; int n = 0;
        for (float f : v) if (f > -1e8f && f <= lo + 1.25f) { sum += f; ++n; }
        cornerH[cx + cw * cz] = sum / static_cast<float>(n);
    }

    // Pass 2: emit each surface cell, reading its 4 corners from the shared grid. Reject
    // a corner that is >1.25 off this cell's own surface — that only happens at a genuine
    // step (a different body/level), where a hard step is correct, not a droop.
    m_surface.reserve(cells.size());
    for (const Cell& c : cells) {
        const float ref = c.surfaceY;
        const float floorY = ref - c.depth; // underside of this water column
        auto cor = [&](int cx, int cz) -> float {
            float h = cornerH[cx + cw * cz];
            return (h > -1e8f && std::fabs(h - ref) <= 1.25f) ? h : ref;
        };
        // Side-face bottom for one edge: how far the vertical "skirt" drops. Against
        // terrain it collapses (hidden by the wall); against a lower water body it stops
        // at that body's surface (closing the step gap); against open air it falls to the
        // column floor (showing the water's full side / a waterfall face).
        auto edgeBottom = [&](int nx, int nz, float edgeTop) -> float {
            if (nx < 0 || nx >= sx || nz < 0 || nz >= sz) return floorY; // open border
            if (m_sim.isSolid(nx, c.y, nz)) return edgeTop;              // buried in wall
            float nTop = colTop[colIdx(nx, nz)];
            float b = (nTop > -1e8f) ? std::max(nTop, floorY) : floorY;
            return std::min(edgeTop, b);
        };
        const float cNN = cor(c.x, c.z), cPN = cor(c.x + 1, c.z);
        const float cPP = cor(c.x + 1, c.z + 1), cNP = cor(c.x, c.z + 1);
        const float wX = static_cast<float>(m_origin.x + c.x);
        const float wZ = static_cast<float>(m_origin.z + c.z);
        // Side bottom for one edge. Where water spills over a tall OPEN drop (down a
        // solid cliff / into a much-lower pool), the skirt extends the whole way to the
        // landing so the falling water renders as a vertical curtain, and a waterfall lip
        // is recorded for mist. Otherwise it's a normal short side face.
        auto edge = [&](int nx, int nz, float top, float lipX, float lipZ) -> float {
            if (nx >= 0 && nx < sx && nz >= 0 && nz < sz &&
                !m_sim.isSolid(nx, c.y, nz)) {           // edge is open (water can pour over)
                float land = static_cast<float>(m_origin.y);     // region bottom if nothing below
                for (int yy = c.y; yy >= 0; --yy)
                    if (m_sim.isSolid(nx, yy, nz)) { land = static_cast<float>(m_origin.y + yy + 1); break; }
                float nW = colTop[colIdx(nx, nz)];        // a lower pool catches the fall
                if (nW > -1e8f) land = std::max(land, nW);
                float drop = ref - land;
                if (drop >= WATERFALL_MIN_DROP) {
                    if (m_waterfalls.size() < MAX_WATERFALLS)
                        m_waterfalls.emplace_back(lipX, ref, lipZ, drop);
                    return land; // vertical falling-water curtain down the drop
                }
            }
            return edgeBottom(nx, nz, top);
        };
        WaterSurfaceCell out;
        out.centerDepth = glm::vec4(wX + 0.5f, ref, wZ + 0.5f, c.depth);
        out.corners = glm::vec4(cNN, cPN, cPP, cNP);
        // FLOW (Phase 3): direction + strength from the sim's flow proxy, plus a foam term.
        // FLOW_FULL ⚑GROUND: 0.15 mass/step is a vigorously flowing channel in this CA (a
        // source-fed 1-wide channel measures ~0.1-0.3), so it maps "clearly moving" to strength 1
        // without saturating on the gentle drift a settling pond shows.
        constexpr float FLOW_FULL = 0.15f;
        const glm::vec2 fv = colFlowSmooth[colIdx(c.x, c.z)];
        const float fmag = std::sqrt(fv.x * fv.x + fv.y * fv.y);
        glm::vec2 fdir = (fmag > 1e-5f) ? fv / fmag : glm::vec2(0.0f);
        float strength = std::min(fmag / FLOW_FULL, 1.0f);
        // KINEMATIC RIVER FLOW: a baked river is pinned full along its carve, so it performs no
        // transfers and the CA proxy above reads ZERO — it would shade as a long thin lake. Where
        // the bake says this column is a channel, take the direction from the drainage network
        // instead (shared helper with flowAtWorld, so shading and the physics current agree).
        if (strength < 0.35f) {
            const glm::vec2 kd = kinematicRiverFlow(wX + 0.5f, wZ + 0.5f);
            if (kd.x != 0.0f || kd.y != 0.0f) {
                fdir = kd;
                // ⚑GROUND: 0.55 — a visible current, deliberately below the 1.0 of a genuinely
                // churning CA flow so a broad river reads as purposeful drift, not rapids.
                strength = 0.55f;
            }
        }
        // Foam where the water is both moving and SHALLOW — that is where a real stream breaks
        // white over its bed. Deep fast water (a river's middle) stays smooth.
        const float shallow = 1.0f - std::min(c.depth / 2.0f, 1.0f);
        const float foam = std::min(strength * (0.35f + 0.65f * shallow), 1.0f);
        out.flow = glm::vec4(fdir.x, fdir.y, strength, foam);
        out.skirt = glm::vec4(edge(c.x + 1, c.z, std::min(cPN, cPP), wX + 1.0f, wZ + 0.5f),  // +x
                              edge(c.x - 1, c.z, std::min(cNN, cNP), wX + 0.0f, wZ + 0.5f),  // -x
                              edge(c.x, c.z + 1, std::min(cNP, cPP), wX + 0.5f, wZ + 1.0f),  // +z
                              edge(c.x, c.z - 1, std::min(cNN, cPN), wX + 0.5f, wZ + 0.0f)); // -z
        m_surface.push_back(out);
    }
}

glm::vec2 WaterManager::kinematicRiverFlow(float wx, float wz, int* orderOut) const {
    if (orderOut) *orderOut = 0;
    if (!m_riverDirFn) return glm::vec2(0.0f);
    const glm::vec2 rd = m_riverDirFn(wx, wz);
    const float rmag = std::sqrt(rd.x * rd.x + rd.y * rd.y);
    if (rmag <= 1e-5f) return glm::vec2(0.0f);
    if (orderOut && m_riverOrderFn) *orderOut = m_riverOrderFn(wx, wz);
    return rd / rmag;
}

glm::vec3 WaterManager::flowAtWorld(const glm::vec3& worldPos) const {
    // Locate the wet cell this point rides in (the point may sit fractionally above the surface
    // cell of the water carrying it — a floating body's AABB center often does).
    int lx, ly, lz;
    if (worldToLocal(worldPos, lx, ly, lz)) {
        if (m_sim.massAt(lx, ly, lz) <= 0.0f) {
            if (ly > 0 && m_sim.massAt(lx, ly - 1, lz) > 0.0f) --ly;
            else return glm::vec3(0.0f);   // genuinely dry cell — no current
        }
        const glm::vec2 fv = m_sim.flowAt(lx, ly, lz);
        const glm::vec2 simVel = fv * kFlowSpeedScale;
        const float simSpeed = std::sqrt(simVel.x * simVel.x + simVel.y * simVel.y);
        // A genuinely moving CA flow (a spill, a breach) wins outright.
        if (simSpeed > 0.5f) return glm::vec3(simVel.x, 0.0f, simVel.y);
        // Pinned river water shows ~no proxy flow; substitute the baked downhill current at an
        // order-scaled speed. ⚑GROUND: creeks ~0.8 m/s (ankle-deep push), order-3 ~1.6 (a real
        // river you lean against), +0.4 per order, capped 3 — hydraulic-geometry-flavored, tuned
        // for feel, not measured discharge.
        int order = 0;
        const glm::vec2 kd = kinematicRiverFlow(std::floor(worldPos.x) + 0.5f,
                                                std::floor(worldPos.z) + 0.5f, &order);
        if ((kd.x != 0.0f || kd.y != 0.0f) && order > 0) {
            const float speed = (order <= 2)
                ? 0.8f
                : std::min(1.6f + 0.4f * static_cast<float>(order - 3), 3.0f);
            return glm::vec3(kd.x * speed, 0.0f, kd.y * speed);
        }
        return glm::vec3(simVel.x, 0.0f, simVel.y);   // gentle residual drift
    }
    // Out of the sim window: the kinematic river current still exists (callers gate on being in
    // water via sampleWater/submergedFraction, which already handle out-of-window wetness).
    int order = 0;
    const glm::vec2 kd = kinematicRiverFlow(std::floor(worldPos.x) + 0.5f,
                                            std::floor(worldPos.z) + 0.5f, &order);
    if ((kd.x != 0.0f || kd.y != 0.0f) && order > 0) {
        const float speed = (order <= 2)
            ? 0.8f
            : std::min(1.6f + 0.4f * static_cast<float>(order - 3), 3.0f);
        return glm::vec3(kd.x * speed, 0.0f, kd.y * speed);
    }
    return glm::vec3(0.0f);
}

bool WaterManager::worldToLocal(const glm::vec3& w, int& lx, int& ly, int& lz) const {
    lx = static_cast<int>(std::floor(w.x)) - m_origin.x;
    ly = static_cast<int>(std::floor(w.y)) - m_origin.y;
    lz = static_cast<int>(std::floor(w.z)) - m_origin.z;
    return m_sim.inBounds(lx, ly, lz);
}

void WaterManager::placeWater(const glm::vec3& worldPos, float amount) {
    int lx, ly, lz;
    if (worldToLocal(worldPos, lx, ly, lz)) {
        m_sim.addWater(lx, ly, lz, amount);
        rebuildSurface();
    }
}

void WaterManager::setFloorWorld(int worldX, int worldY, int worldZ, float fraction) {
    const int lx = worldX - m_origin.x, ly = worldY - m_origin.y, lz = worldZ - m_origin.z;
    if (m_sim.inBounds(lx, ly, lz)) {
        m_sim.setFloor(lx, ly, lz, fraction);
        rebuildSurface();   // render-only input: restamp the surface, no need to re-step
    }
}

float WaterManager::floorAtWorld(const glm::vec3& worldPos) const {
    int lx, ly, lz;
    if (!worldToLocal(worldPos, lx, ly, lz)) return 0.0f;
    return m_sim.floorAt(lx, ly, lz);
}

void WaterManager::setSolidWorld(int worldX, int worldY, int worldZ, bool solid) {
    int lx = worldX - m_origin.x, ly = worldY - m_origin.y, lz = worldZ - m_origin.z;
    if (m_sim.inBounds(lx, ly, lz)) {
        m_sim.setSolid(lx, ly, lz, solid);
        // Terrain changed: re-flood the ocean/table so breaches fill / dug seabed refills.
        if (!m_oceanSeeds.empty() || m_oceanBoundary || m_tableFn) m_oceanDirty = true;
    }
}

void WaterManager::setSeaLevel(float worldY) {
    m_seaLevel = worldY;
    if (!m_oceanSeeds.empty() || m_oceanBoundary) m_oceanDirty = true;
}

void WaterManager::addOceanSeed(const glm::vec3& worldPos) {
    m_oceanSeeds.emplace_back(static_cast<int>(std::floor(worldPos.x)),
                              static_cast<int>(std::floor(worldPos.y)),
                              static_cast<int>(std::floor(worldPos.z)));
    m_oceanDirty = true;
}

void WaterManager::clearOcean() {
    m_oceanSeeds.clear();
    m_sim.fillOcean({}, 0); // clears all source pins
    applySprings();         // ...but keep authored springs
    m_oceanDirty = false;
    rebuildSurface();
}

void WaterManager::setOceanBoundary(bool on) {
    if (m_oceanBoundary == on) return;
    m_oceanBoundary = on;
    m_oceanDirty = true;   // re-flood next update (or clear, if turning off with no point seeds)
}

void WaterManager::setWaterTable(std::function<float(float, float)> levelAt) {
    m_tableFn = std::move(levelAt);
    m_oceanDirty = true;   // re-derive the pins from (or without) the table on the next update
}

void WaterManager::setBodyQuery(std::function<BodyInfo(float, float)> bodyAt) {
    m_bodyFn = std::move(bodyAt);
    m_oceanDirty = true;   // finite bodies unpin / re-derive on the next rebuild
}

void WaterManager::setRiverQuery(std::function<float(float, float)> depthAt) {
    m_riverFn = std::move(depthAt);
    m_oceanDirty = true;   // re-derive channel tags + bed pins on the next update
}

void WaterManager::setRiverOrderQuery(std::function<int(float, float)> orderAt) {
    m_riverOrderFn = std::move(orderAt);
    m_oceanDirty = true;   // pin masses depend on order — re-derive
}

void WaterManager::setRiverFlowQuery(std::function<glm::vec2(float, float)> dirAt) {
    m_riverDirFn = std::move(dirAt);
    rebuildSurface();      // shading-only input: just restamp the surface cells
}

WaterManager::TableValidation WaterManager::validateTable(const glm::ivec2& minXZ,
                                                          const glm::ivec2& maxXZ,
                                                          int maxScanY) const {
    TableValidation v;
    if (!m_tableFn || !m_cm) return v;
    const int w = maxXZ.x - minXZ.x + 1, d = maxXZ.y - minXZ.y + 1;
    if (w <= 0 || d <= 0) return v;

    // Pass 1: per-column baked level + carved surface (topmost solid; INT_MIN = unloaded/none).
    std::vector<float> level(static_cast<size_t>(w) * d);
    std::vector<int>   surf(static_cast<size_t>(w) * d, INT_MIN);
    auto at = [&](int ix, int iz) { return static_cast<size_t>(ix) + static_cast<size_t>(w) * iz; };
    for (int iz = 0; iz < d; ++iz)
        for (int ix = 0; ix < w; ++ix) {
            const int wx = minXZ.x + ix, wz = minXZ.y + iz;
            level[at(ix, iz)] = m_tableFn(wx + 0.5f, wz + 0.5f);
            for (int y = maxScanY; y >= 0; --y)
                if (m_cm->hasVoxelAt(glm::ivec3(wx, y, wz))) { surf[at(ix, iz)] = y; break; }
        }

    // Pass 2: classify. A rim column is baked-DRY next to a baked-wet neighbor; it LEAKS when its
    // carved surface sits below the neighbor's water level (open cells at/below the level exist,
    // so the CA levels water into it and the body spreads beyond its baked extent).
    static const int NX[4] = {1, -1, 0, 0}, NZ[4] = {0, 0, 1, -1};
    for (int iz = 0; iz < d; ++iz)
        for (int ix = 0; ix < w; ++ix) {
            if (surf[at(ix, iz)] == INT_MIN) { ++v.unloaded; continue; }
            ++v.columns;
            const bool wetCol = level[at(ix, iz)] > TABLE_DRY;
            if (wetCol) { ++v.wet; continue; }
            float adjLevel = TABLE_DRY;   // highest wet-neighbor level (loaded neighbors only)
            for (int k = 0; k < 4; ++k) {
                const int nx = ix + NX[k], nz = iz + NZ[k];
                if (nx < 0 || nx >= w || nz < 0 || nz >= d) continue;
                if (surf[at(nx, nz)] == INT_MIN) continue;
                adjLevel = std::max(adjLevel, level[at(nx, nz)]);
            }
            if (adjLevel <= TABLE_DRY) continue;   // not a rim column
            ++v.rim;
            const float leakDepth = std::floor(adjLevel) - static_cast<float>(surf[at(ix, iz)]);
            if (leakDepth > 0.0f) {
                ++v.rimLeaks;
                if (leakDepth > v.worstLeakDepth) {
                    v.worstLeakDepth = leakDepth;
                    v.worstLeakAt = glm::ivec2(minXZ.x + ix, minXZ.y + iz);
                }
            }
        }
    return v;
}

void WaterManager::applyRiverInflows() {
    if (!m_riverFn) return;
    const int sx = m_dims.x, sy = m_dims.y, sz = m_dims.z;
    for (int lz = 0; lz < sz; ++lz)
    for (int lx = 0; lx < sx; ++lx) {
        const float carveDepth = m_riverFn(static_cast<float>(m_origin.x + lx) + 0.5f,
                                           static_cast<float>(m_origin.z + lz) + 0.5f);
        if (carveDepth <= 0.0f) continue;
        // The bed: the first open cell above a REAL solid cell (y-1 >= 0, so an unloaded column —
        // open all the way down to the out-of-bounds floor — yields no bed and is skipped; the
        // stream-in solidity sync re-runs this rebuild once its terrain arrives).
        int bedY = -1;
        for (int y = 1; y < sy; ++y) {
            if (!m_sim.isSolid(lx, y, lz) && m_sim.isSolid(lx, y - 1, lz)) { bedY = y; break; }
        }
        if (bedY < 0) continue;
        m_sim.setChannel(lx, bedY, lz, true);   // riverbed never evaporates dry
        // Order-aware pins (small-scale plan Phase 2a). Unbound order query = legacy: everything
        // is a big river.
        const int order = m_riverOrderFn
            ? m_riverOrderFn(static_cast<float>(m_origin.x + lx) + 0.5f,
                             static_cast<float>(m_origin.z + lz) + 0.5f)
            : 3;
        if (order >= 3) {
            // Implicit-reservoir pins on the RECESSED part of the band only (see setRiverQuery):
            // the ribbon is full inside the carve; the non-recessed parabolic edges are left to
            // the CA. carve ≥ 0.5 is exactly where the generator's lround recessed the bed a full
            // voxel, so every full pin sits at-or-below its banks by construction.
            if (carveDepth >= 0.5f)
                m_sim.setSource(lx, bedY, lz, WaterSimulation::MAX_MASS);
        } else if (carveDepth >= 0.15f) {
            // CREEK (order 1-2): a FRACTIONAL ribbon on uncarved ground. Clamping the pin to the
            // sim's MIN_HOLD makes the ribbon incapable of horizontal transfers — the worst case
            // is a static ribbon, never the hillside sheet that forced the 496cdc10 revert. The
            // parabolic sub-0.15 band edges stay tag-only (mirroring the big rivers' edge rule).
            m_sim.setSource(lx, bedY, lz, std::min(carveDepth, m_sim.minHold()));
        }
    }
}

void WaterManager::rebuildOcean() {
    m_oceanDirty = false;
    // Baked water table bound (Phase C): derive every pin from the per-column baked levels —
    // lakes at their spill height, ocean at sea level — instead of the scalar/authored path.
    // recenter() re-runs this, so the table re-derives wherever the region travels.
    if (m_tableFn) {
        // Per-column baked levels (local grid Y; INT_MIN = dry), sampled once.
        const int sx = m_dims.x, sy = m_dims.y, sz = m_dims.z;
        auto colIdx = [sx](int lx, int lz) {
            return static_cast<size_t>(lx) + static_cast<size_t>(sx) * static_cast<size_t>(lz);
        };
        std::vector<int> lvl(static_cast<size_t>(sx) * sz);
        // FINITE bodies (tangible-water Phase C): excluded from pinning BEFORE the shoreline
        // snap — their water is real conserved mass hydrated to baseline + per-body delta, not
        // an infinite reservoir. m_bodyLvlLocal/m_bodyIdLocal cache the per-column hydration
        // target + ownership for the suppression grid, the no-bleed mask, and capture routing.
        m_bodyLvlLocal.assign(static_cast<size_t>(sx) * sz, INT_MIN);
        m_bodyIdLocal.assign(static_cast<size_t>(sx) * sz, -1);
        for (int lz = 0; lz < sz; ++lz)
            for (int lx = 0; lx < sx; ++lx) {
                const float wx = static_cast<float>(m_origin.x + lx) + 0.5f;
                const float wz = static_cast<float>(m_origin.z + lz) + 0.5f;
                // A FINITE body claims its columns regardless of the table: fine-scale ponds
                // (Phase B) live in table-DRY columns — the bake never saw their sub-cell
                // basins — while a finite bake body would be table-wet. Either way the column
                // is the body's: no pin, hydration owns it.
                if (m_bodyFn) {
                    const BodyInfo bi = m_bodyFn(wx, wz);
                    if (bi.id >= 0 && bi.finite) {
                        lvl[colIdx(lx, lz)] = INT_MIN;   // no pin for finite bodies
                        const float target = bi.baselineLevel + bodyDelta(bi.id);
                        m_bodyIdLocal[colIdx(lx, lz)] = bi.id;
                        m_bodyLvlLocal[colIdx(lx, lz)] =
                            static_cast<int>(std::floor(target - 1e-4f)) - m_origin.y;
                        continue;
                    }
                }
                const float wl = m_tableFn(wx, wz);
                lvl[colIdx(lx, lz)] = (wl <= TABLE_DRY)
                    ? INT_MIN : static_cast<int>(std::floor(wl)) - m_origin.y;
            }
        // RUNTIME SHORELINE SNAP (the L3 rim-leak fix, water side): the bake is 128 m/cell coarse,
        // so its wet/dry boundary sits far from the carved waterline — the validator measured 65/65
        // rim columns below the adjacent water level at a coast, and the CA leveled unpinned water
        // into them forever (pins refill → the coast crept and never settled). Expand each wet
        // level into adjacent DRY columns whose carved in-band terrain top sits BELOW that level,
        // stopping where terrain rises to/above it — the waterline snaps from the coarse cell
        // boundary to the actual per-voxel contour, and the shore becomes properly PINNED water.
        // Columns with no in-band solid are skipped (unloaded terrain or open void — snapping them
        // would pin floating water; same guard as the river beds). Terrain itself is untouched —
        // the beach/coastal-band look is the generator-side follow-up.
        {
            auto topSolid = [&](int lx, int lz) -> int {
                for (int y = sy - 1; y >= 0; --y)
                    if (m_sim.isSolid(lx, y, lz)) return y;
                return INT_MIN;
            };
            std::vector<glm::ivec2> queue;
            queue.reserve(static_cast<size_t>(sx) * 2 + static_cast<size_t>(sz) * 2);
            for (int lz = 0; lz < sz; ++lz)
                for (int lx = 0; lx < sx; ++lx)
                    if (lvl[colIdx(lx, lz)] != INT_MIN) queue.push_back({lx, lz});
            static const int NX[4] = {1, -1, 0, 0}, NZ[4] = {0, 0, 1, -1};
            for (size_t qi = 0; qi < queue.size(); ++qi) {
                const glm::ivec2 c = queue[qi];
                const int L = lvl[colIdx(c.x, c.y)];
                for (int k = 0; k < 4; ++k) {
                    const int nx = c.x + NX[k], nz = c.y + NZ[k];
                    if (nx < 0 || nx >= sx || nz < 0 || nz >= sz) continue;
                    if (lvl[colIdx(nx, nz)] != INT_MIN) continue;   // already wet/snapped
                    // Rim guard (Phase C): a lake must never snap-expand INTO a finite pond
                    // across a low divide — those columns belong to the pond's own hydration.
                    if (m_bodyIdLocal[colIdx(nx, nz)] >= 0) continue;
                    const int t = topSolid(nx, nz);
                    if (t == INT_MIN || t >= L) continue;           // void/unloaded or land above level
                    lvl[colIdx(nx, nz)] = L;
                    queue.push_back({nx, nz});
                }
            }
        }
        m_sim.fillWaterTable([&](int lx, int lz) -> int { return lvl[colIdx(lx, lz)]; });
        // Cache the snapped grid for rebuildSurface's far-layer suppression (water-layer P1).
        m_tableLvlLocal = std::move(lvl);
        // No-bleed mask (Phase C, F3): the P4 edge outflow must not siphon a finite pond that
        // straddles the window ring — its mass is conserved and owned by the body record.
        for (int lz = 0; lz < sz; ++lz)
            for (int lx = 0; lx < sx; ++lx)
                m_sim.setColumnNoBleed(lx, lz, m_bodyIdLocal[colIdx(lx, lz)] >= 0);
        applySprings();       // authored springs still ride on top of the baked table
        applyRiverInflows();  // baked river channel tags + edge inflows (Phase C2)
        applyOverrides();     // P3: reseed captured pours over the pinned bodies
        applyFiniteBodies();  // Phase C: hydrate finite bodies to baseline + delta
        applyOutflowBank();   // P4: redeposit mass that once bled out of the window here
        rebuildSurface();
        return;
    }
    m_tableLvlLocal.clear();   // no table → only the sea-band rule suppresses (below)
    m_bodyLvlLocal.clear();    // finite-body machinery is table-path-only
    m_bodyIdLocal.clear();
    const int seaLevelLocalY = static_cast<int>(std::floor(m_seaLevel)) - m_origin.y;
    std::vector<glm::ivec3> localSeeds;
    localSeeds.reserve(m_oceanSeeds.size());
    for (const glm::ivec3& s : m_oceanSeeds)
        localSeeds.emplace_back(s.x - m_origin.x, s.y - m_origin.y, s.z - m_origin.z);
    // Boundary condition (Phase A2b): also seed from every region-edge cell at/below sea level that
    // is open (non-solid). This makes the ocean re-establish from the frontier wherever the region
    // moves, so a following region doesn't drain the sea when it leaves a point seed behind. The flood
    // (fillOcean) is connectivity-gated, so a sealed sub-sea pocket not reachable from an edge stays dry.
    if (m_oceanBoundary) {
        const int sx = m_dims.x, sy = m_dims.y, sz = m_dims.z;
        const int topY = std::min(seaLevelLocalY, sy - 1);
        for (int y = 0; y <= topY; ++y) {
            for (int x = 0; x < sx; ++x) {          // the z = 0 and z = sz-1 side faces
                if (!m_sim.isSolid(x, y, 0))      localSeeds.emplace_back(x, y, 0);
                if (!m_sim.isSolid(x, y, sz - 1)) localSeeds.emplace_back(x, y, sz - 1);
            }
            for (int z = 1; z < sz - 1; ++z) {      // the x = 0 and x = sx-1 side faces (corners done above)
                if (!m_sim.isSolid(0, y, z))      localSeeds.emplace_back(0, y, z);
                if (!m_sim.isSolid(sx - 1, y, z)) localSeeds.emplace_back(sx - 1, y, z);
            }
        }
    }
    m_sim.fillOcean(localSeeds, seaLevelLocalY); // clears all sources, then pins the ocean
    applySprings();                               // re-pin authored springs over the top
    applyRiverInflows();                          // baked river channel tags + edge inflows (Phase C2)
    applyOverrides();                             // P3: reseed captured pours (authored path too)
    applyOutflowBank();                           // P4: redeposit banked edge outflow
    rebuildSurface();
}

// ── Poured-water persistence (water-as-terrain-stage P3) ─────────────────────────────────────────

void WaterManager::captureColumnOverride(int lx, int lz, int yLo, int yHi) {
    // Finite-body columns route to the per-BODY observation instead of a column override (F1):
    // a body's settled surface is flat, so its state is ONE level — per-column records cannot
    // express a partially-scooped body without re-minting mass from unrecorded neighbors.
    const bool bodyCol = !m_bodyIdLocal.empty() &&
                         m_bodyIdLocal[static_cast<size_t>(lx) +
                                       static_cast<size_t>(m_dims.x) * lz] >= 0;
    const int64_t bodyId = bodyCol
        ? m_bodyIdLocal[static_cast<size_t>(lx) + static_cast<size_t>(m_dims.x) * lz] : -1;

    // Top wet cell of the departing slice decides the column's surface. A pinned top means the
    // water is bake-derived (sea/lake/river/spring) — never captured, it re-derives on return.
    for (int y = yHi; y >= yLo; --y) {
        const float m = m_sim.massAt(lx, y, lz);
        if (m < 0.15f) continue;                        // ignore films below the pin threshold
        if (m_sim.sourceAt(lx, y, lz) >= 0.0f) return;  // pinned water: the bake owns it
        const float f = m_sim.floorAt(lx, y, lz);
        const float level = static_cast<float>(m_origin.y + y) +
                            f + std::min(m, 1.0f) * (1.0f - f);   // same surface formula as sampleWater
        if (bodyCol) {
            auto it = m_bodyObserved.find(bodyId);
            if (it == m_bodyObserved.end() || level < it->second) m_bodyObserved[bodyId] = level;
            return;
        }
        if (m_overrides.size() >= MAX_OVERRIDES) {
            const auto victim = m_overrides.find(packColumnKey(m_origin.x + lx, m_origin.z + lz));
            if (victim == m_overrides.end()) m_overrides.erase(m_overrides.begin());  // arbitrary evict
        }
        m_overrides[packColumnKey(m_origin.x + lx, m_origin.z + lz)] = level;
        return;
    }
    // No wet cell in a departing FINITE-body column that should hold water: observe it DRY at
    // its ground level, so a drained pond does not silently refill from an unchanged record.
    // Whole-column exits only (a vertical slice may just miss the water band); unloaded columns
    // (no solid) are skipped — nothing was observable there.
    if (bodyCol && yLo == 0 && yHi == m_dims.y - 1) {
        for (int y = m_dims.y - 1; y >= 0; --y)
            if (m_sim.isSolid(lx, y, lz)) {
                const float ground = static_cast<float>(m_origin.y + y + 1);
                auto it = m_bodyObserved.find(bodyId);
                if (it == m_bodyObserved.end() || ground < it->second)
                    m_bodyObserved[bodyId] = ground;
                return;
            }
    }
}

void WaterManager::applyFiniteBodies() {
    if (m_bodyLvlLocal.empty()) return;
    const int sx = m_dims.x, sy = m_dims.y, sz = m_dims.z;
    for (int lz = 0; lz < sz; ++lz)
        for (int lx = 0; lx < sx; ++lx) {
            const size_t ci = static_cast<size_t>(lx) + static_cast<size_t>(sx) * lz;
            const int64_t id = m_bodyIdLocal[ci];
            if (id < 0) continue;
            const float wx = static_cast<float>(m_origin.x + lx) + 0.5f;
            const float wz = static_cast<float>(m_origin.z + lz) + 0.5f;
            const BodyInfo bi = m_bodyFn(wx, wz);
            if (bi.id != id) continue;   // paranoia: grid/query drift
            // Resolve a level observation (recorded by the recenter capture) into the body
            // delta now that the baseline is in hand. Never-grow invariant: MIN against the
            // existing record, clamped ≤ 0 — capture can only lower a body and reseed fills to
            // at most the record, so walk-away/walk-back cycles cannot grow water (the column
            // overrides' invariant, lifted to body granularity).
            auto obs = m_bodyObserved.find(id);
            if (obs != m_bodyObserved.end()) {
                const float obsDelta = std::min(0.0f, obs->second - bi.baselineLevel);
                auto cur = m_bodyDeltas.find(id);
                if (cur == m_bodyDeltas.end() || obsDelta < cur->second)
                    m_bodyDeltas[id] = obsDelta;
                m_bodyObserved.erase(obs);
                // The cached hydration/suppression level predates this commit — refresh it so
                // THIS rebuild already hydrates to the just-observed level.
                m_bodyLvlLocal[ci] =
                    static_cast<int>(std::floor(bi.baselineLevel + m_bodyDeltas[id] - 1e-4f)) -
                    m_origin.y;
            }
            const float target = bi.baselineLevel + bodyDelta(id);
            const int yTop = static_cast<int>(std::floor(target - 1e-4f)) - m_origin.y;
            if (yTop < 0 || yTop >= sy) continue;
            // Contiguous open run downward from the surface cell; requires loaded ground (same
            // guard as the overrides/river beds).
            int yBottom = yTop;
            while (yBottom >= 0 && !m_sim.isSolid(lx, yBottom, lz)) --yBottom;
            if (yBottom < 0) continue;   // unloaded/void column: hydrate on a later visit
            // COLUMN-TOTAL deficit fill, distributed bottom-up. Two failure modes bracketed
            // this design: per-cell top-up re-minted a sliver every rebuild (the settled CA
            // redistributes mass inside a column — compression leaves bottom cells over 1.0 and
            // the top under, and topping each cell to nominal added the difference back, ~0.19
            // per rebuild on a 16-column pond); dumping the whole deficit into the TOP cell
            // sloshed over the basin walls before gravity could settle it (measured +5.6 mass
            // escaping per cycle). So: compare COLUMN totals (idempotent under any internal
            // redistribution), then place only the missing mass, bottom-up, capping each cell
            // at its nominal fraction — water lands where it will rest, nothing overfills.
            const auto nominal = [&](int y) {
                const float f = m_sim.floorAt(lx, y, lz);
                return (y == yTop)
                    ? glm::clamp((target - static_cast<float>(m_origin.y + y) - f) /
                                 std::max(1.0f - f, 1e-4f), 0.0f, 1.0f)
                    : 1.0f;
            };
            float targetTotal = 0.0f, haveTotal = 0.0f;
            for (int y = yTop; y > yBottom; --y) {
                targetTotal += nominal(y);
                haveTotal += m_sim.massAt(lx, y, lz);
            }
            float deficit = targetTotal - haveTotal;
            if (deficit <= 0.02f) continue;
            for (int y = yBottom + 1; y <= yTop && deficit > 1e-4f; ++y) {
                const float room = nominal(y) - m_sim.massAt(lx, y, lz);
                if (room <= 0.0f) continue;
                const float add = std::min(room, deficit);
                m_sim.addWater(lx, y, lz, add);
                deficit -= add;
            }
        }
}

void WaterManager::captureOverridesInWindow() {
    for (int lz = 0; lz < m_dims.z; ++lz)
        for (int lx = 0; lx < m_dims.x; ++lx)
            captureColumnOverride(lx, lz, 0, m_dims.y - 1);
}

void WaterManager::applyOverrides() {
    if (m_overrides.empty()) return;
    const int sx = m_dims.x, sy = m_dims.y, sz = m_dims.z;
    for (auto it = m_overrides.begin(); it != m_overrides.end();) {
        const int wx = static_cast<int32_t>(static_cast<uint32_t>(it->first >> 32));
        const int wz = static_cast<int32_t>(static_cast<uint32_t>(it->first & 0xffffffffu));
        const float level = it->second;
        const int lx = wx - m_origin.x, lz = wz - m_origin.z;
        if (lx < 0 || lx >= sx || lz < 0 || lz >= sz) { ++it; continue; }  // out of window: keep
        // Finite-body columns (Phase C): the body record owns this water — a stale column
        // override here would double-pour on top of the hydration. Erase it.
        if (!m_bodyIdLocal.empty() &&
            m_bodyIdLocal[static_cast<size_t>(lx) + static_cast<size_t>(sx) * lz] >= 0) {
            it = m_overrides.erase(it);
            continue;
        }
        // Double-count guard: the snapped baked table already pins water at/above this level in
        // this column — the pour merged into a persistent body; the override is redundant.
        if (!m_tableLvlLocal.empty()) {
            const int t = m_tableLvlLocal[static_cast<size_t>(lx) + static_cast<size_t>(sx) * lz];
            if (t != INT_MIN && static_cast<float>(m_origin.y + t + 1) >= level) {
                it = m_overrides.erase(it);
                continue;
            }
        }
        const int yTop = static_cast<int>(std::floor(level - 1e-4f)) - m_origin.y;
        if (yTop < 0 || yTop >= sy) { ++it; continue; }  // outside the vertical band: keep for later
        // Refill the contiguous open run downward from the surface cell. Requires real ground
        // under the run (the loop stops at the first solid); a column that is open all the way to
        // the window floor is unloaded terrain or void — restoring there would strand water in
        // the wrong place, so keep the override for a visit when the terrain exists (same guard
        // as the river-bed pins).
        int yBottom = yTop;
        while (yBottom >= 0 && !m_sim.isSolid(lx, yBottom, lz)) --yBottom;
        if (yBottom < 0) { ++it; continue; }             // no loaded ground: keep
        bool placedAny = false;
        for (int y = yTop; y > yBottom; --y) {
            const float f = m_sim.floorAt(lx, y, lz);
            const float target = (y == yTop)
                ? glm::clamp((level - static_cast<float>(m_origin.y + y) - f) /
                             std::max(1.0f - f, 1e-4f), 0.0f, 1.0f)
                : 1.0f;
            const float have = m_sim.massAt(lx, y, lz);
            if (target > have) { m_sim.addWater(lx, y, lz, target - have); placedAny = true; }
        }
        // Live again — the water will be re-captured if it departs. An empty restore (the level
        // sat below the terrain after an edit) is dropped rather than kept forever.
        (void)placedAny;
        it = m_overrides.erase(it);
    }
}

void WaterManager::drainOutflowToBank() {
    if (!m_sim.edgeOutflow()) return;
    m_sim.drainEdgeOutflow([this](int lx, int lz, float mass) {
        // The bled mass lands one column OUTSIDE the window, past the ring cell it left through.
        int wx = m_origin.x + lx, wz = m_origin.z + lz;
        if (lx == 0) --wx;
        else if (lx == m_dims.x - 1) ++wx;
        if (lz == 0) --wz;
        else if (lz == m_dims.z - 1) ++wz;
        float& bank = m_outflowBank[packColumnKey(wx, wz)];
        // Cap: a drain fed by a pinned (infinite) reservoir bleeds forever; past the cap the
        // outside world is deemed to have absorbed it (documented loss, not a leak).
        bank = std::min(bank + mass, BANK_CAP_PER_COLUMN);
    });
}

void WaterManager::applyOutflowBank() {
    if (m_outflowBank.empty()) return;
    const int sx = m_dims.x, sy = m_dims.y, sz = m_dims.z;
    for (auto it = m_outflowBank.begin(); it != m_outflowBank.end();) {
        const int wx = static_cast<int32_t>(static_cast<uint32_t>(it->first >> 32));
        const int wz = static_cast<int32_t>(static_cast<uint32_t>(it->first & 0xffffffffu));
        const int lx = wx - m_origin.x, lz = wz - m_origin.z;
        // Interior only: redepositing ON the ring would just bleed straight back out next step.
        if (lx <= 0 || lx >= sx - 1 || lz <= 0 || lz >= sz - 1) { ++it; continue; }
        // Drop the mass on the column's loaded ground; gravity/leveling spreads it naturally.
        // No ground in-window → keep for a later visit (same rule as the overrides).
        int top = -1;
        for (int y = sy - 1; y >= 0; --y)
            if (m_sim.isSolid(lx, y, lz)) { top = y; break; }
        if (top < 0 || top + 1 >= sy) { ++it; continue; }
        m_sim.addWater(lx, top + 1, lz, it->second);
        it = m_outflowBank.erase(it);
    }
}

std::string WaterManager::serializeOverrides() const {
    std::string out;
    out.reserve((m_overrides.size() + m_outflowBank.size()) * 24);
    char line[64];
    for (const auto& [key, level] : m_overrides) {
        const int wx = static_cast<int32_t>(static_cast<uint32_t>(key >> 32));
        const int wz = static_cast<int32_t>(static_cast<uint32_t>(key & 0xffffffffu));
        std::snprintf(line, sizeof(line), "%d %d %.3f\n", wx, wz, level);
        out += line;
    }
    // P4 bank lines: "B x z mass" — banked edge outflow persists alongside the level overrides.
    for (const auto& [key, mass] : m_outflowBank) {
        const int wx = static_cast<int32_t>(static_cast<uint32_t>(key >> 32));
        const int wz = static_cast<int32_t>(static_cast<uint32_t>(key & 0xffffffffu));
        std::snprintf(line, sizeof(line), "B %d %d %.3f\n", wx, wz, mass);
        out += line;
    }
    // Phase C body lines: "D bodyId delta" — one float per scooped/drained finite body.
    for (const auto& [id, delta] : m_bodyDeltas) {
        if (delta >= 0.0f) continue;   // at-baseline records carry no information
        std::snprintf(line, sizeof(line), "D %lld %.3f\n", static_cast<long long>(id), delta);
        out += line;
    }
    return out;
}

bool WaterManager::loadOverrides(const std::string& data) {
    std::unordered_map<uint64_t, float> fresh, freshBank;
    std::unordered_map<int64_t, float>  freshBodies;
    size_t pos = 0;
    while (pos < data.size()) {
        size_t eol = data.find('\n', pos);
        if (eol == std::string::npos) eol = data.size();
        int wx = 0, wz = 0;
        float value = 0.0f;
        long long bodyId = 0;
        const std::string lineStr = data.substr(pos, eol - pos);
        pos = eol + 1;
        if (lineStr.empty()) continue;
        if (std::sscanf(lineStr.c_str(), "D %lld %f", &bodyId, &value) == 2) {
            if (freshBodies.size() < MAX_OVERRIDES)
                freshBodies[static_cast<int64_t>(bodyId)] = std::min(value, 0.0f);
            continue;
        }
        if (std::sscanf(lineStr.c_str(), "B %d %d %f", &wx, &wz, &value) == 3) {
            if (freshBank.size() < MAX_OVERRIDES)
                freshBank[packColumnKey(wx, wz)] = std::min(value, BANK_CAP_PER_COLUMN);
            continue;
        }
        if (std::sscanf(lineStr.c_str(), "%d %d %f", &wx, &wz, &value) != 3) return false;
        if (fresh.size() >= MAX_OVERRIDES) break;
        fresh[packColumnKey(wx, wz)] = value;
    }
    m_overrides = std::move(fresh);
    m_outflowBank = std::move(freshBank);
    m_bodyDeltas = std::move(freshBodies);
    m_oceanDirty = true;   // reseed anything in-window on the next update
    return true;
}

void WaterManager::applySprings() {
    for (const Spring& s : m_springs) {
        int lx = s.cell.x - m_origin.x, ly = s.cell.y - m_origin.y, lz = s.cell.z - m_origin.z;
        if (m_sim.inBounds(lx, ly, lz)) m_sim.setSource(lx, ly, lz, s.mass);
    }
}

void WaterManager::addSpring(const glm::vec3& worldPos, float mass) {
    glm::ivec3 cell(static_cast<int>(std::floor(worldPos.x)),
                    static_cast<int>(std::floor(worldPos.y)),
                    static_cast<int>(std::floor(worldPos.z)));
    m_springs.push_back({cell, mass});
    int lx = cell.x - m_origin.x, ly = cell.y - m_origin.y, lz = cell.z - m_origin.z;
    if (m_sim.inBounds(lx, ly, lz)) m_sim.setSource(lx, ly, lz, mass);
    rebuildSurface();
}

void WaterManager::setChannelWorld(int worldX, int worldY, int worldZ, bool channel) {
    int lx = worldX - m_origin.x, ly = worldY - m_origin.y, lz = worldZ - m_origin.z;
    if (!m_sim.inBounds(lx, ly, lz)) return;
    m_sim.setChannel(lx, ly, lz, channel);
    if (channel) m_channelCells.emplace_back(worldX, worldY, worldZ);
}

void WaterManager::setChannelRegion(const glm::ivec3& a, const glm::ivec3& b) {
    glm::ivec3 lo(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z));
    glm::ivec3 hi(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z));
    for (int z = lo.z; z <= hi.z; ++z)
        for (int y = lo.y; y <= hi.y; ++y)
            for (int x = lo.x; x <= hi.x; ++x)
                setChannelWorld(x, y, z, true);
}

void WaterManager::clearSprings() {
    for (const Spring& s : m_springs) {
        int lx = s.cell.x - m_origin.x, ly = s.cell.y - m_origin.y, lz = s.cell.z - m_origin.z;
        if (m_sim.inBounds(lx, ly, lz)) m_sim.clearSource(lx, ly, lz);
    }
    m_springs.clear();
    rebuildSurface();
}

float WaterManager::massAtWorld(const glm::vec3& worldPos) const {
    int lx, ly, lz;
    if (!worldToLocal(worldPos, lx, ly, lz)) return 0.0f;
    return m_sim.massAt(lx, ly, lz);
}

WaterManager::WaterSample WaterManager::sampleWater(const glm::vec3& worldPos) const {
    WaterSample s;
    int lx, ly, lz;
    if (worldToLocal(worldPos, lx, ly, lz)) {
        // Inside the region: the SIM is authoritative — including "dry" (a sealed sub-sea cavity
        // must not read wet just because it sits below sea level; connectivity decided that).
        const float m = m_sim.massAt(lx, ly, lz);
        if (m <= 0.0f) return s;
        // Surface of THIS cell, floor-aware: water rests ON a sub-voxel platform, so its surface
        // is floor + fill·(1−floor) up the cell (same formula the renderer uses).
        const float cellBase = static_cast<float>(m_origin.y + ly);
        const float f = m_sim.floorAt(lx, ly, lz);
        float surface = cellBase + f + std::min(m, 1.0f) * (1.0f - f);
        // A full cell may carry more water stacked above — walk up while the column stays wet so
        // depth reflects the whole body, not one cell.
        if (m >= 0.999f) {
            for (int y = ly + 1; y < m_dims.y; ++y) {
                const float above = m_sim.massAt(lx, y, lz);
                if (above <= 0.0f) break;
                surface = static_cast<float>(m_origin.y + y) + std::min(above, 1.0f);
                if (above < 0.999f) break;
            }
        }
        s.surfaceY = surface;
        s.depthBelow = std::max(0.0f, surface - worldPos.y);
        s.inWater = s.depthBelow > 0.0f;
        const glm::vec2 fl = m_sim.flowAt(lx, ly, lz);
        s.flow = fl;
        return s;
    }
    // Outside the region: the baked table knows every body's level; else the implicit sea.
    float level = TABLE_DRY;
    if (m_tableFn) level = m_tableFn(worldPos.x, worldPos.z);
    else if (m_implicitSea) level = m_seaLevel;
    // Finite bodies (Phase C, F4): the table reports the BAKE level forever, but a scooped pond
    // is genuinely lower — honor the body delta so a drained pond reads drained (or dry) from
    // outside the window too (NPC buoyancy's slept re-check, fog, wading all come through here).
    if (level > TABLE_DRY && m_bodyFn) {
        const BodyInfo bi = m_bodyFn(worldPos.x, worldPos.z);
        if (bi.id >= 0 && bi.finite) level = bi.baselineLevel + bodyDelta(bi.id);
    }
    if (level > TABLE_DRY && worldPos.y < level) {
        s.inWater = true;
        s.surfaceY = level;
        s.depthBelow = level - worldPos.y;
    }
    return s;
}

float WaterManager::submergedFraction(const glm::vec3& aabbMin, const glm::vec3& aabbMax) const {
    const float height = std::max(aabbMax.y - aabbMin.y, 1e-4f);
    const glm::vec2 c(0.5f * (aabbMin.x + aabbMax.x), 0.5f * (aabbMin.z + aabbMax.z));
    const glm::vec2 corners[5] = {
        c, {aabbMin.x, aabbMin.z}, {aabbMax.x, aabbMin.z}, {aabbMin.x, aabbMax.z}, {aabbMax.x, aabbMax.z}};
    float sum = 0.0f;
    for (const glm::vec2& p : corners) {
        // Sample just above the box floor so the column lookup lands inside the body the box
        // stands in (sampling at aabbMin.y exactly can hit the solid below).
        const WaterSample s = sampleWater(glm::vec3(p.x, aabbMin.y + 0.05f, p.y));
        if (s.inWater)
            sum += glm::clamp((s.surfaceY - aabbMin.y) / height, 0.0f, 1.0f);
    }
    return sum / 5.0f;
}

// ---- GPU backend ----

void WaterManager::enableGpu(Vulkan::VulkanDevice* device) {
    if (m_gpuReady || !device) return;
    // Parity gap, stated loudly: water_flow.comp implements only the base gravity/level/pressure
    // rules — it has NO momentum bias, NO flow proxy, NO sub-voxel floors, and NO MIN_HOLD donor
    // gate. On the GPU path shallow water sheets and creek pins would spread (the exact defects
    // the CPU rules fix). Fine for perf experiments; not behaviorally equivalent.
    LOG_WARN("WaterManager", "GPU water backend enabled: compute step lacks momentum/flow-proxy/"
             "sub-voxel floors/MIN_HOLD — shallow-water behavior differs from the CPU step");
    m_vk = device;
    VkDevice dev = device->getDevice();
    VkPhysicalDevice phys = device->getPhysicalDevice();
    const int n = m_sim.cellCount();
    const VkDeviceSize fbytes = VkDeviceSize(n) * sizeof(float);
    const VkDeviceSize ubytes = VkDeviceSize(n) * sizeof(uint32_t);

    bool ok = makeHostBuffer(dev, phys, fbytes, m_bufMassIn,  m_memMassIn,  m_mapMassIn)
           && makeHostBuffer(dev, phys, fbytes, m_bufMassOut, m_memMassOut, m_mapMassOut)
           && makeHostBuffer(dev, phys, ubytes, m_bufSolid,   m_memSolid,   m_mapSolid)
           && makeHostBuffer(dev, phys, fbytes, m_bufSource,  m_memSource,  m_mapSource)
           && makeHostBuffer(dev, phys, ubytes, m_bufChannel, m_memChannel, m_mapChannel);
    if (!ok) { LOG_ERROR("WaterManager", "GPU water buffer alloc failed; staying on CPU"); return; }

    std::string spv = AssetManager::instance().resolveShader("water_flow.comp.spv");
    if (!m_flowPipe.create(dev, spv, 5, sizeof(FlowPC))) {
        LOG_ERROR("WaterManager", "water_flow pipeline create failed; staying on CPU");
        return;
    }
    m_flowPipe.bindBuffer(0, m_bufMassIn,  fbytes);
    m_flowPipe.bindBuffer(1, m_bufMassOut, fbytes);
    m_flowPipe.bindBuffer(2, m_bufSolid,   ubytes);
    m_flowPipe.bindBuffer(3, m_bufSource,  fbytes);
    m_flowPipe.bindBuffer(4, m_bufChannel, ubytes);
    m_flowPipe.updateDescriptors();
    m_gpuReady = true;
    LOG_INFO("WaterManager", "GPU water flow ready ({} cells)", n);
}

void WaterManager::uploadMasks() {
    const int n = m_sim.cellCount();
    const auto& solid = m_sim.solidMask();   // uint8
    const auto& chan  = m_sim.channelMask(); // uint8
    uint32_t* gs = static_cast<uint32_t*>(m_mapSolid);
    uint32_t* gc = static_cast<uint32_t*>(m_mapChannel);
    for (int i = 0; i < n; ++i) { gs[i] = solid[i] ? 1u : 0u; gc[i] = chan[i] ? 1u : 0u; }
    std::memcpy(m_mapSource, m_sim.sourceMask().data(), VkDeviceSize(n) * sizeof(float));
}

void WaterManager::stepGpu() {
    const int n = m_sim.cellCount();
    // Upload current field + masks (masks round-trip each step for prototype simplicity).
    std::memcpy(m_mapMassIn, m_sim.mass().data(), VkDeviceSize(n) * sizeof(float));
    uploadMasks();

    FlowPC pc{};
    pc.sx = m_dims.x; pc.sy = m_dims.y; pc.sz = m_dims.z;
    pc.evapEnabled   = m_sim.evaporationOn() ? 1u : 0u;
    pc.evapThreshold = WaterSimulation::EVAP_THRESHOLD;
    pc.evapRate      = WaterSimulation::EVAP_RATE;

    VkCommandBuffer cmd = m_vk->beginSingleTimeCommands();
    m_flowPipe.bind(cmd);
    m_flowPipe.pushConstants(cmd, &pc, sizeof(pc));
    m_flowPipe.dispatch(cmd, (uint32_t(n) + 63u) / 64u);
    VkMemoryBarrier mb{};
    mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 1, &mb, 0, nullptr, 0, nullptr);
    m_vk->endSingleTimeCommands(cmd); // submit + wait

    // Read the new field back into the CPU mirror; re-pin sources so the ocean surface
    // reads full (the shader already used the source mask, so this is just for rendering).
    std::memcpy(m_sim.mass().data(), m_mapMassOut, VkDeviceSize(n) * sizeof(float));
    const auto& src = m_sim.sourceMask();
    auto& mass = m_sim.mass();
    for (int i = 0; i < n; ++i) if (src[i] >= 0.0f) mass[i] = src[i];
}

} // namespace Core
} // namespace Phyxel
