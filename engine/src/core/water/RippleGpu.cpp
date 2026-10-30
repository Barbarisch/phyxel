#include "core/water/RippleGpu.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Phyxel::Core::Water {

namespace {

// shaders/wc_ripple.comp's push block
struct RipplePush {
    int32_t nx = 0, nz = 0, mode = 0, n = 0;
    int32_t oR = 0, oPrev = 0, oOut = 0, oOut2 = 0;
    float c0 = 0.0f, c1 = 0.0f, c2 = 0.0f, ci = 0.0f;
    float ds = 0.0f, dtAdv = 0.0f, lim0 = 0.0f, lim1 = 0.0f;
    float tol = 0.0f, pitch = 0.0f; int32_t openBorder = 0, nComp = 0;
    int32_t argBase = 0, groups = 0, impAny = 0, kinAny = 0;
};
static_assert(sizeof(RipplePush) == 96, "RipplePush must match wc_ripple.comp");
// shaders/wc_ripple_limit.comp's push block
struct LimitPush {
    int32_t nx = 0, nz = 0, oIn = 0, oOut = 0;
    int32_t tilesX = 0, fault = 0, pad0 = 0, pad1 = 0;
    float lim0 = 0.0f, lim1 = 0.0f, tol = 0.0f, pad2 = 0.0f;
};
static_assert(sizeof(LimitPush) == 48, "LimitPush must match wc_ripple_limit.comp");

enum Mode : int32_t { Advect = 0, Combine, Conv, Update, CompSum, CompFinal, CompApply };
constexpr float kRhoWater = 1000.0f;           // RippleLayer.cpp kRho
constexpr float kLimitTol = 1e-4f;             // RippleLayer::limitSlope's kTol

double msSince(std::chrono::steady_clock::time_point t) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count(); }

}  // namespace

RippleGpu::~RippleGpu() { shutdown(); }

bool RippleGpu::init(VkDevice device, VkPhysicalDevice physical, VkQueue queue, uint32_t queueFamily, const std::string& shaderDir, std::string* err) {
    m_device = device; m_physical = physical; m_queue = queue; m_family = queueFamily;
    m_spv = shaderDir + "/wc_ripple.comp.spv"; m_spvLimit = shaderDir + "/wc_ripple_limit.comp.spv";
    for (const std::string* path : {&m_spv, &m_spvLimit}) { FILE* f = std::fopen(path->c_str(), "rb"); if (!f) { if (err) *err = "missing " + *path; m_device = VK_NULL_HANDLE; return false; } std::fclose(f); }
    VkCommandPoolCreateInfo pi{}; pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pi.queueFamilyIndex = queueFamily;
    if (vkCreateCommandPool(device, &pi, nullptr, &m_pool) != VK_SUCCESS) { if (err) *err = "vkCreateCommandPool failed"; m_device = VK_NULL_HANDLE; return false; }
    VkFenceCreateInfo fi{}; fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    for (Sub& sb : m_subs) vkCreateFence(device, &fi, nullptr, &sb.fence);
    // device time of a submission: two timestamps (0 when the queue family has no valid timestamp bits)
    uint32_t nq = 0; vkGetPhysicalDeviceQueueFamilyProperties(physical, &nq, nullptr);
    std::vector<VkQueueFamilyProperties> qp(nq); vkGetPhysicalDeviceQueueFamilyProperties(physical, &nq, qp.data());
    VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(physical, &props);
    if (queueFamily < nq && qp[queueFamily].timestampValidBits > 0) {
        VkQueryPoolCreateInfo qi{}; qi.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO; qi.queryType = VK_QUERY_TYPE_TIMESTAMP; qi.queryCount = 2 * kRing;
        if (vkCreateQueryPool(device, &qi, nullptr, &m_queries) == VK_SUCCESS) m_tsPeriodNs = props.limits.timestampPeriod;
    }
    return true;
}

void RippleGpu::waitIdle() {
    for (Sub& sb : m_subs) if (sb.active) vkWaitForFences(m_device, 1, &sb.fence, VK_TRUE, UINT64_MAX);
}

