#include "core/WaterManager.h"
#include "core/ChunkManager.h"
#include "core/AssetManager.h"
#include "vulkan/VulkanDevice.h"
#include "utils/Logger.h"
#include <algorithm>
#include <cmath>
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
        m_sim.setSolid(x, y, z, m_cm->hasVoxelAt(world));
    }
}

void WaterManager::update(float dt) {
    if (m_oceanDirty) rebuildOcean(); // re-flood once before stepping
    m_accum += std::min(dt, 0.25f);
    int steps = 0;
    while (m_accum >= STEP_DT && steps < MAX_STEPS_PER_UPDATE) {
        if (m_useGpu) stepGpu(); else m_sim.step();
        m_accum -= STEP_DT;
        ++steps;
    }
    if (steps == MAX_STEPS_PER_UPDATE) m_accum = 0.0f; // drop backlog after a stall
    if (steps > 0) rebuildSurface();
}

void WaterManager::recenter(const glm::ivec3& newOrigin) {
    const glm::ivec3 delta = newOrigin - m_origin;
    if (delta == glm::ivec3(0)) return;
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
    const int cx = m_origin.x + m_dims.x / 2;   // current box centre (world), XZ only
    const int cz = m_origin.z + m_dims.z / 2;
    const int fx = static_cast<int>(std::floor(focusWorld.x));
    const int fz = static_cast<int>(std::floor(focusWorld.z));
    if (std::abs(fx - cx) <= hysteresisCells && std::abs(fz - cz) <= hysteresisCells)
        return false;                            // still inside the dead zone → don't thrash
    // Re-centre the box on the focus horizontally; keep the Y origin (ground/sea band).
    recenter(glm::ivec3(fx - m_dims.x / 2, m_origin.y, fz - m_dims.z / 2));
    return true;
}

void WaterManager::rebuildSurface() {
    m_surface.clear();
    m_waterfalls.clear();
    const int sx = m_dims.x, sy = m_dims.y, sz = m_dims.z;
    auto colIdx = [sx](int x, int z) { return x + sx * z; };

    // Topmost surface height (world Y) per column, for corner smoothing. -inf = dry.
    std::vector<float> colTop(static_cast<size_t>(sx) * sz, -1e9f);

    // Pass 1: locate surface cells, record column depth, track the topmost per column.
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
        float surfaceY = static_cast<float>(m_origin.y + y) + fill;
        // Column depth: contiguous water cells stacked below this surface cell.
        float depth = fill;
        for (int dy = y - 1; dy >= 0; --dy) {
            float md = m_sim.massAt(x, dy, z);
            if (md <= RENDER_MIN || m_sim.isSolid(x, dy, z)) break;
            depth += std::min(md, 1.0f);
        }
        cells.push_back({x, y, z, surfaceY, depth});
        float& top = colTop[colIdx(x, z)];
        if (surfaceY > top) top = surfaceY;
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
        out.skirt = glm::vec4(edge(c.x + 1, c.z, std::min(cPN, cPP), wX + 1.0f, wZ + 0.5f),  // +x
                              edge(c.x - 1, c.z, std::min(cNN, cNP), wX + 0.0f, wZ + 0.5f),  // -x
                              edge(c.x, c.z + 1, std::min(cNP, cPP), wX + 0.5f, wZ + 1.0f),  // +z
                              edge(c.x, c.z - 1, std::min(cNN, cPN), wX + 0.5f, wZ + 0.0f)); // -z
        m_surface.push_back(out);
    }
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

void WaterManager::setSolidWorld(int worldX, int worldY, int worldZ, bool solid) {
    int lx = worldX - m_origin.x, ly = worldY - m_origin.y, lz = worldZ - m_origin.z;
    if (m_sim.inBounds(lx, ly, lz)) {
        m_sim.setSolid(lx, ly, lz, solid);
        // Terrain changed: re-flood the ocean so breaches fill / dug seabed refills.
        if (!m_oceanSeeds.empty() || m_oceanBoundary) m_oceanDirty = true;
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

void WaterManager::rebuildOcean() {
    m_oceanDirty = false;
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
    rebuildSurface();
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

// ---- GPU backend ----

void WaterManager::enableGpu(Vulkan::VulkanDevice* device) {
    if (m_gpuReady || !device) return;
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
