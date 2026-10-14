// WaterCoreGpu — docs/WaterCore.md §15.11, slice 3 (fills, device-local, one submission per call); §26.6 P2a (bricks).
#include "core/water/WaterCoreGpu.h"
#include "core/water/WaterSurfaceMesh.h"   // SurfaceColumn (the surface buffer layout)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Phyxel { namespace Core { namespace Water {

namespace {
const char* kKernelFiles[WaterCoreGpu::KernelCount] = {
    "wc_fill_advect.comp.spv", "wc_vel_advect.comp.spv", "wc_vel_advect.comp.spv", "wc_vel_advect.comp.spv",
    "wc_face_ops.comp.spv", "wc_face_ops.comp.spv", "wc_face_ops.comp.spv",
    "wc_column_ops.comp.spv", "wc_classify.comp.spv", "wc_rbgs.comp.spv",
    "wc_extrap.comp.spv", "wc_extrap.comp.spv", "wc_extrap.comp.spv", "wc_reduce.comp.spv", "wc_sources.comp.spv", "wc_surface.comp.spv",
    "wc_bricks.comp.spv", "wc_flip_sort.comp.spv", "wc_flip_p2g.comp.spv", "wc_flip_g2p.comp.spv"};
// every non-FLIP kernel ends with the Bricks buffer (26.6: water_core.glsl's WC_BRICKS_BINDING); FillAdvect 8 -> 9 with it
// (26.5's outflow scratch at 7); Surface = f, occ, surf, u, w, solid (+ bricks); 20 (M1): + solid on FillAdvect, FaceOps,
// ColumnOps, Classify, Surface
const uint32_t kKernelBindings[WaterCoreGpu::KernelCount] = {9, 5, 5, 5, 7, 7, 7, 10, 12, 6, 10, 10, 10, 11, 5, 7, 6, 7, 6, 9};
constexpr int kOutSlots = 8;   // vec4 slots: 0 cells (ke, maxDeltaF, mass), 1-3 max speed per lattice, 4 residual max, 5 residue sum
constexpr int kB = 8;          // 26.6: cells per brick edge
int floorMod8(int v) { return ((v % kB) + kB) % kB; }
} // namespace

WaterCoreGpu::~WaterCoreGpu() { shutdown(); }

bool WaterCoreGpu::init(VkDevice device, VkPhysicalDevice physical, VkQueue queue, uint32_t queueFamily,
                        const std::string& shaderDir, std::string* err) {
    m_device = device; m_physical = physical; m_queue = queue; m_queueFamily = queueFamily; m_shaderDir = shaderDir;
    VkCommandPoolCreateInfo pci{}; pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.queueFamilyIndex = queueFamily; pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    if (vkCreateCommandPool(device, &pci, nullptr, &m_pool) != VK_SUCCESS) { if (err) *err = "vkCreateCommandPool failed"; m_pool = VK_NULL_HANDLE; return false; }
    VkFenceCreateInfo fci{}; fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(device, &fci, nullptr, &m_fence) != VK_SUCCESS) { if (err) *err = "vkCreateFence failed"; return false; }
    return true;
}

void WaterCoreGpu::shutdown() {
    if (m_device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(m_device);
    for (Volume* v : std::vector<Volume*>(m_volumes)) destroyVolume(v);
    if (m_fence) vkDestroyFence(m_device, m_fence, nullptr);
    if (m_pool) vkDestroyCommandPool(m_device, m_pool, nullptr);
    m_fence = VK_NULL_HANDLE; m_pool = VK_NULL_HANDLE; m_device = VK_NULL_HANDLE;
}

uint32_t WaterCoreGpu::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(m_physical, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((typeFilter & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
    return UINT32_MAX;
}

bool WaterCoreGpu::createBuffer(Buffer& b, VkDeviceSize bytes, bool hostVisible, std::string* err) {
    b.bytes = std::max<VkDeviceSize>(bytes, 16);
    VkBufferCreateInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = b.bytes; bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bi, nullptr, &b.buf) != VK_SUCCESS) { if (err) *err = "vkCreateBuffer failed for " + std::to_string(b.bytes) + " bytes"; return false; }
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(m_device, b.buf, &mr);
    VkMemoryAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    const VkMemoryPropertyFlags want = hostVisible ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    ai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits, want);
    if (ai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(m_device, &ai, nullptr, &b.mem) != VK_SUCCESS) {
        // the §15.11 refusal: the byte count is in the message, the failure never reaches a dispatch
        if (err) *err = "cannot allocate " + std::to_string(mr.size) + " bytes of " + (hostVisible ? "host-visible" : "device-local") + " memory for a WaterCore GPU volume";
        vkDestroyBuffer(m_device, b.buf, nullptr); b.buf = VK_NULL_HANDLE; return false;
    }
    vkBindBufferMemory(m_device, b.buf, b.mem, 0);
    if (hostVisible) { vkMapMemory(m_device, b.mem, 0, b.bytes, 0, &b.mapped); std::memset(b.mapped, 0, static_cast<size_t>(b.bytes)); }
    return true;
}

void WaterCoreGpu::destroyBuffer(Buffer& b) {
    if (b.mapped) vkUnmapMemory(m_device, b.mem);
    if (b.buf) vkDestroyBuffer(m_device, b.buf, nullptr);
    if (b.mem) vkFreeMemory(m_device, b.mem, nullptr);
    b = Buffer{};
}

bool WaterCoreGpu::createPipelines(Volume& vol, std::string* err) {
    for (int k = 0; k < KernelCount; ++k) {
        if (!vol.particles && k >= FlipSort) break;
        auto pipe = std::make_unique<Vulkan::ComputePipeline>();
        if (!pipe->create(m_device, m_shaderDir + "/" + kKernelFiles[k], kKernelBindings[k], sizeof(WcPush), k == FlipP2g ? 3u : 1u)) {
            if (err) *err = std::string("failed to create pipeline ") + kKernelFiles[k] + " from " + m_shaderDir; return false;
        }
        vol.pipes[k] = std::move(pipe);
    }
    auto bindAll = [](Vulkan::ComputePipeline& p, std::initializer_list<const Buffer*> bufs) {
        uint32_t i = 0; for (const Buffer* b : bufs) { p.bindBuffer(i++, b->buf, b->bytes); } p.updateDescriptors();
    };
    const Buffer* br = &vol.bricks;
    bindAll(*vol.pipes[FillAdvect], {&vol.f, &vol.fOrig, &vol.u, &vol.v, &vol.w, &vol.occ, &vol.solid, &vol.res, br});   // res: 26.5's scratch (the projection rewrites it later in the substep)
    bindAll(*vol.pipes[VelAdvectU], {&vol.uOld, &vol.vOld, &vol.wOld, &vol.u, br});
    bindAll(*vol.pipes[VelAdvectV], {&vol.uOld, &vol.vOld, &vol.wOld, &vol.v, br});
    bindAll(*vol.pipes[VelAdvectW], {&vol.uOld, &vol.vOld, &vol.wOld, &vol.w, br});
    bindAll(*vol.pipes[FaceOpsU],   {&vol.f, &vol.occ, &vol.u, &vol.p, &vol.liq, &vol.solid, br});
    bindAll(*vol.pipes[FaceOpsV],   {&vol.f, &vol.occ, &vol.v, &vol.p, &vol.liq, &vol.solid, br});
    bindAll(*vol.pipes[FaceOpsW],   {&vol.f, &vol.occ, &vol.w, &vol.p, &vol.liq, &vol.solid, br});
    bindAll(*vol.pipes[ColumnOps],  {&vol.f, &vol.occ, &vol.u, &vol.v, &vol.w, &vol.dropped, &vol.solid, &vol.births, &vol.deposit, br});   // 21: + births, deposit
    bindAll(*vol.pipes[Surface],    {&vol.f, &vol.occ, &vol.surf, &vol.u, &vol.w, &vol.solid, br});   // Phase F (+ u, w: E2 surface velocity; + solid: 20)
    bindAll(*vol.pipes[Classify],   {&vol.f, &vol.occ, &vol.u, &vol.v, &vol.w, &vol.src, &vol.liq, &vol.diag, &vol.rhs, &vol.p, &vol.solid, br});
    bindAll(*vol.pipes[Rbgs],       {&vol.liq, &vol.diag, &vol.rhs, &vol.p, &vol.res, br});
    // binding 8 = the lattice before the halo pass: a dedicated scratch, because uOld/vOld/wOld are
    // the FLIP delta base and overwriting them here left the particles with a ~zero delta (they
    // crawled at 0.1 m/s under a 2 m/s grid, 2026-10-08)
    bindAll(*vol.pipes[ExtrapU], {&vol.f, &vol.occ, &vol.valA, &vol.knownA, &vol.valB, &vol.knownB, &vol.first, &vol.keep, &vol.origLat, br});
    bindAll(*vol.pipes[ExtrapV], {&vol.f, &vol.occ, &vol.valA, &vol.knownA, &vol.valB, &vol.knownB, &vol.first, &vol.keep, &vol.origLat, br});
    bindAll(*vol.pipes[ExtrapW], {&vol.f, &vol.occ, &vol.valA, &vol.knownA, &vol.valB, &vol.knownB, &vol.first, &vol.keep, &vol.origLat, br});
    bindAll(*vol.pipes[Reduce],  {&vol.f, &vol.fPrev, &vol.u, &vol.v, &vol.w, &vol.part, &vol.out, &vol.part2, &vol.res, &vol.dropped, br});
    bindAll(*vol.pipes[Sources], {&vol.f, &vol.occ, &vol.src, &vol.sources, br});
    bindAll(*vol.pipes[Bricks],  {&vol.f, &vol.u, &vol.v, &vol.w, &vol.p, br});
    if (vol.particles) {
        bindAll(*vol.pipes[FlipSort], {&vol.posA, &vol.velA, &vol.posB, &vol.velB, &vol.pcount, &vol.pstart, &vol.pcursor});
        // p2g gathers from the SORTED list (B); it writes f and ONE lattice per dispatch (rebound per lattice below)
        {   // p2g: three descriptor sets, one per output lattice (binding 5)
            Vulkan::ComputePipeline& p = *vol.pipes[FlipP2g];
            const Buffer* lat[3] = {&vol.u, &vol.v, &vol.w};
            for (uint32_t set = 0; set < 3; ++set) {
                p.bindBufferInSet(set, 0, vol.posB.buf, vol.posB.bytes); p.bindBufferInSet(set, 1, vol.velB.buf, vol.velB.bytes);
                p.bindBufferInSet(set, 2, vol.pstart.buf, vol.pstart.bytes); p.bindBufferInSet(set, 3, vol.pcount.buf, vol.pcount.bytes);
                p.bindBufferInSet(set, 4, vol.f.buf, vol.f.bytes); p.bindBufferInSet(set, 5, lat[set]->buf, lat[set]->bytes);
            }
            p.updateDescriptors();
        }
        bindAll(*vol.pipes[FlipG2p], {&vol.posB, &vol.velB, &vol.u, &vol.v, &vol.w, &vol.uOld, &vol.vOld, &vol.wOld, &vol.occ});
    }
    return true;
}