void RippleGpu::shutdown() {
    if (m_device == VK_NULL_HANDLE) return;
    waitIdle();
    if (m_cmd) { vkEndCommandBuffer(m_cmd); vkFreeCommandBuffers(m_device, m_pool, 1, &m_cmd); m_cmd = VK_NULL_HANDLE; }
    for (Sub& sb : m_subs) { if (sb.cmd) vkFreeCommandBuffers(m_device, m_pool, 1, &sb.cmd); sb.cmd = VK_NULL_HANDLE; sb.active = false; sb.layers.clear(); }
    for (auto& [id, s] : m_states) destroyState(s);
    m_states.clear();
    if (m_queries) vkDestroyQueryPool(m_device, m_queries, nullptr);
    for (Sub& sb : m_subs) { if (sb.fence) vkDestroyFence(m_device, sb.fence, nullptr); sb.fence = VK_NULL_HANDLE; }
    if (m_pool) vkDestroyCommandPool(m_device, m_pool, nullptr);
    m_queries = VK_NULL_HANDLE; m_pool = VK_NULL_HANDLE; m_device = VK_NULL_HANDLE;
}

bool RippleGpu::createBuffer(Buf& b, VkDeviceSize bytes, VkBufferUsageFlags extra, int kind, std::string* err) {
    b.bytes = std::max<VkDeviceSize>(bytes, 16);
    VkBufferCreateInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = b.bytes; bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | extra;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bi, nullptr, &b.buf) != VK_SUCCESS) { if (err) *err = "vkCreateBuffer failed (" + std::to_string(b.bytes) + " bytes)"; return false; }
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(m_device, b.buf, &mr);
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(m_physical, &mp);
    auto find = [&](VkMemoryPropertyFlags want) -> uint32_t {
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
            if ((mr.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
        return UINT32_MAX;
    };
    uint32_t type = UINT32_MAX;
    if (kind == 0) type = find(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    else if (kind == 1) type = find(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    else {   // readback: CACHED (uncached host reads of a whole layer per frame are slow), coherent if there is such a type
        type = find(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (type == UINT32_MAX) type = find(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
        if (type == UINT32_MAX) type = find(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }
    VkMemoryAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; ai.allocationSize = mr.size; ai.memoryTypeIndex = type;
    if (type == UINT32_MAX || vkAllocateMemory(m_device, &ai, nullptr, &b.mem) != VK_SUCCESS) {
        if (err) *err = "cannot allocate " + std::to_string(mr.size) + " bytes for a ripple layer on the GPU";
        vkDestroyBuffer(m_device, b.buf, nullptr); b.buf = VK_NULL_HANDLE; return false;
    }
    vkBindBufferMemory(m_device, b.buf, b.mem, 0);
    if (kind != 0) {
        vkMapMemory(m_device, b.mem, 0, VK_WHOLE_SIZE, 0, &b.mapped);
        b.coherent = (mp.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    }
    return true;
}

void RippleGpu::destroyBuffer(Buf& b) {
    if (b.mapped) vkUnmapMemory(m_device, b.mem);
    if (b.buf) vkDestroyBuffer(m_device, b.buf, nullptr);
    if (b.mem) vkFreeMemory(m_device, b.mem, nullptr);
    b = Buf{};
}

void RippleGpu::destroyState(State& s) {
    if (s.pipe) { s.pipe->cleanup(); s.pipe.reset(); }
    if (s.limit) { s.limit->cleanup(); s.limit.reset(); }
    for (Buf* b : {&s.state, &s.tmp, &s.inp, &s.vel, &s.kern, &s.lab, &s.cells, &s.chunks, &s.part, &s.range, &s.means, &s.broken, &s.prevDown})
        destroyBuffer(*b);
    for (int k = 0; k < kRing; ++k) { destroyBuffer(s.up[k]); destroyBuffer(s.down[k]); }
}

RippleGpu::State* RippleGpu::stateFor(RippleLayer& L, std::string* err) {
    auto it = m_states.find(L.uid());
    if (it != m_states.end() && it->second.nx == L.nx() && it->second.nz == L.nz()) return &it->second;
    if (it != m_states.end()) { waitIdle(); destroyState(it->second); m_states.erase(it); }
    State s;
    s.uid = L.uid(); s.nx = L.nx(); s.nz = L.nz(); s.n = static_cast<size_t>(s.nx) * s.nz;
    const VkDeviceSize n = s.n, groups = (n + 255) / 256;
    const VkDeviceSize maxChunks = groups + n + 1;   // sum over bodies of ceil(cells / 256) <= n / 256 + bodies
    s.tilesX = (s.nx + 15) / 16; s.tiles = s.tilesX * ((s.nz + 15) / 16);
    const VkDeviceSize tiles = static_cast<VkDeviceSize>(s.tiles);
    bool ok = createBuffer(s.state, 16 * n, 0, 0, err) && createBuffer(s.tmp, 8 * n, 0, 0, err) && createBuffer(s.inp, 12 * n, 0, 0, err)
           && createBuffer(s.vel, 8 * n, 0, 0, err) && createBuffer(s.kern, 4 * 169, 0, 0, err) && createBuffer(s.lab, 4 * n, 0, 0, err)
           && createBuffer(s.cells, 4 * n, 0, 0, err) && createBuffer(s.chunks, 16 * maxChunks, 0, 0, err) && createBuffer(s.part, 8 * maxChunks, 0, 0, err)
           && createBuffer(s.range, 16 * (n + 1), 0, 0, err) && createBuffer(s.means, 8 * (n + 1), 0, 0, err) && createBuffer(s.broken, 4 * tiles, 0, 0, err)
           && createBuffer(s.prevDown, 4 * n, 0, 2, err);
    for (int k = 0; k < kRing && ok; ++k)
        ok = createBuffer(s.up[k], 16 * n + 12 * n + 8 * n + 8 * n + 16 * maxChunks + 16 * (n + 1) + 4 * 169 + 256, 0, 1, err)
          && createBuffer(s.down[k], 4 * n + 4 * tiles, 0, 2, err);   // r | broken per tile
    if (ok) {
        s.pipe = std::make_unique<Vulkan::ComputePipeline>();
        s.limit = std::make_unique<Vulkan::ComputePipeline>();
        ok = s.pipe->create(m_device, m_spv, 11, sizeof(RipplePush)) && s.limit->create(m_device, m_spvLimit, 3, sizeof(LimitPush));
        if (!ok && err) *err = "wc_ripple pipelines failed (" + m_spv + ", " + m_spvLimit + ")";
    }
    if (!ok) { destroyState(s); return nullptr; }
    Buf* binds[11] = {&s.state, &s.tmp, &s.inp, &s.vel, &s.kern, &s.lab, &s.cells, &s.chunks, &s.part, &s.range, &s.means};
    for (uint32_t b = 0; b < 11; ++b) s.pipe->bindBuffer(b, binds[b]->buf, binds[b]->bytes);
    s.pipe->updateDescriptors();
    s.limit->bindBuffer(0, s.state.buf, s.state.bytes); s.limit->bindBuffer(1, s.lab.buf, s.lab.bytes); s.limit->bindBuffer(2, s.broken.buf, s.broken.bytes);
    s.limit->updateDescriptors();
    s.labelRev = ~0u; s.dirty = true;
    auto [pos, _] = m_states.emplace(s.uid, std::move(s));
    return &pos->second;
}

void RippleGpu::ensureCommands() {
    if (m_cmd) return;
    VkCommandBufferAllocateInfo cai{}; cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = m_pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
    vkAllocateCommandBuffers(m_device, &cai, &m_cmd);
    VkCommandBufferBeginInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(m_cmd, &bi);
    if (m_queries) { vkCmdResetQueryPool(m_cmd, m_queries, 2 * m_cur, 2); vkCmdWriteTimestamp(m_cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_queries, 2 * m_cur); }
    m_building = Stats{};
}

void RippleGpu::barrier(VkCommandBuffer cmd) const {
    VkMemoryBarrier mb{}; mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

void RippleGpu::dispatch(State& s, int mode, const void* push, uint32_t groups) {
    RipplePush p = *static_cast<const RipplePush*>(push); p.mode = mode;
    s.pipe->bind(m_cmd);
    s.pipe->pushConstants(m_cmd, &p, sizeof(p));
    if (groups > 0) s.pipe->dispatch(m_cmd, groups);
    barrier(m_cmd);
    ++m_building.dispatches;
}

VkDeviceSize RippleGpu::stage(State& s, const void* data, size_t bytes) {
    const VkDeviceSize off = (s.upUsed + 15) & ~static_cast<VkDeviceSize>(15);
    std::memcpy(static_cast<char*>(s.up[m_cur].mapped) + off, data, bytes);
    s.upUsed = off + bytes;
    m_building.uploadBytes += static_cast<long>(bytes);
    return off;
}

void RippleGpu::copyUp(State& s, VkDeviceSize upOff, Buf& dst, VkDeviceSize bytes, VkDeviceSize dstOff) {
    VkBufferCopy c{}; c.srcOffset = upOff; c.dstOffset = dstOff; c.size = bytes;
    vkCmdCopyBuffer(m_cmd, s.up[m_cur].buf, dst.buf, 1, &c);
}

bool RippleGpu::record(RippleLayer& L, float dt, const std::vector<glm::vec2>* vel, std::string* err) {
    if (!ready()) return false;
    const auto t0 = std::chrono::steady_clock::now();
    const float tAdv = vel ? L.planAdvect(*vel, dt) : 0.0f;
    const int nSub = L.planStep(dt);
    if (nSub <= 0 && tAdv <= 0.0f) return false;
    State* sp = stateFor(L, err);
    if (!sp) return false;
    State& s = *sp;
    ensureCommands();
    s.upUsed = 0;
    const size_t N = s.n;
    const uint32_t groups = static_cast<uint32_t>((N + 255) / 256);
    L.labelComponents();
    if (s.labelRev == ~0u) {   // a new state: the kernel once
        const VkDeviceSize o = stage(s, L.m_kernel.data(), 4 * L.m_kernel.size());
        copyUp(s, o, s.kern, 4 * L.m_kernel.size());
    }
    if (s.labelRev != L.m_labelRev) {   // the bodies: labels, cells sorted by body, 256-cell chunks, per-body chunk ranges
        s.nComp = L.m_nComp;
        std::vector<int32_t> lab(L.m_label.begin(), L.m_label.end());
        std::vector<uint32_t> count(static_cast<size_t>(s.nComp), 0), first(static_cast<size_t>(s.nComp) + 1, 0);
        for (int v : lab) if (v >= 0) ++count[v];
        for (int c = 0; c < s.nComp; ++c) first[c + 1] = first[c] + count[c];
        std::vector<uint32_t> cells(first[s.nComp]), cursor(first.begin(), first.end() - 1);
        for (size_t i = 0; i < N; ++i) if (lab[i] >= 0) cells[cursor[lab[i]]++] = static_cast<uint32_t>(i);
        std::vector<uint32_t> chunks, range;
        for (int c = 0; c < s.nComp; ++c) {
            const uint32_t firstChunk = static_cast<uint32_t>(chunks.size() / 4);
            for (uint32_t k = 0; k < count[c]; k += 256) { chunks.push_back(static_cast<uint32_t>(c)); chunks.push_back(first[c] + k); chunks.push_back(std::min(256u, count[c] - k)); chunks.push_back(0); }
            range.push_back(firstChunk); range.push_back(static_cast<uint32_t>(chunks.size() / 4) - firstChunk); range.push_back(count[c]); range.push_back(0);
        }
        s.nChunks = static_cast<int>(chunks.size() / 4);
        copyUp(s, stage(s, lab.data(), 4 * N), s.lab, 4 * N);
        if (!cells.empty()) copyUp(s, stage(s, cells.data(), 4 * cells.size()), s.cells, 4 * cells.size());
        if (!chunks.empty()) copyUp(s, stage(s, chunks.data(), 4 * chunks.size()), s.chunks, 4 * chunks.size());
        if (!range.empty()) copyUp(s, stage(s, range.data(), 4 * range.size()), s.range, 4 * range.size());
        s.labelRev = L.m_labelRev;
    }
    if (s.dirty) {   // the CPU's levels (a new layer, a shift, a pin, sleep's zero)
        copyUp(s, stage(s, L.m_r.data(), 4 * N), s.state, 4 * N, 4 * N * static_cast<VkDeviceSize>(s.slot[0]));
        copyUp(s, stage(s, L.m_prev.data(), 4 * N), s.state, 4 * N, 4 * N * static_cast<VkDeviceSize>(s.slot[1]));
        s.dirty = false;
    }
    const bool imp = nSub > 0 && L.m_anyImpulse, kin = nSub > 0 && L.m_anyKin;
    if (imp) copyUp(s, stage(s, L.m_impulse.data(), 4 * N), s.inp, 4 * N, 0);
    if (kin) {
        copyUp(s, stage(s, L.m_kin.data(), 4 * N), s.inp, 4 * N, 4 * N);
        std::vector<float> set(N); for (size_t i = 0; i < N; ++i) set[i] = L.m_kinSet[i] ? 1.0f : 0.0f;
        copyUp(s, stage(s, set.data(), 4 * N), s.inp, 4 * N, 8 * N);
        std::fill(L.m_kinSet.begin(), L.m_kinSet.end(), 0); L.m_anyKin = false;   // consumed (the CPU clears them after its step)
    }
    if (tAdv > 0.0f) copyUp(s, stage(s, vel->data(), 8 * N), s.vel, 8 * N);
    const bool lim = L.m_breakSlope > 0.0f && nSub > 0;
    vkCmdFillBuffer(m_cmd, s.broken.buf, 0, s.broken.bytes, 0);
    barrier(m_cmd);

    RipplePush p{};
    p.nx = s.nx; p.nz = s.nz; p.n = static_cast<int32_t>(N); p.pitch = RippleLayer::kPitch;
    p.lim0 = L.m_breakSlope * RippleLayer::kPitch; p.lim1 = L.m_breakSlope * RippleLayer::kPitch * 1.41421356f; p.tol = kLimitTol;
    p.openBorder = L.m_openBorder ? 1 : 0; p.nComp = s.nComp; p.groups = static_cast<int32_t>(groups);
    LimitPush lp{};
    lp.nx = s.nx; lp.nz = s.nz; lp.tilesX = s.tilesX; lp.fault = m_limiterFault ? 1 : 0; lp.lim0 = p.lim0; lp.lim1 = p.lim1; lp.tol = p.tol;
    const float ds = RippleLayer::kStep, damp = L.damping() * ds;   // the film's + A12's hand-over (45.1)
    p.ds = ds; p.c0 = 2.0f / (1.0f + damp); p.c1 = (1.0f - damp) / (1.0f + damp); p.c2 = L.m_g * ds * ds / (1.0f + damp); p.ci = p.c1 * ds / kRhoWater;
    const VkDeviceSize F = static_cast<VkDeviceSize>(N);
    auto off = [&](int slotIndex) { return static_cast<int32_t>(F * static_cast<VkDeviceSize>(s.slot[slotIndex])); };

    if (tAdv > 0.0f) {   // RippleLayer::advect: both levels into the free slots, then they ARE the levels
        p.oR = off(0); p.oPrev = off(1); p.oOut = off(2); p.oOut2 = off(3); p.dtAdv = tAdv;
        dispatch(s, Advect, &p, groups);
        const int a0 = s.slot[0], a1 = s.slot[1];
        s.slot[0] = s.slot[2]; s.slot[1] = s.slot[3]; s.slot[2] = a0; s.slot[3] = a1;
    }
    for (int sub = 0; sub < nSub; ++sub) {
        p.oR = off(0); p.oPrev = off(1); p.oOut = off(2);
        p.impAny = (imp && sub == 0) ? 1 : 0; p.kinAny = kin ? 1 : 0;
        dispatch(s, Combine, &p, groups);
        dispatch(s, Conv, &p, groups);
        dispatch(s, Update, &p, groups);
        if (s.nComp > 0) {
            dispatch(s, CompSum, &p, static_cast<uint32_t>(s.nChunks));
            dispatch(s, CompFinal, &p, static_cast<uint32_t>((s.nComp + 255) / 256));
            dispatch(s, CompApply, &p, groups);
        }
        if (lim) for (int which : {2, 0}) {   // the new level, then the one becoming the previous: read it, write the free slot 3
            lp.oIn = static_cast<int32_t>(F * static_cast<VkDeviceSize>(s.slot[which]));
            lp.oOut = static_cast<int32_t>(F * static_cast<VkDeviceSize>(s.slot[3]));
            s.limit->bind(m_cmd);
            s.limit->pushConstants(m_cmd, &lp, sizeof(lp));
            s.limit->dispatch(m_cmd, static_cast<uint32_t>(s.tiles));
            barrier(m_cmd);
            ++m_building.dispatches;
            std::swap(s.slot[which], s.slot[3]);
        }
        const int r = s.slot[0], pr = s.slot[1];   // prev <- r, r <- next, the old prev is free
        s.slot[0] = s.slot[2]; s.slot[1] = r; s.slot[2] = pr;
    }
    {   // r and the broken partials back
        VkBufferCopy c[2]{};
        c[0].srcOffset = 4 * F * static_cast<VkDeviceSize>(s.slot[0]); c[0].dstOffset = 0; c[0].size = 4 * F;
        vkCmdCopyBuffer(m_cmd, s.state.buf, s.down[m_cur].buf, 1, &c[0]);
        c[1].srcOffset = 0; c[1].dstOffset = 4 * F; c[1].size = 4 * static_cast<VkDeviceSize>(s.tiles);
        vkCmdCopyBuffer(m_cmd, s.broken.buf, s.down[m_cur].buf, 1, &c[1]);
        barrier(m_cmd);
    }
    if (imp) { std::fill(L.m_impulse.begin(), L.m_impulse.end(), 0.0f); L.m_anyImpulse = false; }
    m_subs[m_cur].layers.push_back({s.uid, nSub > 0});
    ++m_building.layers; if (nSub > 0) ++m_building.stepped;
    m_building.recordMs += msSince(t0);
    return true;
}

void RippleGpu::submit() {
    if (!m_cmd) return;
    const auto t0 = std::chrono::steady_clock::now();
    if (m_queries) vkCmdWriteTimestamp(m_cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_queries, 2 * m_cur + 1);
    vkEndCommandBuffer(m_cmd);
    Sub& sb = m_subs[m_cur];
    VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; si.commandBufferCount = 1; si.pCommandBuffers = &m_cmd;
    vkResetFences(m_device, 1, &sb.fence);
    if (vkQueueSubmit(m_queue, 1, &si, sb.fence) != VK_SUCCESS) std::fprintf(stderr, "[RippleGpu] vkQueueSubmit failed\n");
    sb.cmd = m_cmd; sb.active = true;
    m_cmd = VK_NULL_HANDLE;
    m_cur = (m_cur + 1) % kRing;
    m_building.recordMs += msSince(t0);
    m_stats.layers = m_building.layers; m_stats.stepped = m_building.stepped; m_stats.dispatches = m_building.dispatches;
    m_stats.recordMs = m_building.recordMs; m_stats.uploadBytes = m_building.uploadBytes;
}

void RippleGpu::consume(int k, const std::vector<RippleLayer*>& live) {
    Sub& sb = m_subs[k];
    if (sb.cmd) { vkFreeCommandBuffers(m_device, m_pool, 1, &sb.cmd); sb.cmd = VK_NULL_HANDLE; }
    if (m_queries) {
        uint64_t ts[2] = {0, 0};
        if (vkGetQueryPoolResults(m_device, m_queries, 2 * k, 2, sizeof(ts), ts, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
            m_stats.gpuMs = static_cast<double>(ts[1] - ts[0]) * m_tsPeriodNs * 1e-6;
    }
    for (const auto& [uid, stepped] : sb.layers) {
        RippleLayer* L = nullptr;
        for (RippleLayer* l : live) if (l && l->uid() == uid) { L = l; break; }
        auto it = m_states.find(uid);
        if (!L || it == m_states.end() || it->second.dirty) continue;   // gone, or the CPU's copy is newer (dropped)
        State& s = it->second;
        const size_t N = s.n, tiles = static_cast<size_t>(s.tiles);
        Buf& d = s.down[k];
        if (!d.coherent) {
            VkMappedMemoryRange mr{}; mr.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE; mr.memory = d.mem; mr.offset = 0; mr.size = VK_WHOLE_SIZE;
            vkInvalidateMappedMemoryRanges(m_device, 1, &mr);
        }
        const float* f = static_cast<const float*>(d.mapped);
        std::memcpy(L->m_r.data(), f, 4 * N);
        double broken = 0.0;
        for (size_t t = 0; t < tiles; ++t) broken += f[N + t];
        L->m_broken += broken * static_cast<double>(RippleLayer::kPitch) * RippleLayer::kPitch;
        m_stats.readBytes += static_cast<long>(4 * (N + tiles));
        if (stepped) {
            L->sleepRule();
            if (L->m_asleep) s.dirty = true;   // sleep zeroed the CPU's levels: the device's follow before the next step
        }
    }
    sb.layers.clear();
    sb.active = false;
}

void RippleGpu::collect(const std::vector<RippleLayer*>& live) {
    if (!ready()) return;
    const auto t0 = std::chrono::steady_clock::now();
    m_stats.waitMs = 0.0; m_stats.readBytes = 0;
    // oldest first: the slot about to be reused (m_cur) is the oldest; the queue runs in order, so the first unsignalled
    // submission ends the scan - except the slot this frame needs, which is waited for (the ring is full: the GPU is
    // kRing frames behind)
    for (int i = 0; i < kRing; ++i) {
        const int k = (m_cur + i) % kRing;
        Sub& sb = m_subs[k];
        if (!sb.active) continue;
        if (vkGetFenceStatus(m_device, sb.fence) != VK_SUCCESS) {
            if (k != m_cur) break;
            const auto tw = std::chrono::steady_clock::now();
            vkWaitForFences(m_device, 1, &sb.fence, VK_TRUE, UINT64_MAX);
            m_stats.waitMs += msSince(tw);
        }
        consume(k, live);
    }
    // device state of layers that no longer exist and that nothing in flight still uses
    for (auto it = m_states.begin(); it != m_states.end(); ) {
        bool alive = false;
        for (RippleLayer* L : live) if (L && L->uid() == it->first) { alive = true; break; }
        for (const Sub& sb : m_subs) if (sb.active) for (const auto& e : sb.layers) if (e.first == it->first) alive = true;
        if (alive) { ++it; continue; }
        destroyState(it->second); it = m_states.erase(it);
    }
    m_stats.collectMs = msSince(t0);
}

void RippleGpu::drain(const std::vector<RippleLayer*>& live) {
    if (!ready()) return;
    for (int i = 0; i < kRing; ++i) {
        const int k = (m_cur + i) % kRing;
        if (!m_subs[k].active) continue;
        vkWaitForFences(m_device, 1, &m_subs[k].fence, VK_TRUE, UINT64_MAX);
        consume(k, live);
    }
}

void RippleGpu::syncPrev(RippleLayer& L, const std::vector<RippleLayer*>& live) {
    if (!ready()) return;
    drain(live);   // everything in flight in: the CPU's r is the device's newest
    auto it = m_states.find(L.uid());
    if (it == m_states.end() || it->second.dirty) return;   // the CPU's copy is the newest
    State& s = it->second;
    VkCommandBufferAllocateInfo cai{}; cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = m_pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
    VkCommandBuffer cmd; vkAllocateCommandBuffers(m_device, &cai, &cmd);
    VkCommandBufferBeginInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    const VkDeviceSize N = s.n;
    VkBufferCopy c{}; c.srcOffset = 4 * N * static_cast<VkDeviceSize>(s.slot[1]); c.dstOffset = 0; c.size = 4 * N;
    vkCmdCopyBuffer(cmd, s.state.buf, s.prevDown.buf, 1, &c);
    vkEndCommandBuffer(cmd);
    VkFenceCreateInfo fi{}; fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence f; vkCreateFence(m_device, &fi, nullptr, &f);
    VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    vkQueueSubmit(m_queue, 1, &si, f);
    vkWaitForFences(m_device, 1, &f, VK_TRUE, UINT64_MAX);
    vkDestroyFence(m_device, f, nullptr);
    vkFreeCommandBuffers(m_device, m_pool, 1, &cmd);
    if (!s.prevDown.coherent) {
        VkMappedMemoryRange mr{}; mr.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE; mr.memory = s.prevDown.mem; mr.offset = 0; mr.size = VK_WHOLE_SIZE;
        vkInvalidateMappedMemoryRanges(m_device, 1, &mr);
    }
    std::memcpy(L.m_prev.data(), s.prevDown.mapped, 4 * N);
}

void RippleGpu::markDirty(RippleLayer& L) {
    auto it = m_states.find(L.uid());
    if (it != m_states.end()) it->second.dirty = true;
}

}  // namespace Phyxel::Core::Water
