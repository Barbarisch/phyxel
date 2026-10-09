// WaterCoreGpu — docs/WaterCore.md §15.11, slice 3 (fills, device-local, one submission per call).
#include "core/water/WaterCoreGpu.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace Phyxel { namespace Core { namespace Water {

namespace {
const char* kKernelFiles[WaterCoreGpu::KernelCount] = {
    "wc_fill_advect.comp.spv", "wc_vel_advect.comp.spv", "wc_vel_advect.comp.spv", "wc_vel_advect.comp.spv",
    "wc_face_ops.comp.spv", "wc_face_ops.comp.spv", "wc_face_ops.comp.spv",
    "wc_column_ops.comp.spv", "wc_classify.comp.spv", "wc_rbgs.comp.spv",
    "wc_extrap.comp.spv", "wc_extrap.comp.spv", "wc_extrap.comp.spv", "wc_reduce.comp.spv"};
const uint32_t kKernelBindings[WaterCoreGpu::KernelCount] = {6, 4, 4, 4, 5, 5, 5, 6, 10, 5, 9, 9, 9, 10};
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
        auto pipe = std::make_unique<Vulkan::ComputePipeline>();
        if (!pipe->create(m_device, m_shaderDir + "/" + kKernelFiles[k], kKernelBindings[k], sizeof(WcPush))) {
            if (err) *err = std::string("failed to create pipeline ") + kKernelFiles[k] + " from " + m_shaderDir; return false;
        }
        vol.pipes[k] = std::move(pipe);
    }
    auto bindAll = [](Vulkan::ComputePipeline& p, std::initializer_list<const Buffer*> bufs) {
        uint32_t i = 0; for (const Buffer* b : bufs) { p.bindBuffer(i++, b->buf, b->bytes); } p.updateDescriptors();
    };
    bindAll(*vol.pipes[FillAdvect], {&vol.f, &vol.fOrig, &vol.u, &vol.v, &vol.w, &vol.occ});
    bindAll(*vol.pipes[VelAdvectU], {&vol.uOld, &vol.vOld, &vol.wOld, &vol.u});
    bindAll(*vol.pipes[VelAdvectV], {&vol.uOld, &vol.vOld, &vol.wOld, &vol.v});
    bindAll(*vol.pipes[VelAdvectW], {&vol.uOld, &vol.vOld, &vol.wOld, &vol.w});
    bindAll(*vol.pipes[FaceOpsU],   {&vol.f, &vol.occ, &vol.u, &vol.p, &vol.liq});
    bindAll(*vol.pipes[FaceOpsV],   {&vol.f, &vol.occ, &vol.v, &vol.p, &vol.liq});
    bindAll(*vol.pipes[FaceOpsW],   {&vol.f, &vol.occ, &vol.w, &vol.p, &vol.liq});
    bindAll(*vol.pipes[ColumnOps],  {&vol.f, &vol.occ, &vol.u, &vol.v, &vol.w, &vol.dropped});
    bindAll(*vol.pipes[Classify],   {&vol.f, &vol.occ, &vol.u, &vol.v, &vol.w, &vol.src, &vol.liq, &vol.diag, &vol.rhs, &vol.p});
    bindAll(*vol.pipes[Rbgs],       {&vol.liq, &vol.diag, &vol.rhs, &vol.p, &vol.res});
    bindAll(*vol.pipes[ExtrapU], {&vol.f, &vol.occ, &vol.valA, &vol.knownA, &vol.valB, &vol.knownB, &vol.first, &vol.keep, &vol.uOld});
    bindAll(*vol.pipes[ExtrapV], {&vol.f, &vol.occ, &vol.valA, &vol.knownA, &vol.valB, &vol.knownB, &vol.first, &vol.keep, &vol.vOld});
    bindAll(*vol.pipes[ExtrapW], {&vol.f, &vol.occ, &vol.valA, &vol.knownA, &vol.valB, &vol.knownB, &vol.first, &vol.keep, &vol.wOld});
    bindAll(*vol.pipes[Reduce],  {&vol.f, &vol.fPrev, &vol.u, &vol.v, &vol.w, &vol.part, &vol.out, &vol.part2, &vol.res, &vol.dropped});
    return true;
}

WaterCoreGpu::Volume* WaterCoreGpu::createVolume(const WaterGrid& g, std::string* err) {
    if (!ready()) { if (err) *err = "WaterCoreGpu not initialised"; return nullptr; }
    auto vol = std::make_unique<Volume>();
    vol->spec = g.spec();
    const size_t nx = g.nx(), ny = g.ny(), nz = g.nz();
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
           && createBuffer(vol->dropped, vol->columns * F4, false, err) && createBuffer(vol->part, vol->groups * 16, false, err) && createBuffer(vol->part2, vol->groups * 16, false, err)
           && createBuffer(vol->out, kOutSlots * 16, false, err);
    // staging: [f | u | v | w | occ | src | out | p]
    vol->offU = vol->cells * F4; vol->offV = vol->offU + vol->nu * F4; vol->offW = vol->offV + vol->nv * F4;
    vol->offOcc = vol->offW + vol->nw * F4; vol->offSrc = vol->offOcc + vol->cells * 4; vol->offOut = vol->offSrc + vol->cells * F4;
    vol->offP = vol->offOut + kOutSlots * 16;
    ok = ok && createBuffer(vol->staging, vol->offP + vol->cells * F4, true, err);
    if (!ok || !createPipelines(*vol, err)) { Volume* raw = vol.release(); destroyVolume(raw); return nullptr; }
    Volume* raw = vol.release();
    m_volumes.push_back(raw);
    upload(*raw, g);
    return raw;
}

