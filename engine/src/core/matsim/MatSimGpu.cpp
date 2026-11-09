// docs/MaterialSimPlan.md §6c — MatSim P1b: the GPU port of MatSimCore::step. See the header.
#include "core/matsim/MatSimGpu.h"
#include "vulkan/ComputePipeline.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace Phyxel::Core::MatSim {

namespace {
constexpr int kChunk = 256;   // ticks per submission (the stats buffer holds one slot per tick)
constexpr int kSets = 6;      // lcm(2 cell buffers, 3 mark buffers)
constexpr uint32_t kGroup = 64;

struct BlockPC {
    int32_t origin[4], size[4], start[4], nblocks[4], brickLo[4], brickDim[4], press[4];
};
struct DilatePC {
    int32_t brickDim[4], stride[4];
};
struct PressurePC {
    int32_t origin[4], size[4], brickLo[4], brickDim[4], extra[4];
};
static_assert(sizeof(BlockPC) == 112, "matsim_block.comp push constants");
static_assert(sizeof(DilatePC) == 32, "matsim_dilate.comp push constants");
static_assert(sizeof(PressurePC) == 80, "matsim_pressure.comp push constants");
constexpr int kStatsPerTick = 3;   // eligible, evaluated, head changes

// Dispatch geometry for `count` invocations: rows of at most 65,535 groups.
void geometry(uint64_t count, uint32_t& gx, uint32_t& gy, uint32_t& stride) {
    const uint64_t groups = std::max<uint64_t>(1, (count + kGroup - 1) / kGroup);
    gx = static_cast<uint32_t>(std::min<uint64_t>(groups, 65535));
    gy = static_cast<uint32_t>((groups + gx - 1) / gx);
    stride = gx * kGroup;
}

// Staging layout: [cells | marks1 | marks2 | stats | material table | route ptrs | heads | max ptrs].
struct Layout {
    VkDeviceSize cells, m1, m2, stats, mat, q, head, mx, total;
};
Layout layoutFor(size_t cells, size_t bricks) {
    Layout l{};
    l.cells = 0;
    l.m1 = l.cells + cells * 4;
    l.m2 = l.m1 + bricks * 4;
    l.stats = l.m2 + bricks * 4;
    l.mat = l.stats + kChunk * kStatsPerTick * 4;
    l.q = l.mat + 256 * 16;
    l.head = l.q + cells * 4;
    l.mx = l.head + cells * 4;
    l.total = l.mx + cells * 4;
    return l;
}
} // namespace

MatSimGpu::MatSimGpu() = default;
MatSimGpu::~MatSimGpu() { shutdown(); }

bool MatSimGpu::init(VkDevice device, VkPhysicalDevice physical, VkQueue queue, uint32_t family,
                     const std::string& shaderDir, std::string* err) {
    shutdown();
    m_device = device;
    m_physical = physical;
    m_queue = queue;
    VkCommandPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.queueFamilyIndex = family;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateCommandPool(device, &pci, nullptr, &m_pool) != VK_SUCCESS ||
        vkCreateFence(device, &fci, nullptr, &m_fence) != VK_SUCCESS) {
        if (err) *err = "MatSimGpu: command pool / fence creation failed";
        shutdown();
        return false;
    }
    // Timestamps, when the queue family has them.
    uint32_t nf = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &nf, nullptr);
    std::vector<VkQueueFamilyProperties> fam(nf);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &nf, fam.data());
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physical, &props);
    if (family < nf && fam[family].timestampValidBits > 0 && props.limits.timestampPeriod > 0.0f) {
        VkQueryPoolCreateInfo q{};
        q.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        q.queryType = VK_QUERY_TYPE_TIMESTAMP;
        q.queryCount = 2;
        if (vkCreateQueryPool(device, &q, nullptr, &m_queries) == VK_SUCCESS)
            m_timestampMs = double(props.limits.timestampPeriod) * 1e-6;
    }
    m_block = std::make_unique<Vulkan::ComputePipeline>();
    m_dilate = std::make_unique<Vulkan::ComputePipeline>();
    m_pressure = std::make_unique<Vulkan::ComputePipeline>();
    if (!m_block->create(device, shaderDir + "/matsim_block.comp.spv", 8, sizeof(BlockPC), kSets) ||
        !m_dilate->create(device, shaderDir + "/matsim_dilate.comp.spv", 4, sizeof(DilatePC), kSets) ||
        !m_pressure->create(device, shaderDir + "/matsim_pressure.comp.spv", 9, sizeof(PressurePC), kSets)) {
        if (err) *err = "MatSimGpu: cannot create the compute pipelines from " + shaderDir +
                        "/matsim_{block,dilate,pressure}.comp.spv (run build_shaders.bat?)";
        shutdown();
        return false;
    }
    return true;
}