WaterCoreGpu::Volume* WaterCoreGpu::createVolume(const WaterGrid& g, std::string* err, const BrickOptions& bricks) {
    Volume* vol = createVolume(g.spec(), err, false, 0, bricks);
    if (!vol) return nullptr;
    upload(*vol, g);
    if (vol->unstoredM3 > 0.0) {   // 26.6: a capacity smaller than the water itself is refused, never a silent drop
        if (err) *err = "the brick capacity (" + std::to_string(vol->capacity - 1) + ") cannot hold this water: "
                        + std::to_string(vol->unstoredM3) + " m^3 would not be stored";
        destroyVolume(vol);
        return nullptr;
    }
    return vol;
}

WaterCoreGpu::Volume* WaterCoreGpu::createVolume(const GridSpec& spec, std::string* err, bool particles, size_t particleCapacity, const BrickOptions& opts) {
    if (!ready()) { if (err) *err = "WaterCoreGpu not initialised"; return nullptr; }
    auto vol = std::make_unique<Volume>();
    vol->spec = spec;
    vol->particles = particles;
    const size_t nx = static_cast<size_t>(spec.dims.x), ny = static_cast<size_t>(spec.dims.y), nz = static_cast<size_t>(spec.dims.z);
    vol->dCells = nx * ny * nz; vol->dNu = (nx + 1) * ny * nz; vol->dNv = nx * (ny + 1) * nz; vol->dNw = nx * ny * (nz + 1);
    vol->dColumns = nx * nz;
    vol->bricked = !particles;   // 26.6: the FLIP kernels index the dense grid; every fill volume is bricked
    if (particles && opts.sparse) { if (err) *err = "a particle (FLIP) volume cannot be sparse (26.6: the particle transport stays dense)"; return nullptr; }
    if (vol->bricked) {
        vol->sparse = opts.sparse; vol->freezeForTest = opts.freezeForTest;
        vol->o = glm::ivec3(floorMod8(spec.origin.x), floorMod8(spec.origin.y), floorMod8(spec.origin.z));
        // the brick box covers every cell: q = local + o in [0, n + o). A box's upper wall faces (lattice index n) lie in the
        // next brick when n + o is a multiple of 8 - absent, so they read 0, which a wall face always is (the dense grid
        // forced them to 0 every substep). An extra layer of bricks to hold them stepped 27 bricks for a 16^3 box (13,824
        // cells for 4,096) - measured 2026-10-13.
        vol->bn = (spec.dims + vol->o + glm::ivec3(kB - 1)) / kB;
        vol->mapCount = static_cast<size_t>(vol->bn.x) * vol->bn.y * vol->bn.z;
        if (vol->mapCount > kMaxMapEntries || vol->bn.x > 1023 || vol->bn.y > 1023 || vol->bn.z > 1023) {
            // the 15.11 refusal shape: the reason AND the byte count (the brick map alone, and every brick of the box stored)
            const unsigned long long mapBytes = static_cast<unsigned long long>(vol->mapCount) * 4ull;
            const unsigned long long fullBytes = static_cast<unsigned long long>(vol->mapCount) * 512ull * 100ull;   // ~25 floats a cell
            if (err) *err = "a bricked WaterCore volume's brick box holds at most " + std::to_string(kMaxMapEntries) + " bricks and 1023 a side ("
                            + std::to_string(vol->bn.x) + " x " + std::to_string(vol->bn.y) + " x " + std::to_string(vol->bn.z) + " requested: a "
                            + std::to_string(mapBytes) + " bytes map, " + std::to_string(fullBytes) + " bytes with every brick stored; world scale is 26.3 P5)";
            return nullptr;
        }
        vol->capacity = 1 + (opts.capacity > 0 ? std::min(opts.capacity, vol->mapCount) : vol->mapCount);   // slot 0 = the absent brick
        vol->colCap = static_cast<size_t>(vol->bn.x) * vol->bn.z;
        vol->cells = vol->capacity * 512; vol->nu = vol->nv = vol->nw = vol->cells;
        vol->columns = vol->colCap * 64;
        vol->slotTableOff = vol->mapCount; vol->colTableOff = vol->slotTableOff + vol->capacity;
        vol->counterOff = vol->colTableOff + vol->colCap; vol->infoOff = vol->counterOff + 2;
        vol->brickInts = vol->infoOff + 2 * vol->capacity;   // two water records per slot (any water, liquid)
        vol->map.assign(vol->mapCount, 0); vol->slotBrick.assign(vol->capacity, -1); vol->colEntries.clear();
    } else {
        vol->cells = vol->dCells; vol->nu = vol->dNu; vol->nv = vol->dNv; vol->nw = vol->dNw; vol->columns = vol->dColumns;
        vol->brickInts = 4;
    }
    vol->latticeMax = std::max({vol->nu, vol->nv, vol->nw});
    vol->groups = std::max((std::max({vol->cells, vol->latticeMax, vol->columns}) + 255) / 256, 2 * vol->mapCount) + 1;
    const VkDeviceSize F4 = sizeof(float);
    bool ok = createBuffer(vol->f, vol->cells * F4, false, err) && createBuffer(vol->fOrig, vol->cells * F4, false, err) && createBuffer(vol->fPrev, vol->cells * F4, false, err)
           && createBuffer(vol->u, vol->nu * F4, false, err) && createBuffer(vol->v, vol->nv * F4, false, err) && createBuffer(vol->w, vol->nw * F4, false, err)
           && createBuffer(vol->uOld, vol->nu * F4, false, err) && createBuffer(vol->vOld, vol->nv * F4, false, err) && createBuffer(vol->wOld, vol->nw * F4, false, err)
           && createBuffer(vol->occ, vol->cells * 4, false, err) && createBuffer(vol->src, vol->cells * F4, false, err) && createBuffer(vol->liq, vol->cells * 4, false, err)
           && createBuffer(vol->diag, vol->cells * F4, false, err) && createBuffer(vol->rhs, vol->cells * F4, false, err) && createBuffer(vol->p, vol->cells * F4, false, err) && createBuffer(vol->res, vol->cells * F4, false, err)
           && createBuffer(vol->valA, vol->latticeMax * F4, false, err) && createBuffer(vol->valB, vol->latticeMax * F4, false, err)
           && createBuffer(vol->knownA, vol->latticeMax * 4, false, err) && createBuffer(vol->knownB, vol->latticeMax * 4, false, err)
           && createBuffer(vol->first, vol->latticeMax * 4, false, err) && createBuffer(vol->keep, vol->latticeMax * 4, false, err)
           && createBuffer(vol->origLat, vol->latticeMax * F4, false, err)
           && createBuffer(vol->dropped, vol->columns * F4, false, err) && createBuffer(vol->part, vol->groups * 16, false, err) && createBuffer(vol->part2, vol->groups * 16, false, err)
           && createBuffer(vol->out, kOutSlots * 16, false, err) && createBuffer(vol->sources, kMaxSources * sizeof(GpuSource), false, err)
           && createBuffer(vol->surf, vol->columns * sizeof(Core::Water::SurfaceColumn), false, err)    // Phase F: the surface field (G2: 64 B)
           && createBuffer(vol->solid, vol->cells * 16, false, err)                                     // 20: (s, q, wake, ledger) per cell
           && createBuffer(vol->births, vol->columns * 32, false, err) && createBuffer(vol->deposit, vol->cells * F4, false, err)   // 21: droplets
           && createBuffer(vol->bricks, vol->brickInts * 4, false, err);                                // 26.6
    // staging: [f | u | v | w | occ | src | out | p | sources | surf | solid | births | deposit | bricks | particles]
    vol->offU = vol->cells * F4; vol->offV = vol->offU + vol->nu * F4; vol->offW = vol->offV + vol->nv * F4;
    vol->offOcc = vol->offW + vol->nw * F4; vol->offSrc = vol->offOcc + vol->cells * 4; vol->offOut = vol->offSrc + vol->cells * F4;
    vol->offP = vol->offOut + kOutSlots * 16; vol->offSources = vol->offP + vol->cells * F4;
    vol->offSurf = vol->offSources + kMaxSources * sizeof(GpuSource);
    vol->offSolid = vol->offSurf + vol->columns * sizeof(Core::Water::SurfaceColumn);
    vol->offBirths = vol->offSolid + vol->cells * 16;
    vol->offDeposit = vol->offBirths + vol->columns * 32;
    vol->offBricks = vol->offDeposit + vol->cells * F4;
    vol->offParticles = vol->offBricks + vol->brickInts * 4;
    VkDeviceSize particleBytes = 0;
    if (particles) {
        vol->particleCapacity = particleCapacity > 0 ? particleCapacity : std::min<size_t>(FlipTransport::kMaxParticlesPerVolume, vol->cells * FlipTransport::kParticlesPerCell + 4096);
        particleBytes = vol->particleCapacity * 32;
        ok = ok && createBuffer(vol->posA, vol->particleCapacity * 16, false, err) && createBuffer(vol->velA, vol->particleCapacity * 16, false, err)
                && createBuffer(vol->posB, vol->particleCapacity * 16, false, err) && createBuffer(vol->velB, vol->particleCapacity * 16, false, err)
                && createBuffer(vol->pcount, vol->cells * 4, false, err) && createBuffer(vol->pstart, vol->cells * 4, false, err) && createBuffer(vol->pcursor, vol->cells * 4, false, err);
    }
    ok = ok && createBuffer(vol->staging, vol->offParticles + particleBytes, true, err);
    if (!ok || !createPipelines(*vol, err)) { Volume* raw = vol.release(); destroyVolume(raw); return nullptr; }
    {   // every buffer starts all zero: no body, every rule exactly as before; 26.6: the absent brick (slot 0) reads as dry
        // air in EVERY buffer, scratch included (the extrapolation reads its neighbours' known flags there)
        VkCommandBuffer cmd = beginCommands();
        for (Buffer* b : {&vol->f, &vol->fOrig, &vol->fPrev, &vol->u, &vol->v, &vol->w, &vol->uOld, &vol->vOld, &vol->wOld, &vol->occ, &vol->src, &vol->liq,
                          &vol->diag, &vol->rhs, &vol->p, &vol->res, &vol->valA, &vol->valB, &vol->knownA, &vol->knownB, &vol->first, &vol->keep,
                          &vol->origLat, &vol->dropped, &vol->surf, &vol->solid, &vol->births, &vol->deposit, &vol->bricks})
            vkCmdFillBuffer(cmd, b->buf, 0, b->bytes, 0);
        submitAndWait(cmd);
    }
    if (vol->bricked && !vol->sparse) {   // dense: every brick of the box is kept from the start
        std::vector<char> all(vol->mapCount, 1);
        applyResidency(*vol, all);
        writeTablesToStaging(*vol);
        VkCommandBuffer cmd = beginCommands();
        copy(cmd, vol->staging, vol->bricks, vol->counterOff * 4, vol->offBricks);
        submitAndWait(cmd);
        vol->lastAllocated = 0;
    }
    Volume* raw = vol.release();
    m_volumes.push_back(raw);
    return raw;
}

