// WaterCoreGpu — docs/WaterCore.md §15.11, slice 3 (fills, device-local, one submission per call).
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
    "wc_flip_sort.comp.spv", "wc_flip_p2g.comp.spv", "wc_flip_g2p.comp.spv"};
const uint32_t kKernelBindings[WaterCoreGpu::KernelCount] = {7, 4, 4, 4, 6, 6, 6, 7, 11, 5, 9, 9, 9, 10, 4, 6, 7, 6, 9};   // Surface (6: f, occ, surf, u, w, solid) sits before the FLIP kernels; 20 (M1): + solid on FillAdvect, FaceOps, ColumnOps, Classify, Surface
constexpr int kOutSlots = 8;   // vec4 slots: 0 cells (ke, maxDeltaF, mass), 1-3 max speed per lattice, 4 residual max, 5 residue sum
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
    bindAll(*vol.pipes[FillAdvect], {&vol.f, &vol.fOrig, &vol.u, &vol.v, &vol.w, &vol.occ, &vol.solid});
    bindAll(*vol.pipes[VelAdvectU], {&vol.uOld, &vol.vOld, &vol.wOld, &vol.u});
    bindAll(*vol.pipes[VelAdvectV], {&vol.uOld, &vol.vOld, &vol.wOld, &vol.v});
    bindAll(*vol.pipes[VelAdvectW], {&vol.uOld, &vol.vOld, &vol.wOld, &vol.w});
    bindAll(*vol.pipes[FaceOpsU],   {&vol.f, &vol.occ, &vol.u, &vol.p, &vol.liq, &vol.solid});
    bindAll(*vol.pipes[FaceOpsV],   {&vol.f, &vol.occ, &vol.v, &vol.p, &vol.liq, &vol.solid});
    bindAll(*vol.pipes[FaceOpsW],   {&vol.f, &vol.occ, &vol.w, &vol.p, &vol.liq, &vol.solid});
    bindAll(*vol.pipes[ColumnOps],  {&vol.f, &vol.occ, &vol.u, &vol.v, &vol.w, &vol.dropped, &vol.solid});
    bindAll(*vol.pipes[Surface],    {&vol.f, &vol.occ, &vol.surf, &vol.u, &vol.w, &vol.solid});   // Phase F (+ u, w: E2 surface velocity; + solid: 20)
    bindAll(*vol.pipes[Classify],   {&vol.f, &vol.occ, &vol.u, &vol.v, &vol.w, &vol.src, &vol.liq, &vol.diag, &vol.rhs, &vol.p, &vol.solid});
    bindAll(*vol.pipes[Rbgs],       {&vol.liq, &vol.diag, &vol.rhs, &vol.p, &vol.res});
    // binding 8 = the lattice before the halo pass: a dedicated scratch, because uOld/vOld/wOld are
    // the FLIP delta base and overwriting them here left the particles with a ~zero delta (they
    // crawled at 0.1 m/s under a 2 m/s grid, 2026-10-08)
    bindAll(*vol.pipes[ExtrapU], {&vol.f, &vol.occ, &vol.valA, &vol.knownA, &vol.valB, &vol.knownB, &vol.first, &vol.keep, &vol.origLat});
    bindAll(*vol.pipes[ExtrapV], {&vol.f, &vol.occ, &vol.valA, &vol.knownA, &vol.valB, &vol.knownB, &vol.first, &vol.keep, &vol.origLat});
    bindAll(*vol.pipes[ExtrapW], {&vol.f, &vol.occ, &vol.valA, &vol.knownA, &vol.valB, &vol.knownB, &vol.first, &vol.keep, &vol.origLat});
    bindAll(*vol.pipes[Reduce],  {&vol.f, &vol.fPrev, &vol.u, &vol.v, &vol.w, &vol.part, &vol.out, &vol.part2, &vol.res, &vol.dropped});
    bindAll(*vol.pipes[Sources], {&vol.f, &vol.occ, &vol.src, &vol.sources});
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