void MatSimGpu::shutdown() {
    if (m_device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(m_device);
    release();
    if (m_block) m_block->cleanup();
    if (m_dilate) m_dilate->cleanup();
    if (m_pressure) m_pressure->cleanup();
    m_block.reset();
    m_dilate.reset();
    m_pressure.reset();
    if (m_queries) vkDestroyQueryPool(m_device, m_queries, nullptr);
    if (m_fence) vkDestroyFence(m_device, m_fence, nullptr);
    if (m_pool) vkDestroyCommandPool(m_device, m_pool, nullptr);
    m_queries = VK_NULL_HANDLE;
    m_fence = VK_NULL_HANDLE;
    m_pool = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE;
    m_timestampMs = 0.0;
}

void MatSimGpu::release() {
    m_heldGen = 0;
    if (m_device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(m_device);
    for (Buffer& b : m_cellBuf) destroyBuffer(b);
    for (Buffer& b : m_marks) destroyBuffer(b);
    for (Buffer& b : m_q) destroyBuffer(b);
    for (Buffer& b : m_mx) destroyBuffer(b);
    destroyBuffer(m_head);
    destroyBuffer(m_process);
    destroyBuffer(m_mat);
    destroyBuffer(m_stats);
    destroyBuffer(m_staging);
    m_cells = 0;
    m_bricks = 0;
}

uint32_t MatSimGpu::findMemoryType(uint32_t filter, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(m_physical, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((filter & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
    return UINT32_MAX;
}

bool MatSimGpu::createBuffer(Buffer& b, VkDeviceSize bytes, bool hostVisible, std::string* err) {
    b.bytes = std::max<VkDeviceSize>(bytes, 16);
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = b.bytes;
    bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bi, nullptr, &b.buf) != VK_SUCCESS) {
        if (err) *err = "MatSimGpu: vkCreateBuffer failed for " + std::to_string(b.bytes) + " bytes";
        b = Buffer{};
        return false;
    }
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(m_device, b.buf, &mr);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    const VkMemoryPropertyFlags want = hostVisible
        ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    ai.memoryTypeIndex = hostVisible ? findMemoryType(mr.memoryTypeBits, want | VK_MEMORY_PROPERTY_HOST_CACHED_BIT) : UINT32_MAX;
    if (ai.memoryTypeIndex == UINT32_MAX) ai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits, want);
    if (ai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(m_device, &ai, nullptr, &b.mem) != VK_SUCCESS) {
        if (err) *err = "MatSimGpu: cannot allocate " + std::to_string(mr.size) + " bytes of " +
                        (hostVisible ? "host-visible" : "device-local") + " memory";
        vkDestroyBuffer(m_device, b.buf, nullptr);
        b = Buffer{};
        return false;
    }
    vkBindBufferMemory(m_device, b.buf, b.mem, 0);
    if (hostVisible) vkMapMemory(m_device, b.mem, 0, b.bytes, 0, &b.mapped);
    return true;
}

void MatSimGpu::destroyBuffer(Buffer& b) {
    if (b.mapped) vkUnmapMemory(m_device, b.mem);
    if (b.buf) vkDestroyBuffer(m_device, b.buf, nullptr);
    if (b.mem) vkFreeMemory(m_device, b.mem, nullptr);
    b = Buffer{};
}

VkCommandBuffer MatSimGpu::beginCommands() const {
    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = m_pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(m_device, &cai, &cmd);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

void MatSimGpu::submitAndWait(VkCommandBuffer cmd) const {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkResetFences(m_device, 1, &m_fence);
    const VkResult sub = vkQueueSubmit(m_queue, 1, &si, m_fence);
    const VkResult wait = vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX);
    if (sub != VK_SUCCESS || wait != VK_SUCCESS)
        std::fprintf(stderr, "[MatSimGpu] submit %d wait %d\n", int(sub), int(wait));
    vkFreeCommandBuffers(m_device, m_pool, 1, &cmd);
}

void MatSimGpu::barrier(VkCommandBuffer cmd, VkPipelineStageFlags src, VkAccessFlags srcAccess,
                        VkPipelineStageFlags dst, VkAccessFlags dstAccess) const {
    VkMemoryBarrier mb{};
    mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = srcAccess;
    mb.dstAccessMask = dstAccess;
    vkCmdPipelineBarrier(cmd, src, dst, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

void MatSimGpu::bindSets() {
    for (uint32_t s = 0; s < kSets; ++s) {
        const Buffer& cur = m_cellBuf[s % 2];
        const Buffer& nxt = m_cellBuf[(s + 1) % 2];
        const Buffer& fresh = m_marks[s % 3];
        const Buffer& m1 = m_marks[(s + 2) % 3];
        const Buffer& m2 = m_marks[(s + 1) % 3];
        m_block->bindBufferInSet(s, 0, cur.buf, cur.bytes);
        m_block->bindBufferInSet(s, 1, nxt.buf, nxt.bytes);
        m_block->bindBufferInSet(s, 2, m_process.buf, m_process.bytes);
        m_block->bindBufferInSet(s, 3, fresh.buf, fresh.bytes);
        m_block->bindBufferInSet(s, 4, m_mat.buf, m_mat.bytes);
        m_block->bindBufferInSet(s, 5, m_stats.buf, m_stats.bytes);
        const Buffer& qPrev = m_q[s % 2];
        const Buffer& qNow = m_q[(s + 1) % 2];
        m_block->bindBufferInSet(s, 6, qNow.buf, qNow.bytes);
        m_block->bindBufferInSet(s, 7, m_head.buf, m_head.bytes);
        m_pressure->bindBufferInSet(s, 0, cur.buf, cur.bytes);
        m_pressure->bindBufferInSet(s, 1, qPrev.buf, qPrev.bytes);
        m_pressure->bindBufferInSet(s, 2, qNow.buf, qNow.bytes);
        m_pressure->bindBufferInSet(s, 3, m_head.buf, m_head.bytes);
        m_pressure->bindBufferInSet(s, 4, fresh.buf, fresh.bytes);
        m_pressure->bindBufferInSet(s, 5, m_mat.buf, m_mat.bytes);
        m_pressure->bindBufferInSet(s, 6, m_stats.buf, m_stats.bytes);
        m_pressure->bindBufferInSet(s, 7, m_mx[s % 2].buf, m_mx[s % 2].bytes);
        m_pressure->bindBufferInSet(s, 8, m_mx[(s + 1) % 2].buf, m_mx[(s + 1) % 2].bytes);
        m_dilate->bindBufferInSet(s, 0, m1.buf, m1.bytes);
        m_dilate->bindBufferInSet(s, 1, m2.buf, m2.bytes);
        m_dilate->bindBufferInSet(s, 2, m_process.buf, m_process.bytes);
        m_dilate->bindBufferInSet(s, 3, fresh.buf, fresh.bytes);
    }
    m_block->updateDescriptors();
    m_dilate->updateDescriptors();
    m_pressure->updateDescriptors();
}

bool MatSimGpu::upload(const MatSimCore& core, std::string* err) {
    if (!initialized()) { if (err) *err = "MatSimGpu: not initialised"; return false; }
    if (!core.created()) { if (err) *err = "MatSimGpu: the core has no box"; return false; }
    if (m_skipCurrent && m_heldGen != 0 && core.generation() == m_heldGen && loaded()) {
        ++m_uploadsSkipped;
        m_xfer.upHost = m_xfer.upCopy = 0.0;
        return true;
    }
    m_heldGen = 0;
    const size_t n = core.cells().size();
    const size_t nb = core.marksTick1().size();
    if (n != m_cells || nb != m_bricks) {
        release();
        const Layout l = layoutFor(n, nb);
        bool ok = createBuffer(m_cellBuf[0], n * 4, false, err) && createBuffer(m_cellBuf[1], n * 4, false, err);
        for (Buffer& b : m_marks) ok = ok && createBuffer(b, nb * 4, false, err);
        for (Buffer& b : m_q) ok = ok && createBuffer(b, n * 4, false, err);
        for (Buffer& b : m_mx) ok = ok && createBuffer(b, n * 4, false, err);
        ok = ok && createBuffer(m_head, n * 4, false, err);
        ok = ok && createBuffer(m_process, nb * 4, false, err) && createBuffer(m_mat, 256 * 16, false, err) &&
             createBuffer(m_stats, kChunk * kStatsPerTick * 4, false, err) && createBuffer(m_staging, l.total, true, err);
        if (!ok) { release(); return false; }
        m_cells = n;
        m_bricks = nb;
        bindSets();
    }
    m_origin = core.origin();
    m_size = core.size();
    m_brickLo = core.brickLo();
    m_brickDim = core.brickDim();
    m_full = core.full();
    m_seed = core.config().seed;
    m_sleep = core.config().sleep;
    m_pressureOn = core.config().pressure;
    m_deadband = core.pressureDeadband();
    m_orificeK = core.orificeK();
    m_tick = core.tick();
    m_g = 0;

    using XClock = std::chrono::steady_clock;
    auto xms = [](XClock::time_point a, XClock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    const auto u0 = XClock::now();
    const Layout l = layoutFor(n, nb);
    auto* base = static_cast<uint8_t*>(m_staging.mapped);
    std::memcpy(base + l.cells, core.cells().data(), n * 4);
    auto* m1 = reinterpret_cast<uint32_t*>(base + l.m1);
    auto* m2 = reinterpret_cast<uint32_t*>(base + l.m2);
    for (size_t i = 0; i < nb; ++i) { m1[i] = core.marksTick1()[i]; m2[i] = core.marksTick2()[i]; }
    std::memcpy(base + l.q, core.headPtr().data(), n * 4);
    std::memcpy(base + l.head, core.head().data(), n * 4);
    std::memcpy(base + l.mx, core.headMaxPtr().data(), n * 4);
    auto* mt = reinterpret_cast<uint32_t*>(base + l.mat);
    for (int m = 0; m < 256; ++m) {
        mt[4 * m + 0] = static_cast<uint32_t>(materialTable()[m].phase);
        mt[4 * m + 1] = slideThreshold(uint8_t(m), m_full, false);
        mt[4 * m + 2] = slideThreshold(uint8_t(m), m_full, true);
        mt[4 * m + 3] = 0;
    }
    const auto u1 = XClock::now();
    VkCommandBuffer cmd = beginCommands();
    auto copy = [&](const Buffer& dst, VkDeviceSize src, VkDeviceSize bytes) {
        VkBufferCopy r{src, 0, bytes};
        vkCmdCopyBuffer(cmd, m_staging.buf, dst.buf, 1, &r);
    };
    copy(m_cellBuf[0], l.cells, n * 4);   // g = 0: cur = cells[0], marks1 = marks[2], marks2 = marks[1]
    copy(m_marks[2], l.m1, nb * 4);
    copy(m_marks[1], l.m2, nb * 4);
    copy(m_mat, l.mat, 256 * 16);
    copy(m_q[0], l.q, n * 4);   // g = 0: qPrev = q[0]
    copy(m_q[1], l.q, n * 4);   // audit D1: with pressure off nothing writes q[1], and download reads q[g % 2]
    copy(m_head, l.head, n * 4);
    copy(m_mx[0], l.mx, n * 4);
    copy(m_mx[1], l.mx, n * 4);
    submitAndWait(cmd);
    m_xfer.upHost = xms(u0, u1);
    m_xfer.upCopy = xms(u1, XClock::now());
    return true;
}

StepStats MatSimGpu::step(int n) {
    StepStats st;
    if (!loaded() || n <= 0) return st;
    m_heldGen = 0;   // the device is now ahead of any core
    const Layout l = layoutFor(m_cells, m_bricks);
    const bool timed = m_queries != VK_NULL_HANDLE && m_timestampMs > 0.0;
    double ms = 0.0;
    uint32_t dgx, dgy, dstride;
    geometry(m_bricks, dgx, dgy, dstride);
    DilatePC dpc{{m_brickDim.x, m_brickDim.y, m_brickDim.z, int32_t(m_bricks)}, {int32_t(dstride), 0, 0, 0}};
    uint32_t pgx, pgy, pstride;
    geometry(m_cells, pgx, pgy, pstride);
    PressurePC ppc{{m_origin.x, m_origin.y, m_origin.z, int32_t(m_full)},
                   {m_size.x, m_size.y, m_size.z, int32_t(m_deadband)},
                   {m_brickLo.x, m_brickLo.y, m_brickLo.z, 0},
                   {m_brickDim.x, m_brickDim.y, m_brickDim.z, int32_t(pstride)},
                   {0, int32_t(m_cells), 0, 0}};

    for (int done = 0; done < n;) {
        const int k = std::min(kChunk, n - done);
        VkCommandBuffer cmd = beginCommands();
        if (timed) vkCmdResetQueryPool(cmd, m_queries, 0, 2);
        vkCmdFillBuffer(cmd, m_stats.buf, 0, VK_WHOLE_SIZE, 0);
        barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        if (timed) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_queries, 0);
        for (int t = 0; t < k; ++t) {
            const uint64_t tick = m_tick + uint64_t(t);
            const uint32_t set = uint32_t((m_g + uint64_t(t)) % kSets);
            const int p = int(tick & 1u);
            BlockPC pc{};
            const int start[3] = {m_origin.x - ((m_origin.x - p) & 1), m_origin.y - ((m_origin.y - p) & 1),
                                  m_origin.z - ((m_origin.z - p) & 1)};
            const int end[3] = {m_origin.x + m_size.x, m_origin.y + m_size.y, m_origin.z + m_size.z};
            int nbk[3];
            for (int a = 0; a < 3; ++a) nbk[a] = (end[a] - start[a] + 1) / 2;
            const uint64_t blocks = uint64_t(nbk[0]) * uint64_t(nbk[1]) * uint64_t(nbk[2]);
            uint32_t gx, gy, stride;
            geometry(blocks, gx, gy, stride);
            pc.origin[0] = m_origin.x; pc.origin[1] = m_origin.y; pc.origin[2] = m_origin.z; pc.origin[3] = int32_t(m_full);
            pc.size[0] = m_size.x; pc.size[1] = m_size.y; pc.size[2] = m_size.z; pc.size[3] = int32_t(m_seed);
            pc.start[0] = start[0]; pc.start[1] = start[1]; pc.start[2] = start[2]; pc.start[3] = int32_t(tickMix(tick));
            pc.nblocks[0] = nbk[0]; pc.nblocks[1] = nbk[1]; pc.nblocks[2] = nbk[2]; pc.nblocks[3] = t;
            pc.brickLo[0] = m_brickLo.x; pc.brickLo[1] = m_brickLo.y; pc.brickLo[2] = m_brickLo.z; pc.brickLo[3] = m_sleep ? 1 : 0;
            pc.brickDim[0] = m_brickDim.x; pc.brickDim[1] = m_brickDim.y; pc.brickDim[2] = m_brickDim.z; pc.brickDim[3] = int32_t(stride);
            pc.press[0] = m_pressureOn ? 1 : 0; pc.press[1] = int32_t(m_deadband); pc.press[2] = int32_t(m_orificeK); pc.press[3] = 0;

            // This tick's marks start empty; the pressure pass adds head changes, the block pass moves.
            const Buffer& fresh = m_marks[set % 3];
            vkCmdFillBuffer(cmd, fresh.buf, 0, VK_WHOLE_SIZE, 0);
            barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
            if (m_pressureOn) {
                ppc.extra[0] = t;
                m_pressure->bind(cmd, set);
                m_pressure->pushConstants(cmd, &ppc, sizeof(ppc));
                m_pressure->dispatch(cmd, pgx, pgy);
                barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
            }
            m_dilate->bind(cmd, set);
            m_dilate->pushConstants(cmd, &dpc, sizeof(dpc));
            m_dilate->dispatch(cmd, dgx, dgy);
            barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
            m_block->bind(cmd, set);
            m_block->pushConstants(cmd, &pc, sizeof(pc));
            m_block->dispatch(cmd, gx, gy);
            // Audit D2: the next tick starts with a TRANSFER fill of a mark buffer this tick's dilate
            // read, so the end-of-tick dependency must cover the transfer stage too.
            barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
        }
        if (timed) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_queries, 1);
        barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferCopy r{0, l.stats, VkDeviceSize(kChunk) * kStatsPerTick * 4};
        vkCmdCopyBuffer(cmd, m_stats.buf, m_staging.buf, 1, &r);
        barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                VK_ACCESS_HOST_READ_BIT);   // the host reads staging after the fence
        submitAndWait(cmd);

        const auto* s = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(m_staging.mapped) + l.stats);
        for (int t = 0; t < k; ++t) {
            st.eligibleBlocks += s[kStatsPerTick * t];
            st.evaluatedBlocks += s[kStatsPerTick * t + 1];
            st.headChanges += s[kStatsPerTick * t + 2];
        }
        st.lastTickEligible = s[kStatsPerTick * (k - 1)];
        st.lastTickHeadChanges = s[kStatsPerTick * (k - 1) + 2];
        if (timed) {
            uint64_t ts[2] = {0, 0};
            if (vkGetQueryPoolResults(m_device, m_queries, 0, 2, sizeof(ts), ts, sizeof(uint64_t),
                                      VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS)
                ms += double(ts[1] - ts[0]) * m_timestampMs;
        }
        m_tick += uint64_t(k);
        m_g += uint64_t(k);
        st.ticks += k;
        done += k;
    }
    m_lastMsPerTick = timed ? ms / double(n) : -1.0;
    return st;
}

bool MatSimGpu::download(MatSimCore& core, std::string* err) {
    if (!loaded()) { if (err) *err = "MatSimGpu: nothing uploaded"; return false; }
    if (core.cells().size() != m_cells || core.marksTick1().size() != m_bricks || !(core.origin() == m_origin) ||
        !(core.size() == m_size)) {
        if (err) *err = "MatSimGpu: the core's box is not the uploaded box";
        return false;
    }
    using XClock = std::chrono::steady_clock;
    auto xms = [](XClock::time_point a, XClock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    const auto d0 = XClock::now();
    const Layout l = layoutFor(m_cells, m_bricks);
    VkCommandBuffer cmd = beginCommands();
    barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_TRANSFER_READ_BIT);
    auto copy = [&](const Buffer& src, VkDeviceSize dst, VkDeviceSize bytes) {
        VkBufferCopy r{0, dst, bytes};
        vkCmdCopyBuffer(cmd, src.buf, m_staging.buf, 1, &r);
    };
    copy(m_cellBuf[m_g % 2], l.cells, m_cells * 4);
    copy(m_marks[(m_g + 2) % 3], l.m1, m_bricks * 4);
    copy(m_marks[(m_g + 1) % 3], l.m2, m_bricks * 4);
    copy(m_q[m_g % 2], l.q, m_cells * 4);
    copy(m_head, l.head, m_cells * 4);
    copy(m_mx[m_g % 2], l.mx, m_cells * 4);
    barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            VK_ACCESS_HOST_READ_BIT);
    submitAndWait(cmd);
    const auto d1 = XClock::now();
    const auto* base = static_cast<const uint8_t*>(m_staging.mapped);
    // 6l: straight from staging into the core's own arrays (was: new vectors, then moved in).
    auto u32 = [&](VkDeviceSize off) { return reinterpret_cast<const uint32_t*>(base + off); };
    const auto d2 = XClock::now();
    if (!core.adoptSteppedFrom(reinterpret_cast<const Cell*>(base + l.cells), m_tick, u32(l.m1), u32(l.m2), u32(l.q),
                               u32(l.head), u32(l.mx))) {
        if (err) *err = "MatSimGpu: the core refused the stepped state";
        return false;
    }
    m_heldGen = core.generation();   // adoptStepped bumped it: the device holds exactly this
    m_xfer.downCopy = xms(d0, d1);
    m_xfer.downHost = xms(d1, d2);
    m_xfer.downAdopt = xms(d2, XClock::now());
    return true;
}

} // namespace Phyxel::Core::MatSim