void WaterCoreGpu::destroyVolume(Volume* vol) {
    if (!vol) return;
    vkDeviceWaitIdle(m_device);
    for (auto& p : vol->pipes) if (p) { p->cleanup(); p.reset(); }
    for (Buffer* b : {&vol->f, &vol->fOrig, &vol->fPrev, &vol->u, &vol->v, &vol->w, &vol->uOld, &vol->vOld, &vol->wOld, &vol->occ, &vol->src, &vol->liq,
                      &vol->diag, &vol->rhs, &vol->p, &vol->res, &vol->valA, &vol->valB, &vol->knownA, &vol->knownB, &vol->first, &vol->keep, &vol->dropped, &vol->part, &vol->part2, &vol->out, &vol->origLat, &vol->sources, &vol->surf, &vol->solid, &vol->births, &vol->deposit, &vol->bricks, &vol->posA, &vol->velA, &vol->posB, &vol->velB, &vol->pcount, &vol->pstart, &vol->pcursor, &vol->staging})
        destroyBuffer(*b);
    m_volumes.erase(std::remove(m_volumes.begin(), m_volumes.end(), vol), m_volumes.end());
    delete vol;
}

VkCommandBuffer WaterCoreGpu::beginCommands() const {
    VkCommandBufferAllocateInfo cai{}; cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = m_pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
    VkCommandBuffer cmd; vkAllocateCommandBuffers(m_device, &cai, &cmd);
    VkCommandBufferBeginInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

void WaterCoreGpu::submitAndWait(VkCommandBuffer cmd) const {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    vkResetFences(m_device, 1, &m_fence);
    const VkResult sub = vkQueueSubmit(m_queue, 1, &si, m_fence);
    const VkResult wait = vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX);
    if (sub != VK_SUCCESS || wait != VK_SUCCESS) std::fprintf(stderr, "[WaterCoreGpu] submit %d wait %d (VK_ERROR_DEVICE_LOST = -4)\n", static_cast<int>(sub), static_cast<int>(wait));
    vkFreeCommandBuffers(m_device, m_pool, 1, &cmd);
}

void WaterCoreGpu::copy(VkCommandBuffer cmd, const Buffer& from, const Buffer& to, VkDeviceSize bytes, VkDeviceSize srcOff, VkDeviceSize dstOff) const {
    if (bytes == 0) return;
    VkBufferCopy region{}; region.srcOffset = srcOff; region.dstOffset = dstOff; region.size = bytes;
    vkCmdCopyBuffer(cmd, from.buf, to.buf, 1, &region);
}

// ── 26.6 P2a: bricks on the host ─────────────────────────────────────────────────────────────────────────────────────

long WaterCoreGpu::storageIndex(const Volume& vol, int x, int y, int z) const {
    if (!vol.bricked) return static_cast<long>(x + vol.spec.dims.x * (y + vol.spec.dims.y * z));
    const glm::ivec3 q = glm::ivec3(x, y, z) + vol.o;
    if (q.x < 0 || q.y < 0 || q.z < 0) return -1;
    const glm::ivec3 b = q / kB;
    if (b.x >= vol.bn.x || b.y >= vol.bn.y || b.z >= vol.bn.z) return -1;
    const int slot = vol.map[static_cast<size_t>(b.x) + static_cast<size_t>(vol.bn.x) * (b.y + static_cast<size_t>(vol.bn.y) * b.z)];
    if (slot == 0) return -1;
    return static_cast<long>(slot) * 512 + (q.x & 7) + 8 * ((q.y & 7) + 8 * (q.z & 7));
}

long WaterCoreGpu::columnStorage(const Volume& vol, int x, int z) const {
    if (!vol.bricked) return static_cast<long>(x + vol.spec.dims.x * z);
    const int qx = x + vol.o.x, qz = z + vol.o.z;
    if (qx < 0 || qz < 0) return -1;
    const int bx = qx / kB, bz = qz / kB;
    if (bx >= vol.bn.x || bz >= vol.bn.z) return -1;
    const int packed = bx | (bz << 10);
    // the column table is sorted by (bz, bx): binary search
    const auto it = std::lower_bound(vol.colEntries.begin(), vol.colEntries.end(), packed, [](int a, int b) {
        const int az = a >> 10, bz2 = b >> 10; return az != bz2 ? az < bz2 : (a & 1023) < (b & 1023); });
    if (it == vol.colEntries.end() || *it != packed) return -1;
    return static_cast<long>(it - vol.colEntries.begin()) * 64 + (qx & 7) + 8 * (qz & 7);
}

void WaterCoreGpu::fillPushBricks(const Volume& vol, WcPush& push) const {
    push.bricked = vol.bricked ? 1 : 0;
    push.slotsUsed = vol.slotsUsed;
    push.ox = vol.o.x; push.oy = vol.o.y; push.oz = vol.o.z;
    push.bnx = vol.bn.x; push.bny = vol.bn.y; push.bnz = vol.bn.z;
    push.slotTableOff = static_cast<int32_t>(vol.slotTableOff); push.colTableOff = static_cast<int32_t>(vol.colTableOff);
    push.counterOff = static_cast<int32_t>(vol.counterOff); push.colCount = vol.colCount;
}

// A brick is needed when water lies within `reach` cells of it: for each neighbour offset d (within `rings`), the gap along
// an axis is 0 (d = 0) or the water's distance to that face + 1 + 8 (|d| - 1); the neighbour is needed when every axis's gap
// is <= reach (Chebyshev). The per-face distances are minima over the brick's water taken separately per face, so the test
// is conservative: it may keep a brick it did not need, never drop one it did.
void WaterCoreGpu::markNeed(const Volume& vol, std::vector<char>& need, const glm::ivec3& b, const int dist[6], int reach, int rings) const {
    // per axis, the offsets whose gap is within reach (0 always; -k / +k while face + 1 + 8 (k - 1) <= reach and in the box)
    int lo[3], hi[3];
    const int bmax[3] = {vol.bn.x - 1, vol.bn.y - 1, vol.bn.z - 1}, bc[3] = {b.x, b.y, b.z};
    for (int a = 0; a < 3; ++a) {
        lo[a] = 0; hi[a] = 0;
        for (int k = 1; k <= rings; ++k) {
            if (dist[2 * a] + 1 + (k - 1) * kB <= reach && bc[a] - k >= 0) lo[a] = -k;
            if (dist[2 * a + 1] + 1 + (k - 1) * kB <= reach && bc[a] + k <= bmax[a]) hi[a] = k;
        }
    }
    // Chebyshev: a neighbour is needed when EVERY axis's gap is within reach - the box of per-axis ranges
    const size_t sx = static_cast<size_t>(vol.bn.x), sxy = sx * static_cast<size_t>(vol.bn.y);
    for (int dz = lo[2]; dz <= hi[2]; ++dz) for (int dy = lo[1]; dy <= hi[1]; ++dy) {
        const size_t row = static_cast<size_t>(b.x) + sx * static_cast<size_t>(b.y + dy) + sxy * static_cast<size_t>(b.z + dz);
        for (int dx = lo[0]; dx <= hi[0]; ++dx) { char& n = need[row + dx]; if (n < 1) n = 1; }
    }
}

// The bricks of a list of dense cells (pumps), each as a point of water: stored, and their halo.
static void cellToRecord(const glm::ivec3& o, int x, int y, int z, glm::ivec3& b, int dist[6]) {
    const glm::ivec3 q = glm::ivec3(x, y, z) + o, l = q % 8;
    b = q / 8;
    dist[0] = l.x; dist[1] = 7 - l.x; dist[2] = l.y; dist[3] = 7 - l.y; dist[4] = l.z; dist[5] = 7 - l.z;
}

void WaterCoreGpu::needFromDenseFill(const Volume& vol, const std::vector<float>& f, std::vector<char>& need, int reach, int rings) const {
    const glm::ivec3 n = vol.spec.dims;
    std::vector<int> rec(vol.mapCount * 7, -1);   // per map entry: wet flag + 6 distances (-1 = dry)
    for (int z = 0; z < n.z; ++z) for (int y = 0; y < n.y; ++y) for (int x = 0; x < n.x; ++x) {
        const size_t i = static_cast<size_t>(x) + static_cast<size_t>(n.x) * (y + static_cast<size_t>(n.y) * z);
        if (i >= f.size() || f[i] == 0.0f) continue;
        const glm::ivec3 q = glm::ivec3(x, y, z) + vol.o, b = q / kB, l = q % kB;
        int* r = &rec[(static_cast<size_t>(b.x) + static_cast<size_t>(vol.bn.x) * (b.y + static_cast<size_t>(vol.bn.y) * b.z)) * 7];
        if (r[0] < 0) { r[0] = 1; for (int k = 1; k < 7; ++k) r[k] = 7; }
        const int dd[6] = {l.x, 7 - l.x, l.y, 7 - l.y, l.z, 7 - l.z};
        for (int k = 0; k < 6; ++k) r[1 + k] = std::min(r[1 + k], dd[k]);
    }
    for (size_t m = 0; m < vol.mapCount; ++m) {
        const int* r = &rec[m * 7];
        if (r[0] < 0) continue;
        need[m] = 2;   // holds water: stored first
        const glm::ivec3 b(static_cast<int>(m % vol.bn.x), static_cast<int>((m / vol.bn.x) % vol.bn.y), static_cast<int>(m / (static_cast<size_t>(vol.bn.x) * vol.bn.y)));
        markNeed(vol, need, b, r + 1, reach, rings);
    }
}

// After a call: every brick within 4 cells of water (the extrapolation's three layers + a face beside an absent cell) must
// be stored. Faces between a stored cell and an absent brick belong to the absent brick, carry no velocity, and so never
// trip the transport's count - a too-thin halo shows here instead (WaterBrickTest's control).
namespace {
// The halo a cell's water needs (26.6): LIQUID cells (f >= thr) - the extrapolation's three layers + a face beside an absent
// cell = 4 cells; any other water (films, residue) - the rules that read round it reach one cell (settle, compaction, the
// thin-film slope, the transport) + a face = 2 cells. Measured: one reach for both stored a whole brick layer above a
// resting creek for 2.7e-6 of film and churned it every tick (2026-10-13).
constexpr int kReachLiquid = 4, kReachFilm = 2;
}

long WaterCoreGpu::haloShortAfterCall(const Volume& vol) const {
    std::vector<char> need(vol.mapCount, 0);
    const int32_t* info = vol.hostInfo.data();   // staging is uncached: read once into hostInfo after the fence
    for (size_t s = 1; s < static_cast<size_t>(vol.slotsUsed); ++s) {
        const int packed = vol.slotBrick[s];
        if (packed < 0) continue;
        const glm::ivec3 b(packed & 1023, (packed >> 10) & 1023, (packed >> 20) & 1023);
        for (int r = 0; r < 2; ++r) {
            const int rec = info[2 * s + r];
            if (!(rec & 1)) continue;
            int dist[6]; for (int k = 0; k < 6; ++k) dist[k] = (rec >> (1 + 3 * k)) & 7;
            markNeed(vol, need, b, dist, r == 0 ? kReachFilm : kReachLiquid, 1);
        }
    }
    long shortBy = 0;
    for (size_t m = 0; m < vol.mapCount; ++m) if (need[m] && vol.map[m] == 0) ++shortBy;
    return shortBy;
}

