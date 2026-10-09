// WaterCoreGpu — docs/WaterCore.md §15.11, slice 1 (fills). See the header for the contract.
#include "core/water/WaterCoreGpu.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Phyxel { namespace Core { namespace Water {

namespace {
const char* kKernelFiles[WaterCoreGpu::KernelCount] = {
    "wc_fill_advect.comp.spv", "wc_vel_advect.comp.spv", "wc_vel_advect.comp.spv", "wc_vel_advect.comp.spv",
    "wc_face_ops.comp.spv", "wc_face_ops.comp.spv", "wc_face_ops.comp.spv",
    "wc_column_ops.comp.spv", "wc_classify.comp.spv", "wc_rbgs.comp.spv",
    "wc_extrap.comp.spv", "wc_extrap.comp.spv", "wc_extrap.comp.spv", "wc_reduce.comp.spv"};
const uint32_t kKernelBindings[WaterCoreGpu::KernelCount] = {6, 4, 4, 4, 5, 5, 5, 6, 10, 5, 9, 9, 9, 8};
} // namespace

WaterCoreGpu::~WaterCoreGpu() { shutdown(); }

bool WaterCoreGpu::init(VkDevice device, VkPhysicalDevice physical, VkQueue queue, uint32_t queueFamily,
                        const std::string& shaderDir, std::string* err) {
    m_device = device; m_physical = physical; m_queue = queue; m_queueFamily = queueFamily; m_shaderDir = shaderDir;
    VkCommandPoolCreateInfo pci{}; pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.queueFamilyIndex = queueFamily; pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
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

bool WaterCoreGpu::createBuffer(Buffer& b, VkDeviceSize bytes, std::string* err) {
    b.bytes = std::max<VkDeviceSize>(bytes, 16);
    VkBufferCreateInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = b.bytes; bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bi, nullptr, &b.buf) != VK_SUCCESS) { if (err) *err = "vkCreateBuffer failed for " + std::to_string(b.bytes) + " bytes"; return false; }
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(m_device, b.buf, &mr);
    VkMemoryAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(m_device, &ai, nullptr, &b.mem) != VK_SUCCESS) {
        // the §15.11 refusal: the byte count is in the message, the failure never reaches a dispatch
        if (err) *err = "cannot allocate " + std::to_string(mr.size) + " bytes of host-visible memory for a WaterCore GPU volume";
        vkDestroyBuffer(m_device, b.buf, nullptr); b.buf = VK_NULL_HANDLE; return false;
    }
    vkBindBufferMemory(m_device, b.buf, b.mem, 0);
    vkMapMemory(m_device, b.mem, 0, b.bytes, 0, &b.mapped);
    std::memset(b.mapped, 0, static_cast<size_t>(b.bytes));
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
    // extrapolation ping-pongs valA/knownA -> valB/knownB; the lattice itself is copied into valA at
    // the start and the finished result is copied back from valB (recordExtrapolate)
    bindAll(*vol.pipes[ExtrapU], {&vol.f, &vol.occ, &vol.valA, &vol.knownA, &vol.valB, &vol.knownB, &vol.first, &vol.keep, &vol.uOld});
    bindAll(*vol.pipes[ExtrapV], {&vol.f, &vol.occ, &vol.valA, &vol.knownA, &vol.valB, &vol.knownB, &vol.first, &vol.keep, &vol.vOld});
    bindAll(*vol.pipes[ExtrapW], {&vol.f, &vol.occ, &vol.valA, &vol.knownA, &vol.valB, &vol.knownB, &vol.first, &vol.keep, &vol.wOld});
    bindAll(*vol.pipes[Reduce],  {&vol.f, &vol.fPrev, &vol.u, &vol.v, &vol.w, &vol.part, &vol.out, &vol.part2});
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
    bool ok = createBuffer(vol->f, vol->cells * F4, err) && createBuffer(vol->fOrig, vol->cells * F4, err) && createBuffer(vol->fPrev, vol->cells * F4, err)
           && createBuffer(vol->u, vol->nu * F4, err) && createBuffer(vol->v, vol->nv * F4, err) && createBuffer(vol->w, vol->nw * F4, err)
           && createBuffer(vol->uOld, vol->nu * F4, err) && createBuffer(vol->vOld, vol->nv * F4, err) && createBuffer(vol->wOld, vol->nw * F4, err)
           && createBuffer(vol->occ, vol->cells * 4, err) && createBuffer(vol->src, vol->cells * F4, err) && createBuffer(vol->liq, vol->cells * 4, err)
           && createBuffer(vol->diag, vol->cells * F4, err) && createBuffer(vol->rhs, vol->cells * F4, err) && createBuffer(vol->p, vol->cells * F4, err) && createBuffer(vol->res, vol->cells * F4, err)
           && createBuffer(vol->valA, vol->latticeMax * F4, err) && createBuffer(vol->valB, vol->latticeMax * F4, err)
           && createBuffer(vol->knownA, vol->latticeMax * 4, err) && createBuffer(vol->knownB, vol->latticeMax * 4, err)
           && createBuffer(vol->first, vol->latticeMax * 4, err) && createBuffer(vol->keep, vol->latticeMax * 4, err)
           && createBuffer(vol->dropped, vol->columns * F4, err) && createBuffer(vol->part, vol->groups * 16, err) && createBuffer(vol->part2, vol->groups * 16, err) && createBuffer(vol->out, 16 * 4, err);
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
                      &vol->diag, &vol->rhs, &vol->p, &vol->res, &vol->valA, &vol->valB, &vol->knownA, &vol->knownB, &vol->first, &vol->keep, &vol->dropped, &vol->part, &vol->part2, &vol->out})
        destroyBuffer(*b);
    m_volumes.erase(std::remove(m_volumes.begin(), m_volumes.end(), vol), m_volumes.end());
    delete vol;
}