WaterCoreGpu::Volume* WaterCoreGpu::createVolume(const WaterGrid& g, std::string* err) {
    Volume* vol = createVolume(g.spec(), err);
    if (vol) upload(*vol, g);
    return vol;
}

WaterCoreGpu::Volume* WaterCoreGpu::createVolume(const GridSpec& spec, std::string* err, bool particles, size_t particleCapacity) {
    if (!ready()) { if (err) *err = "WaterCoreGpu not initialised"; return nullptr; }
    auto vol = std::make_unique<Volume>();
    vol->spec = spec;
    vol->particles = particles;
    const size_t nx = static_cast<size_t>(spec.dims.x), ny = static_cast<size_t>(spec.dims.y), nz = static_cast<size_t>(spec.dims.z);
    vol->cells = nx * ny * nz; vol->nu = (nx + 1) * ny * nz; vol->nv = nx * (ny + 1) * nz; vol->nw = nx * ny * (nz + 1);
    vol->latticeMax = std::max({vol->nu, vol->nv, vol->nw});
    vol->columns = nx * nz;
    vol->groups = (std::max({vol->cells, vol->latticeMax}) + 255) / 256;
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
           && createBuffer(vol->solid, vol->cells * 16, false, err);                                    // 20: (s, q, wake, ledger) per cell
    // staging: [f | u | v | w | occ | src | out | p | sources]
    vol->offU = vol->cells * F4; vol->offV = vol->offU + vol->nu * F4; vol->offW = vol->offV + vol->nv * F4;
    vol->offOcc = vol->offW + vol->nw * F4; vol->offSrc = vol->offOcc + vol->cells * 4; vol->offOut = vol->offSrc + vol->cells * F4;
    vol->offP = vol->offOut + kOutSlots * 16; vol->offSources = vol->offP + vol->cells * F4;
    vol->offSurf = vol->offSources + kMaxSources * sizeof(GpuSource);
    vol->offSolid = vol->offSurf + vol->columns * sizeof(Core::Water::SurfaceColumn);
    vol->offParticles = vol->offSolid + vol->cells * 16;
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
    {   // the solid buffer starts all zero: no body, every rule exactly as before
        VkCommandBuffer cmd = beginCommands();
        vkCmdFillBuffer(cmd, vol->solid.buf, 0, vol->solid.bytes, 0);
        submitAndWait(cmd);
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
                      &vol->diag, &vol->rhs, &vol->p, &vol->res, &vol->valA, &vol->valB, &vol->knownA, &vol->knownB, &vol->first, &vol->keep, &vol->dropped, &vol->part, &vol->part2, &vol->out, &vol->origLat, &vol->sources, &vol->surf, &vol->solid, &vol->posA, &vol->velA, &vol->posB, &vol->velB, &vol->pcount, &vol->pstart, &vol->pcursor, &vol->staging})
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
    VkBufferCopy region{}; region.srcOffset = srcOff; region.dstOffset = dstOff; region.size = bytes;
    vkCmdCopyBuffer(cmd, from.buf, to.buf, 1, &region);
}