std::vector<int> WaterCoreGpu::applyResidency(Volume& vol, const std::vector<char>& need) {
    std::vector<int> fresh;
    int freed = 0; long held = 0;
    for (size_t m = 0; m < vol.mapCount; ++m)
        if (vol.map[m] != 0 && !need[m]) { vol.slotBrick[static_cast<size_t>(vol.map[m])] = -1; vol.map[m] = 0; ++freed; }
    size_t next = 1;
    for (char pass : {char(2), char(1)}) {   // bricks holding water first, then the halo (a capacity refusal never strands water)
        for (size_t m = 0; m < vol.mapCount; ++m) {
            if (need[m] != pass || vol.map[m] != 0) continue;
            while (next < vol.capacity && vol.slotBrick[next] >= 0) ++next;
            if (next >= vol.capacity) { ++held; continue; }   // 26.6 capacity: the brick stays absent, its faces closed (counted)
            const int bx = static_cast<int>(m % vol.bn.x), by = static_cast<int>((m / vol.bn.x) % vol.bn.y), bz = static_cast<int>(m / (static_cast<size_t>(vol.bn.x) * vol.bn.y));
            vol.map[m] = static_cast<int>(next);
            vol.slotBrick[next] = bx | (by << 10) | (bz << 20);
            fresh.push_back(static_cast<int>(next));
        }
    }
    vol.lastAllocated = static_cast<int>(fresh.size()); vol.lastFreed = freed; vol.lastHeld = held;
    vol.storedCount += static_cast<int>(fresh.size()) - freed;
    if (fresh.empty() && freed == 0 && !vol.colEntries.empty()) return fresh;   // the steady state: tables, range and columns unchanged
    int top = 0;
    for (size_t s = 1; s < vol.capacity; ++s) if (vol.slotBrick[s] >= 0) top = static_cast<int>(s);
    vol.slotsUsed = top + 1;
    vol.colEntries.clear();
    for (int bz = 0; bz < vol.bn.z; ++bz) for (int bx = 0; bx < vol.bn.x; ++bx) {
        bool any = false;
        for (int by = 0; by < vol.bn.y && !any; ++by) any = vol.map[static_cast<size_t>(bx) + static_cast<size_t>(vol.bn.x) * (by + static_cast<size_t>(vol.bn.y) * bz)] != 0;
        if (any) vol.colEntries.push_back(bx | (bz << 10));
    }
    vol.colCount = static_cast<int>(vol.colEntries.size());
    if (!fresh.empty() || freed > 0) vol.tablesDirty = true;
    return fresh;
}

void WaterCoreGpu::writeTablesToStaging(Volume& vol) const {
    auto* st = reinterpret_cast<int32_t*>(static_cast<char*>(vol.staging.mapped) + vol.offBricks);
    std::memcpy(st, vol.map.data(), vol.mapCount * 4);
    std::memcpy(st + vol.slotTableOff, vol.slotBrick.data(), vol.capacity * 4);
    std::fill(st + vol.colTableOff, st + vol.colTableOff + vol.colCap, 0);
    if (!vol.colEntries.empty()) std::memcpy(st + vol.colTableOff, vol.colEntries.data(), vol.colEntries.size() * 4);
}

// staging <- the given slots' cells: fills and faces from `g` (when given), occupancy (out-of-box cells are Solid, as the
// dense grid's out-of-bounds), the bodies' fields. Storage indices ARE the staging offsets (regions sized by storage).
void WaterCoreGpu::scatterSlots(Volume& vol, const std::vector<int>& slots, const WaterGrid* g) const {
    char* st = static_cast<char*>(vol.staging.mapped);
    auto* sf = reinterpret_cast<float*>(st); auto* su = reinterpret_cast<float*>(st + vol.offU);
    auto* sv = reinterpret_cast<float*>(st + vol.offV); auto* sw = reinterpret_cast<float*>(st + vol.offW);
    auto* so = reinterpret_cast<uint32_t*>(st + vol.offOcc); auto* ss = reinterpret_cast<float*>(st + vol.offSolid);
    const glm::ivec3 n = vol.spec.dims;
    for (int slot : slots) {
        const int packed = vol.slotBrick[static_cast<size_t>(slot)];
        const glm::ivec3 b(packed & 1023, (packed >> 10) & 1023, (packed >> 20) & 1023);
        for (int l = 0; l < 512; ++l) {
            const glm::ivec3 c = b * kB + glm::ivec3(l & 7, (l >> 3) & 7, l >> 6) - vol.o;
            const size_t i = static_cast<size_t>(slot) * 512 + l;
            const bool in = c.x >= 0 && c.y >= 0 && c.z >= 0 && c.x < n.x && c.y < n.y && c.z < n.z;
            const size_t d = in ? static_cast<size_t>(c.x) + static_cast<size_t>(n.x) * (c.y + static_cast<size_t>(n.y) * c.z) : 0;
            so[i] = in ? (d < vol.hostOcc.size() ? vol.hostOcc[d] : 0u) : 1u;
            for (int k = 0; k < 4; ++k) ss[i * 4 + k] = (in && d * 4 + k < vol.hostSolid.size()) ? vol.hostSolid[d * 4 + k] : 0.0f;
            if (g) {
                WaterGrid& gm = const_cast<WaterGrid&>(*g);
                sf[i] = in ? g->f(c.x, c.y, c.z) : 0.0f;
                const bool cyz = c.y >= 0 && c.y < n.y && c.z >= 0 && c.z < n.z, cxz = c.x >= 0 && c.x < n.x && c.z >= 0 && c.z < n.z, cxy = c.x >= 0 && c.x < n.x && c.y >= 0 && c.y < n.y;
                su[i] = (cyz && c.x >= 0 && c.x <= n.x) ? gm.u(c.x, c.y, c.z) : 0.0f;
                sv[i] = (cxz && c.y >= 0 && c.y <= n.y) ? gm.v(c.x, c.y, c.z) : 0.0f;
                sw[i] = (cxy && c.z >= 0 && c.z <= n.z) ? gm.w(c.x, c.y, c.z) : 0.0f;
            }
        }
    }
}

// New slots: zero every per-cell and per-face buffer over them (a brick is born as dry air), then copy their occupancy and
// bodies from staging. Contiguous slots are merged into one range per buffer.
void WaterCoreGpu::recordSlotUploads(VkCommandBuffer cmd, Volume& vol, const std::vector<int>& slots, bool zero) const {
    std::vector<int> s(slots); std::sort(s.begin(), s.end());
    for (size_t a = 0; a < s.size();) {
        size_t e = a + 1;
        while (e < s.size() && s[e] == s[e - 1] + 1) ++e;
        const VkDeviceSize first = static_cast<VkDeviceSize>(s[a]) * 512, count = static_cast<VkDeviceSize>(e - a) * 512;
        if (zero)
            for (Buffer* b : {&vol.f, &vol.fOrig, &vol.fPrev, &vol.u, &vol.v, &vol.w, &vol.uOld, &vol.vOld, &vol.wOld, &vol.src, &vol.liq,
                              &vol.diag, &vol.rhs, &vol.p, &vol.res, &vol.valA, &vol.valB, &vol.knownA, &vol.knownB, &vol.first, &vol.keep,
                              &vol.origLat, &vol.deposit})
                vkCmdFillBuffer(cmd, b->buf, first * 4, count * 4, 0);
        copy(cmd, vol.staging, vol.occ, count * 4, vol.offOcc + first * 4, first * 4);
        copy(cmd, vol.staging, vol.solid, count * 16, vol.offSolid + first * 16, first * 16);
        a = e;
    }
}

// Fragmentation repair (26.6): every dispatch walks slots 1..slotsUsed-1, so bricks left in high slots after the halo shrank
// cost as if the freed ones were still there (the diagonal creek kept 1,184 addressed slots for 592 stored, 2026-10-13).
// When the addressed range exceeds the stored count by a quarter, the highest bricks move into the lowest holes: their
// persistent state (fill, faces, pressure, occupancy, bodies, source rates) is copied on the device, the map and slot table
// follow. Scratch buffers are rebuilt every substep and need no move. Cell values are untouched: storage only.
int WaterCoreGpu::compactSlots(VkCommandBuffer cmd, Volume& vol) {
    const int stored = vol.storedCount;
    if (vol.slotsUsed - 1 <= stored + stored / 4) return 0;
    int moved = 0;
    size_t hole = 1, top = static_cast<size_t>(vol.slotsUsed) - 1;
    std::vector<VkBufferCopy> c4, c16;
    while (true) {
        while (hole < top && vol.slotBrick[hole] >= 0) ++hole;
        while (top > hole && vol.slotBrick[top] < 0) --top;
        if (hole >= top) break;
        const int packed = vol.slotBrick[top];
        const glm::ivec3 b(packed & 1023, (packed >> 10) & 1023, (packed >> 20) & 1023);
        vol.map[static_cast<size_t>(b.x) + static_cast<size_t>(vol.bn.x) * (b.y + static_cast<size_t>(vol.bn.y) * b.z)] = static_cast<int>(hole);
        vol.slotBrick[hole] = packed; vol.slotBrick[top] = -1;
        VkBufferCopy r{}; r.srcOffset = top * 512 * 4; r.dstOffset = hole * 512 * 4; r.size = 512 * 4; c4.push_back(r);
        r.srcOffset = top * 512 * 16; r.dstOffset = hole * 512 * 16; r.size = 512 * 16; c16.push_back(r);
        ++moved;
    }
    for (Buffer* bf : {&vol.f, &vol.u, &vol.v, &vol.w, &vol.p, &vol.occ, &vol.src})
        vkCmdCopyBuffer(cmd, bf->buf, bf->buf, static_cast<uint32_t>(c4.size()), c4.data());
    vkCmdCopyBuffer(cmd, vol.solid.buf, vol.solid.buf, static_cast<uint32_t>(c16.size()), c16.data());
    // the vacated slots go back to dry air (a later allocation zeroes them again; the absent-brick check must never see them)
    for (const VkBufferCopy& r : c4) {
        const VkDeviceSize off = r.srcOffset;
        for (Buffer* bf : {&vol.f, &vol.u, &vol.v, &vol.w, &vol.p, &vol.occ, &vol.src}) vkCmdFillBuffer(cmd, bf->buf, off, 512 * 4, 0);
        vkCmdFillBuffer(cmd, vol.solid.buf, off * 4, 512 * 16, 0);
    }
    int topUsed = 0;
    for (size_t s = 1; s < vol.capacity; ++s) if (vol.slotBrick[s] >= 0) topUsed = static_cast<int>(s);
    vol.slotsUsed = topUsed + 1;
    vol.tablesDirty = true;
    return moved;
}