void WaterCoreGpu::destroyVolume(Volume* vol) {
    if (!vol) return;
    vkDeviceWaitIdle(m_device);
    for (auto& p : vol->pipes) if (p) { p->cleanup(); p.reset(); }
    for (Buffer* b : {&vol->f, &vol->fOrig, &vol->fPrev, &vol->u, &vol->v, &vol->w, &vol->uOld, &vol->vOld, &vol->wOld, &vol->occ, &vol->src, &vol->liq,
                      &vol->diag, &vol->rhs, &vol->p, &vol->res, &vol->valA, &vol->valB, &vol->knownA, &vol->knownB, &vol->first, &vol->keep, &vol->dropped, &vol->part, &vol->part2, &vol->out, &vol->staging})
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
    vkQueueSubmit(m_queue, 1, &si, m_fence);
    vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX);
    vkFreeCommandBuffers(m_device, m_pool, 1, &cmd);
}

void WaterCoreGpu::copy(VkCommandBuffer cmd, const Buffer& from, const Buffer& to, VkDeviceSize bytes, VkDeviceSize srcOff, VkDeviceSize dstOff) const {
    VkBufferCopy region{}; region.srcOffset = srcOff; region.dstOffset = dstOff; region.size = bytes;
    vkCmdCopyBuffer(cmd, from.buf, to.buf, 1, &region);
}

void WaterCoreGpu::upload(Volume& vol, const WaterGrid& g) {
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

void WaterCoreGpu::uploadSources(Volume& vol, const std::vector<float>& ratePerCell) {
    auto* st = static_cast<char*>(vol.staging.mapped) + vol.offSrc;
    std::memset(st, 0, vol.cells * sizeof(float));
    std::memcpy(st, ratePerCell.data(), std::min(ratePerCell.size(), vol.cells) * sizeof(float));
    VkCommandBuffer cmd = beginCommands();
    copy(cmd, vol.staging, vol.src, vol.cells * sizeof(float), vol.offSrc);
    submitAndWait(cmd);
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
    const Buffer* old[3] = {&vol.uOld, &vol.vOld, &vol.wOld};
    const size_t n[3] = {vol.nu, vol.nv, vol.nw};
    for (int L = 0; L < 3; ++L) {
        Vulkan::ComputePipeline& pipe = *vol.pipes[ExtrapU + L];
        copy(cmd, *lat[L], *old[L], n[L] * sizeof(float));   // orig = the lattice before extrapolation
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
    enforceSolids();
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
    enforceSolids();
    push.mode = 1; dispatch(cmd, *vol.pipes[ColumnOps], push, vol.columns);                                  // compaction
    push.mode = 0; dispatch(cmd, *vol.pipes[FaceOpsV], push, vol.nv);                                       // gravity
    push.mode = 1; dispatchNoBarrier(cmd, *vol.pipes[FaceOpsU], push, vol.nu);                              // film slope x
    push.mode = 2; dispatchNoBarrier(cmd, *vol.pipes[FaceOpsW], push, vol.nw);                              // film slope z (disjoint lattices)
    barrier(cmd);
    push.mode = 0; dispatch(cmd, *vol.pipes[ColumnOps], push, vol.columns);                                  // settle
    enforceSolids();
    // projection
    push.mode = 0; dispatch(cmd, *vol.pipes[Classify], push, vol.cells);
    for (int s = 0; s < sweeps; ++s) for (int colour = 0; colour < 2; ++colour) {
        push.mode = 0; push.iparam = colour; push.param1 = m_omega; dispatch(cmd, *vol.pipes[Rbgs], push, vol.cells);
    }
    push.mode = 1; dispatch(cmd, *vol.pipes[Rbgs], push, vol.cells);                                         // residual
    for (int L = 0; L < 3; ++L) { push.mode = 6 + L; dispatchNoBarrier(cmd, *vol.pipes[FaceOpsU + L], push, n[L]); }   // velocity update (disjoint)
    barrier(cmd);
    recordExtrapolate(cmd, vol, push, false);
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
    for (int t = 0; t < ticks; ++t) {
        copy(cmd, vol.f, vol.fPrev, vol.cells * sizeof(float));
        barrier(cmd);
        for (int s = 0; s < n; ++s) recordSubstep(cmd, vol, push, params, sweeps, vol.quietBefore);
        push.mode = 2; push.param0 = 1e-6f; dispatch(cmd, *vol.pipes[ColumnOps], push, vol.columns);   // sweep (dropped accumulates per column over the call)
    }
    recordReductions(cmd, vol, push);
    copy(cmd, vol.out, vol.staging, kOutSlots * 16, 0, vol.offOut);
    submitAndWait(cmd);
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
    vol.quietBefore = specificKE < params.keWake;
    if (vol.quietTicks >= params.restTicks) vol.asleep = true;
    st.quietTicks = vol.quietTicks; st.asleep = vol.asleep;
    return st;
}

}}} // namespace Phyxel::Core::Water