void WaterCoreGpu::upload(Volume& vol, const WaterGrid& g) {
    WaterGrid& gm = const_cast<WaterGrid&>(g);
    std::memcpy(vol.f.mapped, gm.fData().data(), vol.cells * sizeof(float));
    std::memcpy(vol.fPrev.mapped, gm.fData().data(), vol.cells * sizeof(float));
    std::memcpy(vol.u.mapped, gm.uData().data(), vol.nu * sizeof(float));
    std::memcpy(vol.v.mapped, gm.vData().data(), vol.nv * sizeof(float));
    std::memcpy(vol.w.mapped, gm.wData().data(), vol.nw * sizeof(float));
    auto* occ = static_cast<uint32_t*>(vol.occ.mapped);
    for (int z = 0; z < g.nz(); ++z) for (int y = 0; y < g.ny(); ++y) for (int x = 0; x < g.nx(); ++x)
        occ[g.idx(x, y, z)] = static_cast<uint32_t>(g.occ(x, y, z));
    std::memset(vol.src.mapped, 0, vol.cells * sizeof(float));
}

void WaterCoreGpu::uploadSources(Volume& vol, const std::vector<float>& ratePerCell) {
    std::memset(vol.src.mapped, 0, vol.cells * sizeof(float));
    std::memcpy(vol.src.mapped, ratePerCell.data(), std::min(ratePerCell.size(), vol.cells) * sizeof(float));
}

void WaterCoreGpu::download(Volume& vol, WaterGrid& g) const {
    std::memcpy(g.fData().data(), vol.f.mapped, vol.cells * sizeof(float));
    std::memcpy(g.uData().data(), vol.u.mapped, vol.nu * sizeof(float));
    std::memcpy(g.vData().data(), vol.v.mapped, vol.nv * sizeof(float));
    std::memcpy(g.wData().data(), vol.w.mapped, vol.nw * sizeof(float));
}