void WaterCoreGpu::upload(Volume& vol, const WaterGrid& g) {
    vol.surfaceValid = false;   // Phase F: the staging field predates this grid
    WaterGrid& gm = const_cast<WaterGrid&>(g);
    auto* st = static_cast<char*>(vol.staging.mapped);
    float mx = 0.0f;
    for (float v : gm.uData()) mx = std::max(mx, std::abs(v));
    for (float v : gm.vData()) mx = std::max(mx, std::abs(v));
    for (float v : gm.wData()) mx = std::max(mx, std::abs(v));
    vol.lastMaxSpeed = mx;
    vol.hostOcc.resize(vol.dCells);
    for (int z = 0; z < g.nz(); ++z) for (int y = 0; y < g.ny(); ++y) for (int x = 0; x < g.nx(); ++x)
        vol.hostOcc[g.idx(x, y, z)] = static_cast<uint32_t>(g.occ(x, y, z));
    VkCommandBuffer cmd = beginCommands();
    if (vol.bricked) {
        // residency from the grid: dense - every brick; sparse - the water and one full ring round it (the first call's reach is
        // not known yet; a ring of 8 cells is more than any one tick moves at the CFL the step will use)
        std::vector<char> need(vol.mapCount, vol.sparse ? 0 : 1);
        if (vol.sparse) {
            needFromDenseFill(vol, gm.fData(), need, kB, 1);
            for (int c : vol.sourceCells) {
                if (c < 0 || static_cast<size_t>(c) >= vol.dCells) continue;
                glm::ivec3 b; int dist[6];
                cellToRecord(vol.o, c % vol.spec.dims.x, (c / vol.spec.dims.x) % vol.spec.dims.y, c / (vol.spec.dims.x * vol.spec.dims.y), b, dist);
                need[static_cast<size_t>(b.x) + static_cast<size_t>(vol.bn.x) * (b.y + static_cast<size_t>(vol.bn.y) * b.z)] = 2;
                markNeed(vol, need, b, dist, kB, 1);
            }
            if (!vol.hostDeposit.empty()) needFromDenseFill(vol, vol.hostDeposit, need, kB, 1);
        }
        std::fill(vol.map.begin(), vol.map.end(), 0);
        std::fill(vol.slotBrick.begin(), vol.slotBrick.end(), -1);
        vol.storedCount = 0; vol.colEntries.clear();
        applyResidency(vol, need);
        vol.unstoredM3 = 0.0;   // water in a brick the capacity could not store: counted (createVolume refuses this up front)
        for (int z = 0; z < g.nz(); ++z) for (int y = 0; y < g.ny(); ++y) for (int x = 0; x < g.nx(); ++x)
            if (g.f(x, y, z) != 0.0f && storageIndex(vol, x, y, z) < 0) vol.unstoredM3 += static_cast<double>(g.f(x, y, z)) * g.cellVolume();
        std::memset(st, 0, static_cast<size_t>(vol.offOcc + vol.cells * 4));   // f, u, v, w, occ regions
        std::memset(st + vol.offSolid, 0, vol.cells * 16);
        std::vector<int> all;
        for (size_t s = 1; s < vol.capacity; ++s) if (vol.slotBrick[s] >= 0) all.push_back(static_cast<int>(s));
        scatterSlots(vol, all, &g);
        writeTablesToStaging(vol);
        for (Buffer* b : {&vol.fOrig, &vol.uOld, &vol.vOld, &vol.wOld, &vol.liq, &vol.diag, &vol.rhs, &vol.res, &vol.valA, &vol.valB,
                          &vol.knownA, &vol.knownB, &vol.first, &vol.keep, &vol.origLat, &vol.deposit})
            vkCmdFillBuffer(cmd, b->buf, 0, b->bytes, 0);
        copy(cmd, vol.staging, vol.solid, vol.cells * 16, vol.offSolid);
        copy(cmd, vol.staging, vol.bricks, vol.counterOff * 4, vol.offBricks);
        vkCmdFillBuffer(cmd, vol.bricks.buf, vol.counterOff * 4, (vol.brickInts - vol.counterOff) * 4, 0);
        vol.recordValid = false; vol.tablesDirty = false;
    } else {
        std::memcpy(st, gm.fData().data(), vol.cells * sizeof(float));
        std::memcpy(st + vol.offU, gm.uData().data(), vol.nu * sizeof(float));
        std::memcpy(st + vol.offV, gm.vData().data(), vol.nv * sizeof(float));
        std::memcpy(st + vol.offW, gm.wData().data(), vol.nw * sizeof(float));
        std::memcpy(st + vol.offOcc, vol.hostOcc.data(), vol.cells * 4);
    }
    copy(cmd, vol.staging, vol.f, vol.cells * sizeof(float));
    copy(cmd, vol.staging, vol.fPrev, vol.cells * sizeof(float));
    copy(cmd, vol.staging, vol.u, vol.nu * sizeof(float), vol.offU);
    copy(cmd, vol.staging, vol.v, vol.nv * sizeof(float), vol.offV);
    copy(cmd, vol.staging, vol.w, vol.nw * sizeof(float), vol.offW);
    copy(cmd, vol.staging, vol.occ, vol.cells * 4, vol.offOcc);
    vkCmdFillBuffer(cmd, vol.src.buf, 0, vol.src.bytes, 0);
    vkCmdFillBuffer(cmd, vol.p.buf, 0, vol.p.bytes, 0);   // 26.5: a replaced state starts its solve fresh (deterministic, as a new volume)
    submitAndWait(cmd);
}

bool WaterCoreGpu::setSources(Volume& vol, const std::vector<GpuSource>& sources, std::string* err) {
    if (sources.size() > static_cast<size_t>(kMaxSources)) { if (err) *err = "a GPU volume holds at most " + std::to_string(kMaxSources) + " sources"; return false; }
    auto* st = static_cast<char*>(vol.staging.mapped) + vol.offSources;
    std::memset(st, 0, kMaxSources * sizeof(GpuSource));
    if (!sources.empty()) std::memcpy(st, sources.data(), sources.size() * sizeof(GpuSource));
    vol.sourceCount = static_cast<int>(sources.size());
    vol.sourceCells.clear();
    for (const GpuSource& s : sources) vol.sourceCells.push_back(s.cell);   // 26.6: a pump's brick is always stored
    VkCommandBuffer cmd = beginCommands();
    copy(cmd, vol.staging, vol.sources, kMaxSources * sizeof(GpuSource), vol.offSources);
    vkCmdFillBuffer(cmd, vol.src.buf, 0, vol.src.bytes, 0);   // the projection's per-cell rates are rebuilt by the kernel
    submitAndWait(cmd);
    return true;
}

void WaterCoreGpu::setSolids(Volume& vol, const std::vector<float>& s, const std::vector<float>& wake, const std::vector<uint8_t>& fresh, float frameSeconds) {
    bool any = false;
    for (size_t i = 0; i < vol.dCells && !any; ++i) any = (i < s.size() && s[i] > 0.0f) || (i < wake.size() && wake[i] > 0.0f);
    if (!any && !vol.hasSolids) return;   // nothing there and nothing was: the buffer is already zero
    vol.hostSolid.assign(vol.dCells * 4, 0.0f);
    for (size_t i = 0; i < vol.dCells; ++i) {
        vol.hostSolid[i * 4 + 0] = i < s.size() ? s[i] : 0.0f;
        vol.hostSolid[i * 4 + 1] = 0.0f;   // q: computed on the device from its own fill (wc_classify mode 1)
        vol.hostSolid[i * 4 + 2] = i < wake.size() ? wake[i] : 0.0f;
        vol.hostSolid[i * 4 + 3] = (i < fresh.size() && fresh[i]) ? 1.0f : 0.0f;
    }
    auto* st = reinterpret_cast<float*>(static_cast<char*>(vol.staging.mapped) + vol.offSolid);
    if (vol.bricked) {
        std::memset(st, 0, vol.cells * 16);
        std::vector<int> all;
        for (size_t sl = 1; sl < vol.capacity; ++sl) if (vol.slotBrick[sl] >= 0) all.push_back(static_cast<int>(sl));
        // scatter the solid field only (occupancy staging is rewritten from hostOcc - unchanged)
        scatterSlots(vol, all, nullptr);
    } else {
        std::memcpy(st, vol.hostSolid.data(), vol.cells * 16);
    }
    VkCommandBuffer cmd = beginCommands();
    copy(cmd, vol.staging, vol.solid, vol.cells * 16, vol.offSolid);
    submitAndWait(cmd);
    if (!any) vol.hostSolid.clear();
    vol.hasSolids = any;
    vol.solidRatesPending = any;
    vol.solidFrame = std::max(frameSeconds, 1e-4f);   // T = 0 would ask for an infinite rate
    vol.asleep = false; vol.quietTicks = 0;           // a body is there (or just left): the water must move
}

void WaterCoreGpu::setDeposits(Volume& vol, const std::vector<float>& cellFractions) {
    if (cellFractions.size() != vol.dCells || !vol.staging.mapped) return;
    if (vol.depositPending && vol.hostDeposit.size() == vol.dCells) { for (size_t i = 0; i < vol.dCells; ++i) vol.hostDeposit[i] += cellFractions[i]; }   // two frames' deposits before a step: add
    else vol.hostDeposit = cellFractions;
    vol.depositPending = true;
    vol.asleep = false; vol.quietTicks = 0;   // water arrived: the volume must step
}

bool WaterCoreGpu::readBirths(Volume& vol, std::vector<float>& out) const {
    if (!vol.birthsFresh || !vol.staging.mapped) return false;
    vol.birthsFresh = false;
    out.assign(vol.dColumns * 8, 0.0f);
    const auto* st = reinterpret_cast<const float*>(static_cast<const char*>(vol.staging.mapped) + vol.offBirths);
    for (int z = 0; z < vol.spec.dims.z; ++z) for (int x = 0; x < vol.spec.dims.x; ++x) {
        const long c = columnStorage(vol, x, z);
        if (c >= 0) std::memcpy(&out[(static_cast<size_t>(x) + static_cast<size_t>(vol.spec.dims.x) * z) * 8], st + c * 8, 32);
    }
    return true;
}

bool WaterCoreGpu::readSurface(const Volume& vol, float* out) const {
    if (!vol.surfaceValid || !vol.staging.mapped) return false;
    const size_t stride = sizeof(Core::Water::SurfaceColumn);
    const char* st = static_cast<const char*>(vol.staging.mapped) + vol.offSurf;
    if (!vol.bricked) { std::memcpy(out, st, vol.dColumns * stride); return true; }
    std::memset(out, 0, vol.dColumns * stride);
    for (int z = 0; z < vol.spec.dims.z; ++z) for (int x = 0; x < vol.spec.dims.x; ++x) {
        const long c = columnStorage(vol, x, z);
        if (c >= 0) std::memcpy(reinterpret_cast<char*>(out) + (static_cast<size_t>(x) + static_cast<size_t>(vol.spec.dims.x) * z) * stride, st + c * stride, stride);
    }
    return true;
}

void WaterCoreGpu::readSources(Volume& vol, std::vector<GpuSource>& out) const {
    out.resize(static_cast<size_t>(vol.sourceCount));
    if (vol.sourceCount == 0) return;
    VkCommandBuffer cmd = beginCommands();
    copy(cmd, vol.sources, vol.staging, vol.sourceCount * sizeof(GpuSource), 0, vol.offSources);
    submitAndWait(cmd);
    std::memcpy(out.data(), static_cast<const char*>(vol.staging.mapped) + vol.offSources, vol.sourceCount * sizeof(GpuSource));
}