void WaterCoreGpu::upload(Volume& vol, const WaterGrid& g) {
    vol.surfaceValid = false;   // Phase F: the staging field predates this grid
    WaterGrid& gm = const_cast<WaterGrid&>(g);
    auto* st = static_cast<char*>(vol.staging.mapped);
    std::memcpy(st, gm.fData().data(), vol.cells * sizeof(float));
    std::memcpy(st + vol.offU, gm.uData().data(), vol.nu * sizeof(float));
    std::memcpy(st + vol.offV, gm.vData().data(), vol.nv * sizeof(float));
    std::memcpy(st + vol.offW, gm.wData().data(), vol.nw * sizeof(float));
    auto* occ = reinterpret_cast<uint32_t*>(st + vol.offOcc);
    for (int z = 0; z < g.nz(); ++z) for (int y = 0; y < g.ny(); ++y) for (int x = 0; x < g.nx(); ++x)
        occ[g.idx(x, y, z)] = static_cast<uint32_t>(g.occ(x, y, z));
    float mx = 0.0f;
    for (float v : gm.uData()) mx = std::max(mx, std::abs(v));
    for (float v : gm.vData()) mx = std::max(mx, std::abs(v));
    for (float v : gm.wData()) mx = std::max(mx, std::abs(v));
    vol.lastMaxSpeed = mx;
    VkCommandBuffer cmd = beginCommands();
    copy(cmd, vol.staging, vol.f, vol.cells * sizeof(float));
    copy(cmd, vol.staging, vol.fPrev, vol.cells * sizeof(float));
    copy(cmd, vol.staging, vol.u, vol.nu * sizeof(float), vol.offU);
    copy(cmd, vol.staging, vol.v, vol.nv * sizeof(float), vol.offV);
    copy(cmd, vol.staging, vol.w, vol.nw * sizeof(float), vol.offW);
    copy(cmd, vol.staging, vol.occ, vol.cells * 4, vol.offOcc);
    vkCmdFillBuffer(cmd, vol.src.buf, 0, vol.src.bytes, 0);
    submitAndWait(cmd);
}

bool WaterCoreGpu::setSources(Volume& vol, const std::vector<GpuSource>& sources, std::string* err) {
    if (sources.size() > static_cast<size_t>(kMaxSources)) { if (err) *err = "a GPU volume holds at most " + std::to_string(kMaxSources) + " sources"; return false; }
    auto* st = static_cast<char*>(vol.staging.mapped) + vol.offSources;
    std::memset(st, 0, kMaxSources * sizeof(GpuSource));
    if (!sources.empty()) std::memcpy(st, sources.data(), sources.size() * sizeof(GpuSource));
    vol.sourceCount = static_cast<int>(sources.size());
    VkCommandBuffer cmd = beginCommands();
    copy(cmd, vol.staging, vol.sources, kMaxSources * sizeof(GpuSource), vol.offSources);
    vkCmdFillBuffer(cmd, vol.src.buf, 0, vol.src.bytes, 0);   // the projection's per-cell rates are rebuilt by the kernel
    submitAndWait(cmd);
    return true;
}

void WaterCoreGpu::setSolids(Volume& vol, const std::vector<float>& s, const std::vector<float>& wake, const std::vector<uint8_t>& fresh, float frameSeconds) {
    bool any = false;
    for (size_t i = 0; i < vol.cells && !any; ++i) any = (i < s.size() && s[i] > 0.0f) || (i < wake.size() && wake[i] > 0.0f);
    if (!any && !vol.hasSolids) return;   // nothing there and nothing was: the buffer is already zero
    auto* st = reinterpret_cast<float*>(static_cast<char*>(vol.staging.mapped) + vol.offSolid);
    for (size_t i = 0; i < vol.cells; ++i) {
        st[i * 4 + 0] = i < s.size() ? s[i] : 0.0f;
        st[i * 4 + 1] = 0.0f;   // q: computed on the device from its own fill (wc_classify mode 1)
        st[i * 4 + 2] = i < wake.size() ? wake[i] : 0.0f;
        st[i * 4 + 3] = (i < fresh.size() && fresh[i]) ? 1.0f : 0.0f;
    }
    VkCommandBuffer cmd = beginCommands();
    copy(cmd, vol.staging, vol.solid, vol.cells * 16, vol.offSolid);
    submitAndWait(cmd);
    vol.hasSolids = any;
    vol.solidRatesPending = any;
    vol.solidFrame = std::max(frameSeconds, 1e-4f);   // T = 0 would ask for an infinite rate
    vol.asleep = false; vol.quietTicks = 0;           // a body is there (or just left): the water must move
}