void WaterCoreGpu::barrier(VkCommandBuffer cmd) const {
    VkMemoryBarrier mb{}; mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

void WaterCoreGpu::dispatch(VkCommandBuffer cmd, Vulkan::ComputePipeline& pipe, const WcPush& push, size_t threads, uint32_t local) const {
    pipe.bind(cmd);
    pipe.pushConstants(cmd, &push, sizeof(push));
    pipe.dispatch(cmd, static_cast<uint32_t>((threads + local - 1) / local));
    barrier(cmd);
}

void WaterCoreGpu::copy(VkCommandBuffer cmd, const Buffer& from, const Buffer& to) const {
    VkBufferCopy region{}; region.size = std::min(from.bytes, to.bytes);
    vkCmdCopyBuffer(cmd, from.buf, to.buf, 1, &region);
    barrier(cmd);
}

void WaterCoreGpu::recordExtrapolate(VkCommandBuffer cmd, Volume& vol, WcPush push, bool particles) {
    const Buffer* lat[3] = {&vol.u, &vol.v, &vol.w};
    const Buffer* old[3] = {&vol.uOld, &vol.vOld, &vol.wOld};
    const size_t n[3] = {vol.nu, vol.nv, vol.nw};
    for (int L = 0; L < 3; ++L) {
        Vulkan::ComputePipeline& pipe = *vol.pipes[ExtrapU + L];
        copy(cmd, *lat[L], *old[L]);            // orig = the lattice before extrapolation
        copy(cmd, *lat[L], vol.valA);
        push.mode = L; push.iparam = particles ? 1 : 0;
        dispatch(cmd, pipe, push, n[L]);         // masks: knownB, keep; valB = val
        // layers: (valB, knownB) -> swap into A -> layer -> B
        for (int layer = 0; layer < 3; ++layer) {
            copy(cmd, vol.valB, vol.valA); copy(cmd, vol.knownB, vol.knownA);
            push.mode = 3 + L; push.iparam = layer == 0 ? 100 : 0;
            dispatch(cmd, pipe, push, n[L]);
        }
        copy(cmd, vol.valB, vol.valA); copy(cmd, vol.knownB, vol.knownA);
        push.mode = 6 + L; push.iparam = particles ? 1 : 0;
        dispatch(cmd, pipe, push, n[L]);         // finish -> valB
        copy(cmd, vol.valB, *lat[L]);
    }
    // extrapolation must not write into solid faces
    for (int L = 0; L < 3; ++L) { push.mode = 3 + L; dispatch(cmd, *vol.pipes[FaceOpsU + L], push, n[L]); }
}

void WaterCoreGpu::recordSubstep(VkCommandBuffer cmd, Volume& vol, WcPush push, const SolverParams& params, int sweeps, bool quietBefore) {
    const size_t n[3] = {vol.nu, vol.nv, vol.nw};
    // (sources: the pump term is in the projection; the fill placement of a source is host-side in slice 1)
    for (int L = 0; L < 3; ++L) { push.mode = 3 + L; dispatch(cmd, *vol.pipes[FaceOpsU + L], push, n[L]); }   // enforce solids
    // transport: fills (6 checkerboard passes), then velocities from the old copies
    copy(cmd, vol.f, vol.fOrig);
    copy(cmd, vol.u, vol.uOld); copy(cmd, vol.v, vol.vOld); copy(cmd, vol.w, vol.wOld);
    for (int dir = 0; dir < 3; ++dir) for (int parity = 0; parity < 2; ++parity) {
        push.mode = dir; push.iparam = parity;
        dispatch(cmd, *vol.pipes[FillAdvect], push, n[dir]);
    }
    for (int L = 0; L < 3; ++L) { push.mode = L; dispatch(cmd, *vol.pipes[VelAdvectU + L], push, n[L]); }
    for (int L = 0; L < 3; ++L) { push.mode = 3 + L; dispatch(cmd, *vol.pipes[FaceOpsU + L], push, n[L]); }   // enforce solids
    push.mode = 1; dispatch(cmd, *vol.pipes[ColumnOps], push, vol.columns);                                  // compaction
    // forces
    push.mode = 0; dispatch(cmd, *vol.pipes[FaceOpsV], push, vol.nv);                                       // gravity
    push.mode = 1; dispatch(cmd, *vol.pipes[FaceOpsU], push, vol.nu);                                       // film slope x
    push.mode = 2; dispatch(cmd, *vol.pipes[FaceOpsW], push, vol.nw);                                       // film slope z
    push.mode = 0; dispatch(cmd, *vol.pipes[ColumnOps], push, vol.columns);                                  // settle
    for (int L = 0; L < 3; ++L) { push.mode = 3 + L; dispatch(cmd, *vol.pipes[FaceOpsU + L], push, n[L]); }
    // projection
    push.mode = 0; dispatch(cmd, *vol.pipes[Classify], push, vol.cells);
    for (int s = 0; s < sweeps; ++s) for (int colour = 0; colour < 2; ++colour) {
        push.mode = 0; push.iparam = colour; push.param1 = m_omega; dispatch(cmd, *vol.pipes[Rbgs], push, vol.cells);
    }
    push.mode = 1; dispatch(cmd, *vol.pipes[Rbgs], push, vol.cells);                                         // residual
    for (int L = 0; L < 3; ++L) { push.mode = 6 + L; dispatch(cmd, *vol.pipes[FaceOpsU + L], push, n[L]); }  // velocity update
    recordExtrapolate(cmd, vol, push, false);
    push.mode = 0; dispatch(cmd, *vol.pipes[ColumnOps], push, vol.columns);                                  // settle again
    if (quietBefore) {
        const float k = std::max(0.0f, 1.0f - params.restDamping * push.dt);
        push.mode = 9; push.param0 = k;   // scale every lattice (wc_face_ops mode 9)
        for (int L = 0; L < 3; ++L) { push.iparam = static_cast<int32_t>(n[L]); dispatch(cmd, *vol.pipes[FaceOpsU + L], push, n[L]); }
    }
}

void WaterCoreGpu::recordReductions(VkCommandBuffer cmd, Volume& vol, WcPush push, bool speedOnly) {
    // one reduction = a per-workgroup partial pass, then a fixed tree of folds that alternates
    // part -> part2 -> part ... until one workgroup remains and writes out[slot] (no atomics)
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
    if (!speedOnly) { push.mode = 0; dispatch(cmd, *vol.pipes[Reduce], push, vol.cells, 256); fold((vol.cells + 255) / 256, 0); }
    for (int L = 0; L < 3; ++L) {
        const size_t n = L == 0 ? vol.nu : (L == 1 ? vol.nv : vol.nw);
        push.mode = 1; push.iparam = L; dispatch(cmd, *vol.pipes[Reduce], push, n, 256);
        fold((n + 255) / 256, 1 + L);
    }
}

void WaterCoreGpu::submitAndWait(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    vkResetFences(m_device, 1, &m_fence);
    vkQueueSubmit(m_queue, 1, &si, m_fence);
    vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX);
}