void WaterCoreGpu::download(Volume& vol, WaterGrid& g) const {
    VkCommandBuffer cmd = beginCommands();
    copy(cmd, vol.f, vol.staging, liveCells(vol) * sizeof(float));
    copy(cmd, vol.u, vol.staging, liveLattice(vol, 0) * sizeof(float), 0, vol.offU);
    copy(cmd, vol.v, vol.staging, liveLattice(vol, 1) * sizeof(float), 0, vol.offV);
    copy(cmd, vol.w, vol.staging, liveLattice(vol, 2) * sizeof(float), 0, vol.offW);
    submitAndWait(cmd);
    const auto* st = static_cast<const char*>(vol.staging.mapped);
    if (!vol.bricked) {
        std::memcpy(g.fData().data(), st, vol.cells * sizeof(float));
        std::memcpy(g.uData().data(), st + vol.offU, vol.nu * sizeof(float));
        std::memcpy(g.vData().data(), st + vol.offV, vol.nv * sizeof(float));
        std::memcpy(g.wData().data(), st + vol.offW, vol.nw * sizeof(float));
        return;
    }
    const auto* sf = reinterpret_cast<const float*>(st); const auto* su = reinterpret_cast<const float*>(st + vol.offU);
    const auto* sv = reinterpret_cast<const float*>(st + vol.offV); const auto* sw = reinterpret_cast<const float*>(st + vol.offW);
    const glm::ivec3 n = vol.spec.dims;
    // an absent brick is dry air: zero first, then every stored site
    std::fill(g.fData().begin(), g.fData().end(), 0.0f); std::fill(g.uData().begin(), g.uData().end(), 0.0f);
    std::fill(g.vData().begin(), g.vData().end(), 0.0f); std::fill(g.wData().begin(), g.wData().end(), 0.0f);
    for (size_t slot = 1; slot < static_cast<size_t>(vol.slotsUsed); ++slot) {
        const int packed = vol.slotBrick[slot];
        if (packed < 0) continue;
        const glm::ivec3 b(packed & 1023, (packed >> 10) & 1023, (packed >> 20) & 1023);
        for (int l = 0; l < 512; ++l) {
            const glm::ivec3 c = b * kB + glm::ivec3(l & 7, (l >> 3) & 7, l >> 6) - vol.o;
            const size_t i = slot * 512 + l;
            if (c.x < 0 || c.y < 0 || c.z < 0) continue;
            if (c.x < n.x && c.y < n.y && c.z < n.z) g.f(c.x, c.y, c.z) = sf[i];
            if (c.x <= n.x && c.y < n.y && c.z < n.z) g.u(c.x, c.y, c.z) = su[i];
            if (c.x < n.x && c.y <= n.y && c.z < n.z) g.v(c.x, c.y, c.z) = sv[i];
            if (c.x < n.x && c.y < n.y && c.z <= n.z) g.w(c.x, c.y, c.z) = sw[i];
        }
    }
}

void WaterCoreGpu::readPressure(Volume& vol, std::vector<float>& p) const { readCells(vol, "p", p); }

bool WaterCoreGpu::setParticles(Volume& vol, const std::vector<FlipParticle>& ps, std::string* err) {
    if (!vol.particles) { if (err) *err = "not a particle volume"; return false; }
    if (ps.size() > vol.particleCapacity) { if (err) *err = "the volume holds at most " + std::to_string(vol.particleCapacity) + " particles (" + std::to_string(ps.size()) + " requested)"; return false; }
    auto* st = static_cast<char*>(vol.staging.mapped) + vol.offParticles;
    auto* pos = reinterpret_cast<glm::vec4*>(st);
    auto* vel = reinterpret_cast<glm::vec4*>(st + vol.particleCapacity * 16);
    for (size_t i = 0; i < ps.size(); ++i) {
        pos[i] = glm::vec4(ps[i].pos, ps[i].mass);
        float idBits; std::memcpy(&idBits, &ps[i].id, 4);
        vel[i] = glm::vec4(ps[i].vel, idBits);
    }
    vol.particleCount = ps.size();
    vol.haveOldGrid = false;
    VkCommandBuffer cmd = beginCommands();
    if (!ps.empty()) {
        copy(cmd, vol.staging, vol.posA, ps.size() * 16, vol.offParticles);
        copy(cmd, vol.staging, vol.velA, ps.size() * 16, vol.offParticles + vol.particleCapacity * 16);
        barrier(cmd);
        // sort and p2g so the grid's f and faces reflect the particles (as FlipTransport::seed does)
        WcPush push; push.nx = vol.spec.dims.x; push.ny = vol.spec.dims.y; push.nz = vol.spec.dims.z; push.h = vol.spec.h;
        fillPushBricks(vol, push);
        recordParticleTransport(cmd, vol, push);   // with iparam = count, mode sequence inside (no move: dt = 0)
    } else {
        vkCmdFillBuffer(cmd, vol.f.buf, 0, vol.f.bytes, 0);
    }
    submitAndWait(cmd);
    return true;
}

void WaterCoreGpu::readParticles(Volume& vol, std::vector<FlipParticle>& out) const {
    out.resize(vol.particleCount);
    if (!vol.particles || vol.particleCount == 0) return;
    VkCommandBuffer cmd = beginCommands();
    copy(cmd, vol.posB, vol.staging, vol.particleCount * 16, 0, vol.offParticles);
    copy(cmd, vol.velB, vol.staging, vol.particleCount * 16, 0, vol.offParticles + vol.particleCapacity * 16);
    submitAndWait(cmd);
    const auto* st = static_cast<const char*>(vol.staging.mapped) + vol.offParticles;
    const auto* pos = reinterpret_cast<const glm::vec4*>(st);
    const auto* vel = reinterpret_cast<const glm::vec4*>(st + vol.particleCapacity * 16);
    for (size_t i = 0; i < vol.particleCount; ++i) {
        out[i].pos = glm::vec3(pos[i]); out[i].mass = pos[i].w; out[i].vel = glm::vec3(vel[i]);
        std::memcpy(&out[i].id, &vel[i].w, 4);
    }
}

// Sort (A -> B, by cell then id) and gather p2g into f, u, v, w; then the base copies for the next
// FLIP delta. The particles live in B afterwards; the next g2p/move reads and writes B in place,
// then the sort runs B -> A -> ... : the host swaps the roles by rebinding, so a step's input list
// is always posA/velA for the sort and posB/velB for everything else.
void WaterCoreGpu::recordParticleTransport(VkCommandBuffer cmd, Volume& vol, WcPush push) {
    push.iparam = static_cast<int32_t>(vol.particleCount);
    Vulkan::ComputePipeline& sort = *vol.pipes[FlipSort];
    push.mode = 0; dispatch(cmd, sort, push, vol.cells, 1024);
    push.mode = 1; dispatch(cmd, sort, push, vol.particleCount, 1024);
    push.mode = 2; dispatch(cmd, sort, push, 1024, 1024);
    push.mode = 3; dispatch(cmd, sort, push, vol.particleCount, 1024);
    push.mode = 4; dispatch(cmd, sort, push, vol.cells, 1024);
    Vulkan::ComputePipeline& p2g = *vol.pipes[FlipP2g];
    push.mode = 0; dispatch(cmd, p2g, push, vol.cells);
    const Buffer* lat[3] = {&vol.u, &vol.v, &vol.w};
    const size_t n[3] = {vol.nu, vol.nv, vol.nw};
    for (int L = 0; L < 3; ++L) {
        p2g.bind(cmd, static_cast<uint32_t>(L));   // set L binds lattice L at binding 5
        p2g.pushConstants(cmd, &push, sizeof(push));
        push.mode = 1 + L; p2g.pushConstants(cmd, &push, sizeof(push));
        p2g.dispatch(cmd, static_cast<uint32_t>((n[L] + 63) / 64));
        barrier(cmd);
    }
    copy(cmd, vol.u, vol.uOld, vol.nu * sizeof(float)); copy(cmd, vol.v, vol.vOld, vol.nv * sizeof(float)); copy(cmd, vol.w, vol.wOld, vol.nw * sizeof(float));
    barrier(cmd);
    vol.haveOldGrid = true;
}

void WaterCoreGpu::readCells(Volume& vol, const char* which, std::vector<float>& out) const {
    const std::string w(which);
    const bool solidComp = w == "solid_s" || w == "solid_q" || w == "solid_wake" || w == "solid_ledger";
    const int comp = w == "solid_s" ? 0 : w == "solid_q" ? 1 : w == "solid_wake" ? 2 : 3;
    const Buffer* b = solidComp ? &vol.solid : w == "f" ? &vol.f : w == "p" ? &vol.p : w == "liq" ? &vol.liq : w == "rhs" ? &vol.rhs : w == "diag" ? &vol.diag : w == "res" ? &vol.res : &vol.occ;
    const VkDeviceSize stride = solidComp ? 16 : 4, region = solidComp ? vol.offSolid : vol.offP;
    VkCommandBuffer cmd = beginCommands();
    copy(cmd, *b, vol.staging, liveCells(vol) * stride, 0, region);
    submitAndWait(cmd);
    const char* st = static_cast<const char*>(vol.staging.mapped) + region;
    const bool asUint = w == "liq" || w == "occ";
    auto valueAt = [&](size_t i) -> float {
        if (solidComp) return reinterpret_cast<const float*>(st)[i * 4 + comp];
        if (asUint) return static_cast<float>(reinterpret_cast<const uint32_t*>(st)[i]);
        return reinterpret_cast<const float*>(st)[i];
    };
    out.assign(vol.dCells, 0.0f);
    const glm::ivec3 n = vol.spec.dims;
    for (int z = 0; z < n.z; ++z) for (int y = 0; y < n.y; ++y) for (int x = 0; x < n.x; ++x) {
        const long i = storageIndex(vol, x, y, z);
        if (i >= 0) out[static_cast<size_t>(x) + static_cast<size_t>(n.x) * (y + static_cast<size_t>(n.y) * z)] = valueAt(static_cast<size_t>(i));
    }
}