bool WaterCoreGpu::readSurface(const Volume& vol, float* out) const {
    if (!vol.surfaceValid || !vol.staging.mapped) return false;
    std::memcpy(out, static_cast<const char*>(vol.staging.mapped) + vol.offSurf, vol.columns * sizeof(Core::Water::SurfaceColumn));
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
    copy(cmd, vol.f, vol.staging, vol.cells * sizeof(float));
    copy(cmd, vol.u, vol.staging, vol.nu * sizeof(float), 0, vol.offU);
    copy(cmd, vol.v, vol.staging, vol.nv * sizeof(float), 0, vol.offV);
    copy(cmd, vol.w, vol.staging, vol.nw * sizeof(float), 0, vol.offW);
    submitAndWait(cmd);
    const auto* st = static_cast<const char*>(vol.staging.mapped);
    std::memcpy(g.fData().data(), st, vol.cells * sizeof(float));
    std::memcpy(g.uData().data(), st + vol.offU, vol.nu * sizeof(float));
    std::memcpy(g.vData().data(), st + vol.offV, vol.nv * sizeof(float));
    std::memcpy(g.wData().data(), st + vol.offW, vol.nw * sizeof(float));
}

void WaterCoreGpu::readPressure(Volume& vol, std::vector<float>& p) const {
    VkCommandBuffer cmd = beginCommands();
    copy(cmd, vol.p, vol.staging, vol.cells * sizeof(float), 0, vol.offP);
    submitAndWait(cmd);
    p.resize(vol.cells);
    std::memcpy(p.data(), static_cast<const char*>(vol.staging.mapped) + vol.offP, vol.cells * sizeof(float));
}

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
    if (w == "solid_s" || w == "solid_q" || w == "solid_wake" || w == "solid_ledger") {   // one component of the vec4 solid buffer
        const int comp = w == "solid_s" ? 0 : w == "solid_q" ? 1 : w == "solid_wake" ? 2 : 3;
        VkCommandBuffer cmd = beginCommands();
        copy(cmd, vol.solid, vol.staging, vol.cells * 16, 0, vol.offSolid);
        submitAndWait(cmd);
        out.resize(vol.cells);
        const auto* st = reinterpret_cast<const float*>(static_cast<const char*>(vol.staging.mapped) + vol.offSolid);
        for (size_t i = 0; i < vol.cells; ++i) out[i] = st[i * 4 + comp];
        return;
    }
    const Buffer* b = w == "f" ? &vol.f : w == "p" ? &vol.p : w == "liq" ? &vol.liq : w == "rhs" ? &vol.rhs : w == "diag" ? &vol.diag : w == "res" ? &vol.res : &vol.occ;
    VkCommandBuffer cmd = beginCommands();
    copy(cmd, *b, vol.staging, vol.cells * 4, 0, vol.offP);
    submitAndWait(cmd);
    out.resize(vol.cells);
    const auto* st = static_cast<const char*>(vol.staging.mapped) + vol.offP;
    if (w == "liq" || w == "occ") { const auto* ui = reinterpret_cast<const uint32_t*>(st); for (size_t i = 0; i < vol.cells; ++i) out[i] = static_cast<float>(ui[i]); }
    else std::memcpy(out.data(), st, vol.cells * 4);
}