GpuStepStats WaterCoreGpu::step(Volume& vol, const SolverParams& params, float dt, int ticks, int sweeps) {
    GpuStepStats st; st.ticks = ticks; st.sweeps = sweeps;
    WcPush push; push.nx = vol.spec.dims.x; push.ny = vol.spec.dims.y; push.nz = vol.spec.dims.z;
    push.h = vol.spec.h; push.thr = params.liquidThreshold; push.gravity = params.gravity; push.filmHold = params.filmHoldDepth / vol.spec.h;
    VkCommandBufferAllocateInfo cai{}; cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = m_pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
    VkCommandBuffer cmd; vkAllocateCommandBuffers(m_device, &cai, &cmd);
    auto* out = static_cast<float*>(vol.out.mapped);
    for (int t = 0; t < ticks; ++t) {
        if (vol.asleep) { st.asleep = true; st.quietTicks = vol.quietTicks; break; }
        // 1. max speed for the CFL count (one small submission; the CPU reads it from the grid directly)
        VkCommandBufferBeginInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);
        copy(cmd, vol.f, vol.fPrev);
        recordReductions(cmd, vol, push, true);
        submitAndWait(cmd);
        const double vmax = std::max({out[4 + 3], out[8 + 3], out[12 + 3]}) + params.gravity * dt;
        const double limit = params.cflFraction * vol.spec.h;
        const int n = std::clamp(static_cast<int>(std::ceil(vmax * dt / limit)), 1, params.maxSubsteps);
        const float ds = dt / static_cast<float>(n);
        push.dt = ds;
        // 2. the tick
        vkResetCommandBuffer(cmd, 0);
        vkBeginCommandBuffer(cmd, &bi);
        for (int s = 0; s < n; ++s) recordSubstep(cmd, vol, push, params, sweeps, vol.quietBefore);
        push.mode = 2; push.param0 = 1e-6f; dispatch(cmd, *vol.pipes[ColumnOps], push, vol.columns);   // sweep
        recordReductions(cmd, vol, push, false);
        submitAndWait(cmd);
        vkResetCommandBuffer(cmd, 0);
        // 3. host-side rest bookkeeping from the fixed-tree reductions
        st.substepsLast = n;
        st.kineticEnergy = out[0]; st.maxDeltaF = out[1]; st.totalMass = static_cast<double>(out[2]) * vol.spec.h * vol.spec.h * vol.spec.h;
        st.maxSpeed = std::max({out[4 + 3], out[8 + 3], out[12 + 3]});
        const auto* dropped = static_cast<const float*>(vol.dropped.mapped);
        double drop = 0.0; for (size_t i = 0; i < vol.columns; ++i) drop += dropped[i];
        st.residueDropped += drop;
        const double specificKE = st.kineticEnergy / std::max(st.totalMass / (vol.spec.h * vol.spec.h * vol.spec.h) * (vol.spec.h * vol.spec.h * vol.spec.h), 1e-9);
        const bool quiet = specificKE < params.keWake && st.maxDeltaF < params.maxDeltaFQuiet;
        vol.quietTicks = quiet ? vol.quietTicks + 1 : 0;
        vol.quietBefore = specificKE < params.keWake;
        if (vol.quietTicks >= params.restTicks) vol.asleep = true;
        st.quietTicks = vol.quietTicks; st.asleep = vol.asleep;
        const auto* res = static_cast<const float*>(vol.res.mapped);
        float rmax = 0.0f; for (size_t i = 0; i < vol.cells; ++i) rmax = std::max(rmax, res[i]);
        st.rbgsResidualMax = rmax;
    }
    vkFreeCommandBuffers(m_device, m_pool, 1, &cmd);
    return st;
}

}}} // namespace Phyxel::Core::Water