void WaterCoreGpu::barrier(VkCommandBuffer cmd) const {
    VkMemoryBarrier mb{}; mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

void WaterCoreGpu::dispatchNoBarrier(VkCommandBuffer cmd, Vulkan::ComputePipeline& pipe, const WcPush& push, size_t threads, uint32_t local) const {
    if (threads == 0) return;
    pipe.bind(cmd);
    pipe.pushConstants(cmd, &push, sizeof(push));
    pipe.dispatch(cmd, static_cast<uint32_t>((threads + local - 1) / local));
}
void WaterCoreGpu::dispatch(VkCommandBuffer cmd, Vulkan::ComputePipeline& pipe, const WcPush& push, size_t threads, uint32_t local) const {
    dispatchNoBarrier(cmd, pipe, push, threads, local);
    barrier(cmd);
}

void WaterCoreGpu::recordExtrapolate(VkCommandBuffer cmd, Volume& vol, WcPush push, bool particles) {
    const Buffer* lat[3] = {&vol.u, &vol.v, &vol.w};
    const size_t n[3] = {liveLattice(vol, 0), liveLattice(vol, 1), liveLattice(vol, 2)};
    for (int L = 0; L < 3; ++L) {
        Vulkan::ComputePipeline& pipe = *vol.pipes[ExtrapU + L];
        copy(cmd, *lat[L], vol.origLat, n[L] * sizeof(float));   // orig = the lattice before extrapolation
        copy(cmd, *lat[L], vol.valA, n[L] * sizeof(float));
        barrier(cmd);
        push.mode = L; push.iparam = particles ? 1 : 0;
        dispatch(cmd, pipe, push, n[L]);                       // masks: knownB, keep; valB = val
        for (int layer = 0; layer < 3; ++layer) {
            copy(cmd, vol.valB, vol.valA, n[L] * sizeof(float)); copy(cmd, vol.knownB, vol.knownA, n[L] * 4);
            barrier(cmd);
            push.mode = 3 + L; push.iparam = layer == 0 ? 100 : 0;
            dispatch(cmd, pipe, push, n[L]);
        }
        copy(cmd, vol.valB, vol.valA, n[L] * sizeof(float)); copy(cmd, vol.knownB, vol.knownA, n[L] * 4);
        barrier(cmd);
        push.mode = 6 + L; push.iparam = particles ? 1 : 0;
        dispatch(cmd, pipe, push, n[L]);                       // finish -> valB
        copy(cmd, vol.valB, *lat[L], n[L] * sizeof(float));
        barrier(cmd);
    }
    // extrapolation must not write into solid faces (three lattices, disjoint writes: one barrier)
    for (int L = 0; L < 3; ++L) { push.mode = 3 + L; dispatchNoBarrier(cmd, *vol.pipes[FaceOpsU + L], push, n[L]); }
    barrier(cmd);
}

void WaterCoreGpu::recordSubstep(VkCommandBuffer cmd, Volume& vol, WcPush push, const SolverParams& params, int sweeps, bool quietBefore) {
    const size_t n[3] = {liveLattice(vol, 0), liveLattice(vol, 1), liveLattice(vol, 2)};
    const size_t cells = liveCells(vol), cols = liveColumns(vol);
    auto enforceSolids = [&]() { for (int L = 0; L < 3; ++L) { push.mode = 3 + L; dispatchNoBarrier(cmd, *vol.pipes[FaceOpsU + L], push, n[L]); } barrier(cmd); };
    for (int i = 0; i < vol.sourceCount; ++i) { push.iparam = i; dispatch(cmd, *vol.pipes[Sources], push, 1, 1); }   // one source per dispatch: no shared-neighbour races
    enforceSolids();
    if (vol.particles) {
        // Phase B2 on the GPU: g2p + move (in the sorted list B), then B -> A, sort A -> B, gather p2g
        push.iparam = static_cast<int32_t>(vol.particleCount); push.param0 = m_flipBlend; push.param1 = vol.haveOldGrid ? 0.0f : 1.0f;
        dispatch(cmd, *vol.pipes[FlipG2p], push, vol.particleCount);
        copy(cmd, vol.posB, vol.posA, vol.particleCount * 16); copy(cmd, vol.velB, vol.velA, vol.particleCount * 16);
        barrier(cmd);
        push.param0 = 0.0f; push.param1 = 0.0f;
        recordParticleTransport(cmd, vol, push);
        enforceSolids();
    } else {
        // transport: fills (6 checkerboard passes, each a hazard on f), then velocities from the old copies
        copy(cmd, vol.f, vol.fOrig, cells * sizeof(float));
        copy(cmd, vol.u, vol.uOld, n[0] * sizeof(float)); copy(cmd, vol.v, vol.vOld, n[1] * sizeof(float)); copy(cmd, vol.w, vol.wOld, n[2] * sizeof(float));
        barrier(cmd);
        if (EulerianTransport::s_allFacesAtOnce) {   // 26.5: the CPU's rule - all faces at once, gathered, the excess carried up
            push.mode = 3; dispatch(cmd, *vol.pipes[FillAdvect], push, cells);
            push.mode = 4; dispatch(cmd, *vol.pipes[FillAdvect], push, cells);
            push.mode = 5; dispatch(cmd, *vol.pipes[FillAdvect], push, cols);
        } else
        for (int dir = 0; dir < 3; ++dir) for (int parity = 0; parity < 2; ++parity) {
            push.mode = dir; push.iparam = parity;
            dispatch(cmd, *vol.pipes[FillAdvect], push, n[dir]);
        }
        for (int L = 0; L < 3; ++L) { push.mode = L; dispatchNoBarrier(cmd, *vol.pipes[VelAdvectU + L], push, n[L]); }   // read old, write new: disjoint
        barrier(cmd);
        if (vol.hasSolids) { push.mode = 3; dispatch(cmd, *vol.pipes[ColumnOps], push, cols); }   // 20.5 ledger: fresh / walled-in water up its column
        enforceSolids();
        {   // compaction (fills only, 24.11): voids under liquid always - slowly (param1 m/s) in moving water, at free fall while
            // settling; under a thin film only while settling (param0 = 1)
            const float p0 = push.param0, p1 = push.param1;
            push.mode = 1; push.param0 = quietBefore ? 1.0f : 0.0f; push.param1 = params.compactionSpeed;
            dispatch(cmd, *vol.pipes[ColumnOps], push, cols);
            push.param0 = p0; push.param1 = p1;
        }
    }
    push.mode = 0; dispatch(cmd, *vol.pipes[FaceOpsV], push, n[1]);                                         // gravity
    push.mode = 1; dispatchNoBarrier(cmd, *vol.pipes[FaceOpsU], push, n[0]);                                // film slope x
    push.mode = 2; dispatchNoBarrier(cmd, *vol.pipes[FaceOpsW], push, n[2]);                                // film slope z (disjoint lattices)
    barrier(cmd);
    push.mode = 0; dispatch(cmd, *vol.pipes[ColumnOps], push, cols);                                        // settle
    enforceSolids();
    // projection
    push.mode = 0; push.param0 = vol.particles ? 1.0f : 0.0f; dispatch(cmd, *vol.pipes[Classify], push, cells); push.param0 = 0.0f;
    if (s_stillWaterGuess) { push.mode = 2; dispatch(cmd, *vol.pipes[Classify], push, cols); }   // 26.9: the still-water first guess
    for (int s = 0; s < sweeps; ++s) for (int colour = 0; colour < 2; ++colour) {
        push.mode = 0; push.iparam = colour; push.param1 = m_omega; dispatch(cmd, *vol.pipes[Rbgs], push, cells);
    }
    push.mode = 1; dispatch(cmd, *vol.pipes[Rbgs], push, cells);                                            // residual
    for (int L = 0; L < 3; ++L) { push.mode = 6 + L; dispatchNoBarrier(cmd, *vol.pipes[FaceOpsU + L], push, n[L]); }   // velocity update (disjoint)
    barrier(cmd);
    recordExtrapolate(cmd, vol, push, vol.particles);
    push.mode = 0; dispatch(cmd, *vol.pipes[ColumnOps], push, cols);                                        // settle again
    // friction and rest damping (FaceOps mode 9): dense - iparam = the lattice's length; bricked - iparam = the lattice (26.6)
    if (params.pondFriction > 0.0f) {   // 24.7 pond friction: all motion, every substep (WaterSolver::applyPondFriction)
        push.mode = 9; push.param0 = std::max(0.0f, 1.0f - params.pondFriction * push.dt);
        for (int L = 0; L < 3; ++L) { push.iparam = vol.bricked ? L : static_cast<int32_t>(n[L]); dispatchNoBarrier(cmd, *vol.pipes[FaceOpsU + L], push, n[L]); }
        barrier(cmd);
    }
    if (quietBefore) {
        const float k = std::max(0.0f, 1.0f - params.restDamping * push.dt);
        push.mode = 9; push.param0 = k;
        for (int L = 0; L < 3; ++L) { push.iparam = vol.bricked ? L : static_cast<int32_t>(n[L]); dispatchNoBarrier(cmd, *vol.pipes[FaceOpsU + L], push, n[L]); }
        barrier(cmd);
    }
}

void WaterCoreGpu::recordReductions(VkCommandBuffer cmd, Volume& vol, WcPush push) {
    auto fold = [&](size_t partials, int slot, int startMode) {
        int mode = startMode;
        while (true) {
            const size_t groups = (partials + 255) / 256;
            push.mode = mode; push.param0 = static_cast<float>(partials); push.param1 = groups == 1 ? 1.0f : 0.0f; push.iparam = slot;
            dispatch(cmd, *vol.pipes[Reduce], push, partials, 256);
            if (groups == 1) break;
            partials = groups; mode = mode == 2 ? 3 : 2;
        }
    };
    const size_t cells = liveCells(vol);
    push.mode = 0; dispatch(cmd, *vol.pipes[Reduce], push, cells, 256);
    if (vol.bricked) {
        // 26.6: the per-half-brick partials into MAP order (absent bricks as zero), then the fold - the same tree whichever
        // bricks are stored, so the sums the rest and settle decisions read are the dense run's bit for bit
        push.mode = 6; dispatch(cmd, *vol.pipes[Reduce], push, 2 * vol.mapCount, 256);
        fold(2 * vol.mapCount, 0, 3);
    } else {
        fold((cells + 255) / 256, 0, 2);
    }
    for (int L = 0; L < 3; ++L) {
        const size_t n = liveLattice(vol, L);
        push.mode = 1; push.iparam = L; dispatch(cmd, *vol.pipes[Reduce], push, n, 256);
        fold((n + 255) / 256, 1 + L, 2);
    }
    push.mode = 4; dispatch(cmd, *vol.pipes[Reduce], push, cells, 256); fold((cells + 255) / 256, 4, 2);
    const size_t cols = std::max<size_t>(liveColumns(vol), 1);
    push.mode = 5; dispatch(cmd, *vol.pipes[Reduce], push, cols, 256); fold((cols + 255) / 256, 5, 2);
}

GpuStepStats WaterCoreGpu::step(Volume& vol, const SolverParams& params, float dt, int ticks, int sweeps) {
    GpuStepStats st; st.ticks = ticks; st.sweeps = sweeps;
    st.capacity = vol.bricked ? static_cast<int>(vol.capacity) - 1 : 0;
    if (vol.asleep || ticks <= 0) {
        st.asleep = vol.asleep; st.quietTicks = vol.quietTicks;
        if (vol.bricked) st.bricks = vol.storedCount;
        return st;
    }
    WcPush push; push.nx = vol.spec.dims.x; push.ny = vol.spec.dims.y; push.nz = vol.spec.dims.z;
    push.h = vol.spec.h; push.thr = params.liquidThreshold; push.gravity = params.gravity; push.filmHold = params.filmHoldDepth / vol.spec.h; push.thetaMin = static_cast<float>(params.thetaMin);
    push.surfaceGhost = WaterSolver::s_surfaceGhost ? 1 : 0;   // 26.9: one switch for both backends
    push.quietRise = EulerianTransport::s_quietRiseFraction * std::sqrt(2.0f * 9.81f * vol.spec.h);   // 26.9: the CPU's gate, the CPU's g
    // CFL from the last known max speed plus the gravity a few ticks can add. The margin is capped at
    // six ticks' worth: a 60-tick benchmark call otherwise carried +9.8 m/s and doubled its substeps
    // (2.39 ms at 1/3 m with 2 substeps, 2026-10-08). The fill move is bounded (min of donor, room)
    // and the velocity advection is semi-Lagrangian, so a late-call speed-up past the bound degrades
    // accuracy, not stability; realtime calls are one tick and exact.
    const double vmax = vol.lastMaxSpeed + params.gravity * dt * std::min(ticks, 6);
    const double limit = params.cflFraction * vol.spec.h;
    const int n = std::clamp(static_cast<int>(std::ceil(vmax * dt / limit)), 1, params.maxSubsteps);
    const float ds = dt / static_cast<float>(n);
    push.dt = ds;
    const auto t0 = std::chrono::steady_clock::now();
    VkCommandBuffer cmd = beginCommands();
    if (vol.bricked && vol.sparse) {
        // 26.6 RESIDENCY - a pure function of the water: the bricks holding water and those within `reach` cells of it, reach =
        // the extrapolation's three layers + one (a face beside an absent cell) + the farthest this call can move water
        // (vmax over every tick of the call, + 1). Needs > one ring (8 cells) only for fast water.
        const int travel = static_cast<int>(std::ceil(vmax * dt * ticks / vol.spec.h)) + 1;
        const int reach = kReachLiquid + travel;                 // the larger of the two (liquid); films get kReachFilm + travel
        const int rings = std::max(1, (reach + kB - 2) / kB);   // ring k reaches 1 + 8 (k - 1) cells past the face at the least
        std::vector<char> need(vol.mapCount, 0);
        if (vol.recordValid && !vol.freezeForTest) {
            const int32_t* info = vol.hostInfo.data();
            for (size_t s = 1; s < static_cast<size_t>(vol.slotsUsed); ++s) {
                const int packed = vol.slotBrick[s];
                if (packed < 0 || !(info[2 * s] & 1)) continue;   // record 0: any water
                const glm::ivec3 b(packed & 1023, (packed >> 10) & 1023, (packed >> 20) & 1023);
                need[static_cast<size_t>(b.x) + static_cast<size_t>(vol.bn.x) * (b.y + static_cast<size_t>(vol.bn.y) * b.z)] = 2;
                for (int r = 0; r < 2; ++r) {
                    const int rec = info[2 * s + r];
                    if (!(rec & 1)) continue;
                    int dist[6]; for (int k = 0; k < 6; ++k) dist[k] = (rec >> (1 + 3 * k)) & 7;
                    markNeed(vol, need, b, dist, (r == 0 ? kReachFilm : kReachLiquid) + travel, rings);
                }
            }
        } else {
            for (size_t m = 0; m < vol.mapCount; ++m) need[m] = vol.map[m] != 0 ? 1 : 0;   // the upload's residency stands for its first call (nothing is freed: all stored bricks stay)
        }
        for (int c : vol.sourceCells) {   // a pump's cell is water from the start of the call (marked from the cell, never a grid scan)
            if (c < 0 || static_cast<size_t>(c) >= vol.dCells) continue;
            const int nx = vol.spec.dims.x, ny = vol.spec.dims.y;
            glm::ivec3 b; int dist[6];
            cellToRecord(vol.o, c % nx, (c / nx) % ny, c / (nx * ny), b, dist);
            need[static_cast<size_t>(b.x) + static_cast<size_t>(vol.bn.x) * (b.y + static_cast<size_t>(vol.bn.y) * b.z)] = 2;
            markNeed(vol, need, b, dist, reach, rings);
        }
        if (vol.depositPending && !vol.hostDeposit.empty()) needFromDenseFill(vol, vol.hostDeposit, need, reach, rings);
        const std::vector<int> fresh = applyResidency(vol, need);
        vol.lastRings = rings;
        if (!fresh.empty()) { scatterSlots(vol, fresh, nullptr); recordSlotUploads(cmd, vol, fresh, true); }
        barrier(cmd);
        vol.lastCompacted = compactSlots(cmd, vol);
        if (vol.lastCompacted > 0) barrier(cmd);
        if (vol.tablesDirty) {   // the steady state (nothing allocated, freed or moved) uploads nothing
            writeTablesToStaging(vol);
            copy(cmd, vol.staging, vol.bricks, vol.counterOff * 4, vol.offBricks);
            vol.tablesDirty = false;
        }
    } else if (vol.bricked) {
        vol.lastAllocated = 0; vol.lastFreed = 0; vol.lastHeld = 0; vol.lastRings = 0; vol.lastCompacted = 0;
    }
    const auto tResidency = std::chrono::steady_clock::now();
    fillPushBricks(vol, push);
    if (vol.bricked) vkCmdFillBuffer(cmd, vol.bricks.buf, vol.counterOff * 4, 8, 0);   // this call's counters
    vkCmdFillBuffer(cmd, vol.dropped.buf, 0, vol.dropped.bytes, 0);
    vkCmdFillBuffer(cmd, vol.births.buf, 0, vol.births.bytes, 0);   // 21: this call's birth records
    const size_t cols = liveColumns(vol);
    if (vol.depositPending) {   // 21: droplets that landed since the last step
        auto* sd = reinterpret_cast<float*>(static_cast<char*>(vol.staging.mapped) + vol.offDeposit);
        if (vol.bricked) {
            std::memset(sd, 0, liveCells(vol) * sizeof(float));
            const glm::ivec3 dn = vol.spec.dims;
            for (int z = 0; z < dn.z; ++z) for (int y = 0; y < dn.y; ++y) for (int x = 0; x < dn.x; ++x) {
                const size_t d = static_cast<size_t>(x) + static_cast<size_t>(dn.x) * (y + static_cast<size_t>(dn.y) * z);
                if (d >= vol.hostDeposit.size() || vol.hostDeposit[d] == 0.0f) continue;
                const long i = storageIndex(vol, x, y, z);
                if (i >= 0) sd[i] = vol.hostDeposit[d];   // the residency above stored every deposit's brick
            }
        } else if (vol.hostDeposit.size() == vol.cells) {
            std::memcpy(sd, vol.hostDeposit.data(), vol.cells * sizeof(float));
        }
        copy(cmd, vol.staging, vol.deposit, liveCells(vol) * sizeof(float), vol.offDeposit);
        barrier(cmd);
        push.mode = 5; dispatch(cmd, *vol.pipes[ColumnOps], push, cols); push.mode = 0;
        vol.depositPending = false; vol.hostDeposit.clear();
    } else barrier(cmd);
    if (vol.solidRatesPending) {   // 20 (M1): q and the ledger marks from the device's own fill, once per upload
        push.mode = 1; push.param0 = vol.solidFrame; dispatch(cmd, *vol.pipes[Classify], push, liveCells(vol)); push.mode = 0; push.param0 = 0.0f;
        vol.solidRatesPending = false;
    }
    for (int t = 0; t < ticks; ++t) {
        copy(cmd, vol.f, vol.fPrev, liveCells(vol) * sizeof(float));
        barrier(cmd);
        for (int s = 0; s < n; ++s) recordSubstep(cmd, vol, push, params, sweeps, vol.quietBefore);
        if (!vol.particles) { push.mode = 2; push.param0 = 1e-6f; dispatch(cmd, *vol.pipes[ColumnOps], push, cols); push.param0 = 0.0f; }   // sweep (fills only)
        if (vol.dropletsOn) { push.mode = 4; push.param0 = dt; dispatch(cmd, *vol.pipes[ColumnOps], push, cols); push.param0 = 0.0f; push.mode = 0; }   // 21: droplet births, once per tick
    }
    if (vol.bricked) {   // 26.6: the water record residency reads next call, and the absent brick's check
        dispatch(cmd, *vol.pipes[Bricks], push, static_cast<size_t>(vol.slotsUsed) * 64, 64);
        copy(cmd, vol.bricks, vol.staging, (vol.infoOff + 2 * static_cast<size_t>(vol.slotsUsed) - vol.counterOff) * 4, vol.counterOff * 4, vol.offBricks + vol.counterOff * 4);
    }
    recordReductions(cmd, vol, push);
    copy(cmd, vol.out, vol.staging, kOutSlots * 16, 0, vol.offOut);
    // Phase F: the surface field rides the same submission (one float per sub-column of top/bottom/
    // solid - never the grid), so the renderer sees every step's surface
    push.param0 = WaterGrid::kSurfaceMinDepth / vol.spec.h; push.iparam = vol.spec.origin.y;
    dispatch(cmd, *vol.pipes[Surface], push, cols);
    copy(cmd, vol.surf, vol.staging, cols * sizeof(Core::Water::SurfaceColumn), 0, vol.offSurf);
    copy(cmd, vol.births, vol.staging, cols * 32, 0, vol.offBirths);   // 21: read after the fence (readBirths)
    push.param0 = 0.0f; push.iparam = 0;
    const auto tSubmit = std::chrono::steady_clock::now();
    submitAndWait(cmd);
    const auto tFence = std::chrono::steady_clock::now();
    st.hostResidencyMs = std::chrono::duration<double, std::milli>(tResidency - t0).count();
    st.submitWaitMs = std::chrono::duration<double, std::milli>(tFence - tSubmit).count();
    vol.surfaceValid = true;
    vol.birthsFresh = true;
    st.gpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const auto* out = reinterpret_cast<const float*>(static_cast<const char*>(vol.staging.mapped) + vol.offOut);
    st.substepsLast = n;
    st.kineticEnergy = out[0]; st.maxDeltaF = out[1]; st.totalMass = static_cast<double>(out[2]) * vol.spec.h * vol.spec.h * vol.spec.h;
    st.maxSpeed = std::max({out[4 + 3], out[8 + 3], out[12 + 3]});
    st.rbgsResidualMax = out[16 + 3];
    st.residueDropped = out[20];
    if (vol.bricked) {
        const auto* bi = reinterpret_cast<const int32_t*>(static_cast<const char*>(vol.staging.mapped) + vol.offBricks);
        int32_t counters[2]; std::memcpy(counters, bi + vol.counterOff, sizeof(counters));
        vol.hostInfo.resize(2 * vol.capacity);
        std::memcpy(vol.hostInfo.data(), bi + vol.infoOff, 2 * static_cast<size_t>(vol.slotsUsed) * 4);   // one bulk read of uncached memory
        st.fluxIntoAbsent = counters[0]; st.sentinelDirty = counters[1] != 0;
        st.bricks = vol.storedCount;
        st.allocated = vol.lastAllocated; st.freed = vol.lastFreed; st.heldByCapacity = vol.lastHeld; st.haloRings = vol.lastRings;
        st.compacted = vol.lastCompacted;
        st.unstoredWater = vol.unstoredM3;
        if (vol.sparse) st.haloShort = haloShortAfterCall(vol);
        st.hostAfterMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tFence).count();
        vol.recordValid = vol.sparse;
    }
    vol.lastMaxSpeed = st.maxSpeed;
    const double specificKE = st.kineticEnergy / std::max(static_cast<double>(out[2]), 1e-9);
    const bool quiet = specificKE < params.keWake && st.maxDeltaF < params.maxDeltaFQuiet;
    vol.quietTicks = quiet ? vol.quietTicks + ticks : 0;
    vol.quietBefore = specificKE < params.keSettle;   // the settle band (SolverParams::keSettle), as on the CPU: rest damping + compaction only below it
    if (vol.quietTicks >= params.restTicks) vol.asleep = true;
    st.quietTicks = vol.quietTicks; st.asleep = vol.asleep;
    return st;
}

}}} // namespace Phyxel::Core::Water