void WaterCoreGpu::barrier(VkCommandBuffer cmd) const {
    VkMemoryBarrier mb{}; mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

void WaterCoreGpu::dispatchNoBarrier(VkCommandBuffer cmd, Vulkan::ComputePipeline& pipe, const WcPush& push, size_t threads, uint32_t local) const {
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
    const size_t n[3] = {vol.nu, vol.nv, vol.nw};
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
    const size_t n[3] = {vol.nu, vol.nv, vol.nw};
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
        copy(cmd, vol.f, vol.fOrig, vol.cells * sizeof(float));
        copy(cmd, vol.u, vol.uOld, vol.nu * sizeof(float)); copy(cmd, vol.v, vol.vOld, vol.nv * sizeof(float)); copy(cmd, vol.w, vol.wOld, vol.nw * sizeof(float));
        barrier(cmd);
        for (int dir = 0; dir < 3; ++dir) for (int parity = 0; parity < 2; ++parity) {
            push.mode = dir; push.iparam = parity;
            dispatch(cmd, *vol.pipes[FillAdvect], push, n[dir]);
        }
        for (int L = 0; L < 3; ++L) { push.mode = L; dispatchNoBarrier(cmd, *vol.pipes[VelAdvectU + L], push, n[L]); }   // read old, write new: disjoint
        barrier(cmd);
        if (vol.hasSolids) { push.mode = 3; dispatch(cmd, *vol.pipes[ColumnOps], push, vol.columns); }   // 20.5 ledger: fresh / walled-in water up its column
        enforceSolids();
        push.mode = 1; dispatch(cmd, *vol.pipes[ColumnOps], push, vol.columns);                              // compaction (fills only)
    }
    push.mode = 0; dispatch(cmd, *vol.pipes[FaceOpsV], push, vol.nv);                                       // gravity
    push.mode = 1; dispatchNoBarrier(cmd, *vol.pipes[FaceOpsU], push, vol.nu);                              // film slope x
    push.mode = 2; dispatchNoBarrier(cmd, *vol.pipes[FaceOpsW], push, vol.nw);                              // film slope z (disjoint lattices)
    barrier(cmd);
    push.mode = 0; dispatch(cmd, *vol.pipes[ColumnOps], push, vol.columns);                                  // settle
    enforceSolids();
    // projection
    push.mode = 0; push.param0 = vol.particles ? 1.0f : 0.0f; dispatch(cmd, *vol.pipes[Classify], push, vol.cells); push.param0 = 0.0f;
    for (int s = 0; s < sweeps; ++s) for (int colour = 0; colour < 2; ++colour) {
        push.mode = 0; push.iparam = colour; push.param1 = m_omega; dispatch(cmd, *vol.pipes[Rbgs], push, vol.cells);
    }
    push.mode = 1; dispatch(cmd, *vol.pipes[Rbgs], push, vol.cells);                                         // residual
    for (int L = 0; L < 3; ++L) { push.mode = 6 + L; dispatchNoBarrier(cmd, *vol.pipes[FaceOpsU + L], push, n[L]); }   // velocity update (disjoint)
    barrier(cmd);
    recordExtrapolate(cmd, vol, push, vol.particles);
    push.mode = 0; dispatch(cmd, *vol.pipes[ColumnOps], push, vol.columns);                                  // settle again
    if (quietBefore) {
        const float k = std::max(0.0f, 1.0f - params.restDamping * push.dt);
        push.mode = 9; push.param0 = k;
        for (int L = 0; L < 3; ++L) { push.iparam = static_cast<int32_t>(n[L]); dispatchNoBarrier(cmd, *vol.pipes[FaceOpsU + L], push, n[L]); }
        barrier(cmd);
    }
}

void WaterCoreGpu::recordReductions(VkCommandBuffer cmd, Volume& vol, WcPush push) {
    auto fold = [&](size_t partials, int slot) {
        int mode = 2;
        while (true) {
            const size_t groups = (partials + 255) / 256;
            push.mode = mode; push.param0 = static_cast<float>(partials); push.param1 = groups == 1 ? 1.0f : 0.0f; push.iparam = slot;
            dispatch(cmd, *vol.pipes[Reduce], push, partials, 256);
            if (groups == 1) break;
            partials = groups; mode = mode == 2 ? 3 : 2;
        }
    };
    push.mode = 0; dispatch(cmd, *vol.pipes[Reduce], push, vol.cells, 256); fold((vol.cells + 255) / 256, 0);
    for (int L = 0; L < 3; ++L) {
        const size_t n = L == 0 ? vol.nu : (L == 1 ? vol.nv : vol.nw);
        push.mode = 1; push.iparam = L; dispatch(cmd, *vol.pipes[Reduce], push, n, 256);
        fold((n + 255) / 256, 1 + L);
    }
    push.mode = 4; dispatch(cmd, *vol.pipes[Reduce], push, vol.cells, 256); fold((vol.cells + 255) / 256, 4);
    push.mode = 5; dispatch(cmd, *vol.pipes[Reduce], push, vol.columns, 256); fold((vol.columns + 255) / 256, 5);
}

GpuStepStats WaterCoreGpu::step(Volume& vol, const SolverParams& params, float dt, int ticks, int sweeps) {
    GpuStepStats st; st.ticks = ticks; st.sweeps = sweeps;
    if (vol.asleep || ticks <= 0) { st.asleep = vol.asleep; st.quietTicks = vol.quietTicks; return st; }
    WcPush push; push.nx = vol.spec.dims.x; push.ny = vol.spec.dims.y; push.nz = vol.spec.dims.z;
    push.h = vol.spec.h; push.thr = params.liquidThreshold; push.gravity = params.gravity; push.filmHold = params.filmHoldDepth / vol.spec.h;
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
    vkCmdFillBuffer(cmd, vol.dropped.buf, 0, vol.dropped.bytes, 0);
    if (vol.solidRatesPending) {   // 20 (M1): q and the ledger marks from the device's own fill, once per upload
        push.mode = 1; push.param0 = vol.solidFrame; dispatch(cmd, *vol.pipes[Classify], push, vol.cells); push.mode = 0; push.param0 = 0.0f;
        vol.solidRatesPending = false;
    }
    for (int t = 0; t < ticks; ++t) {
        copy(cmd, vol.f, vol.fPrev, vol.cells * sizeof(float));
        barrier(cmd);
        for (int s = 0; s < n; ++s) recordSubstep(cmd, vol, push, params, sweeps, vol.quietBefore);
        if (!vol.particles) { push.mode = 2; push.param0 = 1e-6f; dispatch(cmd, *vol.pipes[ColumnOps], push, vol.columns); push.param0 = 0.0f; }   // sweep (fills only)
    }
    recordReductions(cmd, vol, push);
    copy(cmd, vol.out, vol.staging, kOutSlots * 16, 0, vol.offOut);
    // Phase F: the surface field rides the same submission (one float per sub-column of top/bottom/
    // solid - never the grid), so the renderer sees every step's surface
    push.param0 = WaterGrid::kSurfaceMinDepth / vol.spec.h; push.iparam = vol.spec.origin.y;
    dispatch(cmd, *vol.pipes[Surface], push, vol.columns);
    copy(cmd, vol.surf, vol.staging, vol.columns * sizeof(Core::Water::SurfaceColumn), 0, vol.offSurf);
    push.param0 = 0.0f; push.iparam = 0;
    submitAndWait(cmd);
    vol.surfaceValid = true;
    st.gpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const auto* out = reinterpret_cast<const float*>(static_cast<const char*>(vol.staging.mapped) + vol.offOut);
    st.substepsLast = n;
    st.kineticEnergy = out[0]; st.maxDeltaF = out[1]; st.totalMass = static_cast<double>(out[2]) * vol.spec.h * vol.spec.h * vol.spec.h;
    st.maxSpeed = std::max({out[4 + 3], out[8 + 3], out[12 + 3]});
    st.rbgsResidualMax = out[16 + 3];
    st.residueDropped = out[20];
    vol.lastMaxSpeed = st.maxSpeed;
    const double specificKE = st.kineticEnergy / std::max(static_cast<double>(out[2]), 1e-9);
    const bool quiet = specificKE < params.keWake && st.maxDeltaF < params.maxDeltaFQuiet;
    vol.quietTicks = quiet ? vol.quietTicks + ticks : 0;
    vol.quietBefore = specificKE < params.keSettle;   // the settle band (SolverParams::keSettle), as on the CPU: damping below 3 cm/s rms, never above
    if (vol.quietTicks >= params.restTicks) vol.asleep = true;
    st.quietTicks = vol.quietTicks; st.asleep = vol.asleep;
    return st;
}

}}} // namespace Phyxel::Core::Water
